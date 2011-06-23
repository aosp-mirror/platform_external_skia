
LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
  BenchGpuTimer_none.cpp \
  BenchSysTimer_posix.cpp \
  BenchTimer.cpp \
  BitmapBench.cpp \
  DecodeBench.cpp \
  FPSBench.cpp \
  GradientBench.cpp \
  MatrixBench.cpp \
  PathBench.cpp \
  RectBench.cpp \
  RepeatTileBench.cpp \
  TextBench.cpp \
  SkBenchmark.cpp \
  benchmain.cpp

# additional optional class for this tool
LOCAL_SRC_FILES += \
    ../src/utils/SkNWayCanvas.cpp \
    ../src/utils/SkParse.cpp

LOCAL_SHARED_LIBRARIES := libcutils libskia libGLESv2
LOCAL_STATIC_LIBRARIES := libskiagpu
LOCAL_C_INCLUDES := \
    external/skia/include/config \
    external/skia/include/core \
    external/skia/include/images \
    external/skia/include/utils \
    external/skia/include/effects \
    external/skia/gpu/include \
    external/skia/include/gpu

#LOCAL_CFLAGS := 

LOCAL_MODULE := skia_bench

LOCAL_MODULE_TAGS := optional

include $(BUILD_EXECUTABLE)
