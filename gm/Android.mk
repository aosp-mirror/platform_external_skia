
LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
	filltypes.cpp \
    gradients.cpp \
	tilemodes.cpp \
	bitmapfilters.cpp \
	xfermodes.cpp \
	gmmain.cpp

# additional optional class for this tool
LOCAL_SRC_FILES += \
	../src/utils/SkUnitMappers.cpp

LOCAL_SHARED_LIBRARIES := libcutils libskia
LOCAL_C_INCLUDES := \
    external/skia/include/config \
    external/skia/include/core \
    external/skia/include/images \
    external/skia/include/utils \
    external/skia/include/effects

#LOCAL_CFLAGS := 

LOCAL_MODULE := skia_gm

LOCAL_MODULE_TAGS := optional

include $(BUILD_EXECUTABLE)
