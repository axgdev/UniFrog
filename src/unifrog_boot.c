#include <unifrog/boot.h>

#include <stddef.h>
#include <stdint.h>
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

static int valid_firmware_name(const char *name)
{
   size_t len;

   if (!name || name[0] == '.' || name[0] == '\0')
      return 0;
   if (!unifrog_text_ends_with_ci(name, ".asd"))
      return 0;

   len = strlen(name);
   if (len >= UNIFROG_BOOT_NAME_MAX)
      return 0;

   for (size_t i = 0; i < len; i++) {
      if (name[i] == '/' || name[i] == '\\' || name[i] == ':' ||
         name[i] == '\r' || name[i] == '\n' ||
         name[i] == '\t' || name[i] == ' ')
         return 0;
   }

   return 1;
}

int unifrog_boot_firmware_name_supported(const char *name)
{
   return valid_firmware_name(name);
}

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

static void boot_via_stage1_path(const char *path)
{
   volatile struct fastboot_handoff *handoff = FASTBOOT_HANDOFF_ADDR;
   volatile struct fastboot_diag *diag = FASTBOOT_DIAG_ADDR;

   handoff->magic = 0;
   memset((void *)handoff->path, 0, sizeof(handoff->path));
   strcpy((char *)handoff->path, path);
   handoff->magic = FASTBOOT_HANDOFF_MAGIC;
   cache_flush((void *)handoff, sizeof(*handoff));

   diag->magic = FASTBOOT_DIAG_MAGIC;
   diag->stage_addr = FASTBOOT_STAGE1_ADDR;
   diag->event = 100;
   diag->result = 0;
   memset((void *)diag->path, 0, sizeof(diag->path));
   strcpy((char *)diag->path, path);
   cache_flush((void *)diag, sizeof(*diag));

   printf("unifrog boot handoff path=%s direct_stage1=0x%08lx\n",
      path, (unsigned long)FASTBOOT_STAGE1_ADDR);
   (void)unifrog_log_flush();

   diag->event = 101;
   diag->result = 0;
   cache_flush((void *)diag, sizeof(*diag));

   reset_into_fastboot();
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

int unifrog_boot_firmware_asd(const char *name)
{
   static const char prefix[] = "firmware/";
   char path[FASTBOOT_HANDOFF_PATH_BYTES];

   printf("unifrog boot request name=%s supported=%d\n",
      name ? name : "", unifrog_boot_firmware_name_supported(name));
   (void)unifrog_log_flush();

   if (!unifrog_boot_firmware_name_supported(name))
      return -1;

   memset(path, 0, sizeof(path));
   strcpy(path, prefix);
   strcat(path, name);
   boot_via_stage1_path(path);
   return 0;
}
