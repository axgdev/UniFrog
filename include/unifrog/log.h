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

enum unifrog_log_level {
   UNIFROG_LOG_TRACE = 0,
   UNIFROG_LOG_DEBUG,
   UNIFROG_LOG_INFO,
   UNIFROG_LOG_WARN,
   UNIFROG_LOG_ERROR,
   UNIFROG_LOG_OFF,
};

int unifrog_log(const char *fmt, ...);
int unifrog_log_at(enum unifrog_log_level level, const char *component,
   const char *fmt, ...);
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
const char *unifrog_log_level_name(enum unifrog_log_level level);
enum unifrog_log_level unifrog_log_level_from_name(const char *name,
   enum unifrog_log_level fallback);
void unifrog_log_set_level(enum unifrog_log_level level);
enum unifrog_log_level unifrog_log_get_level(void);
int unifrog_log_would_write(enum unifrog_log_level level);

#define UF_LOG_TRACE(component, ...) \
   unifrog_log_at(UNIFROG_LOG_TRACE, component, __VA_ARGS__)
#define UF_LOG_DEBUG(component, ...) \
   unifrog_log_at(UNIFROG_LOG_DEBUG, component, __VA_ARGS__)
#define UF_LOG_INFO(component, ...) \
   unifrog_log_at(UNIFROG_LOG_INFO, component, __VA_ARGS__)
#define UF_LOG_WARN(component, ...) \
   unifrog_log_at(UNIFROG_LOG_WARN, component, __VA_ARGS__)
#define UF_LOG_ERROR(component, ...) \
   unifrog_log_at(UNIFROG_LOG_ERROR, component, __VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif
