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
int unifrog_platform_sd_runtime_supported(void);
int unifrog_platform_sd_apply_profile(const char *profile,
   unsigned mount_attempts, unsigned mount_delay_ms, char *detail,
   size_t detail_size);
int unifrog_platform_sd_restore_boot(unsigned mount_attempts,
   unsigned mount_delay_ms, char *detail, size_t detail_size);
typedef void (*unifrog_platform_storage_stage_cb)(void *userdata,
   const char *operation, const char *stage);
void unifrog_platform_set_storage_stage_callback(
   unifrog_platform_storage_stage_cb cb, void *userdata);
void unifrog_platform_set_storage_log_suspended(int suspended);
int unifrog_platform_wait_for_storage(void);
void unifrog_platform_idle_forever(void);

#ifdef __cplusplus
}
#endif

#endif
