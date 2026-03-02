#include "tjsCommHead.h"

#include "LoadAMV.h"
#include "GraphicsLoaderIntf.h"
#include "MsgIntf.h"

#include <turbojpeg.h>
#include <zlib.h>
#include <cstring>
#include <vector>
#include <algorithm>

#include <spdlog/spdlog.h>
#include "tjsDictionary.h"

#pragma pack(push, 1)
struct AMVHeader {
    tjs_uint32 magic;
    tjs_uint32 size_of_file;
    tjs_uint32 revision;
    tjs_uint32 qt_size_plus_hdr;
    tjs_uint32 unk;
    tjs_uint32 frame_cnt;
    tjs_uint32 unk2;
    tjs_uint32 frame_rate;
    tjs_uint16 width;
    tjs_uint16 height;
    tjs_uint32 alpha_decode_attr; // 1=JPEG, 2=ZLIB
};

struct AMVZlibFrameHeader {
    tjs_uint32 magic;
    tjs_uint32 size_of_frame;
    tjs_uint32 index;
    tjs_uint16 frame_width;
    tjs_uint16 frame_height;
    tjs_uint16 alpha_width;
    tjs_uint16 alpha_height;
    tjs_uint32 rgb_buffer_size;
};

struct AMVJpegFrameHeader {
    tjs_uint32 magic;
    tjs_uint32 size_of_frame;
    tjs_uint32 index;
    tjs_uint16 frame_width;
    tjs_uint16 frame_height;
    tjs_uint16 alpha_width;
    tjs_uint16 alpha_height;
};
#pragma pack(pop)

static const tjs_uint32 AMV_MAGIC = 0x4D504A41;
static const tjs_uint32 FRAM_MAGIC = 0x4D415246;

// ---------------------------------------------------------------------------
// JPEG helpers (for alpha_decode_attr == 1)
// ---------------------------------------------------------------------------
static std::vector<unsigned char> BuildDQTSegment(const unsigned char *qtData,
                                                  size_t qtSize) {
    int numTables = (int)(qtSize / 64);
    size_t lq = 2 + numTables * 65;
    std::vector<unsigned char> seg;
    seg.reserve(2 + lq);
    seg.push_back(0xFF);
    seg.push_back(0xDB);
    seg.push_back((unsigned char)((lq >> 8) & 0xFF));
    seg.push_back((unsigned char)(lq & 0xFF));
    for (int t = 0; t < numTables; t++) {
        seg.push_back((unsigned char)t);
        seg.insert(seg.end(), qtData + t * 64, qtData + (t + 1) * 64);
    }
    return seg;
}

static std::vector<unsigned char>
InjectDQT(const unsigned char *jpegData, size_t jpegSize,
          const std::vector<unsigned char> &dqtSeg) {
    bool hasSOI =
        (jpegSize >= 2 && jpegData[0] == 0xFF && jpegData[1] == 0xD8);
    std::vector<unsigned char> result;
    result.reserve(jpegSize + dqtSeg.size() + 2);
    result.push_back(0xFF);
    result.push_back(0xD8);
    result.insert(result.end(), dqtSeg.begin(), dqtSeg.end());
    size_t srcOff = hasSOI ? 2 : 0;
    result.insert(result.end(), jpegData + srcOff, jpegData + jpegSize);
    return result;
}

static bool FindSecondSOI(const unsigned char *data, size_t len,
                          size_t &colorSize) {
    for (size_t i = 2; i + 1 < len; i++) {
        if (data[i] == 0xFF && data[i + 1] == 0xD8) {
            colorSize = i;
            return true;
        }
    }
    return false;
}

static bool TryDecodeJpeg(const unsigned char *data, size_t size,
                          int pixelFormat, int numComponents, int &outW,
                          int &outH, std::vector<unsigned char> &outBuf) {
    tjhandle dec = tjInitDecompress();
    if (!dec)
        return false;
    int w = 0, h = 0, subsamp = 0;
    if (tjDecompressHeader2(dec, const_cast<unsigned char *>(data),
                            (unsigned long)size, &w, &h, &subsamp) != 0) {
        tjDestroy(dec);
        return false;
    }
    outW = w;
    outH = h;
    outBuf.resize((size_t)w * h * numComponents);
    int ret = tjDecompress2(dec, const_cast<unsigned char *>(data),
                            (unsigned long)size, outBuf.data(), w,
                            w * numComponents, h, pixelFormat, TJFLAG_FASTDCT);
    if (ret != 0) {
        int errCode = tjGetErrorCode(dec);
        if (errCode == TJERR_WARNING) {
            tjDestroy(dec);
            return true;
        }
        tjDestroy(dec);
        return false;
    }
    tjDestroy(dec);
    return true;
}

static bool DecodeJpegWithQT(const unsigned char *jpegData, size_t jpegSize,
                             const std::vector<unsigned char> &dqtSeg,
                             int pixelFormat, int numComponents, int &outW,
                             int &outH,
                             std::vector<unsigned char> &pixelOut) {
    if (TryDecodeJpeg(jpegData, jpegSize, pixelFormat, numComponents, outW,
                      outH, pixelOut))
        return true;
    if (!dqtSeg.empty()) {
        auto patched = InjectDQT(jpegData, jpegSize, dqtSeg);
        return TryDecodeJpeg(patched.data(), patched.size(), pixelFormat,
                             numComponents, outW, outH, pixelOut);
    }
    return false;
}

// ---------------------------------------------------------------------------
// Read helpers — read only what we need from the stream
// ---------------------------------------------------------------------------
static void ReadExact(tTJSBinaryStream *src, void *buf, tjs_uint len) {
    if (src->Read(buf, len) != len)
        TVPThrowExceptionMessage(TJS_W("AMV: read error"));
}

// ---------------------------------------------------------------------------
// Main loader
// ---------------------------------------------------------------------------
void TVPLoadAMV(void *formatdata, void *callbackdata,
                tTVPGraphicSizeCallback sizecallback,
                tTVPGraphicScanLineCallback scanlinecallback,
                tTVPMetaInfoPushCallback metainfopushcallback,
                tTJSBinaryStream *src, tjs_int32 keyidx,
                tTVPGraphicLoadMode mode) {
    if (mode == glmPalettized)
        TVPThrowExceptionMessage(TJS_W("AMV does not support palettized mode"));

    // --- Read & validate header ---
    AMVHeader hdr;
    ReadExact(src, &hdr, sizeof(hdr));
    if (hdr.magic != AMV_MAGIC)
        TVPThrowExceptionMessage(TJS_W("AMV: invalid magic"));
    if (hdr.frame_cnt == 0)
        TVPThrowExceptionMessage(TJS_W("AMV: zero frames"));

    int imgW = hdr.width;
    int imgH = hdr.height;
    if (imgW <= 0 || imgH <= 0)
        TVPThrowExceptionMessage(TJS_W("AMV: invalid dimensions"));

    bool isZlibMode = (hdr.alpha_decode_attr == 2);
    size_t qtSize = hdr.qt_size_plus_hdr - sizeof(AMVHeader);

    auto logger = spdlog::get("core");
    if (logger)
        logger->debug("AMV: {}x{}, {} frames, mode={}",
                      imgW, imgH, hdr.frame_cnt,
                      isZlibMode ? "zlib" : "jpeg");

    // --- Read QT data (skip over it for zlib mode) ---
    std::vector<unsigned char> qtData;
    if (!isZlibMode && qtSize >= 64) {
        qtData.resize(qtSize);
        ReadExact(src, qtData.data(), (tjs_uint)qtSize);
    } else {
        src->SetPosition(src->GetPosition() + qtSize);
    }

    // --- Push metadata ---
    if (metainfopushcallback) {
        metainfopushcallback(callbackdata, ttstr(TJS_W("amv_frames")),
                             ttstr((tjs_int)hdr.frame_cnt));
        metainfopushcallback(callbackdata, ttstr(TJS_W("amv_fps")),
                             ttstr((tjs_int)hdr.frame_rate));
        metainfopushcallback(callbackdata, ttstr(TJS_W("amv_width")),
                             ttstr((tjs_int)imgW));
        metainfopushcallback(callbackdata, ttstr(TJS_W("amv_height")),
                             ttstr((tjs_int)imgH));
    }

    // --- Read first frame header ---
    int alphaW = 0, alphaH = 0;
    tjs_uint32 sizeOfFrame = 0, rgbBufSize = 0;
    size_t extraHdr;

    if (isZlibMode) {
        AMVZlibFrameHeader fh;
        ReadExact(src, &fh, sizeof(fh));
        if (fh.magic != FRAM_MAGIC)
            TVPThrowExceptionMessage(TJS_W("AMV: invalid frame magic"));
        sizeOfFrame = fh.size_of_frame;
        alphaW = fh.alpha_width;
        alphaH = fh.alpha_height;
        rgbBufSize = fh.rgb_buffer_size;
        extraHdr = sizeof(AMVZlibFrameHeader) - 8;
    } else {
        AMVJpegFrameHeader fh;
        ReadExact(src, &fh, sizeof(fh));
        if (fh.magic != FRAM_MAGIC)
            TVPThrowExceptionMessage(TJS_W("AMV: invalid frame magic"));
        sizeOfFrame = fh.size_of_frame;
        alphaW = fh.alpha_width;
        alphaH = fh.alpha_height;
        extraHdr = sizeof(AMVJpegFrameHeader) - 8;
    }

    if (sizeOfFrame < extraHdr)
        TVPThrowExceptionMessage(TJS_W("AMV: frame data too small"));

    size_t payloadLen = sizeOfFrame - extraHdr;

    // --- Read frame payload (only the first frame) ---
    std::vector<unsigned char> payload(payloadLen);
    ReadExact(src, payload.data(), (tjs_uint)payloadLen);
    const unsigned char *payloadStart = payload.data();

    // --- Decode ---
    std::vector<tjs_uint32> rgba(imgW * imgH, 0);

    if (isZlibMode) {
        if (rgbBufSize > payloadLen)
            TVPThrowExceptionMessage(TJS_W("AMV: rgb_buffer_size overflow"));

        int colorW = alphaW > 0 ? alphaW : imgW;
        int colorH = alphaH > 0 ? alphaH : imgH;

        if (rgbBufSize > 0) {
            unsigned long destLen =
                (unsigned long)std::max(colorW * colorH, imgW * imgH);
            std::vector<unsigned char> colorRaw(destLen);
            int zret =
                uncompress(colorRaw.data(), &destLen, payloadStart, rgbBufSize);
            if (zret == Z_OK) {
                int copyW = std::min(colorW, imgW);
                int copyH = std::min(colorH, imgH);
                for (int y = 0; y < copyH; y++) {
                    for (int x = 0; x < copyW; x++) {
                        size_t si = (size_t)y * colorW + x;
                        if (si >= destLen)
                            break;
                        unsigned char v = colorRaw[si];
                        rgba[y * imgW + x] =
                            ((tjs_uint32)v << 24) | 0x00FFFFFFu;
                    }
                }
            } else if (logger) {
                logger->warn("AMV: zlib decompress failed ({})", zret);
            }
        }
    } else {
        auto dqtSeg = BuildDQTSegment(qtData.data(), qtData.size());

        size_t colorSize = payloadLen;
        size_t alphaDataOffset = payloadLen;
        FindSecondSOI(payloadStart, payloadLen, colorSize);
        alphaDataOffset = colorSize;

        int decW = 0, decH = 0;
        std::vector<unsigned char> rgbPixels;
        if (!DecodeJpegWithQT(payloadStart, colorSize, dqtSeg, TJPF_RGBA, 4,
                              decW, decH, rgbPixels))
            TVPThrowExceptionMessage(TJS_W("AMV: color JPEG decode failed"));

        int copyW = std::min(decW, imgW);
        int copyH = std::min(decH, imgH);
        for (int y = 0; y < copyH; y++) {
            const tjs_uint32 *srcRow =
                reinterpret_cast<const tjs_uint32 *>(rgbPixels.data()) +
                y * decW;
            std::memcpy(rgba.data() + y * imgW, srcRow,
                        copyW * sizeof(tjs_uint32));
        }

        if (alphaDataOffset < payloadLen && alphaW > 0 && alphaH > 0) {
            const unsigned char *alphaJpeg = payloadStart + alphaDataOffset;
            size_t alphaJpegLen = payloadLen - alphaDataOffset;
            int aW = 0, aH = 0;
            std::vector<unsigned char> grayPixels;
            if (DecodeJpegWithQT(alphaJpeg, alphaJpegLen, dqtSeg, TJPF_GRAY,
                                 1, aW, aH, grayPixels)) {
                int applyW = std::min({aW, (int)alphaW, imgW});
                int applyH = std::min({aH, (int)alphaH, imgH});
                for (int y = 0; y < applyH; y++) {
                    for (int x = 0; x < applyW; x++) {
                        unsigned char a = grayPixels[y * aW + x];
                        tjs_uint32 &px = rgba[y * imgW + x];
                        px = (px & 0x00FFFFFFu) | ((tjs_uint32)a << 24);
                    }
                }
            }
        }
    }

    // --- Output to engine ---
    if (mode == glmGrayscale) {
        sizecallback(callbackdata, imgW, imgH, gpfLuminance);
        for (int y = 0; y < imgH; y++) {
            void *scanline = scanlinecallback(callbackdata, y);
            if (!scanline)
                break;
            unsigned char *dst = static_cast<unsigned char *>(scanline);
            for (int x = 0; x < imgW; x++) {
                tjs_uint32 px = rgba[y * imgW + x];
                unsigned char b = (px >> 0) & 0xFF;
                unsigned char g = (px >> 8) & 0xFF;
                unsigned char r = (px >> 16) & 0xFF;
                dst[x] = (unsigned char)((r * 77 + g * 150 + b * 29) >> 8);
            }
            scanlinecallback(callbackdata, -1);
        }
    } else {
        sizecallback(callbackdata, imgW, imgH, gpfRGBA);
        for (int y = 0; y < imgH; y++) {
            void *scanline = scanlinecallback(callbackdata, y);
            if (!scanline)
                break;
            std::memcpy(scanline, rgba.data() + y * imgW,
                        imgW * sizeof(tjs_uint32));
            scanlinecallback(callbackdata, -1);
        }
    }
}

void TVPLoadHeaderAMV(void *formatdata, tTJSBinaryStream *src,
                      class iTJSDispatch2 **dic) {
    AMVHeader hdr;
    if (src->Read(&hdr, sizeof(hdr)) != sizeof(hdr))
        return;
    if (hdr.magic != AMV_MAGIC)
        return;

    if (dic) {
        *dic = TJSCreateDictionaryObject();
        tTJSVariant val;
        val = (tjs_int)hdr.width;
        (*dic)->PropSet(TJS_MEMBERENSURE, TJS_W("width"), nullptr, &val, *dic);
        val = (tjs_int)hdr.height;
        (*dic)->PropSet(TJS_MEMBERENSURE, TJS_W("height"), nullptr, &val,
                        *dic);
        val = (tjs_int)hdr.frame_cnt;
        (*dic)->PropSet(TJS_MEMBERENSURE, TJS_W("frames"), nullptr, &val,
                        *dic);
        val = (tjs_int)hdr.frame_rate;
        (*dic)->PropSet(TJS_MEMBERENSURE, TJS_W("fps"), nullptr, &val, *dic);
    }
}
