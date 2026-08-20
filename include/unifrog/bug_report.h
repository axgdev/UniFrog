#ifndef UNIFROG_BUG_REPORT_H
#define UNIFROG_BUG_REPORT_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

int unifrog_bug_report_create(char *output_path, size_t output_path_size,
   char *summary, size_t summary_size);

#ifdef __cplusplus
}
#endif

#endif
