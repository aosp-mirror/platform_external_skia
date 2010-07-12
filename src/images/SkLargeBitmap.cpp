#include "SkLargeBitmap.h"

bool SkLargeBitmap::decodeRegion(SkBitmap* bitmap, SkIRect rect,
                                 SkBitmap::Config pref, int sampleSize) {
    fDecoder->setSampleSize(sampleSize);
    return fDecoder->decodeRegion(bitmap, rect, pref);
}
