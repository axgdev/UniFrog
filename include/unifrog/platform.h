#ifndef UNIFROG_PLATFORM_H
#define UNIFROG_PLATFORM_H

#ifdef __cplusplus
extern "C" {
#endif

void unifrog_platform_init_board(void);
int unifrog_platform_storage_ready(void);
int unifrog_platform_mount_storage(void);
int unifrog_platform_recover_storage(const char *tag, unsigned attempts,
   unsigned delay_ms);
int unifrog_platform_wait_for_storage(void);
void unifrog_platform_idle_forever(void);

#ifdef __cplusplus
}
#endif

#endif
