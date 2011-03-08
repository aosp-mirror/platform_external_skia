#ifndef SkBitmapRegionDecoder_DEFINED
#define SkBitmapRegionDecoder_DEFINED

#include "SkBitmap.h"
#include "SkRect.h"
#include "SkImageDecoder.h"
#include "SkStream.h"

class SkBitmapRegionDecoder {
public:
    SkBitmapRegionDecoder(SkImageDecoder *decoder, SkStream *stream,
            int width, int height) {
        fDecoder = decoder;
        fStream = stream;
        fWidth = width;
        fHeight = height;
    }
    virtual ~SkBitmapRegionDecoder() {
        delete fDecoder;
        fStream->unref();
    }

    virtual bool decodeRegion(SkBitmap* bitmap, SkIRect rect,
                              SkBitmap::Config pref, int sampleSize);

    virtual int getWidth() { return fWidth; }
    virtual int getHeight() { return fHeight; }

    virtual SkImageDecoder* getDecoder() { return fDecoder; }

private:
    SkImageDecoder *fDecoder;
    SkStream *fStream;
    int fWidth;
    int fHeight;
};

#endif
