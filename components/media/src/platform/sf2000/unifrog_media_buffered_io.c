#include "unifrog_media_internal.h"

/* Private FFmpeg AVIO wrapper with SD recovery and readahead. */
static uint8_t *media_alloc_file_buffer(size_t *size_out)
{
   size_t size = MEDIA_FILE_BUFFER_SIZE;
   size_t min_size = MEDIA_FILE_BUFFER_MIN_SIZE;
   uint8_t *buffer = NULL;

   if (size_out)
      *size_out = 0;
   if (size < min_size)
      min_size = size;
   while (size >= min_size && size > 0) {
      buffer = av_malloc(size);
      if (buffer) {
         if (size_out)
            *size_out = size;
         return buffer;
      }
      size /= 2u;
   }
   return NULL;
}

static uint8_t *media_alloc_readahead_buffer(size_t *slot_size_out,
   unsigned *slot_count_out, size_t want, size_t min_size,
   unsigned want_slots)
{
   size_t size = want;
   unsigned slots = want_slots;
   uint8_t *buffer = NULL;

   if (slot_size_out)
      *slot_size_out = 0;
   if (slot_count_out)
      *slot_count_out = 0;
   if (size == 0)
      return NULL;
   if (size < min_size)
      min_size = size;
   if (slots == 0)
      slots = 1;
   if (slots > MEDIA_READAHEAD_MAX_SLOTS)
      slots = MEDIA_READAHEAD_MAX_SLOTS;
   while (slots > 0) {
      size = want;
      while (size >= min_size && size > 0) {
         size_t total = size * (size_t)slots;

         if (total / size != (size_t)slots)
            break;
         buffer = av_malloc(total);
         if (buffer) {
            if (slot_size_out)
               *slot_size_out = size;
            if (slot_count_out)
               *slot_count_out = slots;
            return buffer;
         }
         size /= 2u;
      }
      slots /= 2u;
   }
   return NULL;
}

static int media_buffered_tag_is_video(const char *tag)
{
   return tag && strstr(tag, "video") != NULL;
}

static void media_buffered_record_disk_read(struct media_buffered_input *input,
   int64_t pos, ssize_t got, uint32_t elapsed_ms, size_t want)
{
   if (!input)
      return;
   input->disk_read_calls++;
   input->disk_read_ms_total += elapsed_ms;
   if (got > input->max_disk_read)
      input->max_disk_read = (int)got;
   if (elapsed_ms > input->max_disk_read_ms) {
      input->max_disk_read_ms = elapsed_ms;
      input->max_disk_read_pos = pos;
   }
   if (elapsed_ms >= MEDIA_FILE_SLOW_READ_LOG_MS) {
      input->slow_disk_reads++;
      printf("unifrog media buffered_io slow_read tag=%s ms=%lu pos=%lld want=%lu got=%ld disk_reads=%llu seeks=%lu path=%s\n",
         input->tag ? input->tag : "", (unsigned long)elapsed_ms,
         (long long)pos, (unsigned long)want, (long)got,
         (unsigned long long)input->disk_read_calls,
         (unsigned long)input->seek_calls,
         input->path ? input->path : "");
   }
}

static void media_buffered_input_invalidate_cache(
   struct media_buffered_input *input)
{
   if (!input)
      return;
   for (unsigned i = 0; i < MEDIA_READAHEAD_MAX_SLOTS; i++) {
      input->readahead_slots[i].size = 0;
      input->readahead_slots[i].start = 0;
      input->readahead_slots[i].last_used = 0;
   }
   input->readahead_clock = 0;
}

static int media_buffered_recover_fd(struct media_buffered_input *input,
   const char *stage, int64_t pos)
{
   struct stat st;
   int fd;

   if (!input || !input->path || !input->path[0] || pos < 0)
      return AVERROR(EINVAL);
   if (input->storage_recoveries >= 3u)
      return AVERROR(EIO);

   input->storage_recoveries++;
   printf("unifrog media buffered_io recover begin tag=%s stage=%s count=%lu pos=%lld errno=%d path=%s\n",
      input->tag ? input->tag : "", stage ? stage : "",
      (unsigned long)input->storage_recoveries, (long long)pos,
      input->last_errno, input->path);
   if (input->fd >= 0) {
      close(input->fd);
      input->fd = -1;
   }
   media_buffered_input_invalidate_cache(input);

   if (unifrog_storage_recover_after_io_error(stage ?
       stage : "media_buffered", 24, 250) != 0)
      return AVERROR(EIO);

   errno = 0;
   fd = open(input->path, O_RDONLY);
   if (fd < 0) {
      input->last_errno = errno;
      printf("unifrog media buffered_io recover reopen_failed tag=%s stage=%s errno=%d path=%s\n",
         input->tag ? input->tag : "", stage ? stage : "", errno,
         input->path);
      return AVERROR(errno ? errno : EIO);
   }
   input->fd = fd;
   if (fstat(input->fd, &st) == 0)
      input->file_size = (int64_t)st.st_size;
   errno = 0;
   if (lseek(input->fd, (off_t)pos, SEEK_SET) < 0) {
      int seek_errno = errno;

      input->last_errno = seek_errno;
      printf("unifrog media buffered_io recover seek_failed tag=%s stage=%s errno=%d pos=%lld path=%s\n",
         input->tag ? input->tag : "", stage ? stage : "", seek_errno,
         (long long)pos, input->path);
      close(input->fd);
      input->fd = -1;
      return AVERROR(seek_errno ? seek_errno : EIO);
   }
   input->logical_pos = pos;
   input->fd_pos = pos;
   printf("unifrog media buffered_io recover done tag=%s stage=%s count=%lu file=%lld pos=%lld path=%s\n",
      input->tag ? input->tag : "", stage ? stage : "",
      (unsigned long)input->storage_recoveries, (long long)input->file_size,
      (long long)pos, input->path);
   return 0;
}

static uint8_t *media_buffered_readahead_slot_data(
   const struct media_buffered_input *input, unsigned slot)
{
   if (!input || !input->readahead || input->readahead_size == 0 ||
       slot >= input->readahead_slot_count)
      return NULL;
   return input->readahead + (size_t)slot * input->readahead_size;
}

static unsigned media_buffered_readahead_choose_slot(
   struct media_buffered_input *input)
{
   int evicted = 0;
   unsigned best;

   if (!input || input->readahead_slot_count == 0)
      return 0;
   best = unifrog_media_policy_choose_slot(input->readahead_slots,
      input->readahead_slot_count, &evicted);
   if (evicted)
      input->readahead_evictions++;
   return best;
}

static void media_buffered_readahead_touch(struct media_buffered_input *input,
   unsigned slot)
{
   if (!input || slot >= input->readahead_slot_count)
      return;
   input->readahead_clock = unifrog_media_policy_touch(
      input->readahead_slots, input->readahead_slot_count,
      input->readahead_clock, slot);
}

static int media_buffered_sync_fd(struct media_buffered_input *input)
{
   off_t pos;

   if (!input || input->fd < 0)
      return AVERROR(EINVAL);
   if (input->fd_pos == input->logical_pos)
      return 0;
   errno = 0;
   pos = lseek(input->fd, (off_t)input->logical_pos, SEEK_SET);
   if (pos < 0) {
      input->last_errno = errno;
      if (media_buffered_recover_fd(input, "media_lseek",
          input->logical_pos) == 0)
         return 0;
      return AVERROR(errno ? errno : EIO);
   }
   input->fd_pos = (int64_t)pos;
   return 0;
}

static int media_buffered_read_direct(struct media_buffered_input *input,
   uint8_t *buf, int buf_size)
{
   int ret;
   ssize_t got;
   int64_t pos;
   uint32_t start_ms;
   uint32_t elapsed_ms;

   ret = media_buffered_sync_fd(input);
   if (ret < 0)
      return ret;
   pos = input->logical_pos;
   start_ms = unifrog_perf_time_ms();
   errno = 0;
   got = read(input->fd, buf, (size_t)buf_size);
   elapsed_ms = unifrog_perf_time_ms() - start_ms;
   media_buffered_record_disk_read(input, pos, got, elapsed_ms,
      (size_t)buf_size);
   if (got < 0) {
      input->last_errno = errno;
      if (media_buffered_recover_fd(input, "media_read", pos) == 0) {
         errno = 0;
         got = read(input->fd, buf, (size_t)buf_size);
         elapsed_ms = unifrog_perf_time_ms() - start_ms;
         media_buffered_record_disk_read(input, pos, got, elapsed_ms,
            (size_t)buf_size);
         if (got > 0)
            goto read_ok;
         input->last_errno = errno;
      }
      return AVERROR(errno ? errno : EIO);
   }
   if (got == 0)
      return AVERROR_EOF;
read_ok:
   input->disk_read_bytes += (uint64_t)got;
   input->logical_pos += (int64_t)got;
   input->fd_pos += (int64_t)got;
   return (int)got;
}

static int media_buffered_fill_readahead(struct media_buffered_input *input)
{
   int ret;
   ssize_t got;
   int64_t pos;
   uint32_t start_ms;
   uint32_t elapsed_ms;
   unsigned slot_index;
   uint8_t *slot_data;

   if (!input || !input->readahead || input->readahead_size == 0 ||
       input->readahead_slot_count == 0)
      return AVERROR(EINVAL);
   slot_index = media_buffered_readahead_choose_slot(input);
   slot_data = media_buffered_readahead_slot_data(input, slot_index);
   if (!slot_data)
      return AVERROR(EINVAL);
   ret = media_buffered_sync_fd(input);
   if (ret < 0)
      return ret;
   pos = input->logical_pos;
   start_ms = unifrog_perf_time_ms();
   errno = 0;
   got = read(input->fd, slot_data, input->readahead_size);
   elapsed_ms = unifrog_perf_time_ms() - start_ms;
   media_buffered_record_disk_read(input, pos, got, elapsed_ms,
      input->readahead_size);
   input->readahead_slots[slot_index].start = input->logical_pos;
   input->readahead_slots[slot_index].size = 0;
   if (got < 0) {
      input->last_errno = errno;
      if (media_buffered_recover_fd(input, "media_readahead", pos) == 0) {
         errno = 0;
         got = read(input->fd, slot_data, input->readahead_size);
         elapsed_ms = unifrog_perf_time_ms() - start_ms;
         media_buffered_record_disk_read(input, pos, got, elapsed_ms,
            input->readahead_size);
         input->readahead_slots[slot_index].start = input->logical_pos;
         input->readahead_slots[slot_index].size = 0;
         if (got > 0)
            goto readahead_ok;
         input->last_errno = errno;
      }
      return AVERROR(errno ? errno : EIO);
   }
   if (got == 0)
      return AVERROR_EOF;
readahead_ok:
   input->disk_read_bytes += (uint64_t)got;
   input->fd_pos += (int64_t)got;
   input->readahead_slots[slot_index].size = (size_t)got;
   media_buffered_readahead_touch(input, slot_index);
   input->readahead_fills++;
   return 0;
}

static int media_buffered_fill_readahead_at(
   struct media_buffered_input *input, int64_t pos, size_t *got_out)
{
   int64_t saved_logical;
   int slot_index;
   int ret;

   if (got_out)
      *got_out = 0;
   if (!input || pos < 0)
      return AVERROR(EINVAL);
   slot_index = unifrog_media_policy_find_slot(input->readahead_slots,
      input->readahead_slot_count, pos, 0);
   if (slot_index >= 0) {
      struct unifrog_media_readahead_slot *slot =
         &input->readahead_slots[(unsigned)slot_index];
      int64_t end = slot->start + (int64_t)slot->size;

      media_buffered_readahead_touch(input, (unsigned)slot_index);
      if (got_out && end > pos)
         *got_out = (size_t)(end - pos);
      return 0;
   }

   saved_logical = input->logical_pos;
   input->logical_pos = pos;
   ret = media_buffered_fill_readahead(input);
   input->logical_pos = saved_logical;
   if (ret < 0)
      return ret;
   slot_index = unifrog_media_policy_find_slot(input->readahead_slots,
      input->readahead_slot_count, pos, 0);
   if (slot_index >= 0 && got_out)
      *got_out = input->readahead_slots[(unsigned)slot_index].size;
   return 0;
}

static size_t media_buffered_prefill_readahead(
   struct media_buffered_input *input, int64_t start, size_t target_bytes,
   const struct unifrog_media_video_options *options, const char *stage,
   const char *path)
{
   uint64_t before_disk_bytes;
   uint64_t before_disk_ms;
   uint64_t disk_bytes;
   uint64_t disk_ms;
   uint64_t kib_s = 0;
   size_t done = 0;
   int64_t pos = start;

   if (!input || !input->readahead_enabled || target_bytes == 0 || pos < 0)
      return 0;
   before_disk_bytes = input->disk_read_bytes;
   before_disk_ms = input->disk_read_ms_total;
   media_video_progress(options, stage, 0, target_bytes);
   while (!media_exit_down() && done < target_bytes) {
      size_t got = 0;
      size_t count;
      int ret;

      if (input->file_size >= 0 && pos >= input->file_size)
         break;
      ret = media_buffered_fill_readahead_at(input, pos, &got);
      if (ret < 0 || got == 0)
         break;
      count = got;
      if (count > target_bytes - done)
         count = target_bytes - done;
      done += count;
      pos += (int64_t)got;
      media_video_progress(options, stage, done, target_bytes);
   }
   disk_bytes = input->disk_read_bytes - before_disk_bytes;
   disk_ms = input->disk_read_ms_total - before_disk_ms;
   if (disk_ms > 0)
      kib_s = (disk_bytes * 1000ull) / (disk_ms * 1024ull);
   printf("unifrog media buffered_io prefill tag=%s stage=%s start=%lld target=%lu cached=%lu disk_bytes=%llu disk_ms=%llu kib_s=%llu file=%lld path=%s\n",
      input->tag ? input->tag : "", stage ? stage : "",
      (long long)start, (unsigned long)target_bytes, (unsigned long)done,
      (unsigned long long)disk_bytes, (unsigned long long)disk_ms,
      (unsigned long long)kib_s, (long long)input->file_size,
      path ? path : "");
   return done;
}

static int media_buffered_read(void *opaque, uint8_t *buf, int buf_size)
{
   struct media_buffered_input *input = opaque;
   int total = 0;

   if (!input || input->fd < 0 || !buf || buf_size <= 0)
      return AVERROR(EINVAL);
   input->read_calls++;
   input->read_requested += (uint64_t)buf_size;
   if (buf_size > input->max_request)
      input->max_request = buf_size;
   if (!input->readahead_enabled || !input->readahead ||
       input->readahead_size == 0 || input->readahead_slot_count == 0) {
      int got = media_buffered_read_direct(input, buf, buf_size);

      if (got < 0)
         return got;
      input->read_bytes += (uint64_t)got;
      if (got > input->max_read)
         input->max_read = got;
      if (got < buf_size)
         input->short_reads++;
      return got;
   }

   while (total < buf_size) {
      int slot_index;
      struct unifrog_media_readahead_slot *slot;
      int64_t cache_end;
      size_t available = 0;
      size_t to_copy;

      slot_index = unifrog_media_policy_find_slot(input->readahead_slots,
         input->readahead_slot_count, input->logical_pos, 0);
      if (slot_index >= 0) {
         slot = &input->readahead_slots[(unsigned)slot_index];
         cache_end = slot->start + (int64_t)slot->size;
         available = (size_t)(cache_end - input->logical_pos);
      } else {
         int fill_ret;

         input->readahead_misses++;
         fill_ret = media_buffered_fill_readahead(input);
         if (fill_ret < 0) {
            if (total > 0)
               break;
            return fill_ret;
         }
         continue;
      }

      to_copy = (size_t)(buf_size - total);
      if (to_copy > available)
         to_copy = available;
      memcpy(buf + total, media_buffered_readahead_slot_data(input,
            (unsigned)slot_index) +
         (size_t)(input->logical_pos - slot->start), to_copy);
      input->logical_pos += (int64_t)to_copy;
      total += (int)to_copy;
      media_buffered_readahead_touch(input, (unsigned)slot_index);
      input->readahead_hits++;
      input->readahead_hit_bytes += (uint64_t)to_copy;
   }

   if (total <= 0)
      return AVERROR_EOF;
   input->read_bytes += (uint64_t)total;
   if (total > input->max_read)
      input->max_read = total;
   if (total < buf_size)
      input->short_reads++;
   return total;
}

static int64_t media_buffered_seek(void *opaque, int64_t offset, int whence)
{
   struct media_buffered_input *input = opaque;
   off_t pos;
   int seek_whence;
   int64_t target;

   if (!input || input->fd < 0)
      return AVERROR(EINVAL);
   if (whence == AVSEEK_SIZE)
      return input->file_size >= 0 ? input->file_size : AVERROR(ENOSYS);
   seek_whence = whence & ~AVSEEK_FORCE;
   if (seek_whence != SEEK_SET && seek_whence != SEEK_CUR &&
       seek_whence != SEEK_END)
      return AVERROR(EINVAL);
   input->seek_calls++;
   if (seek_whence == SEEK_SET) {
      target = offset;
   } else if (seek_whence == SEEK_CUR) {
      target = input->logical_pos + offset;
   } else if (input->file_size >= 0) {
      target = input->file_size + offset;
   } else {
      errno = 0;
      pos = lseek(input->fd, (off_t)offset, seek_whence);
      if (pos < 0) {
         input->last_errno = errno;
         return AVERROR(errno ? errno : EIO);
      }
      input->logical_pos = (int64_t)pos;
      input->fd_pos = (int64_t)pos;
      return input->logical_pos;
   }
   if (target < 0)
      return AVERROR(EINVAL);
   {
      int slot_index = unifrog_media_policy_find_slot(
         input->readahead_slots, input->readahead_slot_count, target, 1);

      if (slot_index >= 0) {
         media_buffered_readahead_touch(input, (unsigned)slot_index);
         input->readahead_seek_hits++;
         input->logical_pos = target;
         return input->logical_pos;
      }
   }
   input->logical_pos = target;
   if (input->readahead_enabled) {
      /*
       * Keep older windows alive across MP4 demux seeks. The physical lseek is
       * deferred until a cache miss actually needs to read from this target.
       */
      return input->logical_pos;
   }
   errno = 0;
   pos = lseek(input->fd, (off_t)target, SEEK_SET);
   if (pos < 0) {
      input->last_errno = errno;
      return AVERROR(errno ? errno : EIO);
   }
   input->fd_pos = (int64_t)pos;
   input->logical_pos = (int64_t)pos;
   return input->logical_pos;
}

static size_t media_buffered_readahead_choose_size(size_t want,
   size_t min_size, unsigned slots)
{
   size_t min_total;

   if (want == 0)
      return 0;
   if (slots == 0)
      slots = 1;
   if (slots > MEDIA_READAHEAD_MAX_SLOTS)
      slots = MEDIA_READAHEAD_MAX_SLOTS;
   min_total = min_size * (size_t)slots;
   if (min_size != 0 && min_total / min_size != (size_t)slots)
      return want;
   if (want < min_size)
      want = min_size;
   return want;
}

static uint64_t media_buffered_readahead_cover_ms(size_t size,
   int64_t bit_rate)
{
   if (size == 0 || bit_rate <= 0)
      return 0;
   return ((uint64_t)size * 8000ull) / (uint64_t)bit_rate;
}

static size_t media_video_startup_prefill_bytes(
   const struct media_buffered_input *input, const AVFormatContext *fmt)
{
   uint64_t by_time = 0;
   size_t target = MEDIA_VIDEO_PREFILL_MIN_BYTES;
   int64_t bit_rate = fmt ? fmt->bit_rate : 0;

   if (!input || MEDIA_VIDEO_PREFILL_MAX_BYTES == 0)
      return 0;
   if (MEDIA_VIDEO_PREFILL_TARGET_MS > 0 && bit_rate > 0)
      by_time = ((uint64_t)bit_rate *
         (uint64_t)MEDIA_VIDEO_PREFILL_TARGET_MS + 7999ull) / 8000ull;
   if (by_time > (uint64_t)target)
      target = by_time > (uint64_t)SIZE_MAX ? SIZE_MAX : (size_t)by_time;
   if (target > MEDIA_VIDEO_PREFILL_MAX_BYTES)
      target = MEDIA_VIDEO_PREFILL_MAX_BYTES;
   if (target > input->readahead_total_size)
      target = input->readahead_total_size;
   if (input->file_size >= 0 && target > (size_t)input->file_size)
      target = (size_t)input->file_size;
   if (input->readahead_size > 0 && target > 0) {
      size_t rounded = ((target + input->readahead_size - 1u) /
         input->readahead_size) * input->readahead_size;

      if (rounded > target && rounded <= input->readahead_total_size)
         target = rounded;
   }
   if (input->file_size >= 0 && target > (size_t)input->file_size)
      target = (size_t)input->file_size;
   return target;
}

static int media_buffered_readahead_mode_values(const char *tag,
   size_t *want_out, size_t *min_out, unsigned *slots_out)
{
   int video = media_buffered_tag_is_video(tag);

   if (want_out)
      *want_out = video ? MEDIA_VIDEO_READAHEAD_SIZE :
         MEDIA_FILE_READAHEAD_SIZE;
   if (min_out)
      *min_out = video ? MEDIA_VIDEO_READAHEAD_MIN_SIZE :
         MEDIA_FILE_READAHEAD_MIN_SIZE;
   if (slots_out)
      *slots_out = video ? MEDIA_VIDEO_READAHEAD_SLOTS :
         MEDIA_FILE_READAHEAD_SLOTS;
   return video;
}

static int media_buffered_readahead_sane_config(size_t *want,
   size_t *min_size, unsigned *slots)
{
   if (!want || !min_size || !slots)
      return 0;
   if (*want == 0)
      return 0;
   if (*slots == 0)
      *slots = 1;
   if (*slots > MEDIA_READAHEAD_MAX_SLOTS)
      *slots = MEDIA_READAHEAD_MAX_SLOTS;
   *want = media_buffered_readahead_choose_size(*want, *min_size, *slots);
   if (*min_size == 0 || *min_size > *want)
      *min_size = *want;
   return 1;
}

int media_buffered_input_enable_readahead(
   struct media_buffered_input *input, const AVFormatContext *fmt,
   const char *tag, const char *path)
{
   uint64_t cover_ms = 0;
   uint64_t total_cover_ms = 0;
   int64_t bit_rate = fmt ? fmt->bit_rate : 0;
   size_t want;
   size_t min_size;
   unsigned slots;
   int video = media_buffered_readahead_mode_values(tag, &want, &min_size,
      &slots);
   size_t normal_want;
   size_t normal_min_size;
   unsigned normal_slots;
   int preload = 0;

   if (!input || input->fd < 0)
      return -1;
   if (input->readahead_enabled)
      return 0;
   if (!media_buffered_readahead_sane_config(&want, &min_size, &slots)) {
      printf("unifrog media buffered_io readahead disabled tag=%s mode=%s path=%s\n",
         tag ? tag : "", video ? "video" : "audio", path ? path : "");
      return -1;
   }
   normal_want = want;
   normal_min_size = min_size;
   normal_slots = slots;
   if (video && MEDIA_VIDEO_PRELOAD_MAX_BYTES > 0 && input->file_size > 0 &&
       (uint64_t)input->file_size <= (uint64_t)MEDIA_VIDEO_PRELOAD_MAX_BYTES) {
      want = (size_t)input->file_size;
      min_size = want;
      slots = 1;
      preload = 1;
   }
   input->readahead = media_alloc_readahead_buffer(&input->readahead_size,
      &input->readahead_slot_count, want, min_size, slots);
   if (!input->readahead && preload) {
      printf("unifrog media buffered_io preload alloc_failed tag=%s path=%s want=%lu fallback_want=%lu fallback_slots=%u\n",
         tag ? tag : "", path ? path : "", (unsigned long)want,
         (unsigned long)normal_want, normal_slots);
      want = normal_want;
      min_size = normal_min_size;
      slots = normal_slots;
      preload = 0;
      input->readahead = media_alloc_readahead_buffer(&input->readahead_size,
         &input->readahead_slot_count, want, min_size, slots);
   }
   if (!input->readahead) {
      printf("unifrog media buffered_io readahead alloc_failed tag=%s mode=%s path=%s want=%lu min=%lu slots=%u\n",
         tag ? tag : "", video ? "video" : "audio", path ? path : "",
         (unsigned long)want, (unsigned long)min_size, slots);
      return -1;
   }
   input->readahead_total_size =
      input->readahead_size * (size_t)input->readahead_slot_count;
   input->readahead_enabled = 1;
   input->readahead_preload = preload;
   media_buffered_input_invalidate_cache(input);
   cover_ms = media_buffered_readahead_cover_ms(input->readahead_size,
      bit_rate);
   total_cover_ms = media_buffered_readahead_cover_ms(
      input->readahead_total_size, bit_rate);
   printf("unifrog media buffered_io readahead enabled tag=%s mode=%s preload=%d slot=%lu total=%lu slots=%u want=%lu min=%lu bitrate=%lld cover_ms=%llu total_cover_ms=%llu logical=%lld fd_pos=%lld path=%s\n",
      tag ? tag : "", video ? "video" : "audio",
      preload, (unsigned long)input->readahead_size,
      (unsigned long)input->readahead_total_size,
      input->readahead_slot_count, (unsigned long)want,
      (unsigned long)min_size, (long long)bit_rate,
      (unsigned long long)cover_ms, (unsigned long long)total_cover_ms,
      (long long)input->logical_pos, (long long)input->fd_pos,
      path ? path : "");
   return 0;
}

void media_buffered_input_enable_video_readahead(
   struct media_buffered_input *input, const AVFormatContext *fmt,
   const struct unifrog_media_video_options *options, const char *path)
{
   if (!input || input->readahead_enabled)
      return;
   (void)media_buffered_input_enable_readahead(input, fmt, "native_video",
      path);
   if (!input->readahead_enabled)
      return;
   if (input->readahead_preload && input->file_size > 0) {
      (void)media_buffered_prefill_readahead(input, 0,
         (size_t)input->file_size, options, "preload", path);
   } else {
      size_t prefill = media_video_startup_prefill_bytes(input, fmt);

      if (prefill > 0)
         (void)media_buffered_prefill_readahead(input, input->logical_pos,
            prefill, options, "buffering", path);
   }
}

int media_buffered_input_open(AVFormatContext **fmt_out,
   struct media_buffered_input *input, const char *path, const char *tag)
{
   AVFormatContext *fmt = NULL;
   uint8_t *buffer = NULL;
   size_t buffer_size = 0;
   struct stat st;
   int ret;

   if (fmt_out)
      *fmt_out = NULL;
   if (!fmt_out || !input || !path)
      return AVERROR(EINVAL);
   memset(input, 0, sizeof(*input));
   input->fd = -1;
   input->tag = tag;
   input->path = path;
   input->file_size = -1;
   input->logical_pos = 0;
   input->fd_pos = 0;
   printf("unifrog media buffered_io stage=open_begin tag=%s path=%s\n",
      tag ? tag : "", path);
   errno = 0;
   input->fd = open(path, O_RDONLY);
   if (input->fd < 0) {
      input->last_errno = errno;
      printf("unifrog media buffered_io open_failed tag=%s path=%s errno=%d\n",
         tag ? tag : "", path, errno);
      if (unifrog_storage_recover_after_io_error(tag ? tag :
          "media_open", 24, 250) == 0) {
         errno = 0;
         input->fd = open(path, O_RDONLY);
      }
      if (input->fd < 0) {
         input->last_errno = errno;
         printf("unifrog media buffered_io reopen_failed tag=%s path=%s errno=%d\n",
            tag ? tag : "", path, errno);
         return AVERROR(errno ? errno : EIO);
      }
      input->storage_recoveries++;
   }
   printf("unifrog media buffered_io stage=open_done tag=%s fd=%d path=%s\n",
      tag ? tag : "", input->fd, path);
   memset(&st, 0, sizeof(st));
   if (fstat(input->fd, &st) == 0)
      input->file_size = (int64_t)st.st_size;
   printf("unifrog media buffered_io stage=fstat_done tag=%s fd=%d file=%lld path=%s\n",
      tag ? tag : "", input->fd, (long long)input->file_size, path);
   printf("unifrog media buffered_io stage=buffer_alloc_begin tag=%s want=%lu min=%lu path=%s\n",
      tag ? tag : "", (unsigned long)MEDIA_FILE_BUFFER_SIZE,
      (unsigned long)MEDIA_FILE_BUFFER_MIN_SIZE, path);
   buffer = media_alloc_file_buffer(&buffer_size);
   if (!buffer) {
      printf("unifrog media buffered_io alloc_failed tag=%s path=%s want=%lu min=%lu\n",
         tag ? tag : "", path, (unsigned long)MEDIA_FILE_BUFFER_SIZE,
         (unsigned long)MEDIA_FILE_BUFFER_MIN_SIZE);
      close(input->fd);
      input->fd = -1;
      return AVERROR(ENOMEM);
   }
   printf("unifrog media buffered_io stage=buffer_alloc_done tag=%s buffer=%lu path=%s\n",
      tag ? tag : "", (unsigned long)buffer_size, path);
   input->buffer_size = buffer_size;
   printf("unifrog media buffered_io stage=avio_alloc_begin tag=%s path=%s\n",
      tag ? tag : "", path);
   input->avio = avio_alloc_context(buffer, (int)buffer_size, 0, input,
      media_buffered_read, NULL, media_buffered_seek);
   if (!input->avio) {
      av_freep(&buffer);
      close(input->fd);
      input->fd = -1;
      return AVERROR(ENOMEM);
   }
   printf("unifrog media buffered_io stage=avio_alloc_done tag=%s avio=0x%08lx path=%s\n",
      tag ? tag : "", (unsigned long)(uintptr_t)input->avio, path);
   printf("unifrog media buffered_io stage=format_alloc_begin tag=%s path=%s\n",
      tag ? tag : "", path);
   fmt = avformat_alloc_context();
   if (!fmt) {
      av_freep(&input->avio->buffer);
      avio_context_free(&input->avio);
      close(input->fd);
      input->fd = -1;
      return AVERROR(ENOMEM);
   }
   printf("unifrog media buffered_io stage=format_alloc_done tag=%s fmt=0x%08lx path=%s\n",
      tag ? tag : "", (unsigned long)(uintptr_t)fmt, path);
   fmt->pb = input->avio;
   printf("unifrog media buffered_io stage=avformat_open_begin tag=%s fmt=0x%08lx path=%s\n",
      tag ? tag : "", (unsigned long)(uintptr_t)fmt, path);
   ret = avformat_open_input(&fmt, path, NULL, NULL);
   printf("unifrog media buffered_io open tag=%s ret=%d fd=%d buffer=%lu file=%lld reads=%llu bytes=%llu path=%s\n",
      tag ? tag : "", ret, input->fd, (unsigned long)input->buffer_size,
      (long long)input->file_size, (unsigned long long)input->read_calls,
      (unsigned long long)input->read_bytes, path);
   if (ret < 0) {
      if (fmt)
         avformat_close_input(&fmt);
      return ret;
   }
   *fmt_out = fmt;
   return 0;
}

void media_buffered_input_log_coverage(
   const struct media_buffered_input *input, const AVFormatContext *fmt,
   const char *tag, const char *path)
{
   uint64_t cover_ms = 0;
   int64_t bit_rate = fmt ? fmt->bit_rate : 0;

   if (!input || input->buffer_size == 0)
      return;
   if (bit_rate > 0)
      cover_ms = ((uint64_t)input->buffer_size * 8000ull) / (uint64_t)bit_rate;
   printf("unifrog media buffered_io coverage tag=%s io_chunk=%lu min=%lu bitrate=%lld chunk_cover_ms=%llu duration_us=%lld path=%s\n",
      tag ? tag : "", (unsigned long)input->buffer_size,
      (unsigned long)MEDIA_FILE_BUFFER_MIN_SIZE, (long long)bit_rate,
      (unsigned long long)cover_ms, fmt ? (long long)fmt->duration : -1ll,
      path ? path : "");
}

void media_buffered_input_close(struct media_buffered_input *input,
   const char *tag, const char *path)
{
   if (!input || (input->fd < 0 && !input->avio))
      return;
   printf("unifrog media buffered_io close tag=%s fd=%d buffer=%lu readahead=%lu slot=%lu slots=%u preload=%d file=%lld reads=%llu bytes=%llu requested=%llu disk_reads=%llu disk_bytes=%llu hits=%llu hit_bytes=%llu misses=%llu fills=%llu evict=%llu seek_hits=%llu slow=%llu disk_ms=%llu max_disk_ms=%lu max_disk_pos=%lld max_req=%d max_read=%d max_disk=%d short=%lu seeks=%lu recover=%lu logical=%lld fd_pos=%lld errno=%d path=%s\n",
      tag ? tag : "", input->fd, (unsigned long)input->buffer_size,
      (unsigned long)input->readahead_total_size,
      (unsigned long)input->readahead_size, input->readahead_slot_count,
      input->readahead_preload, (long long)input->file_size,
      (unsigned long long)input->read_calls,
      (unsigned long long)input->read_bytes,
      (unsigned long long)input->read_requested,
      (unsigned long long)input->disk_read_calls,
      (unsigned long long)input->disk_read_bytes,
      (unsigned long long)input->readahead_hits,
      (unsigned long long)input->readahead_hit_bytes,
      (unsigned long long)input->readahead_misses,
      (unsigned long long)input->readahead_fills,
      (unsigned long long)input->readahead_evictions,
      (unsigned long long)input->readahead_seek_hits,
      (unsigned long long)input->slow_disk_reads,
      (unsigned long long)input->disk_read_ms_total,
      (unsigned long)input->max_disk_read_ms,
      (long long)input->max_disk_read_pos, input->max_request,
      input->max_read, input->max_disk_read,
      (unsigned long)input->short_reads, (unsigned long)input->seek_calls,
      (unsigned long)input->storage_recoveries,
      (long long)input->logical_pos, (long long)input->fd_pos,
      input->last_errno, path ? path : "");
   if (input->avio) {
      av_freep(&input->avio->buffer);
      avio_context_free(&input->avio);
   }
   av_freep(&input->readahead);
   if (input->fd >= 0)
      close(input->fd);
   memset(input, 0, sizeof(*input));
   input->fd = -1;
   input->file_size = -1;
}
