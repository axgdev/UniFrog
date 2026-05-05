#ifndef UNIFROG_PLATFORM_H
#define UNIFROG_PLATFORM_H

#include <stddef.h>

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
void unifrog_platform_set_storage_log_suspended(int suspended);
int unifrog_platform_wait_for_storage(void);
void unifrog_platform_idle_forever(void);

#ifdef __cplusplus
}
#endif

#endif
