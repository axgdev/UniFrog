#ifndef UNIFROG_BOOT_H
#define UNIFROG_BOOT_H

#ifdef __cplusplus
extern "C" {
#endif

#define UNIFROG_BOOT_NAME_MAX 64u
#define UNIFROG_BOOT_ROM_BASENAME_MAX 8u
#define UNIFROG_BOOT_ROM_EXTENSION_LEN 3u

/*
 * Prepare a one-shot fastboot handoff for firmware/<name> and reboot.
 * On success this function does not return.
 */
int unifrog_boot_firmware_asd(const char *name);
int unifrog_boot_firmware_name_supported(const char *name);
void unifrog_boot_reboot(void);

#ifdef __cplusplus
}
#endif

#endif
