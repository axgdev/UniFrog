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
#include <unifrog/boot_logo.h>
#include <unifrog/boot_trace.h>
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
static int init_early_display_modules(void);
static int frontend_started;

static const char *early_display_modules[] = {
   "pwm",
   "fb",
   "st7789v2",
   "backlight",
};

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
      log_reset_ret = unifrog_log_reset();
      log_reset_path = unifrog_log_last_path();
      unifrog_boot_trace_mark(FASTBOOT_TRACE_UNIFROG_LOG_RESET_DONE,
         unifrog_perf_time_ms(), (uint32_t)log_reset_ret, 0);
      unifrog_boot_trace_log("boot.after_log_reset");
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
   unifrog_boot_trace_mark(FASTBOOT_TRACE_UNIFROG_JS_BEGIN,
      unifrog_perf_time_ms(), 0, 0);
   unifrog_boot_trace_log("boot.before_js");
   js2300_frontend_main();

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
   pthread_attr_setstacksize(&attr, 0x10000);
   if (pthread_create(&pid, &attr, frontend_thread, NULL) != 0)
      return -1;
   frontend_started = 1;
   return 0;
}

static int init_early_display_modules(void)
{
   uint32_t start_ms;
   int first_ret = 0;

   start_ms = unifrog_perf_time_ms();
   for (unsigned i = 0; i < sizeof(early_display_modules) /
      sizeof(early_display_modules[0]); i++) {
      const char *name = early_display_modules[i];
      uint32_t module_start_ms = unifrog_perf_time_ms();
      uint32_t module_done_ms;
      int ret;

      ret = module_init(name);
      module_done_ms = unifrog_perf_time_ms();
      unifrog_log("unifrog boot_perf phase=early_display_module "
         "name=%s ret=%d ms=%lu total_ms=%lu\n",
         name, ret,
         (unsigned long)(module_done_ms - module_start_ms),
         (unsigned long)(module_done_ms - start_ms));
      if (ret != 0 && first_ret == 0)
         first_ret = ret;
   }
   unifrog_log("unifrog boot_perf phase=early_display_init ret=%d "
      "ms=%lu modules=%lu\n",
      first_ret, (unsigned long)(unifrog_perf_time_ms() - start_ms),
      (unsigned long)(sizeof(early_display_modules) /
         sizeof(early_display_modules[0])));
   return first_ret;
}

static int sf2000_start(void)
{
   return start_frontend_thread();
}

__initcall(sf2000_start)


static void app_main(void *pvParameters)
{
   int early_display_ret;
   int logo_ret = -1;
   int module_ret;
   uint32_t module_start_ms;
   uint32_t module_done_ms;

   (void)pvParameters;

   unifrog_boot_trace_mark(FASTBOOT_TRACE_UNIFROG_APP_MAIN_START,
      unifrog_perf_time_ms(), 0, 0);
   module_start_ms = unifrog_perf_time_ms();
   unifrog_boot_trace_mark(FASTBOOT_TRACE_UNIFROG_MODULE_INIT_BEGIN,
      module_start_ms,
      (uint32_t)(sizeof(fast_ui_boot_excludes) / sizeof(fast_ui_boot_excludes[0])),
      0);
   early_display_ret = init_early_display_modules();
   if (early_display_ret == 0)
      logo_ret = unifrog_boot_logo_present_early();
   else
      unifrog_log("unifrog boot_logo early skipped display_ret=%d\n",
         early_display_ret);
   module_ret = module_init2("all",
      (int)(sizeof(fast_ui_boot_excludes) / sizeof(fast_ui_boot_excludes[0])),
      fast_ui_boot_excludes);
   module_done_ms = unifrog_perf_time_ms();
   unifrog_boot_trace_mark(FASTBOOT_TRACE_UNIFROG_MODULE_INIT_DONE,
      module_done_ms, module_done_ms - module_start_ms, (uint32_t)module_ret);
   printf("unifrog fast_ui_boot module_init ret=%d ms=%lu excludes=%lu\n",
      module_ret, (unsigned long)(module_done_ms - module_start_ms),
      (unsigned long)(sizeof(fast_ui_boot_excludes) /
         sizeof(fast_ui_boot_excludes[0])));
   if (module_ret != 0)
      printf("module_init all failed ret=%d, starting frontend fallback\n", module_ret);
   else if (!unifrog_boot_logo_is_active())
      logo_ret = unifrog_boot_logo_present_early();
   if (logo_ret != 0)
      unifrog_log("unifrog boot_logo early final_ret=%d display_ret=%d "
         "module_ret=%d active=%d\n",
         logo_ret, early_display_ret, module_ret,
         unifrog_boot_logo_is_active());
   start_frontend_thread();

   setenv("TZ", CONFIG_APP_TIMEZONE, 1);
   tzset();

   console_init();
   console_start();

   for (;;)
      ;
}
