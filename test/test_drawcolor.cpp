#include "test.h"

namespace skia {

class draw_color_test : public Test {
public:
    enum { kSizeX = 32, kSizeY = 32 };

    draw_color_test(SkColor color) : fColor(color) {}

    virtual void getSize(SkIPoint* size) {
        size->set(kSizeX, kSizeY);
    }
    
    virtual void draw(SkCanvas* canvas) {
        canvas->drawColor(fColor);
    }
    
    virtual bool getString(StringType st, SkString* str) {
        switch (st) {
            case kTitle:
                str->printf("DrawColor %X", fColor);
                return true;
            case kDescription:
                str->printf("Call drawColor(%X) on the entire canvas", fColor);
                return true;
            default:
                return false;
        }
    }

private:
    SkColor fColor;
};

static Test* factory(void* color) {
    return new draw_color_test(reinterpret_cast<SkColor>(color));
}

static void init() {
    static const SkColor gColors[] = {
        SK_ColorBLACK, SK_ColorWHITE,
        SK_ColorRED, SK_ColorGREEN, SK_ColorBLUE,
        SK_ColorCYAN, SK_ColorMAGENTA, SK_ColorYELLOW
    };
    for (size_t i = 0; i < SK_ARRAY_COUNT(gColors); i++) {
        Test::Register(factory, reinterpret_cast<void*>(gColors[i]));
    }
}
static Test::Init i(init);

} // namespace

