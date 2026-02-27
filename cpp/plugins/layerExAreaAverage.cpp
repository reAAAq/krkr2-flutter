// layerExAreaAverage — Area-average downscaling for Layer
// Ported from https://github.com/wamsoft/layerExAreaAverage

#include "ncbind.hpp"
#include "TickCount.h"

#ifndef TJS_INTF_METHOD
#define TJS_INTF_METHOD
#endif

#define NCB_MODULE_NAME TJS_W("layerExAreaAverage.dll")

typedef tjs_int fixdot;
#define DOTBASE         12
#define INT2FIXDOT(a)   (fixdot)((a) << DOTBASE)
#define REAL2FIXDOT(a)  (fixdot)((a) * INT2FIXDOT(1))
#define FIXDOT2INT(a)   ((a) >> DOTBASE)
#define MULFIXDOT(a, b) (((a) * (b)) >> DOTBASE)
#define DIVFIXDOT(a, b) ((a) / (b))

struct layerExAreaAverage
{
    static tjs_error TJS_INTF_METHOD stretchCopyAA(tTJSVariant *result,
                                                   tjs_int numparams,
                                                   tTJSVariant **param,
                                                   iTJSDispatch2 *objthis)
    {
        if (numparams < 9) return TJS_E_BADPARAMCOUNT;

        tjs_uint64 tick = TVPGetTickCount();

        tjs_int dImageWidth, dImageHeight, dPitch;
        tjs_uint8 *dBuffer;
        tTJSVariant val;
        objthis->PropGet(0, TJS_W("imageWidth"), NULL, &val, objthis);
        dImageWidth = (tjs_int)val;
        objthis->PropGet(0, TJS_W("imageHeight"), NULL, &val, objthis);
        dImageHeight = (tjs_int)val;
        objthis->PropGet(0, TJS_W("mainImageBufferPitch"), NULL, &val, objthis);
        dPitch = (tjs_int)val;
        objthis->PropGet(0, TJS_W("mainImageBufferForWrite"), NULL, &val, objthis);
        dBuffer = (tjs_uint8*)(tjs_intptr_t)val.AsInteger();

        tjs_int dLeft   = param[0]->AsInteger();
        tjs_int dTop    = param[1]->AsInteger();
        tjs_int dWidth  = param[2]->AsInteger();
        tjs_int dHeight = param[3]->AsInteger();

        tjs_int sImageWidth, sImageHeight, sPitch;
        tjs_uint8 *sBuffer;
        iTJSDispatch2 *srcobj = param[4]->AsObjectNoAddRef();
        srcobj->PropGet(0, TJS_W("imageWidth"), NULL, &val, srcobj);
        sImageWidth = (tjs_int)val;
        srcobj->PropGet(0, TJS_W("imageHeight"), NULL, &val, srcobj);
        sImageHeight = (tjs_int)val;
        srcobj->PropGet(0, TJS_W("mainImageBufferPitch"), NULL, &val, srcobj);
        sPitch = (tjs_int)val;
        srcobj->PropGet(0, TJS_W("mainImageBuffer"), NULL, &val, srcobj);
        sBuffer = (tjs_uint8*)(tjs_intptr_t)val.AsInteger();

        tjs_int sLeft   = param[5]->AsInteger();
        tjs_int sTop    = param[6]->AsInteger();
        tjs_int sWidth  = param[7]->AsInteger();
        tjs_int sHeight = param[8]->AsInteger();

        if (dWidth > sWidth || dHeight > sHeight) {
            TVPThrowExceptionMessage(TJS_W("stretchCopyAA cannot enlarge."));
            return TJS_E_FAIL;
        }

        if (dLeft + dWidth > dImageWidth) {
            tjs_int dw = dImageWidth - dLeft;
            sWidth = (tjs_int)((tjs_real)sWidth * ((tjs_real)dw / (tjs_real)dWidth));
            dWidth = dw;
        }
        if (sLeft + sWidth > sImageWidth) {
            tjs_int sw = sImageWidth - sLeft;
            dWidth = (tjs_int)((tjs_real)dWidth * ((tjs_real)sw / (tjs_real)sWidth));
            sWidth = sw;
        }
        if (dTop + dHeight > dImageHeight) {
            tjs_int dh = dImageHeight - dTop;
            sHeight = (tjs_int)((tjs_real)sHeight * ((tjs_real)dh / (tjs_real)dHeight));
            dHeight = dh;
        }
        if (sTop + sHeight > sImageHeight) {
            tjs_int sh = sImageHeight - sTop;
            dHeight = (tjs_int)((tjs_real)dHeight * ((tjs_real)sh / (tjs_real)sHeight));
            sHeight = sh;
        }

        fixdot sl = INT2FIXDOT(sLeft);
        fixdot st = INT2FIXDOT(sTop);
        fixdot rw = REAL2FIXDOT((tjs_real)sWidth / dWidth);
        fixdot rh = REAL2FIXDOT((tjs_real)sHeight / dHeight);

        for (tjs_int y = 0; y < dHeight; y++) {
            tjs_uint32 *outpixel = (tjs_uint32*)(dBuffer + (y + dTop) * dPitch) + dLeft;
            for (tjs_int x = 0; x < dWidth; x++) {
                fixdot x1 = sl + x * rw;
                fixdot y1 = st + y * rh;
                fixdot x2 = x1 + rw;
                fixdot y2 = y1 + rh;

                tjs_int sx = FIXDOT2INT(x1);
                tjs_int sy = FIXDOT2INT(y1);
                tjs_int ex = FIXDOT2INT(x2 + INT2FIXDOT(1) - 1);
                tjs_int ey = FIXDOT2INT(y2 + INT2FIXDOT(1) - 1);
                if (ex >= sImageWidth)  ex = sImageWidth - 1;
                if (ey >= sImageHeight) ey = sImageHeight - 1;

                fixdot totalarea_a = 0, a = 0;
                fixdot totalarea_rgb = 0, r = 0, g = 0, b = 0;
                fixdot totalarea_trgb = 0, tr = 0, tg = 0, tb = 0;

                for (tjs_int ay = sy; ay < ey; ay++) {
                    tjs_uint32 *inpixel = (tjs_uint32*)(sBuffer + ay * sPitch) + sx;
                    fixdot e1 = INT2FIXDOT(ay);
                    fixdot e2 = INT2FIXDOT(ay + 1);
                    if (e1 < y1) e1 = y1;
                    if (e2 > y2) e2 = y2;
                    fixdot ah = e2 - e1;

                    for (tjs_int ax = sx; ax < ex; ax++) {
                        e1 = INT2FIXDOT(ax);
                        e2 = INT2FIXDOT(ax + 1);
                        if (e1 < x1) e1 = x1;
                        if (e2 > x2) e2 = x2;
                        fixdot aw = e2 - e1;
                        fixdot area = MULFIXDOT(aw, ah);
                        totalarea_a += area;
                        tjs_uint32 alpha = (*inpixel >> 24) & 0xFF;
                        a += alpha * area;
                        if (alpha > 0) {
                            area = (area * alpha) >> 8;
                            r += ((*inpixel >> 16) & 0xFF) * area;
                            g += ((*inpixel >>  8) & 0xFF) * area;
                            b += ((*inpixel      ) & 0xFF) * area;
                            totalarea_rgb += area;
                        } else {
                            tr += ((*inpixel >> 16) & 0xFF) * area;
                            tg += ((*inpixel >>  8) & 0xFF) * area;
                            tb += ((*inpixel      ) & 0xFF) * area;
                            totalarea_trgb += area;
                        }
                        inpixel++;
                    }
                }

                if (totalarea_a == 0) continue;
                a = DIVFIXDOT(a, totalarea_a);
                if (totalarea_rgb == 0) {
                    totalarea_rgb = totalarea_trgb;
                    r = tr; g = tg; b = tb;
                }
                if (totalarea_rgb == 0) {
                    r = g = b = 0;
                } else {
                    r = DIVFIXDOT(r, totalarea_rgb);
                    g = DIVFIXDOT(g, totalarea_rgb);
                    b = DIVFIXDOT(b, totalarea_rgb);
                }
                *outpixel = ((((tjs_int)a) & 0xFF) << 24) |
                            ((((tjs_int)r) & 0xFF) << 16) |
                            ((((tjs_int)g) & 0xFF) <<  8) |
                            (((tjs_int)b) & 0xFF);
                outpixel++;
            }
        }

        {
            tTJSVariant uval[4];
            tTJSVariant *pval[4] = { uval, uval + 1, uval + 2, uval + 3 };
            uval[0] = (tjs_int64)dLeft;
            uval[1] = (tjs_int64)dTop;
            uval[2] = (tjs_int64)dWidth;
            uval[3] = (tjs_int64)dHeight;
            static tjs_uint32 update_hint = 0;
            objthis->FuncCall(0, TJS_W("update"), &update_hint, NULL, 4, pval, objthis);
        }

        TVPAddLog(TJS_W("stretch copy by area average:RESULT (") +
                  ttstr((int)sWidth) + TJS_W(",") + ttstr((int)sHeight) +
                  TJS_W(")->(") +
                  ttstr((int)dWidth) + TJS_W(",") + ttstr((int)dHeight) +
                  TJS_W("), time = ") + ttstr((int)(TVPGetTickCount() - tick)) + TJS_W("(ms)"));

        return TJS_S_OK;
    }
};

NCB_ATTACH_CLASS(layerExAreaAverage, Layer)
{
    RawCallback("stretchCopyAA", &Class::stretchCopyAA, 0);
}
