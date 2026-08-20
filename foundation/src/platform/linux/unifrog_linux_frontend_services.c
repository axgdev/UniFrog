#include <unifrog/abi.h>
#include <unifrog/boot.h>
#include <unifrog/device.h>
#include <unifrog/media.h>
#include <unifrog/storage_probe.h>

#include <js2300/unifrog_host.h>

#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

static char linux_board_override[16] = "auto";

int unifrog_media_play_video_ex(const char *path,
   const struct unifrog_media_video_options *options)
{
   (void)path;
   (void)options;
   return -ENOSYS;
}

int unifrog_media_play_video(const char *path)
{
   return unifrog_media_play_video_ex(path, NULL);
}

int unifrog_media_run_audio_diagnostics_ex(char *summary, size_t summary_size,
   unifrog_media_progress_cb progress, void *userdata)
{
   if (progress)
      progress(userdata, "Linux audio diagnostics unavailable", 100, 100);
   if (summary && summary_size)
      snprintf(summary, summary_size, "Linux audio diagnostics unavailable");
   return -ENOSYS;
}

int unifrog_media_run_audio_diagnostics(char *summary, size_t summary_size)
{
   return unifrog_media_run_audio_diagnostics_ex(summary, summary_size, NULL,
      NULL);
}

int unifrog_boot_asd_path(const char *path)
{
   return unifrog_boot_asd_path_supported(path) ? -ENOSYS : -EINVAL;
}

void unifrog_boot_reboot(void)
{
}

int unifrog_device_set_board_override(const char *name)
{
   if (!name || !name[0] || strlen(name) >= sizeof(linux_board_override))
      return -EINVAL;
   if (strcmp(name, "auto") != 0 && strcmp(name, "sf2000") != 0 &&
       strcmp(name, "gb300") != 0)
      return -EINVAL;
   snprintf(linux_board_override, sizeof(linux_board_override), "%s", name);
   return 0;
}

const char *unifrog_device_board_override_name(void)
{
   return linux_board_override;
}

unsigned long unifrog_device_lcd_panel_id(void)
{
   return 0;
}

enum unifrog_device_panel unifrog_device_panel(void)
{
   return UNIFROG_DEVICE_PANEL_UNKNOWN;
}

const char *unifrog_device_panel_name(enum unifrog_device_panel panel)
{
   return panel == UNIFROG_DEVICE_PANEL_SF2000 ? "sf2000" :
      panel == UNIFROG_DEVICE_PANEL_GB300 ? "gb300" : "unknown";
}

void unifrog_device_note_input_profile(int uses_gb300_stock_bits,
   const char *reason)
{
   (void)uses_gb300_stock_bits;
   (void)reason;
}

enum unifrog_device_board unifrog_device_board(void)
{
   return strcmp(linux_board_override, "sf2000") == 0 ?
      UNIFROG_DEVICE_BOARD_SF2000 :
      strcmp(linux_board_override, "gb300") == 0 ?
      UNIFROG_DEVICE_BOARD_GB300 : UNIFROG_DEVICE_BOARD_AUTO;
}

const char *unifrog_device_board_name(enum unifrog_device_board board)
{
   return board == UNIFROG_DEVICE_BOARD_SF2000 ? "sf2000" :
      board == UNIFROG_DEVICE_BOARD_GB300 ? "gb300" : "auto";
}

const char *unifrog_device_variant_name(void)
{
   return "linux";
}

int unifrog_device_uses_gb300_quirks(void)
{
   return unifrog_device_board() == UNIFROG_DEVICE_BOARD_GB300;
}

int unifrog_storage_fast_probe_run(unifrog_storage_probe_progress_cb progress,
   void *userdata, char *summary, size_t summary_size)
{
   if (progress)
      progress(userdata, "Linux host storage", "filesystem available");
   if (summary && summary_size)
      snprintf(summary, summary_size, "Linux host filesystem available");
   return 0;
}

int js2300_run_script_file(const char *path)
{
   (void)path;
   return -ENOSYS;
}

const struct unifrog_abi *unifrog_abi_get(void)
{
   return NULL;
}

int unifrog_abi_table_compatible(const struct unifrog_abi *abi,
   uint32_t required_version, size_t required_size)
{
   (void)abi;
   (void)required_version;
   (void)required_size;
   return 0;
}
