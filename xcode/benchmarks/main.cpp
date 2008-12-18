#include <iostream>
#include "SkCanvas.h"
#include "SkBitmap.h"
#include "SkPaint.h"
#include "SkImageDecoder.h"

int main (int argc, char * const argv[]) {
    // insert code here...
    std::cout << "Begin test\n";
    
    const int W = 256;
    const int H = 256;
    const SkScalar w = SkIntToScalar(W);
    const SkScalar h = SkIntToScalar(H);

    SkBitmap bm;
    bm.setConfig(SkBitmap::kARGB_8888_Config, W, H);
    bm.allocPixels();
    
    SkCanvas canvas(bm);

    SkPaint paint;
    paint.setAntiAlias(true);
    
    canvas.drawColor(SK_ColorWHITE);
    canvas.drawCircle(w/2, h/2, w/3, paint);
    
    SkImageEncoder::EncodeFile("../../bench1.png", bm,
                               SkImageEncoder::kPNG_Type);
    
    std::cout << "End test\n";
    return 0;
}
