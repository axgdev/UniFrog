#ifndef UNIFROG_LOG_H
#define UNIFROG_LOG_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define UNIFROG_LOG_ERR_OPEN -1
#define UNIFROG_LOG_ERR_WRITE -2

int unifrog_log(const char *fmt, ...);
int unifrog_log_reset(void);
int unifrog_log_flush(void);
int unifrog_log_flush_force(void);
void unifrog_log_defer_begin(void);
void unifrog_log_defer_end(void);
void unifrog_log_defer_force_end(void);
int unifrog_log_flush_deferred(void);
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
