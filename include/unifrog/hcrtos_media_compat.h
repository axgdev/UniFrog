#ifndef UNIFROG_HCRTOS_MEDIA_COMPAT_H
#define UNIFROG_HCRTOS_MEDIA_COMPAT_H

#include <stdbool.h>
#include <stdint.h>

#include <hcuapi/iocbase.h>
#include <hcuapi/pixfmt.h>
#include <hcuapi/vidmp.h>

#ifndef AUDDEV_DEFAULT
#define AUDDEV_DEFAULT 0u
#endif
#ifndef AUDDEV_I2SO
#define AUDDEV_I2SO (1u << 0)
#endif

typedef enum AvPacketType {
   AV_PACKET_ES_DATA,
   AV_PACKET_EXTRA_DATA,
   AV_PACKET_EOS,
   AV_PACKET_ES_DATA_POINTER,
} AvPktTp;

typedef struct AvPacketHeader AvPktHd;
struct AvPacketHeader {
   int32_t pts;
   int32_t dur;
   uint32_t size : 30;
   uint32_t flag : 2;
   uint16_t video_rotate_mode;
   uint16_t video_mirror_mode;
} __attribute__((packed));

#define AUDDEC_INIT _IOW(AUDIO_IOCBASE, 1, struct audio_config)
#define AUDDEC_RLS _IO(AUDIO_IOCBASE, 2)
#define AUDDEC_PAUSE _IO(AUDIO_IOCBASE, 3)
#define AUDDEC_START _IO(AUDIO_IOCBASE, 4)
#define AUDDEC_FLUSH _IO(AUDIO_IOCBASE, 5)
#define AUDDEC_DRAIN _IO(AUDIO_IOCBASE, 6)
#define AUDDEC_CHECK_EOS _IOR(AUDIO_IOCBASE, 8, int)
#define AUDDEC_GET_CUR_TIME _IOR(AUDIO_IOCBASE, 10, int64_t)
#define AUDDEC_GET_STATUS _IOR(AUDIO_IOCBASE, 11, struct audio_decore_status)

#define AUDDEC_SET_BASE_TIME _IOW(AUDIO_IOCBASE, 21, unsigned long)
#define AUDDEC_CHANGE_SYNC_TYPE _IOW(AUDIO_IOCBASE, 22, enum AVSYNC_TYPE)
#define AUDDEC_GET_CAPABILITIES _IOR(AUDIO_IOCBASE, 23, unsigned int)
#define AUDDEC_SET_FLUSH_TIME _IO(AUDIO_IOCBASE, 24)

#define AUDIO_SET_MUTE _IOW(AUDIO_IOCBASE, 200, unsigned int)
#define AUDIO_SET_VOLUME _IOW(AUDIO_IOCBASE, 201, uint8_t)
#define AUDIO_GET_VOLUME _IOR(AUDIO_IOCBASE, 202, uint8_t)
#define AUDIO_SET_BYPASS_MODE _IOW(AUDIO_IOCBASE, 203, int)
#define AUDIO_CHANNEL_SELECT _IOW(AUDIO_IOCBASE, 204, audio_channel_select_t)
#define AUDIO_GET_STATUS _IOR(AUDIO_IOCBASE, 205, audio_status_t)
#define AUDIO_GET_UNDERRUN_TIMES _IOR(AUDIO_IOCBASE, 206, unsigned int)

typedef enum {
   AUDIO_STOPPED,
   AUDIO_PLAYING,
   AUDIO_PAUSED,
} audio_play_state_t;

typedef enum {
   AUDIO_STEREO,
   AUDIO_MONO_LEFT,
   AUDIO_MONO_RIGHT,
   AUDIO_MONO,
   AUDIO_STEREO_SWAPPED,
} audio_channel_select_t;

typedef struct audio_status {
   uint8_t AV_sync_state;
   uint8_t mute_state;
   uint8_t play_state;
   uint8_t channel_select;
   uint8_t bypass_mode;
} audio_status_t;

struct audio_config {
   uint8_t decode_mode;
   uint8_t sync_mode;
   uint8_t bits_per_coded_sample;
   uint8_t channels;
   uint32_t codec_id;
   uint32_t codec_tag;
   uint32_t sample_rate;
   uint32_t bit_rate;
   uint32_t block_align;
   uint32_t snd_devs;
   int audio_flush_thres;
   unsigned char extra_data[512];
   void *extradata;
   uint32_t extradata_size;
   unsigned char extradata_mode;
   unsigned char bypass;
   int kshm_size;
   uint64_t channel_layout;
   int buffering_start;
   int buffering_end;
   int enable_audsink;
} __attribute__((aligned(8)));

struct audio_decore_status {
   uint32_t sample_rate;
   uint8_t channels;
   uint8_t bits_per_sample;
   uint8_t first_header_got;
   uint8_t first_header_parsed;
   uint32_t frames_decoded;
};

struct vframe_info {
   enum FFPixelFormat pixfmt;
   int16_t width;
   int16_t height;
   int16_t src_width;
   int16_t src_height;
   uint8_t *pixels[4];
   int pitch[4];
   enum IMG_DIS_MODE mode;
   enum ROTATE_TYPE angle;
   enum MIRROR_TYPE mirror;
   image_effect_t img_effect;
   struct av_area src_area;
   struct av_area dst_area;
   bool preview_enable;
   bool bg_disable;
   struct video_transcode_config transcode_config;
};

#ifndef VIDSINK_DISPLAY_FRAME
#define VIDSINK_DISPLAY_FRAME _IOW(VIDSINK_IOCBASE, 0, struct vframe_info)
#endif
#ifndef VIDSINK_ENABLE_IMG_EFFECT
#define VIDSINK_ENABLE_IMG_EFFECT _IO(VIDSINK_IOCBASE, 1)
#endif
#ifndef VIDSINK_DISABLE_IMG_EFFECT
#define VIDSINK_DISABLE_IMG_EFFECT _IO(VIDSINK_IOCBASE, 2)
#endif
#ifndef VIDSINK_CHECK_EOS
#define VIDSINK_CHECK_EOS _IOR(VIDSINK_IOCBASE, 3, int)
#endif

#endif
