#pragma once

#include "GraphicsLoaderIntf.h"

void TVPLoadAMV(void *formatdata, void *callbackdata,
                tTVPGraphicSizeCallback sizecallback,
                tTVPGraphicScanLineCallback scanlinecallback,
                tTVPMetaInfoPushCallback metainfopushcallback,
                tTJSBinaryStream *src, tjs_int32 keyidx,
                tTVPGraphicLoadMode mode);

void TVPLoadHeaderAMV(void *formatdata, tTJSBinaryStream *src,
                      class iTJSDispatch2 **dic);
