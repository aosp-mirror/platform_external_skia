
LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
  aarectmodes.cpp \
  bitmapfilters.cpp \
  bitmapscroll.cpp \
  blurs.cpp \
  complexclip.cpp \
  complexclip2.cpp \
  emptypath.cpp \
  filltypes.cpp \
  filltypespersp.cpp \
  gm.cpp \
  gmmain.cpp \
  gradients.cpp \
  hairmodes.cpp \
  lcdtext.cpp \
  ninepatchstretch.cpp \
  nocolorbleed.cpp \
  pathfill.cpp \
  points.cpp \
  poly2poly.cpp \
  shadertext.cpp \
  shadows.cpp \
  shapes.cpp \
  strokerects.cpp \
  strokes.cpp \
  texdata.cpp \
  tilemodes.cpp \
  tinybitmap.cpp \
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
