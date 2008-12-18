#include "test.h"

namespace skia {

class drawrect_test : public Test {
public:
    enum { kSizeX = 510, kSizeY = 510 };

    drawrect_test() {}

    virtual void getSize(SkIPoint* size) {
        size->set(kSizeX, kSizeY);
    }
    
    virtual void draw(SkCanvas* canvas) {
        SkRect r;
        SkPaint p;
        
        p.setAntiAlias(true);
        r.set(SkIntToScalar(10), SkIntToScalar(10),
              SkIntToScalar(10+80), SkIntToScalar(10+80));
        
        static const SkPaint::Style gStyles[] = {
            SkPaint::kStroke_Style,
            SkPaint::kStrokeAndFill_Style
        };
        static const SkScalar gWidths[] = { 0, SkIntToScalar(9) };
        static const SkPaint::Join gJoins[] = {
            SkPaint::kMiter_Join,
            SkPaint::kRound_Join,
            SkPaint::kBevel_Join
        };

        const SkScalar dx = r.width() + SkIntToScalar(20);
        const SkScalar dy = r.height() + SkIntToScalar(10);

        canvas->drawRect(r, p);
        for (size_t k = 0; k < SK_ARRAY_COUNT(gJoins); k++) {
            p.setStrokeJoin(gJoins[k]);
            canvas->translate(0, dy);
            canvas->save(SkCanvas::kMatrix_SaveFlag);
            for (size_t i = 0; i < SK_ARRAY_COUNT(gStyles); i++) {
                p.setStyle(gStyles[i]);
                for (size_t j = 0; j < SK_ARRAY_COUNT(gWidths); j++) {
                    p.setStrokeWidth(gWidths[j]);
                    canvas->drawRect(r, p);
                    canvas->translate(dx, 0);
                }
            }
            canvas->restore();
        }
    }
    
    virtual bool getString(StringType st, SkString* str) {
        switch (st) {
            case kTitle:
                str->set("drawRect");
                return true;
            case kDescription:
                str->set("Call drawRect with different stroke widths and joins");
                return true;
            default:
                return false;
        }
    }
};

static Test* factory(void*) {
    return new drawrect_test;
}    
static Test::Registrar reg(factory, 0);
    
}

