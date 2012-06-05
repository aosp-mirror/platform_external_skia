BASE_PATH := $(call my-dir)
LOCAL_PATH:= $(call my-dir)

###############################################################################
#
# PROBLEMS WITH SKIA DEBUGGING?? READ THIS...
#
# The debug build results in changes to the Skia headers. This means that those
# using libskia must also be built with the debug version of the Skia headers.
# There are a few scenarios where this comes into play:
#
# (1) You're building debug code that depends on libskia.
#   (a) If libskia is built in release, then define SK_RELEASE when building
#       your sources.
#   (b) If libskia is built with debugging (see step 2), then no changes are
#       needed since your sources and libskia have been built with SK_DEBUG.
# (2) You're building libskia in debug mode.
#   (a) RECOMMENDED: You can build the entire system in debug mode. Do this by
#       updating your buildspec.mk to include TARGET_BUILD_TYPE=debug
#   (b) You can update all the users of libskia to define SK_DEBUG when they are
#       building their sources.
#
# NOTE: If neither SK_DEBUG or SK_RELEASE are defined then Skia checks NDEBUG to
#       determine which build type to use.
###############################################################################


#############################################################
#   build the skia+fretype+png+jpeg+zlib+gif+webp library
#

include $(CLEAR_VARS)

LOCAL_ARM_MODE := arm

# need a flag to tell the C side when we're on devices with large memory
# budgets (i.e. larger than the low-end devices that initially shipped)
ifeq ($(ARCH_ARM_HAVE_VFP),true)
    LOCAL_CFLAGS += -DANDROID_LARGE_MEMORY_DEVICE
endif

ifneq ($(ARCH_ARM_HAVE_VFP),true)
	LOCAL_CFLAGS += -DSK_SOFTWARE_FLOAT
endif

ifeq ($(ARCH_ARM_HAVE_NEON),true)
	LOCAL_CFLAGS += -D__ARM_HAVE_NEON
endif

# special checks for alpha == 0 and alpha == 255 in S32A_Opaque_BlitRow32
# procedures (C and assembly) seriously improve skia performance
LOCAL_CFLAGS += -DTEST_SRC_ALPHA

# using freetype's embolden allows us to adjust fake bold settings at
# draw-time, at which point we know which SkTypeface is being drawn
LOCAL_CFLAGS += -DSK_USE_FREETYPE_EMBOLDEN

LOCAL_SRC_FILES:= \
	src/core/Sk64.cpp \
	src/core/SkAAClip.cpp \
	src/core/SkAdvancedTypefaceMetrics.cpp \
	src/core/SkAlphaRuns.cpp \
	src/core/SkBitmap.cpp \
	src/core/SkBitmapProcShader.cpp \
	src/core/SkBitmapProcState.cpp \
	src/core/SkBitmapProcState_matrixProcs.cpp \
	src/core/SkBitmapSampler.cpp \
	src/core/SkBitmap_scroll.cpp \
	src/core/SkBlitMask_D32.cpp \
	src/core/SkBlitRow_D16.cpp \
	src/core/SkBlitRow_D32.cpp \
	src/core/SkBlitRow_D4444.cpp \
	src/core/SkBlitter.cpp \
	src/core/SkBlitter_4444.cpp \
	src/core/SkBlitter_A1.cpp \
	src/core/SkBlitter_A8.cpp \
	src/core/SkBlitter_ARGB32.cpp \
	src/core/SkBlitter_RGB16.cpp \
	src/core/SkBlitter_Sprite.cpp \
	src/core/SkBuffer.cpp \
	src/core/SkCanvas.cpp \
	src/core/SkChunkAlloc.cpp \
	src/core/SkClampRange.cpp \
	src/core/SkClipStack.cpp \
	src/core/SkColor.cpp \
	src/core/SkColorFilter.cpp \
	src/core/SkColorTable.cpp \
	src/core/SkComposeShader.cpp \
	src/core/SkConcaveToTriangles.cpp \
	src/core/SkConfig8888.cpp \
	src/core/SkCordic.cpp \
	src/core/SkCubicClipper.cpp \
	src/core/SkData.cpp \
	src/core/SkDebug.cpp \
	src/core/SkDeque.cpp \
	src/core/SkDevice.cpp \
	src/core/SkDeviceProfile.cpp \
	src/core/SkDither.cpp \
	src/core/SkDraw.cpp \
  src/core/SkEdgeBuilder.cpp \
	src/core/SkEdgeClipper.cpp \
  src/core/SkEdge.cpp \
	src/core/SkFilterProc.cpp \
	src/core/SkFlattenable.cpp \
	src/core/SkFloat.cpp \
	src/core/SkFloatBits.cpp \
	src/core/SkFontHost.cpp \
	src/core/SkGeometry.cpp \
	src/core/SkGlyphCache.cpp \
	src/core/SkGraphics.cpp \
	src/core/SkLineClipper.cpp \
	src/core/SkMallocPixelRef.cpp \
	src/core/SkMask.cpp \
	src/core/SkMaskFilter.cpp \
	src/core/SkMath.cpp \
	src/core/SkMatrix.cpp \
	src/core/SkMetaData.cpp \
	src/core/SkMMapStream.cpp \
	src/core/SkPackBits.cpp \
	src/core/SkPaint.cpp \
	src/core/SkPath.cpp \
	src/core/SkPathEffect.cpp \
	src/core/SkPathHeap.cpp \
	src/core/SkPathMeasure.cpp \
	src/core/SkPicture.cpp \
	src/core/SkPictureFlat.cpp \
	src/core/SkPicturePlayback.cpp \
	src/core/SkPictureRecord.cpp \
	src/core/SkPixelRef.cpp \
	src/core/SkPoint.cpp \
	src/core/SkProcSpriteBlitter.cpp \
	src/core/SkPtrRecorder.cpp \
	src/core/SkQuadClipper.cpp \
	src/core/SkRasterClip.cpp \
	src/core/SkRasterizer.cpp \
	src/core/SkRect.cpp \
	src/core/SkRefDict.cpp \
	src/core/SkRegion.cpp \
	src/core/SkRegion_path.cpp \
  src/core/SkScalar.cpp \
  src/core/SkScalerContext.cpp \
	src/core/SkScan.cpp \
	src/core/SkScan_AntiPath.cpp \
	src/core/SkScan_Antihair.cpp \
	src/core/SkScan_Hairline.cpp \
	src/core/SkScan_Path.cpp \
	src/core/SkShader.cpp \
	src/core/SkShape.cpp \
	src/core/SkSpriteBlitter_ARGB32.cpp \
	src/core/SkSpriteBlitter_RGB16.cpp \
	src/core/SkStream.cpp \
	src/core/SkString.cpp \
	src/core/SkStroke.cpp \
	src/core/SkStrokerPriv.cpp \
	src/core/SkTSearch.cpp \
	src/core/SkTypeface.cpp \
	src/core/SkTypefaceCache.cpp \
	src/core/SkUnPreMultiply.cpp \
	src/core/SkUtils.cpp \
	src/core/SkFlate.cpp \
	src/core/SkWriter32.cpp \
	src/core/SkXfermode.cpp \
	src/effects/Sk1DPathEffect.cpp \
	src/effects/Sk2DPathEffect.cpp \
	src/effects/SkAvoidXfermode.cpp \
	src/effects/SkArithmeticMode.cpp \
	src/effects/SkBitmapCache.cpp \
	src/effects/SkBlurDrawLooper.cpp \
	src/effects/SkBlurImageFilter.cpp \
	src/effects/SkBlurMask.cpp \
	src/effects/SkBlurMaskFilter.cpp \
	src/effects/SkColorFilters.cpp \
	src/effects/SkColorMatrixFilter.cpp \
	src/effects/SkCornerPathEffect.cpp \
	src/effects/SkDashPathEffect.cpp \
	src/effects/SkDiscretePathEffect.cpp \
	src/effects/SkEffects.cpp \
	src/effects/SkEmbossMask.cpp \
	src/effects/SkEmbossMaskFilter.cpp \
	src/effects/SkGradientShader.cpp \
	src/effects/SkGroupShape.cpp \
	src/effects/SkKernel33MaskFilter.cpp \
	src/effects/SkLayerDrawLooper.cpp \
	src/effects/SkLayerRasterizer.cpp \
	src/effects/SkMorphologyImageFilter.cpp \
	src/effects/SkPaintFlagsDrawFilter.cpp \
	src/effects/SkPixelXorXfermode.cpp \
	src/effects/SkPorterDuff.cpp \
	src/effects/SkRectShape.cpp \
	src/effects/SkTableColorFilter.cpp \
  src/effects/SkTableMaskFilter.cpp \
  src/effects/SkTestImageFilters.cpp \
	src/effects/SkTransparentShader.cpp \
	src/images/bmpdecoderhelper.cpp \
	src/images/SkBitmapRegionDecoder.cpp \
	src/images/SkCreateRLEPixelRef.cpp \
	src/images/SkFDStream.cpp \
	src/images/SkFlipPixelRef.cpp \
	src/images/SkImageDecoder.cpp \
	src/images/SkImageDecoder_Factory.cpp \
	src/images/SkImageDecoder_libbmp.cpp \
	src/images/SkImageDecoder_libgif.cpp \
	src/images/SkImageDecoder_libico.cpp \
	src/images/SkImageDecoder_libjpeg.cpp \
	src/images/SkImageDecoder_libpng.cpp \
	src/images/SkImageDecoder_libwebp.cpp \
	src/images/SkImageDecoder_wbmp.cpp \
	src/images/SkImageEncoder.cpp \
	src/images/SkImageEncoder_Factory.cpp \
	src/images/SkImageRef.cpp \
	src/images/SkImageRefPool.cpp \
	src/images/SkImageRef_GlobalPool.cpp \
	src/images/SkJpegUtility.cpp \
	src/images/SkMovie.cpp \
	src/images/SkMovie_gif.cpp \
	src/images/SkPageFlipper.cpp \
	src/images/SkScaledBitmapSampler.cpp \
	src/ports/FontHostConfiguration_android.cpp \
	src/ports/SkDebug_android.cpp \
	src/ports/SkGlobalInitialization_default.cpp \
	src/ports/SkFontHost_FreeType.cpp \
	src/ports/SkFontHost_sandbox_none.cpp	\
	src/ports/SkFontHost_android.cpp \
	src/ports/SkFontHost_gamma.cpp \
	src/ports/SkFontHost_tables.cpp \
	src/ports/SkImageRef_ashmem.cpp \
	src/ports/SkMemory_malloc.cpp \
	src/ports/SkOSFile_stdio.cpp \
	src/ports/SkThread_pthread.cpp \
	src/ports/SkTime_Unix.cpp \
	src/utils/SkBase64.cpp \
	src/utils/SkBoundaryPatch.cpp \
	src/utils/SkCamera.cpp \
	src/utils/SkColorMatrix.cpp \
  src/utils/SkCubicInterval.cpp \
	src/utils/SkCullPoints.cpp \
	src/utils/SkDeferredCanvas.cpp \
	src/utils/SkDumpCanvas.cpp \
	src/utils/SkInterpolator.cpp \
	src/utils/SkLayer.cpp \
	src/utils/SkMatrix44.cpp \
	src/utils/SkMeshUtils.cpp \
	src/utils/SkNinePatch.cpp \
	src/utils/SkNWayCanvas.cpp \
	src/utils/SkOSFile.cpp \
	src/utils/SkParse.cpp \
	src/utils/SkParseColor.cpp \
	src/utils/SkParsePath.cpp \
	src/utils/SkProxyCanvas.cpp \
	src/utils/SkSfntUtils.cpp \
	src/utils/SkUnitMappers.cpp

ifeq ($(TARGET_ARCH),arm)

ifeq ($(ARCH_ARM_HAVE_NEON),true)
LOCAL_SRC_FILES += \
	src/opts/memset16_neon.S \
	src/opts/memset32_neon.S
endif

LOCAL_SRC_FILES += \
	src/opts/opts_check_arm.cpp \
	src/opts/memset.arm.S \
	src/opts/SkBitmapProcState_opts_arm.cpp \
	src/opts/SkBlitRow_opts_arm.cpp
else
LOCAL_SRC_FILES += \
	src/opts/SkBlitRow_opts_none.cpp \
	src/opts/SkBitmapProcState_opts_none.cpp \
	src/opts/SkUtils_opts_none.cpp
endif

# these are for emoji support, needed by webkit
LOCAL_SRC_FILES += \
	emoji/EmojiFont.cpp

LOCAL_SHARED_LIBRARIES := \
	libcutils \
	libemoji \
	libjpeg \
	libutils \
	libz \
	libexpat

LOCAL_STATIC_LIBRARIES := \
	libft2 \
	libpng \
	libgif \
	libwebp-decode \
	libwebp-encode

LOCAL_C_INCLUDES += \
	$(LOCAL_PATH)/src/core \
	$(LOCAL_PATH)/include/core \
	$(LOCAL_PATH)/include/config \
	$(LOCAL_PATH)/include/effects \
	$(LOCAL_PATH)/include/images \
	$(LOCAL_PATH)/include/utils \
	$(LOCAL_PATH)/include/xml \
	external/freetype/include \
	external/zlib \
	external/libpng \
	external/giflib \
	external/jpeg \
	external/webp/include \
	frameworks/opt/emoji \
	external/expat/lib

ifeq ($(NO_FALLBACK_FONT),true)
	LOCAL_CFLAGS += -DNO_FALLBACK_FONT
endif

LOCAL_LDLIBS += -lpthread

LOCAL_MODULE:= libskia

include $(BUILD_SHARED_LIBRARY)

#############################################################
# Build the skia gpu (ganesh) library
#

include $(CLEAR_VARS)

LOCAL_ARM_MODE := arm

ifneq ($(ARCH_ARM_HAVE_VFP),true)
       LOCAL_CFLAGS += -DSK_SOFTWARE_FLOAT
endif

ifeq ($(ARCH_ARM_HAVE_NEON),true)
       LOCAL_CFLAGS += -DGR_ANDROID_BUILD=1
endif

LOCAL_SRC_FILES:= \
  src/gpu/GrPrintf_skia.cpp \
  src/gpu/SkGpuCanvas.cpp \
  src/gpu/SkGpuDevice.cpp \
  src/gpu/SkGr.cpp \
  src/gpu/SkGrFontScaler.cpp \
  src/gpu/SkGrTexturePixelRef.cpp \
  src/gpu/android/SkNativeGLContext_android.cpp \
  src/gpu/gl/SkGLContext.cpp \
  src/gpu/gl/SkNullGLContext.cpp

LOCAL_SRC_FILES += \
  src/gpu/GrAAHairLinePathRenderer.cpp \
  src/gpu/GrAAConvexPathRenderer.cpp \
  src/gpu/GrAddPathRenderers_default.cpp \
  src/gpu/GrAllocPool.cpp \
  src/gpu/GrAtlas.cpp \
  src/gpu/GrBufferAllocPool.cpp \
  src/gpu/GrClip.cpp \
  src/gpu/GrContext.cpp \
  src/gpu/GrDefaultPathRenderer.cpp \
  src/gpu/GrDrawTarget.cpp \
  src/gpu/GrGpu.cpp \
  src/gpu/GrGpuFactory.cpp \
  src/gpu/GrInOrderDrawBuffer.cpp \
  src/gpu/GrMatrix.cpp \
  src/gpu/GrMemory.cpp \
  src/gpu/GrPathRendererChain.cpp \
  src/gpu/GrPathRenderer.cpp \
  src/gpu/GrPathUtils.cpp \
  src/gpu/GrRectanizer.cpp \
  src/gpu/GrRenderTarget.cpp \
  src/gpu/GrResource.cpp \
  src/gpu/GrResourceCache.cpp \
  src/gpu/GrStencil.cpp \
  src/gpu/GrStencilBuffer.cpp \
  src/gpu/GrTesselatedPathRenderer.cpp \
  src/gpu/GrTextContext.cpp \
  src/gpu/GrTextStrike.cpp \
  src/gpu/GrTexture.cpp \
  src/gpu/gr_unittests.cpp \
  src/gpu/android/GrGLCreateNativeInterface_android.cpp

LOCAL_SRC_FILES += \
  src/gpu/gl/GrGLCaps.cpp \
  src/gpu/gl/GrGLContextInfo.cpp \
  src/gpu/gl/GrGLCreateNullInterface.cpp \
  src/gpu/gl/GrGLDefaultInterface_native.cpp \
  src/gpu/gl/GrGLIndexBuffer.cpp \
  src/gpu/gl/GrGLInterface.cpp \
  src/gpu/gl/GrGLProgram.cpp \
  src/gpu/gl/GrGLRenderTarget.cpp \
  src/gpu/gl/GrGLSL.cpp \
  src/gpu/gl/GrGLStencilBuffer.cpp \
  src/gpu/gl/GrGLTexture.cpp \
  src/gpu/gl/GrGLUtil.cpp \
  src/gpu/gl/GrGLVertexBuffer.cpp \
  src/gpu/gl/GrGpuGL.cpp \
  src/gpu/gl/GrGpuGLShaders.cpp
  
LOCAL_STATIC_LIBRARIES := libskiatess
LOCAL_SHARED_LIBRARIES := \
  libcutils \
  libutils \
  libskia \
  libEGL \
  libGLESv2

LOCAL_C_INCLUDES += \
  $(LOCAL_PATH)/include/core \
  $(LOCAL_PATH)/include/config \
  $(LOCAL_PATH)/include/gpu \
  $(LOCAL_PATH)/src/core \
  $(LOCAL_PATH)/src/gpu \
  $(LOCAL_PATH)/third_party/glu \
  frameworks/base/opengl/include

LOCAL_LDLIBS += -lpthread

LOCAL_MODULE:= libskiagpu
LOCAL_MODULE_TAGS := optional

include $(BUILD_STATIC_LIBRARY)

#############################################################
# Build the skia gpu (ganesh) library
#

include $(CLEAR_VARS)

LOCAL_ARM_MODE := arm

LOCAL_SRC_FILES := \
  third_party/glu/libtess/dict.c \
  third_party/glu/libtess/geom.c \
  third_party/glu/libtess/memalloc.c \
  third_party/glu/libtess/mesh.c \
  third_party/glu/libtess/normal.c \
  third_party/glu/libtess/priorityq.c \
  third_party/glu/libtess/render.c \
  third_party/glu/libtess/sweep.c \
  third_party/glu/libtess/tess.c \
  third_party/glu/libtess/tessmono.c

LOCAL_SHARED_LIBRARIES := \
  libcutils \
  libutils \
  libEGL \
  libGLESv2

LOCAL_C_INCLUDES += \
  $(LOCAL_PATH)/third_party/glu \
  $(LOCAL_PATH)/third_party/glu/libtess \
  frameworks/base/opengl/include

LOCAL_LDLIBS += -lpthread

LOCAL_MODULE:= libskiatess
LOCAL_MODULE_TAGS := optional

include $(BUILD_STATIC_LIBRARY)

#############################################################
# Build the skia tools
#

# benchmark (timings)
include $(BASE_PATH)/bench/Android.mk

# golden-master (fidelity / regression test)
include $(BASE_PATH)/gm/Android.mk

# unit-tests
include $(BASE_PATH)/tests/Android.mk
