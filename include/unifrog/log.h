#ifndef UNIFROG_LOG_H
#define UNIFROG_LOG_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define UNIFROG_LOG_ERR_OPEN -1
#define UNIFROG_LOG_ERR_WRITE -2
#define UNIFROG_LOG_ERR_DISABLED -3
#define UNIFROG_LOG_ERR_UNAVAILABLE -4

int unifrog_log(const char *fmt, ...);
int unifrog_log_sync(const char *fmt, ...);
int unifrog_log_reset(void);
int unifrog_log_flush(void);
int unifrog_log_flush_force(void);
void unifrog_log_note_storage_quiet(unsigned ms);
void unifrog_log_set_disk_suspended(int suspended);
void unifrog_log_defer_begin(void);
void unifrog_log_defer_end(void);
int unifrog_log_flush_deferred(void);
int unifrog_log_disk_writes_enabled(void);
int unifrog_log_disk_available(void);
void unifrog_log_set_disk_available(int available);
const char *unifrog_log_last_path(void);
int unifrog_log_last_result(void);
size_t unifrog_log_auto_flush_bytes(void);
void unifrog_log_set_auto_flush_bytes(size_t bytes);
size_t unifrog_log_capacity(void);
size_t unifrog_log_pending(void);

#ifdef __cplusplus
}
#endif

#endif
