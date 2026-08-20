#define _POSIX_C_SOURCE 200809L

#include <unifrog/backlight.h>
#include <unifrog/abi.h>
#include <unifrog/battery.h>
#include <unifrog/boot_logo.h>
#include <unifrog/diag.h>
#include <unifrog/exception_record.h>
#include <unifrog/input.h>
#include <unifrog/log.h>
#include <unifrog/linux_host.h>
#include <unifrog/perf.h>
#include <unifrog/platform.h>
#include <unifrog/scpu.h>
#include <unifrog/task.h>
#include <unifrog/panic.h>

#include <errno.h>
#include <stdarg.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static unsigned linux_backlight = 100;
static unsigned linux_scpu_mhz = 198;
static uint32_t linux_buttons;
static uint32_t linux_previous_buttons;
static int linux_input_suppressed;
static int linux_stop_requested;
static pthread_mutex_t linux_input_lock = PTHREAD_MUTEX_INITIALIZER;
static char linux_active_profile[16] = "linux";
static size_t linux_log_auto_flush_bytes = 64u * 1024u;

struct linux_task_start {
   unifrog_task_entry entry;
   void *arg;
};

static void *linux_task_trampoline(void *arg)
{
   struct linux_task_start *start = arg;
   unifrog_task_entry entry = start->entry;
   void *entry_arg = start->arg;

   free(start);
   entry(entry_arg);
   return NULL;
}

void unifrog_platform_init_board(void)
{
}

int unifrog_platform_storage_ready(void)
{
   return 1;
}

int unifrog_platform_mount_storage(void)
{
   return 0;
}

int unifrog_platform_recover_storage(const char *tag, unsigned attempts,
   unsigned delay_ms)
{
   (void)tag;
   (void)attempts;
   (void)delay_ms;
   return 0;
}

int unifrog_platform_recover_storage_after_io_error(const char *tag,
   unsigned attempts, unsigned delay_ms)
{
   return unifrog_platform_recover_storage(tag, attempts, delay_ms);
}

int unifrog_platform_sd_runtime_supported(void)
{
   return 1;
}

int unifrog_platform_sd_apply_profile(const char *profile,
   unsigned mount_attempts, unsigned mount_delay_ms, char *detail,
   size_t detail_size)
{
   (void)mount_attempts;
   (void)mount_delay_ms;
   snprintf(linux_active_profile, sizeof(linux_active_profile), "%s",
      profile && profile[0] ? profile : "linux");
   if (detail && detail_size)
      snprintf(detail, detail_size, "linux profile %s", linux_active_profile);
   return 0;
}

int unifrog_platform_sd_restore_boot(unsigned mount_attempts,
   unsigned mount_delay_ms, char *detail, size_t detail_size)
{
   return unifrog_platform_sd_apply_profile("linux", mount_attempts,
      mount_delay_ms, detail, detail_size);
}

int unifrog_platform_sd_profile_allowed(const char *profile)
{
   (void)profile;
   return 1;
}

const char *unifrog_platform_sd_active_profile(void)
{
   return linux_active_profile;
}

int unifrog_platform_sd_describe(char *detail, size_t detail_size)
{
   if (detail && detail_size)
      snprintf(detail, detail_size, "linux host filesystem");
   return 0;
}

void unifrog_platform_set_storage_stage_callback(
   unifrog_platform_storage_stage_cb cb, void *userdata)
{
   (void)cb;
   (void)userdata;
}

void unifrog_platform_storage_diag_note(const char *operation,
   const char *stage)
{
   (void)operation;
   (void)stage;
}

void unifrog_platform_sd_debug_dump(const char *tag)
{
   fprintf(stderr, "linux sd debug: %s\n", tag ? tag : "");
}

void unifrog_platform_sd_mmc_diag_begin(const char *tag) { (void)tag; }
void unifrog_platform_sd_mmc_diag_checkpoint(const char *tag) { (void)tag; }
void unifrog_platform_sd_mmc_diag_checkpoint_summary(const char *tag) { (void)tag; }
void unifrog_platform_sd_mmc_diag_end(const char *tag) { (void)tag; }
void unifrog_platform_set_storage_log_suspended(int suspended)
{
   unifrog_log_set_disk_suspended(suspended);
}
void unifrog_platform_note_storage_unstable(unsigned ticks) { (void)ticks; }

void unifrog_platform_debug_status(struct unifrog_platform_debug_status *status)
{
   if (status)
      memset(status, 0, sizeof(*status));
}

int unifrog_platform_debug_write(const void *data, size_t size,
   unsigned repeat)
{
   if ((!data && size) || !repeat)
      return -EINVAL;
   for (unsigned i = 0; i < repeat; i++) {
      if (size && fwrite(data, 1, size, stderr) != size)
         return -EIO;
   }
   return fflush(stderr) == 0 ? 0 : -EIO;
}

int unifrog_platform_wait_for_storage(void)
{
   return 0;
}

void unifrog_platform_idle_forever(void)
{
   for (;;)
      sleep(60);
}

static int linux_vlog(FILE *stream, enum unifrog_log_level level,
   const char *component, const char *fmt, va_list ap)
{
   int ret;

   if (!unifrog_log_would_write(level))
      return 0;
   fprintf(stream, "UFLOG level=%s component=%s msg=",
      unifrog_log_level_name(level), component && component[0] ? component : "app");
   ret = vfprintf(stream, fmt, ap);
   if (ret > 0 && fmt && fmt[0] && fmt[strlen(fmt) - 1] != '\n')
      fputc('\n', stream);
   fflush(stream);
   return ret;
}

int unifrog_log(const char *fmt, ...)
{
   va_list ap;
   int ret;

   va_start(ap, fmt);
   ret = linux_vlog(stderr, UNIFROG_LOG_INFO, "app", fmt, ap);
   va_end(ap);
   return ret;
}

int unifrog_log_at(enum unifrog_log_level level, const char *component,
   const char *fmt, ...)
{
   va_list ap;
   int ret;

   va_start(ap, fmt);
   ret = linux_vlog(stderr, level, component, fmt, ap);
   va_end(ap);
   return ret;
}

int unifrog_log_sync(const char *fmt, ...)
{
   va_list ap;
   int ret;

   va_start(ap, fmt);
   ret = linux_vlog(stderr, UNIFROG_LOG_WARN, "storage.sync", fmt, ap);
   va_end(ap);
   return ret;
}

int unifrog_log_reset(void) { return 0; }
int unifrog_log_flush(void) { return fflush(stderr); }
int unifrog_log_flush_force(void) { return fflush(stderr); }
void unifrog_log_note_storage_quiet(unsigned ms) { (void)ms; }
void unifrog_log_set_disk_suspended(int suspended) { (void)suspended; }
void unifrog_log_defer_begin(void) {}
void unifrog_log_defer_end(void) {}
int unifrog_log_flush_deferred(void) { return 0; }
int unifrog_log_disk_writes_enabled(void) { return 0; }
int unifrog_log_disk_available(void) { return 0; }
void unifrog_log_set_disk_available(int available) { (void)available; }
const char *unifrog_log_last_path(void) { return ""; }
int unifrog_log_last_result(void) { return 0; }
size_t unifrog_log_auto_flush_bytes(void) { return linux_log_auto_flush_bytes; }
void unifrog_log_set_auto_flush_bytes(size_t bytes) { linux_log_auto_flush_bytes = bytes; }
size_t unifrog_log_capacity(void) { return 0; }
size_t unifrog_log_pending(void) { return 0; }

int unifrog_abi_application_memory_slot(struct unifrog_abi_memory_slot *slot)
{
   if (!slot)
      return -EINVAL;
   memset(slot, 0, sizeof(*slot));
   slot->size = sizeof(*slot);
   return -1;
}

int unifrog_abi_application_memory_reserve_top(size_t bytes,
   size_t alignment, void **out_ptr)
{
   (void)bytes;
   (void)alignment;
   if (out_ptr)
      *out_ptr = NULL;
   return -1;
}

void unifrog_abi_application_memory_release_top(void *ptr)
{
   (void)ptr;
}

void unifrog_diag_memory_snapshot(const char *tag)
{
   (void)tag;
}

void unifrog_panic_screen(const char *title, uint32_t a, uint32_t b,
   uint32_t c, uint32_t d)
{
   fprintf(stderr, "linux panic: %s %08lx %08lx %08lx %08lx\n",
      title ? title : "", (unsigned long)a, (unsigned long)b,
      (unsigned long)c, (unsigned long)d);
}

void unifrog_panic_screen_labeled(const char *title, const char *label0,
   uint32_t value0, const char *label1, uint32_t value1,
   const char *label2, uint32_t value2, const char *label3,
   uint32_t value3)
{
   fprintf(stderr,
      "linux panic: %s %s=%08lx %s=%08lx %s=%08lx %s=%08lx\n",
      title ? title : "", label0 ? label0 : "a", (unsigned long)value0,
      label1 ? label1 : "b", (unsigned long)value1,
      label2 ? label2 : "c", (unsigned long)value2,
      label3 ? label3 : "d", (unsigned long)value3);
}

void unifrog_exception_panic(uint32_t cause, uint32_t epc,
   uint32_t badvaddr, uint32_t ra)
{
   unifrog_panic_screen("EXCEPTION", cause, epc, badvaddr, ra);
}

void unifrog_panic_trigger_test_exception(void)
{
}

void unifrog_panic_trigger_cpu_exception(void)
{
}

int unifrog_boot_logo_present(struct unifrog_fb *fb, const char *tag)
{
   (void)fb;
   (void)tag;
   return 0;
}

int unifrog_boot_logo_present_early(void) { return 0; }
int unifrog_boot_logo_is_active(void) { return 0; }
uint32_t unifrog_boot_logo_shown_ms(void) { return 0; }
void unifrog_boot_logo_mark_replaced(void) {}
void unifrog_boot_logo_release_early(void) {}

void unifrog_exception_record_store(uint32_t cause, uint32_t epc,
   uint32_t badvaddr, uint32_t ra)
{
   (void)cause;
   (void)epc;
   (void)badvaddr;
   (void)ra;
}

int unifrog_exception_record_peek(uint32_t *cause, uint32_t *epc,
   uint32_t *badvaddr, uint32_t *ra, uint32_t *count)
{
   if (cause)
      *cause = 0;
   if (epc)
      *epc = 0;
   if (badvaddr)
      *badvaddr = 0;
   if (ra)
      *ra = 0;
   if (count)
      *count = 0;
   return 0;
}

void unifrog_exception_record_log_and_clear(const char *tag)
{
   (void)tag;
}

uint32_t unifrog_exception_activity_hash(const char *text)
{
   uint32_t hash = 2166136261u;
   const unsigned char *p = (const unsigned char *)(text ? text : "");

   while (*p) {
      hash ^= *p++;
      hash *= 16777619u;
   }
   return hash;
}

void unifrog_exception_activity_set(uint32_t phase, uint32_t marker,
   uint32_t detail0, uint32_t detail1)
{
   (void)phase;
   (void)marker;
   (void)detail0;
   (void)detail1;
}

void unifrog_exception_activity_clear(void)
{
}

void unifrog_exception_activity_log_and_clear(const char *tag)
{
   (void)tag;
}

uint64_t unifrog_perf_time_us(void)
{
   struct timespec ts;

   clock_gettime(CLOCK_MONOTONIC, &ts);
   return (uint64_t)ts.tv_sec * 1000000ull + (uint64_t)ts.tv_nsec / 1000ull;
}

uint32_t unifrog_perf_time_ms(void)
{
   return (uint32_t)(unifrog_perf_time_us() / 1000ull);
}

uint32_t unifrog_perf_count(void)
{
   return (uint32_t)unifrog_perf_time_us();
}

uint32_t unifrog_perf_elapsed(uint32_t start, uint32_t end)
{
   return end - start;
}

void unifrog_perf_delay_us(unsigned us)
{
   struct timespec ts;

   ts.tv_sec = us / 1000000u;
   ts.tv_nsec = (long)(us % 1000000u) * 1000L;
   while (nanosleep(&ts, &ts) != 0 && errno == EINTR)
      ;
}

int unifrog_task_create(unifrog_task_entry entry, void *arg,
   const char *name, enum unifrog_task_priority priority,
   unifrog_task_handle *handle)
{
   struct linux_task_start *start;
   pthread_t thread;

   (void)name;
   (void)priority;
   if (handle)
      *handle = NULL;
   if (!entry)
      return -EINVAL;
   start = malloc(sizeof(*start));
   if (!start)
      return -ENOMEM;
   start->entry = entry;
   start->arg = arg;
   if (pthread_create(&thread, NULL, linux_task_trampoline, start) != 0) {
      free(start);
      return -1;
   }
   pthread_detach(thread);
   if (handle)
      *handle = (unifrog_task_handle)(uintptr_t)1u;
   return 0;
}

void unifrog_task_delay_ms(unsigned ms)
{
   struct timespec ts;

   ts.tv_sec = ms / 1000u;
   ts.tv_nsec = (long)(ms % 1000u) * 1000000L;
   while (nanosleep(&ts, &ts) != 0 && errno == EINTR)
      ;
}

void unifrog_perf_cache_flush(const void *ptr, size_t len) { (void)ptr; (void)len; }
void unifrog_perf_cache_invalidate(const void *ptr, size_t len) { (void)ptr; (void)len; }
void unifrog_perf_cache_flush_invalidate(const void *ptr, size_t len) { (void)ptr; (void)len; }
void unifrog_perf_cache_flush_all(void) {}
void *unifrog_perf_cached_addr(const void *ptr) { return (void *)ptr; }
void *unifrog_perf_uncached_addr(const void *ptr) { return (void *)ptr; }
uintptr_t unifrog_perf_phys_addr(const void *ptr) { return (uintptr_t)ptr; }

int unifrog_perf_query_caps(struct unifrog_perf_caps *caps)
{
   if (!caps)
      return -EINVAL;
   memset(caps, 0, sizeof(*caps));
   caps->scpu_mhz_est = linux_scpu_mhz;
   caps->framebuffer_width = 320;
   caps->framebuffer_height = 240;
   caps->framebuffer_bpp = 16;
   return 0;
}

int unifrog_backlight_get(unsigned *level)
{
   if (!level)
      return -EINVAL;
   *level = linux_backlight;
   return 0;
}

int unifrog_backlight_set(unsigned level)
{
   linux_backlight = level > 100 ? 100 : level;
   return 0;
}

int unifrog_scpu_supported(void)
{
   return 1;
}

unsigned unifrog_scpu_current_mhz(void)
{
   return linux_scpu_mhz;
}

int unifrog_scpu_capture(struct unifrog_scpu_clock *clock)
{
   if (!clock)
      return -EINVAL;
   memset(clock, 0, sizeof(*clock));
   clock->valid = 1;
   clock->mhz = linux_scpu_mhz;
   return 0;
}

int unifrog_scpu_apply_mhz(unsigned mhz)
{
   linux_scpu_mhz = mhz;
   return 0;
}

int unifrog_scpu_restore(const struct unifrog_scpu_clock *clock)
{
   if (!clock || !clock->valid)
      return -EINVAL;
   linux_scpu_mhz = clock->mhz;
   return 0;
}

int unifrog_battery_read_raw(unsigned char *raw, const char **source,
   int force_log)
{
   (void)force_log;
   if (raw)
      *raw = 100;
   if (source)
      *source = "linux";
   return 0;
}

int unifrog_battery_update(struct unifrog_battery_status *status,
   int force_log)
{
   (void)force_log;
   if (!status)
      return -EINVAL;
   (void)unifrog_battery_status_apply_sample(status, 210u, 4200u, "linux",
      unifrog_perf_time_ms());
   return 0;
}

void unifrog_input_init(void) {}
void unifrog_linux_input_set_buttons(uint32_t buttons)
{
   pthread_mutex_lock(&linux_input_lock);
   if (linux_input_suppressed) {
      if (buttons == 0)
         linux_input_suppressed = 0;
      linux_buttons = 0;
   } else {
      linux_buttons = buttons;
   }
   pthread_mutex_unlock(&linux_input_lock);
}
void unifrog_linux_set_stop_requested(int requested)
{
   pthread_mutex_lock(&linux_input_lock);
   linux_stop_requested = requested != 0;
   pthread_mutex_unlock(&linux_input_lock);
}
int unifrog_linux_stop_requested(void)
{
   int requested;

   pthread_mutex_lock(&linux_input_lock);
   requested = linux_stop_requested;
   pthread_mutex_unlock(&linux_input_lock);
   return requested;
}
void unifrog_input_clear(void)
{
   pthread_mutex_lock(&linux_input_lock);
   linux_buttons = linux_previous_buttons = 0;
   linux_input_suppressed = 1;
   pthread_mutex_unlock(&linux_input_lock);
}
void unifrog_input_recover_core_transition(const char *tag) { (void)tag; }
void unifrog_input_recover_after_core(void) {}
void unifrog_input_save_previous(void)
{
   pthread_mutex_lock(&linux_input_lock);
   linux_previous_buttons = linux_buttons;
   pthread_mutex_unlock(&linux_input_lock);
}
void unifrog_input_poll(void) {}
void unifrog_input_poll_with_wireless_divisor(unsigned wireless_divisor) { (void)wireless_divisor; }
uint32_t unifrog_input_poll_local_raw(void) { return unifrog_input_menu_buttons(); }
uint32_t unifrog_input_poll_local_direct_buttons(void) { return unifrog_input_menu_buttons(); }
uint32_t unifrog_input_buttons(void) { return unifrog_input_menu_buttons(); }
uint32_t unifrog_input_menu_buttons(void)
{
   uint32_t buttons;

   pthread_mutex_lock(&linux_input_lock);
   buttons = linux_buttons;
   pthread_mutex_unlock(&linux_input_lock);
   return buttons;
}
uint32_t unifrog_input_previous_buttons(void)
{
   uint32_t buttons;

   pthread_mutex_lock(&linux_input_lock);
   buttons = linux_previous_buttons;
   pthread_mutex_unlock(&linux_input_lock);
   return buttons;
}
uint32_t unifrog_input_local_buttons(void) { return unifrog_input_menu_buttons(); }
uint32_t unifrog_input_local_raw(void) { return unifrog_input_menu_buttons(); }
int unifrog_input_uses_stock_bits(void) { return 0; }
int unifrog_input_down(enum unifrog_button button) { return (unifrog_input_buttons() & UNIFROG_BUTTON_MASK(button)) != 0; }
int unifrog_input_pressed(enum unifrog_button button)
{
   uint32_t pressed;

   pthread_mutex_lock(&linux_input_lock);
   pressed = linux_buttons & ~linux_previous_buttons;
   pthread_mutex_unlock(&linux_input_lock);
   return (pressed & UNIFROG_BUTTON_MASK(button)) != 0;
}
int unifrog_input_menu_pressed(enum unifrog_button button) { return unifrog_input_pressed(button); }
const char *unifrog_input_button_name(enum unifrog_button button)
{
   static const char *const names[] = {
      "R", "Y", "X", "L", "A", "B", "SELECT", "START",
      "UP", "DOWN", "LEFT", "RIGHT",
   };

   return (unsigned)button < (sizeof(names) / sizeof(names[0])) ?
      names[button] : "?";
}
void unifrog_input_snapshot(struct unifrog_input_snapshot *snapshot)
{
   if (!snapshot)
      return;
   memset(snapshot, 0, sizeof(*snapshot));
   pthread_mutex_lock(&linux_input_lock);
   snapshot->buttons = linux_buttons;
   snapshot->previous_buttons = linux_previous_buttons;
   snapshot->local_buttons = linux_buttons;
   pthread_mutex_unlock(&linux_input_lock);
}
void unifrog_input_wireless_reset(void) {}
void unifrog_input_wireless_clear(void) {}
void unifrog_input_wireless_init(void) {}
int unifrog_input_wireless_available(void) { return 0; }
int unifrog_input_wireless_initialized(void) { return 0; }
int unifrog_input_wireless_bus_ok(void) { return 0; }
unsigned unifrog_input_wireless_channel_index(void) { return 0; }
uint32_t unifrog_input_wireless_buttons(unsigned port) { (void)port; return 0; }
uint32_t unifrog_input_wireless_raw(unsigned port) { (void)port; return 0; }
uint32_t unifrog_input_wireless_all_buttons(void) { return 0; }
unsigned unifrog_input_wireless_timeout(unsigned port) { (void)port; return 0; }
uint8_t unifrog_input_wireless_status(void) { return 0; }
void unifrog_input_wireless_poll(void) {}
void unifrog_input_wireless_poll_once(void) {}
void unifrog_input_wireless_prepare_poll(void) {}
void unifrog_input_restore_local_bus(void) {}
void unifrog_input_log_local_bus_state(const char *tag) { (void)tag; }
void unifrog_input_log_wireless_sdio_state(const char *tag) { (void)tag; }
int unifrog_input_wireless_receive_window(const char *tag, uint8_t channel,
   unsigned duration_ms, unsigned poll_delay_us)
{
   (void)tag;
   (void)channel;
   (void)duration_ms;
   (void)poll_delay_us;
   return 0;
}
