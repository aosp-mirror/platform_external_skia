LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)

LOCAL_ARM_MODE := arm

LOCAL_SRC_FILES:= \
	effects/Sk1DPathEffect.cpp \
	effects/Sk2DPathEffect.cpp \
	effects/SkAvoidXfermode.cpp \
	effects/SkBlurDrawLooper.cpp \
	effects/SkBlurMask.cpp \
	effects/SkBlurMaskFilter.cpp \
	effects/SkCamera.cpp \
	effects/SkColorFilters.cpp \
	effects/SkColorMatrixFilter.cpp \
	effects/SkCornerPathEffect.cpp \
	effects/SkDashPathEffect.cpp \
	effects/SkDiscretePathEffect.cpp \
	effects/SkEmbossMask.cpp \
	effects/SkEmbossMaskFilter.cpp \
	effects/SkGradientShader.cpp \
	effects/SkLayerRasterizer.cpp \
	effects/SkNinePatch.cpp \
	effects/SkPaintFlagsDrawFilter.cpp \
	effects/SkPixelXorXfermode.cpp \
	effects/SkShaderExtras.cpp \
	effects/SkTransparentShader.cpp \
    gl/SkGL.cpp \
    gl/SkGLCanvas.cpp \
    gl/SkGLDevice.cpp \
    gl/SkGLDevice_SWLayer.cpp \
    gl/SkGLTextCache.cpp \
    gl/SkTextureCache.cpp \
    images/bmpdecoderhelper.cpp \
	images/SkImageDecoder.cpp \
	images/SkImageDecoder_libbmp.cpp \
	images/SkImageDecoder_libgif.cpp \
	images/SkImageDecoder_libjpeg.cpp \
	images/SkImageDecoder_libpng.cpp \
	images/SkImageDecoder_libico.cpp \
	images/SkImageDecoder_wbmp.cpp \
	images/SkImageRef.cpp \
	images/SkImageRef_GlobalPool.cpp \
	images/SkImageRefPool.cpp \
	images/SkMMapStream.cpp \
	images/SkMovie.cpp \
	images/SkMovie_gif.cpp \
    images/SkScaledBitmapSampler.cpp \
	images/SkStream.cpp \
	images/SkCreateRLEPixelRef.cpp \
    picture/SkPictureFlat.cpp \
    picture/SkPicturePlayback.cpp \
    picture/SkPictureRecord.cpp \
	ports/SkImageDecoder_Factory.cpp \
	ports/SkFontHost_android.cpp \
	ports/SkFontHost_gamma.cpp \
	ports/SkFontHost_FreeType.cpp \
	ports/SkGlobals_global.cpp \
	ports/SkImageRef_ashmem.cpp \
	ports/SkOSFile_stdio.cpp \
	ports/SkTime_Unix.cpp \
    ports/SkXMLPullParser_expat.cpp \
	sgl/SkAlphaRuns.cpp \
	sgl/SkBitmap.cpp \
	sgl/SkBitmap_scroll.cpp \
	sgl/SkBitmapProcShader.cpp \
	sgl/SkBitmapProcState.cpp \
	sgl/SkBitmapProcState_matrixProcs.cpp \
	sgl/SkBitmapSampler.cpp \
	sgl/SkBitmapShader.cpp \
	sgl/SkBlitRow_D16.cpp \
	sgl/SkBlitRow_D4444.cpp \
	sgl/SkBlitter.cpp \
	sgl/SkBlitter_4444.cpp \
	sgl/SkBlitter_A1.cpp \
	sgl/SkBlitter_A8.cpp \
	sgl/SkBlitter_ARGB32.cpp \
	sgl/SkBlitter_RGB16.cpp \
	sgl/SkBlitter_Sprite.cpp \
	sgl/SkCanvas.cpp \
	sgl/SkColor.cpp \
	sgl/SkColorFilter.cpp \
	sgl/SkColorTable.cpp \
	sgl/SkDeque.cpp \
	sgl/SkDevice.cpp \
	sgl/SkDither.cpp \
	sgl/SkDraw.cpp \
	sgl/SkEdge.cpp \
	sgl/SkFilterProc.cpp \
	sgl/SkFlattenable.cpp \
	sgl/SkGeometry.cpp \
	sgl/SkGlobals.cpp \
	sgl/SkGlyphCache.cpp \
	sgl/SkGraphics.cpp \
	sgl/SkMask.cpp \
	sgl/SkMaskFilter.cpp \
	sgl/SkPackBits.cpp \
	sgl/SkPaint.cpp \
	sgl/SkPath.cpp \
	sgl/SkPathEffect.cpp \
	sgl/SkPathMeasure.cpp \
	sgl/SkPicture.cpp \
	sgl/SkPixelRef.cpp \
	sgl/SkProcSpriteBlitter.cpp \
    sgl/SkPtrRecorder.cpp \
	sgl/SkRasterizer.cpp \
	sgl/SkRefCnt.cpp \
	sgl/SkRegion_path.cpp \
	sgl/SkScalerContext.cpp \
	sgl/SkScan.cpp \
	sgl/SkScan_AntiPath.cpp \
	sgl/SkScan_Antihair.cpp \
	sgl/SkScan_Hairline.cpp \
	sgl/SkScan_Path.cpp \
	sgl/SkShader.cpp \
	sgl/SkSpriteBlitter_ARGB32.cpp \
	sgl/SkSpriteBlitter_RGB16.cpp \
	sgl/SkString.cpp \
	sgl/SkStroke.cpp \
	sgl/SkStrokerPriv.cpp \
	sgl/SkTSearch.cpp \
	sgl/SkTypeface.cpp \
    sgl/SkUnPreMultiply.cpp \
	sgl/SkUtils.cpp \
	sgl/SkXfermode.cpp \
	sgl/SkWriter32.cpp \
	xml/SkXMLPullParser.cpp

LOCAL_SHARED_LIBRARIES := \
	libcutils \
	libutils \
	libcorecg \
	libexpat \
	libGLES_CM \
	libz

LOCAL_STATIC_LIBRARIES := \
	libft2 \
	libpng \
	libgif \
	libjpeg

LOCAL_C_INCLUDES += \
	$(LOCAL_PATH)/animator \
	$(LOCAL_PATH)/sgl \
	$(LOCAL_PATH)/images \
	$(LOCAL_PATH)/picture \
	$(LOCAL_PATH)/ports \
	$(LOCAL_PATH)/../libcorecg \
	$(call include-path-for, graphics corecg) \
	external/freetype/include \
	external/zlib \
	external/libpng \
	external/giflib \
	external/expat/lib \
	external/jpeg

LOCAL_CFLAGS += -fpic -fstrict-aliasing

LOCAL_LDLIBS += -lpthread

LOCAL_MODULE:= libsgl

include $(BUILD_SHARED_LIBRARY)
