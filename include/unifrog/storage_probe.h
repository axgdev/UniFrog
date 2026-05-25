#ifndef UNIFROG_STORAGE_PROBE_H
#define UNIFROG_STORAGE_PROBE_H

#include <stddef.h>

#include <unifrog/paths.h>

#ifdef __cplusplus
extern "C" {
#endif

#define UNIFROG_STORAGE_FAST_PROBE_REPORT \
   UNIFROG_REPORT_ROOT "/storage-fast-probe-result.txt"
#define UNIFROG_STORAGE_WRITE_PROBE_REPORT \
   UNIFROG_REPORT_ROOT "/storage-write-probe-result.txt"

typedef void (*unifrog_storage_probe_progress_cb)(void *userdata,
   const char *line1, const char *line2);

int unifrog_storage_fast_probe_run(unifrog_storage_probe_progress_cb progress,
   void *userdata, char *summary, size_t summary_size);
int unifrog_storage_write_probe_run(unifrog_storage_probe_progress_cb progress,
   void *userdata, char *summary, size_t summary_size);

#ifdef __cplusplus
}
#endif

#endif
