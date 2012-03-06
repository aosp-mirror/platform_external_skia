
LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
  benchmain.cpp \
  BenchTimer.cpp \
  BenchSysTimer_posix.cpp \
  BenchGpuTimer_gl.cpp \
  SkBenchmark.cpp

LOCAL_SRC_FILES += \
  AAClipBench.cpp \
  BitmapBench.cpp \
  BlurBench.cpp \
  ChromeBench.cpp \
  DecodeBench.cpp \
  FontScalerBench.cpp \
  GradientBench.cpp \
  MathBench.cpp \
  MatrixBench.cpp \
  MutexBench.cpp \
  PathBench.cpp \
  PicturePlaybackBench.cpp \
  RectBench.cpp \
  RepeatTileBench.cpp \
  ScalarBench.cpp \
  ShaderMaskBench.cpp \
  TextBench.cpp \
  VertBench.cpp

LOCAL_SHARED_LIBRARIES := libcutils libskia libGLESv2 libEGL
LOCAL_STATIC_LIBRARIES := libskiagpu
LOCAL_C_INCLUDES := \
    external/skia/include/core \
    external/skia/include/config \
    external/skia/include/effects \
    external/skia/include/gpu \
    external/skia/include/images \
    external/skia/include/utils \
    external/skia/src/core \
    external/skia/src/gpu

#LOCAL_CFLAGS := 

LOCAL_MODULE := skia_bench

LOCAL_MODULE_TAGS := optional

include $(BUILD_EXECUTABLE)
