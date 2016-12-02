LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE:= avcodec-prebuilt-armeabi
LOCAL_SRC_FILES:= ../ffmpeg/arm/lib/libavcodec-57.so
include $(PREBUILT_SHARED_LIBRARY)

include $(CLEAR_VARS)
LOCAL_MODULE:= avdevice-prebuilt-armeabi
LOCAL_SRC_FILES:= ../ffmpeg/arm/lib/libavdevice-57.so
include $(PREBUILT_SHARED_LIBRARY)

include $(CLEAR_VARS)
LOCAL_MODULE:= avfilter-prebuilt-armeabi
LOCAL_SRC_FILES:= ../ffmpeg/arm/lib/libavfilter-6.so
include $(PREBUILT_SHARED_LIBRARY)

include $(CLEAR_VARS)
LOCAL_MODULE:= avformat-prebuilt-armeabi
LOCAL_SRC_FILES:= ../ffmpeg/arm/lib/libavformat-57.so
include $(PREBUILT_SHARED_LIBRARY)

include $(CLEAR_VARS)
LOCAL_MODULE :=  avutil-prebuilt-armeabi
LOCAL_SRC_FILES := ../ffmpeg/arm/lib/libavutil-55.so
include $(PREBUILT_SHARED_LIBRARY)

include $(CLEAR_VARS)
LOCAL_MODULE := swresample-prebuilt-armeabi
LOCAL_SRC_FILES := ../ffmpeg/arm/lib/libswresample-2.so
include $(PREBUILT_SHARED_LIBRARY)

include $(CLEAR_VARS)
LOCAL_MODULE := swscale-prebuilt-armeabi
LOCAL_SRC_FILES := ../ffmpeg/arm/lib/libswscale-4.so
include $(PREBUILT_SHARED_LIBRARY)

include $(CLEAR_VARS)

LOCAL_MODULE := main

SDL_PATH := ../SDL

LOCAL_C_INCLUDES := $(LOCAL_PATH)/$(SDL_PATH)/include
LOCAL_C_INCLUDES += -L$(SYSROOT)/usr/include
LOCAL_C_INCLUDES += $(LOCAL_PATH)/../ffmpeg/arm/include

# Add your application source files here...
LOCAL_SRC_FILES := $(SDL_PATH)/src/main/android/SDL_android_main.c \
	main.c

LOCAL_SHARED_LIBRARIES := SDL2 \
						 avcodec-prebuilt-armeabi \
                         avdevice-prebuilt-armeabi \
                         avfilter-prebuilt-armeabi \
                         avformat-prebuilt-armeabi \
                         avutil-prebuilt-armeabi \
                         swresample-prebuilt-armeabi \
                         swscale-prebuilt-armeabi

LOCAL_LDLIBS := -lGLESv1_CM -lGLESv2 -llog

include $(BUILD_SHARED_LIBRARY)
