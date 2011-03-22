LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

LOCAL_MODULE := libx264
LOCAL_MODULE_TAGS := eng debug

X264_TOP := $(LOCAL_PATH)

.PHONY: libx264-configure
libx264-configure:
	cd $(X264_TOP) ; \
	CC="$(CONFIGURE_CC)" \
	CFLAGS="$(CONFIGURE_CFLAGS)" \
	LD=$(TARGET_LD) \
	LDFLAGS="$(CONFIGURE_LDFLAGS)" \
	CPP=$(CONFIGURE_CPP) \
	CPPFLAGS="$(CONFIGURE_CPPFLAGS)" \
	PKG_CONFIG_LIBDIR=$(CONFIGURE_PKG_CONFIG_LIBDIR) \
	PKG_CONFIG_TOP_BUILD_DIR=/ \
	$(abspath $(X264_TOP))/configure --host=arm-linux-androideabi \
	--prefix=/system --enable-shared

CONFIGURE_TARGETS += libx264-configure

-include $(X264_TOP)/config.mak

LOCAL_SRC_FILES := common/mc.c common/predict.c common/pixel.c \
	common/macroblock.c common/frame.c common/dct.c common/cpu.c common/cabac.c \
	common/common.c common/osdep.c common/rectangle.c common/set.c common/quant.c \
	common/deblock.c common/vlc.c common/mvpred.c common/bitstream.c \
	encoder/analyse.c encoder/me.c encoder/ratecontrol.c encoder/set.c \
	encoder/macroblock.c encoder/cabac.c encoder/cavlc.c encoder/encoder.c \
	encoder/lookahead.c common/arm/mc-c.c common/arm/predict-c.c \
	common/arm/cpu-a.S common/arm/pixel-a.S common/arm/mc-a.S \
        common/arm/dct-a.S common/arm/quant-a.S common/arm/deblock-a.S \
        common/arm/predict-a.S

LOCAL_CFLAGS := $(CFLAGS)
LOCAL_ASFLAGS := $(ASFLAGS)
LOCAL_PRELINK_MODULE := false

include $(BUILD_SHARED_LIBRARY)
