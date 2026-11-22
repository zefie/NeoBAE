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

# Collect Git metadata
ifeq ($(OS),Windows_NT)
  COMMIT      := $(shell git rev-parse --short HEAD 2>NUL)
  DIRTY       := $(shell powershell -Command "git status --porcelain | Select-Object -First 1")
  TAG_COMMIT  := $(shell git rev-list --abbrev-commit --tags --max-count=1 2>NUL)
  TAG         := $(shell git describe --abbrev=0 --tags $(TAG_COMMIT) 2>NUL)
  DATE        := $(shell git log -1 --format=%cd --date=format:"%Y%m%d" 2>NUL)
else
  COMMIT      := $(shell git rev-parse --short HEAD 2>/dev/null)
  DIRTY       := $(shell git status --porcelain 2>/dev/null | head -n 1)
  TAG_COMMIT  := $(shell git rev-list --abbrev-commit --tags --max-count=1 2>/dev/null)
  TAG         := $(shell git describe --abbrev=0 --tags $(TAG_COMMIT) 2>/dev/null || true)
  DATE        := $(shell git log -1 --format=%cd --date=format:"%Y%m%d" 2>/dev/null)
endif

# Compute VERSION
ifeq ($(COMMIT),)
  ifeq ($(DATE),)
    ifeq ($(OS),Windows_NT)
      DATE := $(shell powershell -Command "Get-Date -Format yyyyMMdd")
    else
      DATE := $(shell date +%Y%m%d)
    endif
  endif
  VERSION := $(DATE)
else
  ifneq ($(TAG),)
    ifeq ($(COMMIT),$(TAG_COMMIT))
      VERSION := $(TAG:v%=%)
    else
      VERSION := git-$(COMMIT)
    endif
  else
    VERSION := git-$(COMMIT)
  endif
  ifneq ($(DIRTY),)
    VERSION := $(VERSION)-dirty
  endif
endif

NDK_TOOLCHAIN_VERSION=clang

LOCAL_PATH := $(call my-dir)/../../BAE_Source
include $(CLEAR_VARS)

LOCAL_MODULE    := NeoBAE
NEOBAE_SOURCES := \
			Common/BAE_Override.c \
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
			Common/GenReverbNeo.c \
			Common/GenSample.c \
			Common/GenSeq.c \
			Common/GenSeqTools.c \
			Common/GenBankBalance.c \
			Common/GenSetup.c \
			Common/GenSong.c \
			Common/GenSoundFiles.c \
			Common/GenSynth.c \
			Common/GenSynthFiltersSimple.c \
			Common/GenSynthFiltersU3232.c \
			Common/GenSynthInterp2Simple.c \
			Common/GenSynthInterp2U3232.c \
			Common/GenDLS_MobileBAE.c \
			Common/GenSF2_FluidLite.c \
			Common/GenRMI.c \
      		Common/GenXMF.c \
			Common/cast5mini.c \
      		Common/GenRingtone.c \
			Common/NeoBAE.c \
			Common/NeoBAEBankLoad.c \
			Common/NewNewLZSS.c \
			Common/SampleTools.c \
			Common/X_API.c \
			Common/X_Instruments.c \
			Common/X_EditorTools.c \
			Common/X_Decompress.c \
			Common/X_IMA.c \
			../thirdparty/adpcm-xq/adpcm-lib.c \
			../thirdparty/adpcm-xq/adpcm-dns.c \
			Common/X_LZMA.c \
			Common/g711.c \
			Common/g721.c \
			Common/g723_24.c \
			Common/g723_40.c \
			Common/g72x.c \
			Common/sha1mini.c \
			Common/XADXFiles.c \
			../adx2wav/adx2wav_decode.c \
			Common/XFileTypes.c \
      		Common/XVorbisFiles.c \
			Common/XOpusFiles.c \
			Common/XQOAFiles.c \
			Common/X_DebugCallback.c \
			Common/XWMAFiles.c \
     		Common/libwma/wma_decoder.c \
			../script/baescript.c \
			../script/baescript_lexer.c \
			../script/baescript_parser.c \
			../script/baescript_vm.c \
			../mthc/mthc_decomp.c \
			../adp2wav/adp2wav_decode.c \
			../thirdparty/libg722/g722_decode.c \
			../BAE_MPEG_Source_II/XMPEG_minimp3_wrapper.c \
			../BAE_MPEG_Source_II/XMPEGFilesSun.c \
			Platform/jni/com_zefie_NeoBAE_Mixer.c \
			Platform/jni/com_zefie_NeoBAE_SongExt.c \
			Platform/jni/com_zefie_NeoBAE_Sound.c \
			Platform/jni/com_zefie_NeoBAE_SQLiteHelper.c \
			Platform/BAE_API_Android.c \
			../thirdparty/libogg/src/bitwise.c \
			../thirdparty/libogg/src/framing.c \
      		../thirdparty/flac/src/libFLAC/stream_decoder.c \
			../thirdparty/flac/src/libFLAC/bitreader.c \
			../thirdparty/flac/src/libFLAC/bitmath.c \
			../thirdparty/flac/src/libFLAC/bitwriter.c \
			../thirdparty/flac/src/libFLAC/cpu.c \
			../thirdparty/flac/src/libFLAC/crc.c \
			../thirdparty/flac/src/libFLAC/fixed.c \
			../thirdparty/flac/src/libFLAC/format.c \
			../thirdparty/flac/src/libFLAC/lpc.c \
			../thirdparty/flac/src/libFLAC/md5.c \
			../thirdparty/flac/src/libFLAC/memory.c \
			../thirdparty/flac/src/libFLAC/metadata_iterators.c \
			../thirdparty/flac/src/libFLAC/metadata_object.c \
			../thirdparty/flac/src/libFLAC/stream_encoder_framing.c \
			../thirdparty/flac/src/libFLAC/window.c \
			../thirdparty/flac/src/libFLAC/ogg_decoder_aspect.c \
			../thirdparty/flac/src/libFLAC/ogg_helper.c \
			../thirdparty/flac/src/libFLAC/ogg_mapping.c \
      		../thirdparty/flac/src/libFLAC/stream_encoder.c \
		  	../thirdparty/flac/src/libFLAC/ogg_encoder_aspect.c \
			../thirdparty/lzma-26.00/C/Alloc.c \
			../thirdparty/lzma-26.00/C/7zAlloc.c \
			../thirdparty/lzma-26.00/C/7zStream.c \
			../thirdparty/lzma-26.00/C/7zCrc.c \
			../thirdparty/lzma-26.00/C/7zCrcOpt.c \
			../thirdparty/lzma-26.00/C/XzCrc64.c \
			../thirdparty/lzma-26.00/C/XzCrc64Opt.c \
			../thirdparty/lzma-26.00/C/Sha256.c \
			../thirdparty/lzma-26.00/C/Sha256Opt.c \
			../thirdparty/lzma-26.00/C/CpuArch.c \
			../thirdparty/lzma-26.00/C/Bra.c \
			../thirdparty/lzma-26.00/C/Bra86.c \
			../thirdparty/lzma-26.00/C/BraIA64.c \
			../thirdparty/lzma-26.00/C/Delta.c \
			../thirdparty/lzma-26.00/C/LzFind.c \
			../thirdparty/lzma-26.00/C/LzmaEnc.c \
			../thirdparty/lzma-26.00/C/LzmaDec.c \
			../thirdparty/lzma-26.00/C/Lzma2Enc.c \
			../thirdparty/lzma-26.00/C/Lzma2Dec.c \
			../thirdparty/lzma-26.00/C/Xz.c \
			../thirdparty/lzma-26.00/C/XzDec.c	\
			../fluidlite/src/fluid_chan.c \
			../fluidlite/src/fluid_chorus.c \
			../fluidlite/src/fluid_conv.c \
			../fluidlite/src/fluid_defsfont.c \
			../fluidlite/src/fluid_dsp_float.c \
			../fluidlite/src/fluid_gen.c \
			../fluidlite/src/fluid_hash.c \
			../fluidlite/src/fluid_init.c \
			../fluidlite/src/fluid_list.c \
			../fluidlite/src/fluid_mod.c \
			../fluidlite/src/fluid_ramsfont.c \
			../fluidlite/src/fluid_rev.c \
			../fluidlite/src/fluid_settings.c \
			../fluidlite/src/fluid_synth.c \
			../fluidlite/src/fluid_sys.c \
			../fluidlite/src/fluid_tuning.c \
			../fluidlite/src/fluid_voice.c

OPUS_SOURCES := \
			../thirdparty/opus/src/opus.c \
			../thirdparty/opus/src/opus_decoder.c \
			../thirdparty/opus/src/opus_encoder.c \
			../thirdparty/opus/src/extensions.c \
			../thirdparty/opus/src/opus_multistream.c \
			../thirdparty/opus/src/opus_multistream_encoder.c \
			../thirdparty/opus/src/opus_multistream_decoder.c \
			../thirdparty/opus/src/repacketizer.c \
			../thirdparty/opus/src/opus_projection_encoder.c \
			../thirdparty/opus/src/opus_projection_decoder.c \
			../thirdparty/opus/src/mapping_matrix.c \
			../thirdparty/opus/src/analysis.c \
			../thirdparty/opus/src/mlp.c \
			../thirdparty/opus/src/mlp_data.c \
			../thirdparty/opus/celt/bands.c \
			../thirdparty/opus/celt/celt.c \
			../thirdparty/opus/celt/celt_encoder.c \
			../thirdparty/opus/celt/celt_decoder.c \
			../thirdparty/opus/celt/cwrs.c \
			../thirdparty/opus/celt/entcode.c \
			../thirdparty/opus/celt/entdec.c \
			../thirdparty/opus/celt/entenc.c \
			../thirdparty/opus/celt/kiss_fft.c \
			../thirdparty/opus/celt/laplace.c \
			../thirdparty/opus/celt/mathops.c \
			../thirdparty/opus/celt/mdct.c \
			../thirdparty/opus/celt/modes.c \
			../thirdparty/opus/celt/pitch.c \
			../thirdparty/opus/celt/celt_lpc.c \
			../thirdparty/opus/celt/quant_bands.c \
			../thirdparty/opus/celt/rate.c \
			../thirdparty/opus/celt/vq.c \
			../thirdparty/opus/silk/CNG.c \
			../thirdparty/opus/silk/code_signs.c \
			../thirdparty/opus/silk/init_decoder.c \
			../thirdparty/opus/silk/decode_core.c \
			../thirdparty/opus/silk/decode_frame.c \
			../thirdparty/opus/silk/decode_parameters.c \
			../thirdparty/opus/silk/decode_indices.c \
			../thirdparty/opus/silk/decode_pulses.c \
			../thirdparty/opus/silk/decoder_set_fs.c \
			../thirdparty/opus/silk/dec_API.c \
			../thirdparty/opus/silk/enc_API.c \
			../thirdparty/opus/silk/encode_indices.c \
			../thirdparty/opus/silk/encode_pulses.c \
			../thirdparty/opus/silk/gain_quant.c \
			../thirdparty/opus/silk/interpolate.c \
			../thirdparty/opus/silk/LP_variable_cutoff.c \
			../thirdparty/opus/silk/NLSF_decode.c \
			../thirdparty/opus/silk/NSQ.c \
			../thirdparty/opus/silk/NSQ_del_dec.c \
			../thirdparty/opus/silk/PLC.c \
			../thirdparty/opus/silk/shell_coder.c \
			../thirdparty/opus/silk/tables_gain.c \
			../thirdparty/opus/silk/tables_LTP.c \
			../thirdparty/opus/silk/tables_NLSF_CB_NB_MB.c \
			../thirdparty/opus/silk/tables_NLSF_CB_WB.c \
			../thirdparty/opus/silk/tables_other.c \
			../thirdparty/opus/silk/tables_pitch_lag.c \
			../thirdparty/opus/silk/tables_pulses_per_block.c \
			../thirdparty/opus/silk/VAD.c \
			../thirdparty/opus/silk/control_audio_bandwidth.c \
			../thirdparty/opus/silk/quant_LTP_gains.c \
			../thirdparty/opus/silk/VQ_WMat_EC.c \
			../thirdparty/opus/silk/HP_variable_cutoff.c \
			../thirdparty/opus/silk/NLSF_encode.c \
			../thirdparty/opus/silk/NLSF_VQ.c \
			../thirdparty/opus/silk/NLSF_unpack.c \
			../thirdparty/opus/silk/NLSF_del_dec_quant.c \
			../thirdparty/opus/silk/process_NLSFs.c \
			../thirdparty/opus/silk/stereo_LR_to_MS.c \
			../thirdparty/opus/silk/stereo_MS_to_LR.c \
			../thirdparty/opus/silk/check_control_input.c \
			../thirdparty/opus/silk/control_SNR.c \
			../thirdparty/opus/silk/init_encoder.c \
			../thirdparty/opus/silk/control_codec.c \
			../thirdparty/opus/silk/A2NLSF.c \
			../thirdparty/opus/silk/ana_filt_bank_1.c \
			../thirdparty/opus/silk/biquad_alt.c \
			../thirdparty/opus/silk/bwexpander_32.c \
			../thirdparty/opus/silk/bwexpander.c \
			../thirdparty/opus/silk/debug.c \
			../thirdparty/opus/silk/decode_pitch.c \
			../thirdparty/opus/silk/inner_prod_aligned.c \
			../thirdparty/opus/silk/lin2log.c \
			../thirdparty/opus/silk/log2lin.c \
			../thirdparty/opus/silk/LPC_analysis_filter.c \
			../thirdparty/opus/silk/LPC_inv_pred_gain.c \
			../thirdparty/opus/silk/table_LSF_cos.c \
			../thirdparty/opus/silk/NLSF2A.c \
			../thirdparty/opus/silk/NLSF_stabilize.c \
			../thirdparty/opus/silk/NLSF_VQ_weights_laroia.c \
			../thirdparty/opus/silk/pitch_est_tables.c \
			../thirdparty/opus/silk/resampler.c \
			../thirdparty/opus/silk/resampler_down2_3.c \
			../thirdparty/opus/silk/resampler_down2.c \
			../thirdparty/opus/silk/resampler_private_AR2.c \
			../thirdparty/opus/silk/resampler_private_down_FIR.c \
			../thirdparty/opus/silk/resampler_private_IIR_FIR.c \
			../thirdparty/opus/silk/resampler_private_up2_HQ.c \
			../thirdparty/opus/silk/resampler_rom.c \
			../thirdparty/opus/silk/sigm_Q15.c \
			../thirdparty/opus/silk/sort.c \
			../thirdparty/opus/silk/sum_sqr_shift.c \
			../thirdparty/opus/silk/stereo_decode_pred.c \
			../thirdparty/opus/silk/stereo_encode_pred.c \
			../thirdparty/opus/silk/stereo_find_predictor.c \
			../thirdparty/opus/silk/stereo_quant_pred.c \
			../thirdparty/opus/silk/LPC_fit.c \
			../thirdparty/opus/silk/float/apply_sine_window_FLP.c \
			../thirdparty/opus/silk/float/corrMatrix_FLP.c \
			../thirdparty/opus/silk/float/encode_frame_FLP.c \
			../thirdparty/opus/silk/float/find_LPC_FLP.c \
			../thirdparty/opus/silk/float/find_LTP_FLP.c \
			../thirdparty/opus/silk/float/find_pitch_lags_FLP.c \
			../thirdparty/opus/silk/float/find_pred_coefs_FLP.c \
			../thirdparty/opus/silk/float/LPC_analysis_filter_FLP.c \
			../thirdparty/opus/silk/float/LTP_analysis_filter_FLP.c \
			../thirdparty/opus/silk/float/LTP_scale_ctrl_FLP.c \
			../thirdparty/opus/silk/float/noise_shape_analysis_FLP.c \
			../thirdparty/opus/silk/float/process_gains_FLP.c \
			../thirdparty/opus/silk/float/regularize_correlations_FLP.c \
			../thirdparty/opus/silk/float/residual_energy_FLP.c \
			../thirdparty/opus/silk/float/warped_autocorrelation_FLP.c \
			../thirdparty/opus/silk/float/wrappers_FLP.c \
			../thirdparty/opus/silk/float/autocorrelation_FLP.c \
			../thirdparty/opus/silk/float/burg_modified_FLP.c \
			../thirdparty/opus/silk/float/bwexpander_FLP.c \
			../thirdparty/opus/silk/float/energy_FLP.c \
			../thirdparty/opus/silk/float/inner_product_FLP.c \
			../thirdparty/opus/silk/float/k2a_FLP.c \
			../thirdparty/opus/silk/float/LPC_inv_pred_gain_FLP.c \
			../thirdparty/opus/silk/float/pitch_analysis_core_FLP.c \
			../thirdparty/opus/silk/float/scale_copy_vector_FLP.c \
			../thirdparty/opus/silk/float/scale_vector_FLP.c \
			../thirdparty/opus/silk/float/schur_FLP.c \
			../thirdparty/opus/silk/float/sort_FLP.c \
			../thirdparty/opusfile/src/info.c \
			../thirdparty/opusfile/src/internal.c \
			../thirdparty/opusfile/src/opusfile.c \
			../thirdparty/opusfile/src/stream.c			

VORBIS_SOURCES := \
			../thirdparty/libvorbis/lib/analysis.c \
      		../thirdparty/libvorbis/lib/bitrate.c \
      		../thirdparty/libvorbis/lib/block.c \
      		../thirdparty/libvorbis/lib/codebook.c \
      		../thirdparty/libvorbis/lib/envelope.c \
      		../thirdparty/libvorbis/lib/floor0.c \
      		../thirdparty/libvorbis/lib/floor1.c \
      		../thirdparty/libvorbis/lib/info.c \
      		../thirdparty/libvorbis/lib/lookup.c \
      		../thirdparty/libvorbis/lib/lsp.c \
      		../thirdparty/libvorbis/lib/mapping0.c \
      		../thirdparty/libvorbis/lib/mdct.c \
      		../thirdparty/libvorbis/lib/psy.c \
      		../thirdparty/libvorbis/lib/registry.c \
      		../thirdparty/libvorbis/lib/res0.c \
      		../thirdparty/libvorbis/lib/sharedbook.c \
      		../thirdparty/libvorbis/lib/smallft.c \
      		../thirdparty/libvorbis/lib/synthesis.c \
      		../thirdparty/libvorbis/lib/vorbisfile.c \
      		../thirdparty/libvorbis/lib/lpc.c \
      		../thirdparty/libvorbis/lib/window.c \
			../thirdparty/libvorbis/lib/vorbisenc.c

PATCHES_SCRIPT := $(LOCAL_PATH)/../../../scripts/create_embedded_patches_h.py
PATCHES_HSB := $(LOCAL_PATH)/../banks/patches111/patches111.hsb
GEN_DIR := $(LOCAL_PATH)/../build/gen
PATCHES_H := $(GEN_DIR)/BAEPatches.h

# ndk-build doesn't reliably trigger custom prerequisite rules for generated
# headers, so generate at parse time via $(shell ...).
BAE_GEN := $(shell python3 "$(PATCHES_SCRIPT)" "$(PATCHES_HSB)" "$(PATCHES_H)")

# --- OPUS ---
include $(CLEAR_VARS)
LOCAL_MODULE := opus
LOCAL_SRC_FILES := $(OPUS_SOURCES)
LOCAL_C_INCLUDES := \
	$(LOCAL_PATH)/../thirdparty/config \
	$(LOCAL_PATH)/../thirdparty/libogg/include \
	$(LOCAL_PATH)/../thirdparty/opus/src \
    $(LOCAL_PATH)/../thirdparty/opus/include \
    $(LOCAL_PATH)/../thirdparty/opus/celt \
    $(LOCAL_PATH)/../thirdparty/opus/silk \
	$(LOCAL_PATH)/../thirdparty/opus/silk/float \
	$(LOCAL_PATH)/../thirdparty/opusfile/src \
	$(LOCAL_PATH)/../thirdparty/opusfile/include
# -O2 always: opus emits a pragma warning when built without optimization.
LOCAL_CFLAGS := -O2 -DOPUS_BUILD=1 -DVAR_ARRAYS=1 -D__ANDROID__=1 -DPACKAGE_VERSION=\"1.5.0\"
ifeq ($(TARGET_ARCH_ABI), armeabi-v7a)
LOCAL_ARM_MODE := arm
endif
include $(BUILD_STATIC_LIBRARY) 

# --- VORBIS ---
include $(CLEAR_VARS)
LOCAL_MODULE := vorbis
LOCAL_SRC_FILES := $(VORBIS_SOURCES)
LOCAL_C_INCLUDES := \
	$(LOCAL_PATH)/../thirdparty/config \
	$(LOCAL_PATH)/../thirdparty/libogg/include \
    $(LOCAL_PATH)/../thirdparty/libvorbis/include \
    $(LOCAL_PATH)/../thirdparty/libvorbis/lib
LOCAL_CFLAGS := -O2 -D__ANDROID__=1
ifeq ($(TARGET_ARCH_ABI), armeabi-v7a)
LOCAL_ARM_MODE := arm
endif
include $(BUILD_STATIC_LIBRARY)

# --- NeoBAE ---
include $(CLEAR_VARS)
LOCAL_MODULE := libNeoBAE
LOCAL_SRC_FILES := $(NEOBAE_SOURCES)
LOCAL_LDFLAGS += -Wl,-z,max-page-size=16384

LOCAL_C_INCLUDES	  := $(LOCAL_PATH)/Common
LOCAL_C_INCLUDES	  += $(LOCAL_PATH)/Platform
LOCAL_C_INCLUDES	  += $(LOCAL_PATH)/../fluidlite/include
LOCAL_C_INCLUDES	  += $(LOCAL_PATH)/../fluidlite/src
LOCAL_C_INCLUDES	  += $(LOCAL_PATH)/../fluidlite/src/watcom
LOCAL_C_INCLUDES	  += $(LOCAL_PATH)/../BAE_MPEG_Source_II
LOCAL_C_INCLUDES	  += $(LOCAL_PATH)/../thirdparty/minimp3/
LOCAL_C_INCLUDES	  += $(LOCAL_PATH)/../NeoBAEDroid
LOCAL_C_INCLUDES	  += $(LOCAL_PATH)/../BAE_Source/Common/libwma
LOCAL_C_INCLUDES	  += $(LOCAL_PATH)/../../../deps/android/jniLibs/$(TARGET_ARCH_ABI)/sqlite3/include
LOCAL_C_INCLUDES	  += $(LOCAL_PATH)/../thirdparty/lzma-26.00/C
LOCAL_C_INCLUDES      += $(LOCAL_PATH)/../thirdparty/config
LOCAL_C_INCLUDES      += $(LOCAL_PATH)/../thirdparty/libogg/include
LOCAL_C_INCLUDES      += $(LOCAL_PATH)/../thirdparty/libvorbis/include
LOCAL_C_INCLUDES      += $(LOCAL_PATH)/../thirdparty/flac/include
LOCAL_C_INCLUDES      += $(LOCAL_PATH)/../thirdparty/flac/src/libFLAC/include
LOCAL_C_INCLUDES      += $(LOCAL_PATH)/../thirdparty/opus/include
LOCAL_C_INCLUDES      += $(LOCAL_PATH)/../thirdparty/opusfile/include
LOCAL_C_INCLUDES      += $(LOCAL_PATH)/../thirdparty/libg722
LOCAL_C_INCLUDES      += $(LOCAL_PATH)/../thirdparty/qoa
LOCAL_C_INCLUDES      += $(LOCAL_PATH)/../thirdparty/adpcm-xq
LOCAL_C_INCLUDES      += $(LOCAL_PATH)/../script
LOCAL_C_INCLUDES      += $(GEN_DIR)

LOCAL_CFLAGS := -std=c99 -O2 -D_VERSION=\"$(VERSION)\" \
	-DX_PLATFORM=X_ANDROID -D__ANDROID__=1 -D_BUILT_IN_PATCHES=1 \
	-DUSE_MINIMP3_WRAPPER=1 -DUSE_VORBIS_DECODER=1 -DUSE_FLAC_DECODER=1 \
	-DUSE_MPEG_DECODER=1 -DUSE_SF2_SUPPORT=1 -DUSE_OGG_FORMAT=1 \
	-DUSE_VORBIS_ENCODER=1 -DUSE_FLAC_ENCODER=1 -D_USING_FLUIDLITE=1 \
	-DUSE_XMF_SUPPORT=1 -DUSE_HIGHLEVEL_FILE_API=1 -DSUPPORT_KARAOKE=1 \
	-DUSE_OPUS_DECODER=1 -DUSE_LZMA_COMPRESSION=1 -DUSE_ZMF_SUPPORT=1 \
	-DBAE_FIX_SPAN_DC=1 -DBAE_CLASSIC_CHORUS=1 -DUSE_WEBTV_MODE=1 -DFLAC__NO_DLL=1 \
	-D_LOAD_BUILTIN_PATCHES_FOR_SF2=1 -D_LOAD_BUILTIN_PATCHES_FOR_DLS=1 \
	-DUSE_MTHC_SUPPORT=1 -DUSE_ADP_SUPPORT=1 -DUSE_NATIVE_DLS=1 \
	-DUSE_RETRO_RINGTONE_SUPPORT=1 -DUSE_QOA_SUPPORT=1 -DUSE_RMI_SUPPORT=1 \
	-DBAE_ENABLE_ROLLED_MIDI_UNROLL=1 -DUSE_J2ME_PATCH=1 -DFLUIDLITE_STATIC=1 \
	-DHAVE_CONFIG_H=1 -Wall -fsigned-char -DZ7_ST -DUSE_ADX_SUPPORT=1 \
	-DSUPPORT_OGG_FORMAT=1 -DSF3_SUPPORT=1 -DUSE_WMA_SUPPORT=1

ifeq ($(APP_OPTIM),debug)
    LOCAL_CFLAGS += -D_DEBUG=1
endif


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
# for GenXMF zlib support
LOCAL_LDLIBS    += -lz


# Link against prebuilt libraries
LOCAL_SHARED_LIBRARIES := sqlite3
LOCAL_STATIC_LIBRARIES := opus vorbis
include $(BUILD_SHARED_LIBRARY)

# Import prebuilt sqlite3 .so
include $(CLEAR_VARS)
LOCAL_MODULE := sqlite3
LOCAL_SRC_FILES := $(LOCAL_PATH)/../../../deps/android/jniLibs/$(TARGET_ARCH_ABI)/sqlite3/lib/libsqlite3.so
LOCAL_EXPORT_C_INCLUDES := $(LOCAL_PATH)/../../../deps/android/jniLibs/$(TARGET_ARCH_ABI)/sqlite3/include
include $(PREBUILT_SHARED_LIBRARY)