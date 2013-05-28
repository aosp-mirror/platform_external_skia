
LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
  benchmain.cpp \
  SkBenchmark.cpp \
  BenchTimer.cpp \
  BenchSysTimer_posix.cpp \
  BenchGpuTimer_gl.cpp \
  SkBenchLogger.cpp \
  TimerData.cpp

LOCAL_SRC_FILES += \
  AAClipBench.cpp \
  BicubicBench.cpp \
  BitmapBench.cpp \
  BitmapRectBench.cpp \
  BlendBench.cpp \
  BlurBench.cpp \
  BlurImageFilterBench.cpp \
  BlurRectBench.cpp \
  ChartBench.cpp \
  ChromeBench.cpp \
  ColorFilterBench.cpp \
  DashBench.cpp \
  DecodeBench.cpp \
  DeferredCanvasBench.cpp \
  DisplacementBench.cpp \
  FontScalerBench.cpp \
  GameBench.cpp \
  GradientBench.cpp \
  GrMemoryPoolBench.cpp \
  InterpBench.cpp \
  LineBench.cpp \
  LightingBench.cpp \
  MagnifierBench.cpp \
  MathBench.cpp \
  Matrix44Bench.cpp \
  MatrixBench.cpp \
  MatrixConvolutionBench.cpp \
  MemoryBench.cpp \
  MergeBench.cpp \
  MorphologyBench.cpp \
  MutexBench.cpp \
  PathBench.cpp \
  PathIterBench.cpp \
  PerlinNoiseBench.cpp \
  PicturePlaybackBench.cpp \
  PictureRecordBench.cpp \
  ReadPixBench.cpp \
  RectBench.cpp \
  RectoriBench.cpp \
  RefCntBench.cpp \
  RegionBench.cpp \
  RegionContainBench.cpp \
  RepeatTileBench.cpp \
  RTreeBench.cpp \
  ScalarBench.cpp \
  ShaderMaskBench.cpp \
  SortBench.cpp \
  StrokeBench.cpp \
  TableBench.cpp \
  TextBench.cpp \
  TileBench.cpp \
  VertBench.cpp \
  WriterBench.cpp \
  XfermodeBench.cpp

# Files that are missing dependencies
#LOCAL_SRC_FILES += \
#  ChecksumBench.cpp \
#  DeferredSurfaceCopyBench.cpp \

# When built as part of the system image we can enable certian non-NDK compliant
# optimizations.
LOCAL_CFLAGS += -DSK_BUILD_FOR_ANDROID_FRAMEWORK
LOCAL_CFLAGS += -DSK_SUPPORT_GPU

LOCAL_SHARED_LIBRARIES := libcutils libskia libGLESv2 libEGL 

LOCAL_STATIC_LIBRARIES := libstlport_static

LOCAL_C_INCLUDES := \
  external/skia/src/core \
  external/skia/src/effects \
  external/skia/src/utils \
  external/skia/src/gpu \
  external/stlport/stlport \
  bionic

LOCAL_MODULE := skia_bench

LOCAL_MODULE_TAGS := optional

include $(BUILD_EXECUTABLE)
