LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

LOCAL_ARM_MODE := arm

LOCAL_SRC_FILES:= \
	../src/core/Sk64.cpp \
	../src/core/SkBuffer.cpp \
	../src/core/SkChunkAlloc.cpp \
	../src/core/SkCordic.cpp \
	../src/core/SkDebug.cpp \
	../src/core/SkDebug_stdio.cpp \
	../src/core/SkFloatBits.cpp \
	../src/core/SkMath.cpp \
	../src/core/SkMatrix.cpp \
	../src/core/SkMemory_stdlib.cpp \
	../src/core/SkPoint.cpp \
	../src/core/SkRect.cpp \
	../src/core/SkRegion.cpp \
	../src/core/SkString.cpp \
	../src/core/SkUtils.cpp \

LOCAL_SHARED_LIBRARIES := \
	libcutils \
	libutils

LOCAL_C_INCLUDES += \
	$(LOCAL_PATH)/../include/core

#LOCAL_CFLAGS+= 
#LOCAL_LDFLAGS:= 

LOCAL_MODULE:= libcorecg

LOCAL_CFLAGS += -fstrict-aliasing

ifeq ($(TARGET_ARCH),arm)
	LOCAL_CFLAGS += -fomit-frame-pointer
endif

include $(BUILD_SHARED_LIBRARY)
