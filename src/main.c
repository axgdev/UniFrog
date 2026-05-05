#include <generated/br2_autoconf.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include <kernel/module.h>
#include <kernel/lib/console.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <pthread.h>

#include <frontend/js2300_frontend.h>
#include <unifrog/diag.h>
#include <unifrog/log.h>
#include <unifrog/perf.h>
#include <unifrog/platform.h>
#include <unifrog/runtime.h>

#ifndef UNIFROG_GIT_COMMIT
#define UNIFROG_GIT_COMMIT "unknown"
#endif

#ifndef UNIFROG_GIT_DIRTY
#define UNIFROG_GIT_DIRTY 1
#endif

#ifndef UNIFROG_SDK_GIT_COMMIT
#define UNIFROG_SDK_GIT_COMMIT "unknown"
#endif

#ifndef UNIFROG_CORES_GIT_COMMIT
#define UNIFROG_CORES_GIT_COMMIT "unknown"
#endif

#ifndef UNIFROG_JS2300_GIT_COMMIT
#define UNIFROG_JS2300_GIT_COMMIT "unknown"
#endif

#ifndef UNIFROG_FRONTEND_GIT_COMMIT
#define UNIFROG_FRONTEND_GIT_COMMIT "unknown"
#endif
#ifndef UNIFROG_HCRTOS_MEDIA
#define UNIFROG_HCRTOS_MEDIA "unknown"
#endif

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

   (void)arg;

   thread_start_ms = unifrog_perf_time_ms();
   unifrog_diag_memory_snapshot("boot.thread_start");
   unifrog_platform_init_board();
   board_ready_ms = unifrog_perf_time_ms();
   unifrog_log("unifrog boot_time stage=board_ready total_ms=%lu board_ms=%lu\n",
      (unsigned long)(board_ready_ms - thread_start_ms),
      (unsigned long)(board_ready_ms - thread_start_ms));
   unifrog_diag_memory_snapshot("boot.board_ready");
   storage_ready = unifrog_platform_wait_for_storage();
   storage_done_ms = unifrog_perf_time_ms();
   unifrog_log("unifrog boot_time stage=storage_wait_done total_ms=%lu board_ms=%lu storage_ms=%lu ret=%d\n",
      (unsigned long)(storage_done_ms - thread_start_ms),
      (unsigned long)(board_ready_ms - thread_start_ms),
      (unsigned long)(storage_done_ms - board_ready_ms),
      storage_ready);
   unifrog_diag_memory_snapshot("boot.storage_wait_done");

   if (storage_ready == 0) {
      log_reset_ret = unifrog_log_reset();
      log_reset_path = unifrog_log_last_path();
      unifrog_log("unifrog log reset ret=%d path=%s\n",
         log_reset_ret, log_reset_path ? log_reset_path : "?");
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
   js2300_frontend_main();

   unifrog_platform_idle_forever();
   return NULL;
}

int main(void)
{
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
   pthread_attr_setstacksize(&attr, 0x10000);
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
   uint32_t module_start_ms;
   uint32_t module_done_ms;

   (void)pvParameters;

   module_start_ms = unifrog_perf_time_ms();
   module_ret = module_init2("all",
      (int)(sizeof(fast_ui_boot_excludes) / sizeof(fast_ui_boot_excludes[0])),
      fast_ui_boot_excludes);
   module_done_ms = unifrog_perf_time_ms();
   printf("unifrog fast_ui_boot module_init ret=%d ms=%lu excludes=%lu\n",
      module_ret, (unsigned long)(module_done_ms - module_start_ms),
      (unsigned long)(sizeof(fast_ui_boot_excludes) /
         sizeof(fast_ui_boot_excludes[0])));
   if (module_ret != 0)
      printf("module_init all failed ret=%d, starting frontend fallback\n", module_ret);
   start_frontend_thread();

   setenv("TZ", CONFIG_APP_TIMEZONE, 1);
   tzset();

   console_init();
   console_start();

   for (;;)
      ;
}
