#ifndef UNIFROG_CONFIG_H
#define UNIFROG_CONFIG_H

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int (*unifrog_config_entry_cb)(void *userdata, const char *section,
   const char *key, const char *value, unsigned line_number);

/*
 * Read a small sectioned key=value file without allocation.
 *
 * Leading/trailing ASCII whitespace is ignored. # and ; introduce comments
 * outside quoted values. Single- and double-quoted values are supported.
 * The callback arguments remain valid only for the duration of the call.
 *
 * Returns 0 after a readable file, -1 when the file cannot be opened, and -2
 * for invalid arguments. Malformed or overlong lines are skipped and counted
 * through error_count when it is non-NULL.
 */
int unifrog_config_read(const char *path, unifrog_config_entry_cb callback,
   void *userdata, unsigned *error_count);

typedef int (*unifrog_config_section_writer)(FILE *file, void *userdata);

/*
 * Atomically replace one section while preserving all other text verbatim.
 * The writer emits only key=value lines; this function emits [section].
 */
int unifrog_config_replace_section(const char *path, const char *section,
   unifrog_config_section_writer writer, void *userdata);
int unifrog_config_remove_section(const char *path, const char *section);

/* Replace path with an already-closed temporary file, retaining path on error. */
int unifrog_config_commit(const char *temporary, const char *path);

#ifdef __cplusplus
}
#endif

#endif
