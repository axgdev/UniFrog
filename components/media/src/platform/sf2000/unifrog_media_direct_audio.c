#include "unifrog_media_internal.h"

/* Private direct audio parsers and hardware/software audio players. */
static int media_wav_ms_adpcm_nibble(int nibble, int16_t sample1,
   int16_t sample2, int *delta, int coeff1, int coeff2)
{
   static const int adapt_table[16] = {
      230, 230, 230, 230, 307, 409, 512, 614,
      768, 614, 512, 409, 307, 230, 230, 230,
   };
   int signed_nibble = nibble & 0x08 ? nibble - 0x10 : nibble;
   int decoded = (((int)sample1 * coeff1 + (int)sample2 * coeff2) / 256) +
      signed_nibble * *delta;

   if (decoded > 32767)
      decoded = 32767;
   else if (decoded < -32768)
      decoded = -32768;
   *delta = (*delta * adapt_table[nibble & 0x0f]) / 256;
   if (*delta < 16)
      *delta = 16;
   return decoded;
}

static unsigned media_mp3_bitrate_kbps(unsigned version, unsigned layer,
   unsigned index)
{
   static const unsigned v1_l3[16] = {
      0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 0,
   };
   static const unsigned v2_l3[16] = {
      0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 0,
   };

   (void)layer;
   return version == 3u ? v1_l3[index & 15u] : v2_l3[index & 15u];
}

static unsigned media_mp3_sample_rate(unsigned version, unsigned index)
{
   static const unsigned base[4] = { 44100, 48000, 32000, 0 };
   unsigned rate = base[index & 3u];

   if (version == 2u)
      rate /= 2u;
   else if (version == 0u)
      rate /= 4u;
   return rate;
}

static int media_parse_mp3_header(const uint8_t h[4], unsigned *frame_size,
   unsigned *sample_rate, unsigned *channels, unsigned *duration_ms)
{
   unsigned version;
   unsigned layer;
   unsigned bitrate_index;
   unsigned rate_index;
   unsigned padding;
   unsigned bitrate;
   unsigned rate;

   if (!h || h[0] != 0xff || (h[1] & 0xe0) != 0xe0)
      return -1;
   version = (h[1] >> 3) & 0x03;
   layer = (h[1] >> 1) & 0x03;
   bitrate_index = (h[2] >> 4) & 0x0f;
   rate_index = (h[2] >> 2) & 0x03;
   padding = (h[2] >> 1) & 0x01;
   if (version == 1u || layer != 1u || bitrate_index == 0u ||
       bitrate_index == 15u || rate_index == 3u)
      return -1;
   bitrate = media_mp3_bitrate_kbps(version, layer, bitrate_index);
   rate = media_mp3_sample_rate(version, rate_index);
   if (!bitrate || !rate)
      return -1;
   *frame_size = ((version == 3u ? 144000u : 72000u) * bitrate) / rate +
      padding;
   *sample_rate = rate;
   *channels = ((h[3] >> 6) & 0x03) == 3u ? 1u : 2u;
   *duration_ms = version == 3u ? 26u : 13u;
   return *frame_size >= 4u ? 0 : -1;
}

static long media_skip_id3v2(FILE *file)
{
   uint8_t h[10];
   uint32_t size;

   if (!file)
      return 0;
   if (fread(h, 1, sizeof(h), file) != sizeof(h)) {
      (void)fseek(file, 0, SEEK_SET);
      return 0;
   }
   if (memcmp(h, "ID3", 3) != 0) {
      (void)fseek(file, 0, SEEK_SET);
      return 0;
   }
   size = ((uint32_t)(h[6] & 0x7f) << 21) |
      ((uint32_t)(h[7] & 0x7f) << 14) |
      ((uint32_t)(h[8] & 0x7f) << 7) | (uint32_t)(h[9] & 0x7f);
   (void)fseek(file, (long)size, SEEK_CUR);
   return 10L + (long)size;
}

int media_play_mp3_auddec(const char *path)
{
   FILE *file = NULL;
   struct media_auddec auddec;
   uint8_t *frame = NULL;
   uint8_t h[4];
   unsigned frame_size = 0;
   unsigned rate = 44100;
   unsigned channels = 2;
   unsigned dur = 26;
   unsigned packets = 0;
   int32_t pts = 0;
   int ret = -1;

   memset(&auddec, 0, sizeof(auddec));
   auddec.fd = -1;
   auddec.prime_fd = -1;
   file = fopen(path, "rb");
   if (!file &&
       unifrog_storage_reopen_file_after_io_error(&file, path, 0, "mp3_auddec_open", 24, 250) != 0)
      return -1;
   (void)media_skip_id3v2(file);
   while (fread(h, 1, sizeof(h), file) == sizeof(h)) {
      if (media_parse_mp3_header(h, &frame_size, &rate, &channels, &dur) == 0)
         break;
      (void)fseek(file, -3, SEEK_CUR);
   }
   if (!frame_size)
      goto out;
   if (media_auddec_open_raw("mp3", HC_AVCODEC_ID_MP3, rate, channels, 16,
      NULL, 0, 0, &auddec) != 0)
      goto out;
   frame = malloc(frame_size > 4096u ? frame_size : 4096u);
   if (!frame)
      goto out;
   for (;;) {
      unsigned next_size;
      unsigned next_rate;
      unsigned next_channels;
      unsigned next_dur;

      if (frame_size > 65536u)
         break;
      memcpy(frame, h, sizeof(h));
      if (fread(frame + 4, 1, frame_size - 4u, file) != frame_size - 4u)
         break;
      if (media_auddec_send_raw(&auddec, frame, frame_size, pts,
         (int32_t)dur) != 0)
         break;
      packets++;
      pts += (int32_t)dur;
      if (media_exit_down())
         break;
      if (fread(h, 1, sizeof(h), file) != sizeof(h))
         break;
      if (media_parse_mp3_header(h, &next_size, &next_rate, &next_channels,
         &next_dur) != 0)
         break;
      if (next_size > frame_size) {
         uint8_t *new_frame = realloc(frame, next_size);

         if (!new_frame)
            break;
         frame = new_frame;
      }
      frame_size = next_size;
      dur = next_dur;
   }
   ret = packets ? 0 : -1;

out:
   printf("unifrog media mp3 native end ret=%d packets=%u path=%s\n",
      ret, packets, path ? path : "");
   if (auddec.fd >= 0)
      media_auddec_finish(&auddec, 1000);
   media_auddec_close(&auddec);
   free(frame);
   if (file)
      fclose(file);
   return ret;
}

static int media_parse_adts_header(const uint8_t h[7], unsigned *frame_size,
   unsigned *sample_rate, unsigned *channels, unsigned *duration_ms)
{
   static const unsigned rates[16] = {
      96000, 88200, 64000, 48000, 44100, 32000, 24000, 22050,
      16000, 12000, 11025, 8000, 7350, 0, 0, 0,
   };
   unsigned sr_index;

   if (!h || h[0] != 0xff || (h[1] & 0xf0) != 0xf0)
      return -1;
   sr_index = (h[2] >> 2) & 0x0f;
   *frame_size = ((unsigned)(h[3] & 0x03) << 11) |
      ((unsigned)h[4] << 3) | ((unsigned)h[5] >> 5);
   *sample_rate = rates[sr_index];
   *channels = ((unsigned)(h[2] & 0x01) << 2) | ((unsigned)h[3] >> 6);
   if (!*channels)
      *channels = 2;
   *duration_ms = *sample_rate ? (1024u * 1000u) / *sample_rate : 23u;
   return *frame_size >= 7u && *sample_rate ? 0 : -1;
}

int media_play_aac_adts_auddec(const char *path)
{
   FILE *file = NULL;
   struct media_auddec auddec;
   uint8_t *frame = NULL;
   uint8_t h[7];
   unsigned frame_size = 0;
   unsigned rate = 44100;
   unsigned channels = 2;
   unsigned dur = 23;
   unsigned packets = 0;
   int32_t pts = 0;
   int ret = -1;

   memset(&auddec, 0, sizeof(auddec));
   auddec.fd = -1;
   auddec.prime_fd = -1;
   file = fopen(path, "rb");
   if (!file &&
       unifrog_storage_reopen_file_after_io_error(&file, path, 0, "wav_auddec_open", 24, 250) != 0)
      return -1;
   if (fread(h, 1, sizeof(h), file) != sizeof(h) ||
       media_parse_adts_header(h, &frame_size, &rate, &channels, &dur) != 0)
      goto out;
   if (media_auddec_open_raw("aac_adts", HC_AVCODEC_ID_AAC, rate, channels,
      16, NULL, 0, 0, &auddec) != 0)
      goto out;
   frame = malloc(frame_size > 4096u ? frame_size : 4096u);
   if (!frame)
      goto out;
   for (;;) {
      unsigned next_size;
      unsigned next_rate;
      unsigned next_channels;
      unsigned next_dur;

      if (frame_size > 65536u)
         break;
      memcpy(frame, h, sizeof(h));
      if (fread(frame + 7, 1, frame_size - 7u, file) != frame_size - 7u)
         break;
      if (media_auddec_send_raw(&auddec, frame, frame_size, pts,
         (int32_t)dur) != 0)
         break;
      packets++;
      pts += (int32_t)dur;
      if (media_exit_down())
         break;
      if (fread(h, 1, sizeof(h), file) != sizeof(h))
         break;
      if (media_parse_adts_header(h, &next_size, &next_rate, &next_channels,
         &next_dur) != 0)
         break;
      if (next_size > frame_size) {
         uint8_t *new_frame = realloc(frame, next_size);

         if (!new_frame)
            break;
         frame = new_frame;
      }
      frame_size = next_size;
      dur = next_dur;
   }
   ret = packets ? 0 : -1;

out:
   printf("unifrog media aac native end ret=%d packets=%u path=%s\n",
      ret, packets, path ? path : "");
   if (auddec.fd >= 0)
      media_auddec_finish(&auddec, 1000);
   media_auddec_close(&auddec);
   free(frame);
   if (file)
      fclose(file);
   return ret;
}

int media_play_flac_auddec(const char *path)
{
   FILE *file = NULL;
   struct media_auddec auddec;
   uint8_t magic[4];
   uint8_t block_header[4];
   uint8_t streaminfo[34];
   uint8_t buf[4096];
   unsigned rate = 44100;
   unsigned channels = 2;
   unsigned packets = 0;
   long audio_pos = -1;
   int have_streaminfo = 0;
   int ret = -1;

   memset(&auddec, 0, sizeof(auddec));
   auddec.fd = -1;
   auddec.prime_fd = -1;
   file = fopen(path, "rb");
   if (!file)
      return -1;
   if (fread(magic, 1, sizeof(magic), file) != sizeof(magic) ||
       memcmp(magic, "fLaC", 4) != 0)
      goto out;
   while (fread(block_header, 1, sizeof(block_header), file) ==
      sizeof(block_header)) {
      int last = block_header[0] & 0x80;
      unsigned type = block_header[0] & 0x7f;
      uint32_t size = ((uint32_t)block_header[1] << 16) |
         ((uint32_t)block_header[2] << 8) | block_header[3];

      if (type == 0u && size == sizeof(streaminfo)) {
         if (fread(streaminfo, 1, sizeof(streaminfo), file) !=
             sizeof(streaminfo))
            goto out;
         have_streaminfo = 1;
      } else if (fseek(file, (long)size, SEEK_CUR) != 0) {
         goto out;
      }
      if (last) {
         audio_pos = ftell(file);
         break;
      }
   }
   if (!have_streaminfo || audio_pos < 0)
      goto out;
   {
      uint32_t packed = media_read_be32(streaminfo + 10);

      rate = packed >> 12;
      channels = ((packed >> 9) & 0x07) + 1u;
      if (!rate)
         rate = 44100;
   }
   if (media_auddec_open_raw("flac", HC_AVCODEC_ID_FLAC, rate, channels, 16,
      streaminfo, sizeof(streaminfo), 0, &auddec) != 0)
      goto out;
   if (fseek(file, audio_pos, SEEK_SET) != 0)
      goto out;
   while (!media_exit_down()) {
      size_t got = fread(buf, 1, sizeof(buf), file);

      if (!got)
         break;
      if (media_auddec_send_raw(&auddec, buf, got, -1, 0) != 0)
         break;
      packets++;
   }
   ret = packets ? 0 : -1;

out:
   printf("unifrog media flac native end ret=%d packets=%u rate=%u ch=%u audio_pos=%ld streaminfo=%d path=%s\n",
      ret, packets, rate, channels, audio_pos, have_streaminfo,
      path ? path : "");
   if (auddec.fd >= 0)
      media_auddec_finish(&auddec, 1000);
   media_auddec_close(&auddec);
   if (file)
      fclose(file);
   return ret;
}

struct media_ogg_state {
   struct media_auddec auddec;
   uint8_t *packet;
   size_t packet_size;
   size_t packet_cap;
   uint8_t *headers[3];
   size_t header_sizes[3];
   unsigned header_count;
   unsigned codec_id;
   unsigned sample_rate;
   unsigned channels;
   unsigned packets;
   int opened;
   int open_failed;
};

static int media_ogg_append(struct media_ogg_state *st, const uint8_t *data,
   size_t size)
{
   if (!st || (!data && size))
      return -1;
   if (st->packet_size + size > st->packet_cap) {
      size_t cap = st->packet_cap ? st->packet_cap * 2u : 4096u;
      uint8_t *new_packet;

      while (cap < st->packet_size + size)
         cap *= 2u;
      new_packet = realloc(st->packet, cap);
      if (!new_packet)
         return -1;
      st->packet = new_packet;
      st->packet_cap = cap;
   }
   memcpy(st->packet + st->packet_size, data, size);
   st->packet_size += size;
   return 0;
}

static int media_xiph_lace(uint8_t *out, size_t *pos, size_t out_size,
   size_t value)
{
   while (value >= 255u) {
      if (*pos >= out_size)
         return -1;
      out[(*pos)++] = 255u;
      value -= 255u;
   }
   if (*pos >= out_size)
      return -1;
   out[(*pos)++] = (uint8_t)value;
   return 0;
}

static uint8_t *media_make_vorbis_extradata(const uint8_t *a, size_t as,
   const uint8_t *b, size_t bs, const uint8_t *c, size_t cs, size_t *out_size)
{
   size_t lace = 1u + (as / 255u + 1u) + (bs / 255u + 1u);
   size_t total = lace + as + bs + cs;
   uint8_t *out = malloc(total);
   size_t pos = 0;

   if (!out)
      return NULL;
   out[pos++] = 2u;
   if (media_xiph_lace(out, &pos, total, as) != 0 ||
       media_xiph_lace(out, &pos, total, bs) != 0) {
      free(out);
      return NULL;
   }
   memcpy(out + pos, a, as);
   pos += as;
   memcpy(out + pos, b, bs);
   pos += bs;
   memcpy(out + pos, c, cs);
   pos += cs;
   *out_size = pos;
   return out;
}

static int media_ogg_open_decoder(struct media_ogg_state *st)
{
   uint8_t *extra = NULL;
   size_t extra_size = 0;
   int ret;

   if (!st || st->opened || !st->codec_id)
      return st && st->opened ? 0 : -1;
   if (st->codec_id == HC_AVCODEC_ID_OPUS) {
      extra = st->headers[0];
      extra_size = st->header_sizes[0];
   } else if (st->codec_id == HC_AVCODEC_ID_VORBIS && st->header_count >= 3u) {
      extra = media_make_vorbis_extradata(st->headers[0],
         st->header_sizes[0], st->headers[1], st->header_sizes[1],
         st->headers[2], st->header_sizes[2], &extra_size);
      if (!extra)
         return -1;
   } else {
      return -1;
   }
   ret = media_auddec_open_raw(st->codec_id == HC_AVCODEC_ID_OPUS ?
      "ogg_opus" : "ogg_vorbis", st->codec_id, st->sample_rate,
      st->channels, 16, extra, (unsigned)extra_size, 0, &st->auddec);
   if (st->codec_id == HC_AVCODEC_ID_VORBIS)
      free(extra);
   st->opened = ret == 0;
   return ret;
}

static int media_ogg_packet(struct media_ogg_state *st)
{
   uint8_t *copy;

   if (!st || !st->packet || !st->packet_size)
      return 0;
   if (st->header_count == 0u && st->packet_size >= 19u &&
       memcmp(st->packet, "OpusHead", 8) == 0) {
      st->codec_id = HC_AVCODEC_ID_OPUS;
      st->channels = st->packet[9] ? st->packet[9] : 2u;
      st->sample_rate = media_read_le32(st->packet + 12);
      if (!st->sample_rate)
         st->sample_rate = 48000u;
   } else if (st->header_count == 0u && st->packet_size >= 30u &&
       st->packet[0] == 1u && memcmp(st->packet + 1, "vorbis", 6) == 0) {
      st->codec_id = HC_AVCODEC_ID_VORBIS;
      st->channels = st->packet[11] ? st->packet[11] : 2u;
      st->sample_rate = media_read_le32(st->packet + 12);
      if (!st->sample_rate)
         st->sample_rate = 44100u;
   }
   if (!st->codec_id)
      goto done;
   if (st->codec_id == HC_AVCODEC_ID_OPUS &&
       (st->header_count < 2u &&
        ((st->packet_size >= 8u && memcmp(st->packet, "OpusHead", 8) == 0) ||
         (st->packet_size >= 8u && memcmp(st->packet, "OpusTags", 8) == 0)))) {
      copy = malloc(st->packet_size);
      if (!copy)
         return -1;
      memcpy(copy, st->packet, st->packet_size);
      st->headers[st->header_count] = copy;
      st->header_sizes[st->header_count++] = st->packet_size;
      goto done;
   }
   if (st->codec_id == HC_AVCODEC_ID_VORBIS && st->header_count < 3u &&
       st->packet_size >= 7u && memcmp(st->packet + 1, "vorbis", 6) == 0) {
      copy = malloc(st->packet_size);
      if (!copy)
         return -1;
      memcpy(copy, st->packet, st->packet_size);
      st->headers[st->header_count] = copy;
      st->header_sizes[st->header_count++] = st->packet_size;
      goto done;
   }
   if (st->open_failed)
      goto done;
   if (!st->opened && media_ogg_open_decoder(st) != 0) {
      st->open_failed = 1;
      goto done;
   }
   if (st->opened &&
       media_auddec_send_raw(&st->auddec, st->packet, st->packet_size,
          -1, 0) == 0)
      st->packets++;

done:
   st->packet_size = 0;
   return 0;
}

int media_play_ogg_auddec(const char *path)
{
   FILE *file = NULL;
   struct media_ogg_state st;
   uint8_t page[27 + 255];
   int ret = -1;

   memset(&st, 0, sizeof(st));
   st.auddec.fd = -1;
   st.auddec.prime_fd = -1;
   file = fopen(path, "rb");
   if (!file)
      return -1;
   while (!media_exit_down()) {
      unsigned segments;
      unsigned body_size = 0;

      if (fread(page, 1, 27, file) != 27)
         break;
      if (memcmp(page, "OggS", 4) != 0)
         break;
      segments = page[26];
      if (segments > 255u || fread(page + 27, 1, segments, file) != segments)
         break;
      for (unsigned i = 0; i < segments; i++)
         body_size += page[27 + i];
      for (unsigned i = 0; i < segments; i++) {
         unsigned seg = page[27 + i];
         uint8_t tmp[255];

         if (seg && fread(tmp, 1, seg, file) != seg)
            goto out;
         if (media_ogg_append(&st, tmp, seg) != 0)
            goto out;
         if (seg < 255u && media_ogg_packet(&st) != 0)
            goto out;
      }
      (void)body_size;
   }
   ret = st.packets ? 0 : -1;

out:
   printf("unifrog media ogg native end ret=%d codec=%lu packets=%u headers=%u rate=%u ch=%u path=%s\n",
      ret, (unsigned long)st.codec_id, st.packets, st.header_count,
      st.sample_rate, st.channels, path ? path : "");
   if (st.auddec.fd >= 0)
      media_auddec_finish(&st.auddec, 1000);
   media_auddec_close(&st.auddec);
   for (unsigned i = 0; i < ARRAY_SIZE(st.headers); i++)
      free(st.headers[i]);
   free(st.packet);
   if (file)
      fclose(file);
   return ret;
}

static int media_wav_decode_ms_adpcm_block(const uint8_t *block,
   unsigned block_size, unsigned channels, const int coeffs[][2],
   unsigned coeff_count, int16_t *out, unsigned out_frames)
{
   int predictor[2];
   int delta[2];
   int16_t sample1[2];
   int16_t sample2[2];
   unsigned pos;
   unsigned frame = 0;

   if (!block || !out || (channels != 1u && channels != 2u) ||
       coeff_count == 0)
      return -1;
   if (channels == 1u) {
      if (block_size < 7u)
         return -1;
      predictor[0] = block[0] < coeff_count ? block[0] : 0;
      delta[0] = (int16_t)media_read_le16(block + 1);
      sample1[0] = (int16_t)media_read_le16(block + 3);
      sample2[0] = (int16_t)media_read_le16(block + 5);
      pos = 7u;
      if (out_frames > 0)
         out[frame++] = sample2[0];
      if (frame < out_frames)
         out[frame++] = sample1[0];
      while (pos < block_size && frame < out_frames) {
         uint8_t byte = block[pos++];
         int decoded;

         decoded = media_wav_ms_adpcm_nibble(byte >> 4, sample1[0],
            sample2[0], &delta[0], coeffs[predictor[0]][0],
            coeffs[predictor[0]][1]);
         sample2[0] = sample1[0];
         sample1[0] = (int16_t)decoded;
         out[frame++] = sample1[0];
         if (frame >= out_frames)
            break;
         decoded = media_wav_ms_adpcm_nibble(byte, sample1[0],
            sample2[0], &delta[0], coeffs[predictor[0]][0],
            coeffs[predictor[0]][1]);
         sample2[0] = sample1[0];
         sample1[0] = (int16_t)decoded;
         out[frame++] = sample1[0];
      }
      return (int)frame;
   }

   if (block_size < 14u)
      return -1;
   predictor[0] = block[0] < coeff_count ? block[0] : 0;
   predictor[1] = block[1] < coeff_count ? block[1] : 0;
   delta[0] = (int16_t)media_read_le16(block + 2);
   delta[1] = (int16_t)media_read_le16(block + 4);
   sample1[0] = (int16_t)media_read_le16(block + 6);
   sample1[1] = (int16_t)media_read_le16(block + 8);
   sample2[0] = (int16_t)media_read_le16(block + 10);
   sample2[1] = (int16_t)media_read_le16(block + 12);
   pos = 14u;
   if (out_frames > 0)
      out[frame++] = media_wav_clip_sample((sample2[0] + sample2[1]) >> 1);
   if (frame < out_frames)
      out[frame++] = media_wav_clip_sample((sample1[0] + sample1[1]) >> 1);
   while (pos < block_size && frame < out_frames) {
      uint8_t byte = block[pos++];
      int decoded_l = media_wav_ms_adpcm_nibble(byte >> 4, sample1[0],
         sample2[0], &delta[0], coeffs[predictor[0]][0],
         coeffs[predictor[0]][1]);
      int decoded_r = media_wav_ms_adpcm_nibble(byte, sample1[1],
         sample2[1], &delta[1], coeffs[predictor[1]][0],
         coeffs[predictor[1]][1]);

      sample2[0] = sample1[0];
      sample1[0] = (int16_t)decoded_l;
      sample2[1] = sample1[1];
      sample1[1] = (int16_t)decoded_r;
      out[frame++] = media_wav_clip_sample((decoded_l + decoded_r) >> 1);
   }
   return (int)frame;
}

static int media_wav_pcm_codec(unsigned format, unsigned bits,
   uint32_t *codec_id)
{
   if (!codec_id || (format != 1u && format != 65534u))
      return -1;
   switch (bits) {
   case 8:
      *codec_id = HC_AVCODEC_ID_PCM_U8;
      return 0;
   case 16:
      *codec_id = HC_AVCODEC_ID_PCM_S16LE;
      return 0;
   case 24:
      *codec_id = HC_AVCODEC_ID_PCM_S24LE;
      return 0;
   case 32:
      *codec_id = HC_AVCODEC_ID_PCM_S32LE;
      return 0;
   default:
      return -1;
   }
}

static int media_wav_auddec_seek(FILE *file, long data_pos,
   uint32_t pcm_size, uint32_t frames, unsigned bytes_per_frame,
   unsigned rate, int seek_delta_ms, struct media_auddec *auddec,
   struct media_audio_pacer *pacer, uint32_t *sent, unsigned *loop_polls,
   const char *path)
{
   int64_t hw_ms = -1;
   int64_t cur_ms;
   int64_t target_ms;
   uint32_t duration_ms;
   uint32_t target_frame;
   uint32_t target_byte;
   uint64_t offset64;

   if (!file || data_pos < 0 || pcm_size == 0 || frames == 0 ||
       bytes_per_frame == 0 || rate == 0 || !auddec || auddec->fd < 0 ||
       !pacer || !sent)
      return -1;

   duration_ms = media_audio_frames_to_ms(frames, (int)rate);
   if (duration_ms == 0)
      return -1;

   (void)ioctl(auddec->fd, AUDDEC_GET_CUR_TIME, &hw_ms);
   cur_ms = media_seek_current_ms(-1, hw_ms, NULL, pacer, MEDIA_TIME_UNSET);
   if (cur_ms < 0)
      cur_ms = media_audio_frames_to_ms(*sent / bytes_per_frame, (int)rate);
   target_ms = media_seek_target_ms(cur_ms, seek_delta_ms, duration_ms);
   target_frame = media_audio_ms_to_frames(target_ms, rate);
   if (target_frame > frames)
      target_frame = frames;
   target_byte = target_frame * bytes_per_frame;
   if (target_byte > pcm_size)
      target_byte = pcm_size;
   offset64 = (uint64_t)data_pos + target_byte;
   if (offset64 > (uint64_t)LONG_MAX ||
       fseek(file, (long)offset64, SEEK_SET) != 0) {
      printf("unifrog media seek wav_auddec file_failed cur=%lld hw=%lld dur=%lu delta=%d target=%lld frame=%lu offset=%llu path=%s\n",
         (long long)cur_ms, (long long)hw_ms, (unsigned long)duration_ms,
         seek_delta_ms, (long long)target_ms,
         (unsigned long)target_frame, (unsigned long long)offset64,
         path ? path : "");
      return -1;
   }

   media_flush_auddec_for_seek(auddec, "wav_auddec", path);
   media_audio_pacer_seek_reset(pacer, target_ms);
   *sent = target_byte;
   if (loop_polls)
      *loop_polls = 0;
   media_audio_screen_draw("wav_auddec_seek", path, target_ms, duration_ms, 1);
   printf("unifrog media seek wav_auddec cur=%lld hw=%lld dur=%lu delta=%d target=%lld frame=%lu/%lu byte=%lu/%lu path=%s\n",
      (long long)cur_ms, (long long)hw_ms, (unsigned long)duration_ms,
      seek_delta_ms, (long long)target_ms, (unsigned long)target_frame,
      (unsigned long)frames, (unsigned long)target_byte,
      (unsigned long)pcm_size, path ? path : "");
   return 0;
}

int media_play_wav_auddec(const char *path)
{
   FILE *file = NULL;
   struct media_auddec auddec;
   struct media_audio_pacer pacer;
   uint8_t header[12];
   uint8_t chunk[8];
   uint8_t *buf = NULL;
   unsigned channels = 0;
   unsigned rate = 0;
   unsigned bits = 0;
   unsigned bytes_per_sample = 0;
   unsigned bytes_per_frame = 0;
   unsigned chunk_bytes = 0;
   unsigned feed_lead_ms = MEDIA_AUDIO_FEED_LEAD_MS;
   unsigned max_ahead_ms = MEDIA_AUDIO_MAX_HW_AHEAD_MS;
   unsigned finish_timeout_ms = 1000u;
   unsigned format = 0;
   uint32_t data_size = 0;
   uint32_t pcm_size = 0;
   long data_pos = -1;
   uint32_t codec_id = 0;
   uint32_t sent = 0;
   uint32_t frames = 0;
   int stopped_by_user = 0;
   int write_failed = 0;
   unsigned loop_polls = 0;
   int ret = -1;

   memset(&auddec, 0, sizeof(auddec));
   memset(&pacer, 0, sizeof(pacer));
   auddec.fd = -1;
   auddec.prime_fd = -1;
   file = fopen(path, "rb");
   if (!file)
      return -1;
   if (fread(header, 1, sizeof(header), file) != sizeof(header) ||
       memcmp(header, "RIFF", 4) != 0 || memcmp(header + 8, "WAVE", 4) != 0)
      goto out;
   while (fread(chunk, 1, sizeof(chunk), file) == sizeof(chunk)) {
      uint32_t size = media_read_le32(chunk + 4);
      long pos = ftell(file);
      long next = pos + (long)((size + 1u) & ~1u);

      if (memcmp(chunk, "fmt ", 4) == 0) {
         uint8_t fmt[40];
         size_t want = size < sizeof(fmt) ? size : sizeof(fmt);

         if (fread(fmt, 1, want, file) != want)
            goto out;
         if (want >= 16u) {
            format = media_read_le16(fmt);
            channels = media_read_le16(fmt + 2);
            rate = media_read_le32(fmt + 4);
            bits = media_read_le16(fmt + 14);
         }
      } else if (memcmp(chunk, "data", 4) == 0) {
         data_pos = pos;
         data_size = size;
      }
      if (fseek(file, next, SEEK_SET) != 0)
         break;
      if (format && data_pos >= 0)
         break;
   }
   if ((channels != 1u && channels != 2u) || rate < 8000u ||
       rate > 48000u || data_pos < 0 ||
       media_wav_pcm_codec(format, bits, &codec_id) != 0) {
      printf("unifrog media wav auddec unsupported path=%s format=%u ch=%u rate=%u bits=%u data=%lu\n",
         path, format, channels, rate, bits, (unsigned long)data_size);
      goto out;
   }
   bytes_per_sample = (bits + 7u) / 8u;
   bytes_per_frame = bytes_per_sample * channels;
   if (bytes_per_frame == 0)
      goto out;
   frames = data_size / bytes_per_frame;
   if (frames == 0)
      goto out;
   pcm_size = frames * bytes_per_frame;
   {
      uint32_t kshm_ms = media_audio_bytes_to_ms(MEDIA_AUDIO_KSHM_SIZE,
         bytes_per_frame, rate);

      if (kshm_ms > 0) {
         unsigned safe_feed_ms = kshm_ms / 2u;
         unsigned safe_ahead_ms = (kshm_ms * 3u) / 4u;

         if (safe_feed_ms < MEDIA_WAV_AUDDEC_MIN_FEED_MS)
            safe_feed_ms = MEDIA_WAV_AUDDEC_MIN_FEED_MS;
         if (safe_ahead_ms < MEDIA_WAV_AUDDEC_MIN_FEED_MS)
            safe_ahead_ms = MEDIA_WAV_AUDDEC_MIN_FEED_MS;
         if (feed_lead_ms > safe_feed_ms)
            feed_lead_ms = safe_feed_ms;
         if (max_ahead_ms > safe_ahead_ms)
            max_ahead_ms = safe_ahead_ms;
      }
   }
   finish_timeout_ms = max_ahead_ms + MEDIA_WAV_AUDDEC_FINISH_PAD_MS;
   chunk_bytes = MEDIA_WAV_AUDDEC_CHUNK_BYTES -
      (MEDIA_WAV_AUDDEC_CHUNK_BYTES % bytes_per_frame);
   if (chunk_bytes < bytes_per_frame)
      chunk_bytes = bytes_per_frame;
   printf("unifrog media wav auddec start path=%s codec=%lu format=%u ch=%u rate=%u bits=%u data=%lu frames=%lu duration=%lu feed_lead_ms=%u max_hw_ahead_ms=%u kshm=%u chunk=%u\n",
      path, (unsigned long)codec_id, format, channels, rate, bits,
      (unsigned long)data_size, (unsigned long)frames,
      (unsigned long)media_audio_frames_to_ms(frames, (int)rate),
      feed_lead_ms, max_ahead_ms, MEDIA_AUDIO_KSHM_SIZE, chunk_bytes);
   if (media_auddec_open_raw("wav_pcm", codec_id, rate, channels, bits,
      NULL, 0, 0, &auddec) != 0)
      goto out;
   if (fseek(file, data_pos, SEEK_SET) != 0)
      goto out;
   buf = malloc(chunk_bytes);
   if (!buf)
      goto out;
   media_controls_reset_for_playback("wav_auddec", path);
   media_audio_screen_draw("wav_auddec_start", path, 0,
      media_audio_frames_to_ms(frames, (int)rate), 1);
   while (sent < pcm_size && !media_exit_down()) {
      size_t want = pcm_size - sent;
      size_t got;
      uint32_t sent_frames;
      uint32_t got_frames;
      int32_t pts_ms;
      int32_t dur_ms;
      struct media_controls controls;
      int read_failed = 0;

      if (want > chunk_bytes)
         want = chunk_bytes;
      if (want > bytes_per_frame && sent + want < pcm_size)
         want -= want % bytes_per_frame;
      if (want < bytes_per_frame)
         break;
      errno = 0;
      got = fread(buf, 1, want, file);
      if (got < want && ferror(file)) {
         uint64_t pos64 = (uint64_t)data_pos + sent;

         printf("unifrog media wav auddec read_failed path=%s sent=%lu want=%lu got=%lu errno=%d\n",
            path ? path : "", (unsigned long)sent, (unsigned long)want,
            (unsigned long)got, errno);
         if (pos64 <= (uint64_t)LONG_MAX &&
             unifrog_storage_reopen_file_after_io_error(&file, path, (long)pos64,
             "wav_auddec_read", 24, 250) == 0) {
            errno = 0;
            got = fread(buf, 1, want, file);
         } else {
            read_failed = 1;
         }
      }
      if (read_failed)
         break;
      if (!got)
         break;
      got -= got % bytes_per_frame;
      if (!got)
         break;
      sent_frames = sent / bytes_per_frame;
      got_frames = (uint32_t)(got / bytes_per_frame);
      pts_ms = (int32_t)media_audio_frames_to_ms(sent_frames, (int)rate);
      dur_ms = (int32_t)media_audio_frames_to_ms(got_frames, (int)rate);
      if (dur_ms <= 0)
         dur_ms = 1;
      media_audio_pacer_wait_ms(&pacer, pts_ms, dur_ms, feed_lead_ms);
      (void)media_wait_hardware_ahead("wav_auddec", auddec.fd, 0, &pacer,
         max_ahead_ms, path);
      if (media_auddec_send_raw(&auddec, buf, got, pts_ms, dur_ms) != 0) {
         write_failed = 1;
         break;
      }
      sent += (uint32_t)got;
      media_poll_controls(&controls);
      if (controls.exit_down) {
         stopped_by_user = 1;
         break;
      }
      if (controls.seek_delta_ms) {
         if (media_wav_auddec_seek(file, data_pos, pcm_size, frames,
             bytes_per_frame, rate, controls.seek_delta_ms, &auddec, &pacer,
             &sent, &loop_polls, path) != 0)
            break;
         continue;
      }
      if (controls.overlay_toggle)
         media_audio_screen_draw("wav_auddec_toggle", path,
            media_audio_frames_to_ms(sent / bytes_per_frame, (int)rate),
            media_audio_frames_to_ms(frames, (int)rate), 1);
      if ((++loop_polls % 16u) == 0)
         media_audio_screen_draw("wav_auddec", path,
            media_audio_frames_to_ms(sent / bytes_per_frame, (int)rate),
            media_audio_frames_to_ms(frames, (int)rate), 0);
   }
   ret = sent >= pcm_size || (stopped_by_user && sent > 0) ? 0 : -1;

out:
   printf("unifrog media wav auddec end ret=%d sent=%lu/%lu data=%lu frames=%lu/%lu write_failed=%d stopped=%d path=%s\n",
      ret, (unsigned long)sent, (unsigned long)pcm_size,
      (unsigned long)data_size,
      bytes_per_frame ? (unsigned long)(sent / bytes_per_frame) : 0ul,
      (unsigned long)frames, write_failed, stopped_by_user,
      path ? path : "");
   if (auddec.fd >= 0)
      media_auddec_finish(&auddec, finish_timeout_ms);
   media_auddec_close(&auddec);
   free(buf);
   if (file)
      fclose(file);
   return ret;
}

static int media_wav_seek_playback(FILE *file, long data_pos,
   uint32_t data_size, uint32_t frames, unsigned rate, unsigned channels,
   unsigned bits, unsigned block_align, unsigned samples_per_block,
   int adpcm, int seek_delta_ms, struct unifrog_audio *audio,
   unsigned output_channels, uint32_t *played, uint32_t *loop_polls,
   const char *path)
{
   int64_t cur_ms;
   int64_t target_ms;
   uint32_t duration_ms;
   uint32_t target_frame;
   uint64_t offset64;
   uint64_t data_end;

   if (!file || data_pos < 0 || frames == 0 || rate == 0 ||
       channels == 0 || !audio || !played)
      return -1;
   duration_ms = media_audio_frames_to_ms(frames, (int)rate);
   if (duration_ms == 0)
      return -1;
   cur_ms = media_sw_audio_clock_ms(audio, *played, (int)rate, 0);
   target_ms = media_seek_target_ms(cur_ms, seek_delta_ms, duration_ms);
   target_frame = media_audio_ms_to_frames(target_ms, rate);
   if (target_frame > frames)
      target_frame = frames;
   data_end = (uint64_t)data_pos + (uint64_t)data_size;
   if (adpcm) {
      uint32_t block = 0;
      uint32_t max_blocks = block_align ? data_size / block_align : 0;

      if (block_align == 0 || samples_per_block == 0)
         return -1;
      block = target_frame / samples_per_block;
      if (block > max_blocks)
         block = max_blocks;
      target_frame = block * samples_per_block;
      if (target_frame > frames)
         target_frame = frames;
      offset64 = (uint64_t)data_pos + (uint64_t)block * block_align;
   } else {
      unsigned sample_bytes = (bits + 7u) / 8u;

      if (sample_bytes == 0)
         return -1;
      offset64 = (uint64_t)data_pos +
         (uint64_t)target_frame * channels * sample_bytes;
   }
   if (offset64 > data_end)
      offset64 = data_end;
   if (offset64 > (uint64_t)LONG_MAX ||
       fseek(file, (long)offset64, SEEK_SET) != 0) {
      printf("unifrog media seek wav_audio file_failed cur=%lld dur=%lu delta=%d target=%lld frame=%lu offset=%llu path=%s\n",
         (long long)cur_ms, (unsigned long)duration_ms, seek_delta_ms,
         (long long)target_ms, (unsigned long)target_frame,
         (unsigned long long)offset64, path ? path : "");
      return -1;
   }
   if (media_sw_audio_reset_output(audio, rate, output_channels,
       UNIFROG_AUDIO_BACKEND_AUTO, 0, "wav_audio_seek", path) != 0)
      return -1;
   *played = target_frame;
   if (loop_polls)
      *loop_polls = 0;
   media_audio_screen_draw("wav_audio_seek", path,
      media_audio_frames_to_ms(*played, (int)rate), duration_ms, 1);
   printf("unifrog media seek wav_audio cur=%lld dur=%lu delta=%d target=%lld frame=%lu/%lu offset=%llu path=%s\n",
      (long long)cur_ms, (unsigned long)duration_ms, seek_delta_ms,
      (long long)target_ms, (unsigned long)target_frame,
      (unsigned long)frames, (unsigned long long)offset64,
      path ? path : "");
   return 0;
}

int media_play_wav_pcm(const char *path)
{
   struct unifrog_audio audio;
   FILE *file = NULL;
   uint8_t header[12];
   uint8_t chunk[8];
   unsigned channels = 0;
   unsigned rate = 0;
   unsigned bits = 0;
   unsigned format = 0;
   unsigned block_align = 0;
   unsigned samples_per_block = 0;
   unsigned coeff_count = 0;
   int coeffs[7][2] = {
      { 256, 0 }, { 512, -256 }, { 0, 0 }, { 192, 64 },
      { 240, 0 }, { 460, -208 }, { 392, -232 },
   };
   uint32_t data_size = 0;
   long data_pos = -1;
   uint32_t frames;
   uint32_t played = 0;
   uint32_t loop_polls = 0;
   unsigned output_channels = media_audio_output_channels();
   int16_t mono[MEDIA_WAV_CHUNK_FRAMES];
   int16_t pcm[MEDIA_WAV_CHUNK_FRAMES * 2u];
   int wrote_audio = 0;
   int ret = -1;

   memset(&audio, 0, sizeof(audio));
   audio.fd = -1;
   file = fopen(path, "rb");
   if (!file &&
       unifrog_storage_reopen_file_after_io_error(&file, path, 0, "wav_audio_open", 24, 250) != 0) {
      printf("unifrog media wav open failed path=%s\n", path);
      return -1;
   }
   if (fread(header, 1, sizeof(header), file) != sizeof(header) ||
       memcmp(header, "RIFF", 4) != 0 || memcmp(header + 8, "WAVE", 4) != 0)
      goto out;
   while (fread(chunk, 1, sizeof(chunk), file) == sizeof(chunk)) {
      uint32_t size = media_read_le32(chunk + 4);
      long pos = ftell(file);
      long next = pos + (long)((size + 1u) & ~1u);

      if (memcmp(chunk, "fmt ", 4) == 0) {
         uint8_t fmt[40];
         size_t want = size < sizeof(fmt) ? size : sizeof(fmt);

         if (fread(fmt, 1, want, file) != want)
            goto out;
         if (want >= 16u) {
            format = media_read_le16(fmt);
            channels = media_read_le16(fmt + 2);
            rate = media_read_le32(fmt + 4);
            block_align = media_read_le16(fmt + 12);
            bits = media_read_le16(fmt + 14);
            if (format == 2u && want >= 22u) {
               samples_per_block = media_read_le16(fmt + 18);
               coeff_count = media_read_le16(fmt + 20);
               if (coeff_count > ARRAY_SIZE(coeffs))
                  coeff_count = ARRAY_SIZE(coeffs);
               if (want >= 22u + coeff_count * 4u) {
                  for (unsigned c = 0; c < coeff_count; c++) {
                     coeffs[c][0] =
                        (int16_t)media_read_le16(fmt + 22u + c * 4u);
                     coeffs[c][1] =
                        (int16_t)media_read_le16(fmt + 24u + c * 4u);
                  }
               } else {
                  coeff_count = ARRAY_SIZE(coeffs);
               }
            }
         }
      } else if (memcmp(chunk, "data", 4) == 0) {
         data_pos = pos;
         data_size = size;
      }
      if (fseek(file, next, SEEK_SET) != 0)
         break;
      if (format && data_pos >= 0)
         break;
   }
   printf("unifrog media wav probe path=%s format=%u ch=%u rate=%u bits=%u block=%u spb=%u coeffs=%u data=%lu\n",
      path, format, channels, rate, bits, block_align, samples_per_block,
      coeff_count,
      (unsigned long)data_size);
   if ((channels != 1u && channels != 2u) ||
       rate < 8000u || rate > 48000u || data_pos < 0) {
      printf("unifrog media wav unsupported path=%s format=%u ch=%u rate=%u bits=%u\n",
         path, format, channels, rate, bits);
      goto out;
   }
   if ((format == 1u || format == 65534u) &&
       (bits == 8u || bits == 16u || bits == 24u || bits == 32u) &&
       data_size >= channels * ((bits + 7u) / 8u)) {
      frames = data_size / (channels * ((bits + 7u) / 8u));
   } else if (format == 2u && block_align >= (channels == 2u ? 14u : 7u) &&
       samples_per_block >= 2u && coeff_count > 0u &&
       data_size >= block_align) {
      frames = (data_size / block_align) * samples_per_block;
   } else {
      printf("unifrog media wav unsupported path=%s format=%u ch=%u rate=%u bits=%u\n",
         path, format, channels, rate, bits);
      goto out;
   }
   if (fseek(file, data_pos, SEEK_SET) != 0)
      goto out;
   if (unifrog_audio_open(&audio, rate, output_channels, 512, 8) != 0) {
      printf("unifrog media wav audio_open failed rate=%u ch=%u path=%s\n",
         rate, output_channels, path);
      goto out;
   }
   (void)unifrog_audio_set_volume(&audio, media_audio_runtime_volume());
   (void)unifrog_audio_set_mute(&audio, 0);
   (void)unifrog_audio_start(&audio);
   (void)unifrog_audio_set_output_enabled(&audio, 1);
   unifrog_audio_debug_dump(&audio, "wav_after_start");
   printf("unifrog media wav audio start rate=%u src_ch=%u out_ch=%u frames=%lu duration=%lu overlay=1 overlay_hide=A path=%s\n",
      rate, channels, output_channels, (unsigned long)frames,
      (unsigned long)media_audio_frames_to_ms(frames, (int)rate),
      path ? path : "");
   media_controls_reset_for_playback("wav_audio", path);
   media_audio_screen_draw("wav_audio_start", path, 0,
      media_audio_frames_to_ms(frames, (int)rate), 1);
   if (format == 2u) {
      uint8_t *block = malloc(block_align);

      if (!block)
         goto out;
      while (played < frames &&
          fread(block, 1, block_align, file) == block_align) {
         struct media_controls controls;
         int got = media_wav_decode_ms_adpcm_block(block, block_align,
            channels, coeffs, coeff_count, mono,
            frames - played > MEDIA_WAV_CHUNK_FRAMES ?
            MEDIA_WAV_CHUNK_FRAMES : frames - played);

         if (got <= 0)
            break;
         if (media_audio_write_mono_output(&audio, mono, pcm,
             (unsigned)got, output_channels) != 0)
            break;
         wrote_audio = 1;
         played += (uint32_t)got;
         media_poll_controls(&controls);
         if (controls.exit_down)
            break;
         if (controls.seek_delta_ms) {
            if (media_wav_seek_playback(file, data_pos, data_size,
                frames, rate, channels, bits, block_align,
                samples_per_block, 1, controls.seek_delta_ms, &audio,
                output_channels, &played, &loop_polls, path) != 0)
               break;
            continue;
         }
         if (controls.overlay_toggle)
            media_audio_screen_draw("wav_audio_toggle", path,
               media_audio_frames_to_ms(played, (int)rate),
               media_audio_frames_to_ms(frames, (int)rate), 1);
         if ((++loop_polls % 16u) == 0)
            media_audio_screen_draw("wav_audio", path,
               media_audio_frames_to_ms(played, (int)rate),
               media_audio_frames_to_ms(frames, (int)rate), 0);
      }
      free(block);
   } else while (played < frames) {
      unsigned chunk_frames = frames - played;
      unsigned got = 0;

      if (chunk_frames > MEDIA_WAV_CHUNK_FRAMES)
         chunk_frames = MEDIA_WAV_CHUNK_FRAMES;
      while (got < chunk_frames) {
         int32_t left;
         int32_t right;

         if (media_wav_read_pcm_sample(file, bits, &left) != 0)
            break;
         right = left;
         if (channels == 2u &&
             media_wav_read_pcm_sample(file, bits, &right) != 0)
            break;
         if (output_channels > 1u) {
            int16_t sample = media_wav_clip_sample((left + right) >> 1);

            pcm[got * output_channels] = sample;
            pcm[got * output_channels + 1u] = sample;
            got++;
         } else {
            pcm[got++] = media_wav_clip_sample((left + right) >> 1);
         }
      }
      if (!got)
         break;
      if (unifrog_audio_write(&audio, pcm, got) != 0)
         break;
      wrote_audio = 1;
      played += got;
      {
         struct media_controls controls;

         media_poll_controls(&controls);
         if (controls.exit_down)
            break;
         if (controls.seek_delta_ms) {
            if (media_wav_seek_playback(file, data_pos, data_size,
                frames, rate, channels, bits, block_align,
                samples_per_block, 0, controls.seek_delta_ms, &audio,
                output_channels, &played, &loop_polls, path) != 0)
               break;
            continue;
         }
         if (controls.overlay_toggle)
            media_audio_screen_draw("wav_audio_toggle", path,
               media_audio_frames_to_ms(played, (int)rate),
               media_audio_frames_to_ms(frames, (int)rate), 1);
         if ((++loop_polls % 16u) == 0)
            media_audio_screen_draw("wav_audio", path,
               media_audio_frames_to_ms(played, (int)rate),
               media_audio_frames_to_ms(frames, (int)rate), 0);
      }
   }
   printf("unifrog media wav played path=%s frames=%lu/%lu rate=%u\n",
      path, (unsigned long)played, (unsigned long)frames, rate);
   ret = wrote_audio ? 0 : -1;

out:
   if (audio.fd >= 0)
      unifrog_audio_close(&audio);
   if (file)
      fclose(file);
   return ret;
}
