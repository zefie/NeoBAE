# Copyright (C) 2010 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

# build miniBAE
NDK_TOOLCHAIN_VERSION=clang

LOCAL_PATH := $(call my-dir)/../../BAE_Source
include $(CLEAR_VARS)

LOCAL_MODULE    := miniBAE
LOCAL_SRC_FILES	:= \
			Common/DriverTools.c \
			Common/GenAudioStreams.c \
			Common/GenCache.c \
			Common/GenChorus.c \
			Common/GenFiltersReverbU3232.c \
			Common/GenInterp2ReverbU3232.c \
			Common/GenOutput.c \
			Common/GenPatch.c \
			Common/GenReverb.c \
			Common/GenReverbNew.c \
			Common/GenSample.c \
			Common/GenSeq.c \
			Common/GenSeqTools.c \
			Common/GenSetup.c \
			Common/GenSong.c \
			Common/GenSoundFiles.c \
			Common/GenSynth.c \
			Common/GenSynthFiltersSimple.c \
			Common/GenSynthFiltersU3232.c \
			Common/GenSynthInterp2Simple.c \
			Common/GenSynthInterp2U3232.c \
			Common/GenSF2_FluidSynth.c \
			Common/GenRMI.c \
			Common/MiniBAE.c \
			Common/NewNewLZSS.c \
			Common/SampleTools.c \
			Common/X_API.c \
			Common/X_Decompress.c \
			Common/X_IMA.c \
			Common/g711.c \
			Common/g721.c \
			Common/g723_24.c \
			Common/g723_40.c \
			Common/g72x.c \
			Common/sha1mini.c \
			Common/XFileTypes.c \
			../BAE_MPEG_Source_II/XMPEG_minimp3_wrapper.c \
			../BAE_MPEG_Source_II/XMPEGFilesSun.c \
			Platform/jni/org_minibae_Mixer.c \
			Platform/jni/org_minibae_SongExt.c \
			Platform/jni/org_minibae_Sound.c \
			Platform/BAE_API_Android.c

LOCAL_LDFLAGS += -Wl,-z,max-page-size=16384

LOCAL_C_INCLUDES	:= $(LOCAL_PATH)/Common
LOCAL_C_INCLUDES	+= $(LOCAL_PATH)/Platform
LOCAL_C_INCLUDES	+= $(LOCAL_PATH)/../BAE_MPEG_Source_II
LOCAL_C_INCLUDES	+= $(LOCAL_PATH)/../thirdparty/minimp3/
LOCAL_C_INCLUDES	+= $(LOCAL_PATH)/../miniBAEDroid
LOCAL_C_INCLUDES	+= $(LOCAL_PATH)/../miniBAEDroid/app/src/main/jniLibs/$(TARGET_ARCH_ABI)/fluidsynth/include

LOCAL_CFLAGS := -O2 -DX_PLATFORM=X_ANDROID -D__ANDROID__=1 -D_BUILT_IN_PATCHES=1 -DUSE_MINIMP3_WRAPPER=1 -DUSE_MPEG_DECODER=1 -D_DEBUG=1 -DUSE_SF2_SUPPORT=1 -D_USING_FLUIDSYNTH=1 -Wall -fsigned-char

# Only set ARM mode for 32-bit ARM builds; do not force for arm64
ifeq ($(TARGET_ARCH_ABI), armeabi-v7a)
LOCAL_ARM_MODE := arm
endif

# for native audio
LOCAL_LDLIBS    += -lOpenSLES
# for logging
LOCAL_LDLIBS    += -llog
# for native asset manager
LOCAL_LDLIBS    += -landroid
# for OpenMP (fluidsynth)

# Link against prebuilt FluidSynth
LOCAL_SHARED_LIBRARIES := fluidsynth sndfile ogg vorbis vorbisenc FLAC opus

include $(BUILD_SHARED_LIBRARY)

# Import prebuilt FluidSynth .so
include $(CLEAR_VARS)
LOCAL_MODULE := fluidsynth
LOCAL_SRC_FILES := $(LOCAL_PATH)/../miniBAEDroid/app/src/main/jniLibs/$(TARGET_ARCH_ABI)/fluidsynth/lib/libfluidsynth.so
include $(PREBUILT_SHARED_LIBRARY)

# Import prebuilt libsndfile .so
include $(CLEAR_VARS)
LOCAL_MODULE := sndfile
LOCAL_SRC_FILES := $(LOCAL_PATH)/../miniBAEDroid/app/src/main/jniLibs/$(TARGET_ARCH_ABI)/libsndfile/lib/libsndfile.so
include $(PREBUILT_SHARED_LIBRARY)

# Import prebuilt libogg .so
include $(CLEAR_VARS)
LOCAL_MODULE := ogg
LOCAL_SRC_FILES := $(LOCAL_PATH)/../miniBAEDroid/app/src/main/jniLibs/$(TARGET_ARCH_ABI)/libogg/lib/libogg.so
include $(PREBUILT_SHARED_LIBRARY)

# Import prebuilt libvorbis .so
include $(CLEAR_VARS)
LOCAL_MODULE := vorbis
LOCAL_SRC_FILES := $(LOCAL_PATH)/../miniBAEDroid/app/src/main/jniLibs/$(TARGET_ARCH_ABI)/libvorbis/lib/libvorbis.so
include $(PREBUILT_SHARED_LIBRARY)

# Import prebuilt libvorbisenc .so
include $(CLEAR_VARS)
LOCAL_MODULE := vorbisenc
LOCAL_SRC_FILES := $(LOCAL_PATH)/../miniBAEDroid/app/src/main/jniLibs/$(TARGET_ARCH_ABI)/libvorbis/lib/libvorbisenc.so
include $(PREBUILT_SHARED_LIBRARY)

# Import prebuilt FLAC .so
include $(CLEAR_VARS)
LOCAL_MODULE := FLAC
LOCAL_SRC_FILES := $(LOCAL_PATH)/../miniBAEDroid/app/src/main/jniLibs/$(TARGET_ARCH_ABI)/libflac/lib/libFLAC.so
include $(PREBUILT_SHARED_LIBRARY)

# Import prebuilt opus .so
include $(CLEAR_VARS)
LOCAL_MODULE := opus
LOCAL_SRC_FILES := $(LOCAL_PATH)/../miniBAEDroid/app/src/main/jniLibs/$(TARGET_ARCH_ABI)/libopus/lib/libopus.so
include $(PREBUILT_SHARED_LIBRARY)

