#ifndef SkLargeBitmap_DEFINED
#define SkLargeBitmap_DEFINED

#include "SkBitmap.h"
#include "SkRect.h"
#include "SkImageDecoder.h"

class SkLargeBitmap {
public:
    SkLargeBitmap(SkImageDecoder *decoder, int width, int height) {
        fDecoder = decoder;
        fWidth = width;
        fHeight = height;
    }
    virtual ~SkLargeBitmap() {
        delete fDecoder;
    }

    virtual bool decodeRegion(SkBitmap* bitmap, SkIRect rect,
                              SkBitmap::Config pref, int sampleSize);

    virtual int getWidth() { return fWidth; }
    virtual int getHeight() { return fHeight; }

    virtual SkImageDecoder* getDecoder() { return fDecoder; }

private:
    SkImageDecoder *fDecoder;
    int fWidth;
    int fHeight;
};

#endif
