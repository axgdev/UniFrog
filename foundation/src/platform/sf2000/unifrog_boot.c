#include <unifrog/boot.h>

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <cpu_func.h>
#include <fcntl.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <hcuapi/gpio.h>
#include <hcuapi/pinpad.h>
#include <kernel/irqflags.h>
#include <sys/unistd.h>

#include <fastboot/handoff.h>
#include <unifrog/audio.h>
#include <unifrog/log.h>
#include <unifrog/text.h>

#define printf unifrog_log

extern const unsigned char unifrog_fastboot_stage_start[];
extern const unsigned char unifrog_fastboot_stage_end[];

static void set_backlight_off(void)
{
   unsigned char level = 0;
   int fd;

   fd = open("/dev/backlight", O_RDWR);
   if (fd >= 0) {
      (void)write(fd, &level, 1);
      close(fd);
   }

   gpio_configure(PINPAD_R05, GPIO_DIR_OUTPUT);
   gpio_set_output(PINPAD_R05, true);
}

static void reset_into_fastboot(void)
{
   unifrog_audio_set_system_output_enabled(0);
   set_backlight_off();
   vTaskSuspendAll();
   taskDISABLE_INTERRUPTS();
   (void)arch_local_irq_disable();
   reset();
   for (;;)
      ;
}

static int boot_via_stage1_path(const char *path)
{
   volatile struct fastboot_handoff *handoff = FASTBOOT_HANDOFF_ADDR;
   volatile struct fastboot_diag *diag = FASTBOOT_DIAG_ADDR;
   size_t stage_size = (size_t)(unifrog_fastboot_stage_end -
      unifrog_fastboot_stage_start);
   void (*stage1)(void) = (void (*)(void))(uintptr_t)FASTBOOT_STAGE1_ADDR;

   if (stage_size == 0 || stage_size >
       FASTBOOT_STACK_ADDR - FASTBOOT_STAGE1_ADDR) {
      printf("unifrog boot stage invalid size=%lu limit=%lu\n",
         (unsigned long)stage_size,
         (unsigned long)(FASTBOOT_STACK_ADDR - FASTBOOT_STAGE1_ADDR));
      return -1;
   }

   /*
    * A hardware reset reinitializes SDRAM and loses the RAM handoff. Refresh
    * the reserved stage1 image and enter it directly while the request is
    * still valid. Stage1 installs its own stack and disables interrupts.
    */
   memcpy((void *)(uintptr_t)FASTBOOT_STAGE1_ADDR,
      unifrog_fastboot_stage_start, stage_size);
   cache_flush((void *)(uintptr_t)FASTBOOT_STAGE1_ADDR, stage_size);

   handoff->magic = 0;
   memset((void *)handoff->path, 0, sizeof(handoff->path));
   unifrog_text_copy((char *)handoff->path, sizeof(handoff->path), path);
   handoff->magic = FASTBOOT_HANDOFF_MAGIC;
   cache_flush((void *)handoff, sizeof(*handoff));

   diag->magic = FASTBOOT_DIAG_MAGIC;
   diag->stage_addr = FASTBOOT_STAGE1_ADDR;
   diag->event = FASTBOOT_DIAG_HANDOFF_REQUEST;
   diag->result = 0;
   memset((void *)diag->path, 0, sizeof(diag->path));
   unifrog_text_copy((char *)diag->path, sizeof(diag->path), path);
   cache_flush((void *)diag, sizeof(*diag));

   printf("unifrog boot handoff path=%s stage1=0x%08lx bytes=%lu mode=direct\n",
      path, (unsigned long)FASTBOOT_STAGE1_ADDR, (unsigned long)stage_size);
   (void)unifrog_log_flush();

   diag->event = FASTBOOT_DIAG_DIRECT_REQUEST;
   diag->result = 0;
   cache_flush((void *)diag, sizeof(*diag));

   unifrog_audio_set_system_output_enabled(0);
   set_backlight_off();
   vTaskSuspendAll();
   taskDISABLE_INTERRUPTS();
   (void)arch_local_irq_disable();
   stage1();
   for (;;)
      ;
}

void unifrog_boot_reboot(void)
{
   volatile struct fastboot_handoff *handoff = FASTBOOT_HANDOFF_ADDR;
   volatile struct fastboot_diag *diag = FASTBOOT_DIAG_ADDR;

   handoff->magic = 0;
   diag->magic = 0;
   cache_flush((void *)handoff, sizeof(*handoff));
   cache_flush((void *)diag, sizeof(*diag));
   printf("unifrog reboot request\n");
   (void)unifrog_log_flush();
   reset_into_fastboot();
}

int unifrog_boot_asd_path(const char *path)
{
   printf("unifrog boot request path=%s supported=%d\n",
      path ? path : "", unifrog_boot_asd_path_supported(path));
   (void)unifrog_log_flush();

   if (!unifrog_boot_asd_path_supported(path))
      return -1;

   return boot_via_stage1_path(path);
}
