
LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
  bitmapfilters.cpp \
  blurs.cpp \
  complexclip.cpp \
  filltypes.cpp \
  gradients.cpp \
  nocolorbleed.cpp \
  pathfill.cpp \
  points.cpp \
  poly2poly.cpp \
  shadertext.cpp \
  shadows.cpp \
  shapes.cpp \
  strokerects.cpp \
  tilemodes.cpp \
  xfermodes.cpp \
  gmmain.cpp

# additional optional class for this tool
LOCAL_SRC_FILES += \
  ../src/utils/SkEGLContext_none.cpp

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
  external/skia/include/images \
  external/skia/include/utils \
  external/skia/include/effects \
  external/skia/gpu/include \
  external/skia/include/gpu

#LOCAL_CFLAGS := 

LOCAL_MODULE := skia_gm

LOCAL_MODULE_TAGS := optional

include $(BUILD_EXECUTABLE)
