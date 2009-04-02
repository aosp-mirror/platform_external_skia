LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

LOCAL_SRC_FILES:= \
	GeometryTest.cpp \
	MathTest.cpp \
	MatrixTest.cpp \
	PackBitsTest.cpp \
	Sk64Test.cpp \
	StringTest.cpp \
	Test.cpp UtilsTest.cpp \
	PathTest.cpp \
	SrcOverTest.cpp \
	StreamTest.cpp \
	SortTest.cpp \
	PathMeasureTest.cpp \
	testmain.cpp

LOCAL_C_INCLUDES := \
	external/skia/include/core \
	external/skia/include/effects \
	external/skia/include/images \
	external/skia/include/ports \
	external/skia/include/utils \
	external/skia/src/core

LOCAL_SHARED_LIBRARIES := \
	libcorecg \
	libsgl

LOCAL_MODULE:= test-skia-unit

LOCAL_MODULE_TAGS := tests

include $(BUILD_EXECUTABLE)
