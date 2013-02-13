LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)


LOCAL_SRC_FILES:= \
  AAClipTest.cpp \
  BitmapCopyTest.cpp \
  BitmapGetColorTest.cpp \
  BlitRowTest.cpp \
  BlurTest.cpp \
  CanvasTest.cpp \
  ClampRangeTest.cpp \
  ClipCubicTest.cpp \
  ClipStackTest.cpp \
  ClipperTest.cpp \
  ColorFilterTest.cpp \
  ColorTest.cpp \
  DataRefTest.cpp \
  DeferredCanvasTest.cpp \
  DequeTest.cpp \
  DrawBitmapRectTest.cpp \
  DrawTextTest.cpp \
  EmptyPathTest.cpp \
  FillPathTest.cpp \
  FlateTest.cpp \
  FontHostTest.cpp \
  GeometryTest.cpp \
  GLInterfaceValidation.cpp \
  GLProgramsTest.cpp \
  InfRectTest.cpp \
  MathTest.cpp \
  MatrixTest.cpp \
  Matrix44Test.cpp \
  MemsetTest.cpp \
  MetaDataTest.cpp \
  PackBitsTest.cpp \
  PaintTest.cpp \
  ParsePathTest.cpp \
  PathCoverageTest.cpp \
  PathMeasureTest.cpp \
  PathTest.cpp \
  PointTest.cpp \
  PremulAlphaRoundTripTest.cpp \
  QuickRejectTest.cpp \
  Reader32Test.cpp \
  ReadPixelsTest.cpp \
  RefDictTest.cpp \
  RegionTest.cpp \
  ScalarTest.cpp \
  ShaderOpacityTest.cpp \
  Sk64Test.cpp \
  skia_test.cpp \
  SortTest.cpp \
  SrcOverTest.cpp \
  StreamTest.cpp \
  StringTest.cpp \
  Test.cpp \
  Test.h \
  TestSize.cpp \
  UnicodeTest.cpp \
  UtilsTest.cpp \
  WArrayTest.cpp \
  WritePixelsTest.cpp \
  Writer32Test.cpp \
  XfermodeTest.cpp

# TODO: tests that currently are causing build problems
#LOCAL_SRC_FILES += \
#  BitSetTest.cpp \
#  PDFPrimitivesTest.cpp \
#  ToUnicode.cpp

LOCAL_MODULE:= skia_test

LOCAL_C_INCLUDES := \
   external/freetype/include \
   external/skia/include/core \
   external/skia/include/config \
   external/skia/include/effects \
   external/skia/include/gpu \
   external/skia/include/images \
   external/skia/include/pdf \
   external/skia/include/ports \
   external/skia/include/utils \
   external/skia/src/core \
   external/skia/src/gpu

LOCAL_SHARED_LIBRARIES := libcutils libskia libGLESv2 libEGL
LOCAL_STATIC_LIBRARIES := libskiagpu

LOCAL_MODULE_TAGS := eng tests

include $(BUILD_EXECUTABLE)
