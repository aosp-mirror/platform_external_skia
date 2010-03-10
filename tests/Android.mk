LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)


LOCAL_SRC_FILES:= \
        BlitRowTest.cpp \
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
        PathMeasureTest.cpp

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
