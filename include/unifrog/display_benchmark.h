#ifndef UNIFROG_DISPLAY_BENCHMARK_H
#define UNIFROG_DISPLAY_BENCHMARK_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#include <unifrog/paths.h>

#define UNIFROG_DISPLAY_BENCHMARK_REPORT \
   UNIFROG_REPORT_ROOT "/display-benchmark.txt"

int unifrog_display_benchmark_run(char *summary, size_t summary_size);
int unifrog_display_color_test_run(char *summary, size_t summary_size);

#ifdef __cplusplus
}
#endif

#endif
