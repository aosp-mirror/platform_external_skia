#include "SkCanvas.h"
#include "SkDevice.h"
#include "SkGraphics.h"
#include "SkPaint.h"
#include "SkPicture.h"
#include "SkStream.h"
#include "SkWindow.h"

//////////////////////////////////////////////////////////////////////////////

class SimpleWindow : public SkOSWindow {
public:
	SimpleWindow(void* hwnd);

protected:
    virtual void onDraw(SkCanvas* canvas);
	virtual bool onHandleKey(SkKey key);
    virtual bool onHandleChar(SkUnichar);
    virtual void onSizeChange();
    
    virtual SkCanvas* beforeChildren(SkCanvas*);
    virtual void afterChildren(SkCanvas*);

	virtual bool onEvent(const SkEvent& evt);

private:
    typedef SkOSWindow INHERITED;
};

SimpleWindow::SimpleWindow(void* hwnd) : INHERITED(hwnd) {
//	this->setConfig(SkBitmap::kRGB_565_Config);
	this->setConfig(SkBitmap::kARGB_8888_Config);
	this->setVisibleP(true);
    this->setTitle("Simple");
}

void SimpleWindow::onDraw(SkCanvas* canvas) {
    canvas->drawColor(SK_ColorWHITE);
    
    const SkScalar w = this->width();
    const SkScalar h = this->height();

    SkPaint paint;
    paint.setAntiAlias(true);
    paint.setTextSize(SkIntToScalar(40));
    paint.setTextAlign(SkPaint::kCenter_Align);

    canvas->drawText("Hello world", 11, w/2, h/2, paint);
}

SkCanvas* SimpleWindow::beforeChildren(SkCanvas* canvas) {
    // can wack the canvas here, which will affect child views
    // and can be "undone" in afterChildren()
    //
    // e.g. return a picture-canvas, or wack the clip or matrix, etc.

    return canvas;
}

void SimpleWindow::afterChildren(SkCanvas* orig) {
}

bool SimpleWindow::onEvent(const SkEvent& evt) {
    return this->INHERITED::onEvent(evt);
}

bool SimpleWindow::onHandleChar(SkUnichar uni) {
    return this->INHERITED::onHandleChar(uni);
}

bool SimpleWindow::onHandleKey(SkKey key) {
    return this->INHERITED::onHandleKey(key);
}

void SimpleWindow::onSizeChange() {
    this->INHERITED::onSizeChange();
}

///////////////////////////////////////////////////////////////////////////////

SkOSWindow* create_sk_window(void* hwnd) {
	return new SimpleWindow(hwnd);
}

void get_preferred_size(int* x, int* y, int* width, int* height) {
    *x = 10;
    *y = 50;
    *width = 640;
    *height = 480;
}

void application_init() {
//    setenv("ANDROID_ROOT", "../../../data", 0);
    setenv("ANDROID_ROOT", "/android/device/data", 0);
	SkGraphics::Init(true);
	SkEvent::Init();
}

void application_term() {
	SkEvent::Term();
	SkGraphics::Term();
}
