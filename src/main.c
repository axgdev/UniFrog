#include <generated/br2_autoconf.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include <kernel/module.h>
#include <kernel/lib/console.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <pthread.h>

#include <unifrog/build_info.h>
#include <frontend/js2300_frontend.h>
#include <unifrog/boot_logo.h>
#include <unifrog/boot_trace.h>
#include <unifrog/diag.h>
#include <unifrog/exception_record.h>
#include <unifrog/log.h>
#include <unifrog/native_frontend.h>
#include <unifrog/perf.h>
#include <unifrog/platform.h>
#include <unifrog/runtime.h>

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

static void app_main(void *pvParameters);
static void *frontend_thread(void *arg);
static int start_frontend_thread(void);
static int frontend_started;

static char *fast_ui_boot_excludes[] = {
   "apll_dai",
   "audsink",
   "auddec",
   "avsync",
   "cjc8988_dai",
   "cjc8990_dai",
   "cs4344_dai",
   "hc16xx_link",
   "i2s_dai",
   "i2si_platform",
   "i2so_platform",
   "llav_vdec",
   "pwm_dac_dai",
   "spin_platform",
   "spo_dai",
   "spo_platform",
   "viddec",
   "vidsink",
   "wm8960_for_i2si_dai",
};

static void *frontend_thread(void *arg)
{
   int storage_ready;
   int log_reset_ret = 0;
   const char *log_reset_path = NULL;
   uint32_t thread_start_ms;
   uint32_t board_ready_ms;
   uint32_t storage_done_ms;
   uint32_t crash_cause = 0;
   uint32_t crash_epc = 0;
   uint32_t crash_badvaddr = 0;
   uint32_t crash_ra = 0;
   uint32_t crash_count = 0;
   int retained_exception;
   int crash_recovery_boot = 0;
   int crash_safe_ret = 0;
   char crash_safe_detail[96];

   (void)arg;

   thread_start_ms = unifrog_perf_time_ms();
   retained_exception = unifrog_exception_record_peek(&crash_cause,
      &crash_epc, &crash_badvaddr, &crash_ra, &crash_count);
   if (retained_exception != 0) {
      crash_recovery_boot = 1;
      unifrog_log_set_auto_flush_bytes(0);
      unifrog_log_set_disk_suspended(1);
      unifrog_log("unifrog crash_recovery retained=%d count=%lu cause=0x%08lx epc=0x%08lx badv=0x%08lx ra=0x%08lx disk_suspended=1\n",
         retained_exception, (unsigned long)crash_count,
         (unsigned long)crash_cause, (unsigned long)crash_epc,
         (unsigned long)crash_badvaddr, (unsigned long)crash_ra);
   }
   unifrog_boot_trace_mark(FASTBOOT_TRACE_UNIFROG_FRONTEND_THREAD_START,
      thread_start_ms, 0, 0);
   unifrog_boot_trace_log("boot.thread_start");
   unifrog_diag_memory_snapshot("boot.thread_start");
   unifrog_boot_trace_mark(FASTBOOT_TRACE_UNIFROG_BOARD_INIT_BEGIN,
      unifrog_perf_time_ms(), 0, 0);
   unifrog_platform_init_board();
   board_ready_ms = unifrog_perf_time_ms();
   unifrog_boot_trace_mark(FASTBOOT_TRACE_UNIFROG_BOARD_INIT_DONE,
      board_ready_ms, board_ready_ms - thread_start_ms, 0);
   unifrog_boot_trace_log("boot.board_ready");
   unifrog_log("unifrog boot_time stage=board_ready total_ms=%lu board_ms=%lu\n",
      (unsigned long)(board_ready_ms - thread_start_ms),
      (unsigned long)(board_ready_ms - thread_start_ms));
   unifrog_diag_memory_snapshot("boot.board_ready");
   if (crash_recovery_boot) {
      crash_safe_detail[0] = '\0';
      crash_safe_ret = unifrog_platform_sd_apply_profile("safe", 4, 150,
         crash_safe_detail, sizeof(crash_safe_detail));
      unifrog_log("unifrog crash_recovery safe_profile ret=%d detail=%s\n",
         crash_safe_ret, crash_safe_detail);
   }
   storage_ready = unifrog_platform_wait_for_storage();
   storage_done_ms = unifrog_perf_time_ms();
   unifrog_log("unifrog boot_time stage=storage_wait_done total_ms=%lu board_ms=%lu storage_ms=%lu ret=%d\n",
      (unsigned long)(storage_done_ms - thread_start_ms),
      (unsigned long)(board_ready_ms - thread_start_ms),
      (unsigned long)(storage_done_ms - board_ready_ms),
      storage_ready);
   unifrog_boot_trace_mark(FASTBOOT_TRACE_UNIFROG_STORAGE_DONE,
      storage_done_ms, storage_done_ms - board_ready_ms, (uint32_t)storage_ready);
   unifrog_boot_trace_log("boot.storage_wait_done");
   unifrog_diag_memory_snapshot("boot.storage_wait_done");

   if (storage_ready == 0) {
      if (crash_recovery_boot) {
         unifrog_log("unifrog crash_recovery storage_ready ret=%d safe_ret=%d detail=%s disk_suspended=0\n",
            storage_ready, crash_safe_ret, crash_safe_detail);
         unifrog_log_set_disk_suspended(0);
      }
      log_reset_ret = unifrog_log_reset();
      log_reset_path = unifrog_log_last_path();
      unifrog_boot_trace_mark(FASTBOOT_TRACE_UNIFROG_LOG_RESET_DONE,
         unifrog_perf_time_ms(), (uint32_t)log_reset_ret, 0);
      unifrog_boot_trace_log("boot.after_log_reset");
      unifrog_log("unifrog log reset ret=%d path=%s\n",
         log_reset_ret, log_reset_path ? log_reset_path : "?");
      unifrog_exception_record_log_and_clear("boot.after_log_reset");
      unifrog_log("unifrog boot_time stage=storage_ready total_ms=%lu board_ms=%lu storage_ms=%lu\n",
         (unsigned long)(storage_done_ms - thread_start_ms),
         (unsigned long)(board_ready_ms - thread_start_ms),
         (unsigned long)(storage_done_ms - board_ready_ms));
      unifrog_log("unifrog build commit=%s dirty=%d sdk=%s cores=%s js2300=%s frontend=%s media=%s\n",
         UNIFROG_GIT_COMMIT, UNIFROG_GIT_DIRTY,
         UNIFROG_SDK_GIT_COMMIT, UNIFROG_CORES_GIT_COMMIT,
         UNIFROG_JS2300_GIT_COMMIT, UNIFROG_FRONTEND_GIT_COMMIT,
         UNIFROG_HCRTOS_MEDIA);
      unifrog_diag_memory_snapshot("boot.after_log_reset");
      (void)unifrog_log_flush();
   } else if (crash_recovery_boot) {
      unifrog_log("unifrog crash_recovery storage_unavailable ret=%d safe_ret=%d detail=%s disk_still_suspended=1\n",
         storage_ready, crash_safe_ret, crash_safe_detail);
   }
   printf("Init native %s api=%u storage=%s log_reset=%d log_path=%s commit=%s dirty=%d sdk=%s cores=%s js2300=%s frontend=%s media=%s\n",
      unifrog_runtime_name(), unifrog_runtime_api_version(),
      storage_ready == 0 ? "ready" :
      storage_ready == -2 ? "deferred" : "timeout",
      log_reset_ret, log_reset_path ? log_reset_path : "?",
      UNIFROG_GIT_COMMIT, UNIFROG_GIT_DIRTY,
      UNIFROG_SDK_GIT_COMMIT, UNIFROG_CORES_GIT_COMMIT,
      UNIFROG_JS2300_GIT_COMMIT, UNIFROG_FRONTEND_GIT_COMMIT,
      UNIFROG_HCRTOS_MEDIA);
   unifrog_boot_trace_mark(FASTBOOT_TRACE_UNIFROG_FRONTEND_BEGIN,
      unifrog_perf_time_ms(), 0, 0);
   unifrog_boot_trace_log("boot.before_js");
#if UNIFROG_FRONTEND_NATIVE || UNIFROG_FRONTEND_MUOS
   unifrog_native_frontend_main();
#else
   js2300_frontend_main();
#endif

   unifrog_platform_idle_forever();
   return NULL;
}

int main(void)
{
   unifrog_boot_trace_mark(FASTBOOT_TRACE_UNIFROG_MAIN_START, 0, 0, 0);
   xTaskCreate(app_main, (const char *)"app_main", configTASK_STACK_DEPTH,
      NULL, portPRI_TASK_NORMAL, NULL);

   vTaskStartScheduler();

   abort();
   return 0;
}

static int start_frontend_thread(void)
{
   pthread_t pid;
   pthread_attr_t attr;

   if (frontend_started)
      return 0;
   pthread_attr_init(&attr);
   pthread_attr_setstacksize(&attr, 0x20000);
   printf("unifrog frontend stack bytes=%u\n", 0x20000u);
   if (pthread_create(&pid, &attr, frontend_thread, NULL) != 0)
      return -1;
   frontend_started = 1;
   return 0;
}

static int sf2000_start(void)
{
   return start_frontend_thread();
}

__initcall(sf2000_start)


static void app_main(void *pvParameters)
{
   int module_ret;
   int exclude_count = (int)ARRAY_SIZE(fast_ui_boot_excludes);
   uint32_t module_start_ms;
   uint32_t module_done_ms;

   (void)pvParameters;

   unifrog_boot_trace_mark(FASTBOOT_TRACE_UNIFROG_APP_MAIN_START,
      unifrog_perf_time_ms(), 0, 0);
   module_start_ms = unifrog_perf_time_ms();
   unifrog_boot_trace_mark(FASTBOOT_TRACE_UNIFROG_MODULE_INIT_BEGIN,
      module_start_ms, (uint32_t)exclude_count, 0);
   module_ret = module_init2("all", exclude_count, fast_ui_boot_excludes);
   module_done_ms = unifrog_perf_time_ms();
   unifrog_boot_trace_mark(FASTBOOT_TRACE_UNIFROG_MODULE_INIT_DONE,
      module_done_ms, module_done_ms - module_start_ms, (uint32_t)module_ret);
   printf("unifrog fast_ui_boot module_init ret=%d ms=%lu excludes=%lu\n",
      module_ret, (unsigned long)(module_done_ms - module_start_ms),
      (unsigned long)exclude_count);
   if (module_ret != 0)
      printf("module_init all failed ret=%d, starting frontend fallback\n", module_ret);
   else
      (void)unifrog_boot_logo_present_early();
   start_frontend_thread();

   setenv("TZ", CONFIG_APP_TIMEZONE, 1);
   tzset();

   console_init();
   console_start();

   for (;;)
      ;
}
