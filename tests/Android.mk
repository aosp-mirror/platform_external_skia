LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)


LOCAL_SRC_FILES:= \
  BitmapCopyTest.cpp \
  BitmapGetColorTest.cpp \
  BlitRowTest.cpp \
  ClampRangeTest.cpp \
  ClipCubicTest.cpp \
  ClipStackTest.cpp \
  ClipperTest.cpp \
  ColorFilterTest.cpp \
  ColorTest.cpp \
  DequeTest.cpp \
  DrawBitmapRectTest.cpp \
  FillPathTest.cpp \
  FlateTest.cpp \
  GeometryTest.cpp \
  InfRectTest.cpp \
  MathTest.cpp \
  MatrixTest.cpp \
  MetaDataTest.cpp \
  PackBitsTest.cpp \
  PaintTest.cpp \
  ParsePathTest.cpp \
  PathCoverageTest.cpp \
  PathMeasureTest.cpp \
  PathTest.cpp \
  Reader32Test.cpp \
  RefDictTest.cpp \
  RegionTest.cpp \
  Sk64Test.cpp \
  SortTest.cpp \
  SrcOverTest.cpp \
  StreamTest.cpp \
  StringTest.cpp \
  Test.cpp \
  TestSize.cpp \
  UtilsTest.cpp \
  Writer32Test.cpp \
  XfermodeTest.cpp

# The name of the file with a main function must
# match native test's naming rule: xxx_test.cpp.
LOCAL_SRC_FILES += \
  skia_test.cpp

LOCAL_MODULE:= skia_test

LOCAL_C_INCLUDES := \
        external/skia/include/core \
        external/skia/include/effects \
        external/skia/include/images \
        external/skia/include/ports \
        external/skia/include/utils \
        external/skia/src/core

LOCAL_SHARED_LIBRARIES := \
        libskia libcutils

LOCAL_MODULE_TAGS := eng tests

include $(BUILD_EXECUTABLE)
