#ifndef UNIFROG_TASK_H
#define UNIFROG_TASK_H

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*unifrog_task_entry)(void *arg);
typedef void *unifrog_task_handle;

enum unifrog_task_priority {
   UNIFROG_TASK_PRIORITY_NORMAL = 0,
   UNIFROG_TASK_PRIORITY_HIGH = 1,
};

int unifrog_task_create(unifrog_task_entry entry, void *arg,
   const char *name, enum unifrog_task_priority priority,
   unifrog_task_handle *handle);
void unifrog_task_delay_ms(unsigned ms);

#ifdef __cplusplus
}
#endif

#endif
