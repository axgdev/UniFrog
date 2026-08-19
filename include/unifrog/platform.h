#ifndef UNIFROG_PLATFORM_H
#define UNIFROG_PLATFORM_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void unifrog_platform_init_board(void);
int unifrog_platform_storage_ready(void);
int unifrog_platform_mount_storage(void);
int unifrog_platform_recover_storage(const char *tag, unsigned attempts,
   unsigned delay_ms);
int unifrog_platform_recover_storage_after_io_error(const char *tag,
   unsigned attempts, unsigned delay_ms);
int unifrog_platform_sd_runtime_supported(void);
int unifrog_platform_sd_apply_profile(const char *profile,
   unsigned mount_attempts, unsigned mount_delay_ms, char *detail,
   size_t detail_size);
int unifrog_platform_sd_restore_boot(unsigned mount_attempts,
   unsigned mount_delay_ms, char *detail, size_t detail_size);
int unifrog_platform_sd_profile_allowed(const char *profile);
const char *unifrog_platform_sd_active_profile(void);
int unifrog_platform_sd_describe(char *detail, size_t detail_size);
typedef void (*unifrog_platform_storage_stage_cb)(void *userdata,
   const char *operation, const char *stage);
struct unifrog_platform_debug_status {
   uint32_t pending;
   uint32_t suspended;
   uint32_t quiet_ticks;
   uint32_t dirty_bytes;
};
void unifrog_platform_set_storage_stage_callback(
   unifrog_platform_storage_stage_cb cb, void *userdata);
void unifrog_platform_storage_diag_note(const char *operation,
   const char *stage);
void unifrog_platform_sd_debug_dump(const char *tag);
void unifrog_platform_sd_mmc_diag_begin(const char *tag);
void unifrog_platform_sd_mmc_diag_checkpoint(const char *tag);
void unifrog_platform_sd_mmc_diag_checkpoint_summary(const char *tag);
void unifrog_platform_sd_mmc_diag_end(const char *tag);
void unifrog_platform_set_storage_log_suspended(int suspended);
void unifrog_platform_note_storage_unstable(unsigned ticks);
void unifrog_platform_debug_status(struct unifrog_platform_debug_status *status);
int unifrog_platform_debug_write(const void *data, size_t size,
   unsigned repeat);
int unifrog_platform_wait_for_storage(void);
void unifrog_platform_idle_forever(void);

#ifdef __cplusplus
}
#endif

#endif
