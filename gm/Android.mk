
LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
  gm.cpp \
  gmmain.cpp \
  system_preferences_default.cpp

# Slides
LOCAL_SRC_FILES += \
  aaclip.cpp \
  aarectmodes.cpp \
  arithmode.cpp \
  bitmapcopy.cpp \
  bitmapfilters.cpp \
  bitmapscroll.cpp \
  blurs.cpp \
  colormatrix.cpp \
  complexclip.cpp \
  complexclip2.cpp \
  convexpaths.cpp \
  cubicpaths.cpp \
  degeneratesegments.cpp \
  drawbitmaprect.cpp \
  emptypath.cpp \
  filltypes.cpp \
  filltypespersp.cpp \
  fontscaler.cpp \
  gammatext.cpp \
  gradients.cpp \
  gradtext.cpp \
  hairmodes.cpp \
  imageblur.cpp \
  lcdtext.cpp \
  linepaths.cpp \
  morphology.cpp \
  ninepatchstretch.cpp \
  nocolorbleed.cpp \
  patheffects.cpp \
  pathfill.cpp \
  pathreverse.cpp \
  points.cpp \
  poly2poly.cpp \
  quadpaths.cpp \
  shadertext.cpp \
  shadows.cpp \
  shapes.cpp \
  strokefill.cpp \
  strokerects.cpp \
  strokes.cpp \
  tablecolorfilter.cpp \
  testimagefilters.cpp \
  texdata.cpp \
  tilemodes.cpp \
  tinybitmap.cpp \
  verttext.cpp \
  verttext2.cpp \
  xfermodes.cpp

LOCAL_STATIC_LIBRARIES := libskiagpu
LOCAL_SHARED_LIBRARIES := \
  libcutils \
  libutils \
  libskia \
  libEGL \
  libGLESv2
  
LOCAL_C_INCLUDES := \
  external/skia/include/config \
  external/skia/include/core \
  external/skia/include/effects \
  external/skia/include/gpu \
  external/skia/include/images \
  external/skia/include/utils \
  external/skia/gm

#LOCAL_CFLAGS := 

LOCAL_MODULE := skia_gm

LOCAL_MODULE_TAGS := optional

include $(BUILD_EXECUTABLE)
