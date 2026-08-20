#ifndef UNIFROG_BOOT_H
#define UNIFROG_BOOT_H

#ifdef __cplusplus
extern "C" {
#endif

#define UNIFROG_BOOT_PATH_MAX 96u
#define UNIFROG_BOOT_ROM_BASENAME_MAX 8u
#define UNIFROG_BOOT_ROM_EXTENSION_LEN 3u

/*
 * Prepare a one-shot fastboot handoff for an SD-relative .asd path and reboot.
 * The path must match the stage1 handoff rules: no leading slash, no dot
 * component, no spaces, and no backslash or colon.
 */
int unifrog_boot_asd_path(const char *path);
int unifrog_boot_asd_path_supported(const char *path);
void unifrog_boot_reboot(void);

#ifdef __cplusplus
}
#endif

#endif
