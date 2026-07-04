# Android.mk - NDK build file for apkpatch shared library

LOCAL_PATH := $(call my-dir)
PROJECT_ROOT := $(LOCAL_PATH)/..

include $(CLEAR_VARS)

LOCAL_MODULE := apkpatch

LOCAL_SRC_FILES := \
    apkpatch.c \
    $(PROJECT_ROOT)/common/zlib_stream.c \
    $(PROJECT_ROOT)/bspatch.c \
    $(PROJECT_ROOT)/third_party/minizip/ioapi.c \
    $(PROJECT_ROOT)/third_party/minizip/unzip.c \
    $(PROJECT_ROOT)/third_party/minizip/zip.c

LOCAL_C_INCLUDES := \
    $(PROJECT_ROOT) \
    $(PROJECT_ROOT)/third_party/minizip

LOCAL_LDLIBS := -lz -llog

include $(BUILD_SHARED_LIBRARY)
