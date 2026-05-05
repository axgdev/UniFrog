#include <unifrog/platform.h>

#include <stdbool.h>
#include <stdint.h>
#include <errno.h>
#include <dirent.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/unistd.h>

#include <hcuapi/gpio.h>
#include <hcuapi/pinpad.h>
#include <kernel/delay.h>

#include <fastboot/handoff.h>
#include <unifrog/log.h>
#include <unifrog/perf.h>
#include <unifrog/runtime.h>

#define CLOCK_GATE0_REG 0xb8800060u
#define CLOCK_GATE1_REG 0xb8800064u
#define RESET_GATE0_REG 0xb8800080u
#define RESET_GATE1_REG 0xb8800084u

extern unsigned long PINMUXL;
extern unsigned long PINMUXR;

#define STORAGE_STABLE_SAMPLES 2
#define STORAGE_MAX_POLLS 100
#define STORAGE_POLL_MS 100

#ifndef UNIFROG_SD_MODE
#define UNIFROG_SD_MODE "unknown"
#endif
#ifndef UNIFROG_SD_EXPERIMENTAL
#define UNIFROG_SD_EXPERIMENTAL 0
#endif

struct storage_mount {
   const char *dev;
   const char *target;
};

static const struct storage_mount storage_mounts[] = {
   {"/dev/mmcblk0", "/media/mmcblk0"},
   {"/dev/mmcblk0p1", "/media/mmcblk0p1"},
   {"/dev/mmcblk0p2", "/media/mmcblk0p2"},
};
static unsigned storage_mounted_mask;
static unsigned storage_mount_attempts;

int mount(const char *source, const char *target,
   const char *filesystemtype, unsigned long mountflags, const void *data);
extern int fdt_get_node_offset_by_path(const char *path);
extern int fdt_get_property_u_32_index(int offset, const char *name, int index,
   uint32_t *outval);
extern int fdt_get_property_string_index(int offset, const char *name, int index,
   const char **outval);
extern bool fdt_property_read_bool(int offset, const char *propname);

static void write_reg32(uint32_t addr, uint32_t value)
{
   *(volatile uint32_t *)addr = value;
}

static void apply_clock_defaults(void)
{
   write_reg32(CLOCK_GATE0_REG, 0x00000f88u);
   write_reg32(CLOCK_GATE1_REG, 0x0bc04040u);
   write_reg32(RESET_GATE0_REG, 0x000023c0u);
   write_reg32(RESET_GATE1_REG, 0xa00b4000u);
}

static void init_board_gpios(void)
{
   gpio_configure(PINPAD_R05, GPIO_DIR_OUTPUT);
   gpio_set_output(PINPAD_R05, false);

   gpio_configure(PINPAD_L00, GPIO_DIR_OUTPUT);
   gpio_set_output(PINPAD_L00, false);

   gpio_configure(PINPAD_T07, GPIO_DIR_OUTPUT);
   gpio_set_output(PINPAD_T07, true);

   unifrog_log("unifrog platform gpio_init l=0x%08lx/0x%08lx r=0x%08lx/0x%08lx "
          "mux_l22=%u mux_l23=%u mux_l24=%u mux_l25=%u mux_l26=%u mux_l27=%u mux_l28=%u mux_l29=%u mux_r07=%u\n",
      (unsigned long)*(volatile uint32_t *)0xb8800058u,
      (unsigned long)*(volatile uint32_t *)0xb8800054u,
      (unsigned long)*(volatile uint32_t *)0xb88000f8u,
      (unsigned long)*(volatile uint32_t *)0xb88000f4u,
      ((volatile unsigned char *)&PINMUXL)[PINPAD_L22],
      ((volatile unsigned char *)&PINMUXL)[PINPAD_L23],
      ((volatile unsigned char *)&PINMUXL)[PINPAD_L24],
      ((volatile unsigned char *)&PINMUXL)[PINPAD_L25],
      ((volatile unsigned char *)&PINMUXL)[PINPAD_L26],
      ((volatile unsigned char *)&PINMUXL)[PINPAD_L27],
      ((volatile unsigned char *)&PINMUXL)[PINPAD_L28],
      ((volatile unsigned char *)&PINMUXL)[PINPAD_L29],
      ((volatile unsigned char *)&PINMUXR)[PINPAD_R07 - 64]);
}

static void log_storage_config(void)
{
   uint32_t bus_width = 0;
   uint32_t clock = 0;
   const char *status = "missing";
   int node = fdt_get_node_offset_by_path("/hcrtos/mmc");

   if (node < 0)
      node = fdt_get_node_offset_by_path("/mmc");
   if (node < 0) {
      unifrog_log("unifrog storage config mmc_node=missing\n");
      return;
   }

   (void)fdt_get_property_string_index(node, "status", 0, &status);
   (void)fdt_get_property_u_32_index(node, "bus-width", 0, &bus_width);
   (void)fdt_get_property_u_32_index(node, "clock-frequency", 0, &clock);

   unifrog_log("unifrog storage config mode=%s experimental=%d node=%d status=%s clock=%lu "
          "bus-width=%lu cap-highspeed=%d supports-highspeed=%d "
          "uhs-sdr12=%d uhs-sdr25=%d uhs-sdr50=%d no-1v8=%d broken-cd=%d\n",
      UNIFROG_SD_MODE,
      UNIFROG_SD_EXPERIMENTAL,
      node, status,
      (unsigned long)clock,
      (unsigned long)bus_width,
      fdt_property_read_bool(node, "cap-sd-highspeed") ? 1 : 0,
      fdt_property_read_bool(node, "supports-highspeed") ? 1 : 0,
      fdt_property_read_bool(node, "sd-uhs-sdr12") ? 1 : 0,
      fdt_property_read_bool(node, "sd-uhs-sdr25") ? 1 : 0,
      fdt_property_read_bool(node, "sd-uhs-sdr50") ? 1 : 0,
      fdt_property_read_bool(node, "no-support_1_8v") ? 1 : 0,
      fdt_property_read_bool(node, "broken-cd") ? 1 : 0);
}

static void log_fastboot_diag(void)
{
   volatile struct fastboot_diag *diag = FASTBOOT_DIAG_ADDR;

   if (diag->magic != FASTBOOT_DIAG_MAGIC)
      return;

   unifrog_log("unifrog fastboot diag stage=0x%08lx event=%lu result=%ld path=%s\n",
      (unsigned long)diag->stage_addr,
      (unsigned long)diag->event,
      (long)diag->result,
      (const char *)diag->path);
   diag->magic = 0;
}

void unifrog_platform_init_board(void)
{
   log_fastboot_diag();
   init_board_gpios();
   log_storage_config();
   apply_clock_defaults();
}

static int directory_has_entries(const char *path)
{
   DIR *dir;
   struct dirent *ent;
   int entries = 0;

   dir = opendir(path);
   if (!dir)
      return 0;

   while ((ent = readdir(dir)) != NULL) {
      if (ent->d_name[0] == '.' &&
         (ent->d_name[1] == '\0' ||
            (ent->d_name[1] == '.' && ent->d_name[2] == '\0')))
         continue;
      entries = 1;
      break;
   }

   closedir(dir);
   return entries;
}

int unifrog_platform_mount_storage(void)
{
   int mounted = -1;
   uint32_t start_ms = unifrog_perf_time_ms();

   (void)mkdir("/media", 0777);

   for (unsigned i = 0; i < sizeof(storage_mounts) / sizeof(storage_mounts[0]); i++) {
      uint32_t target_start_ms;
      int had_entries;
      int vfat_ret;
      int vfat_errno = 0;
      int ntfs_ret;
      int ntfs_errno = 0;

      (void)mkdir(storage_mounts[i].target, 0777);

      target_start_ms = unifrog_perf_time_ms();
      had_entries = directory_has_entries(storage_mounts[i].target);
      if (had_entries) {
         unifrog_log("unifrog storage mount target=%s already_ready=1 ms=%lu mask=0x%lx\n",
            storage_mounts[i].target,
            (unsigned long)(unifrog_perf_time_ms() - target_start_ms),
            (unsigned long)storage_mounted_mask);
         return 0;
      }
      if (storage_mounted_mask & (1u << i))
         continue;

      storage_mount_attempts++;
      target_start_ms = unifrog_perf_time_ms();
      errno = 0;
      vfat_ret = mount(storage_mounts[i].dev, storage_mounts[i].target,
         "vfat", 0, NULL);
      vfat_errno = errno;
      if (vfat_ret == 0) {
         storage_mounted_mask |= 1u << i;
         mounted = 0;
         unifrog_log("unifrog storage mount attempt=%lu dev=%s target=%s fs=vfat ret=0 errno=%d ms=%lu mask=0x%lx\n",
            (unsigned long)storage_mount_attempts, storage_mounts[i].dev,
            storage_mounts[i].target, vfat_errno,
            (unsigned long)(unifrog_perf_time_ms() - target_start_ms),
            (unsigned long)storage_mounted_mask);
      } else {
         unifrog_log("unifrog storage mount attempt=%lu dev=%s target=%s fs=vfat ret=%d errno=%d ms=%lu\n",
            (unsigned long)storage_mount_attempts, storage_mounts[i].dev,
            storage_mounts[i].target, vfat_ret, vfat_errno,
            (unsigned long)(unifrog_perf_time_ms() - target_start_ms));
      }
      if (vfat_ret == 0)
         continue;

      storage_mount_attempts++;
      target_start_ms = unifrog_perf_time_ms();
      errno = 0;
      ntfs_ret = mount(storage_mounts[i].dev, storage_mounts[i].target,
         "ntfs", 0, NULL);
      ntfs_errno = errno;
      if (ntfs_ret == 0) {
         storage_mounted_mask |= 1u << i;
         mounted = 0;
         unifrog_log("unifrog storage mount attempt=%lu dev=%s target=%s fs=ntfs ret=0 errno=%d ms=%lu mask=0x%lx\n",
            (unsigned long)storage_mount_attempts, storage_mounts[i].dev,
            storage_mounts[i].target, ntfs_errno,
            (unsigned long)(unifrog_perf_time_ms() - target_start_ms),
            (unsigned long)storage_mounted_mask);
      } else {
         unifrog_log("unifrog storage mount attempt=%lu dev=%s target=%s fs=ntfs ret=%d errno=%d ms=%lu\n",
            (unsigned long)storage_mount_attempts, storage_mounts[i].dev,
            storage_mounts[i].target, ntfs_ret, ntfs_errno,
            (unsigned long)(unifrog_perf_time_ms() - target_start_ms));
      }
   }

   unifrog_log("unifrog storage mount summary ret=%d ms=%lu mask=0x%lx attempts=%lu\n",
      mounted, (unsigned long)(unifrog_perf_time_ms() - start_ms),
      (unsigned long)storage_mounted_mask,
      (unsigned long)storage_mount_attempts);
   return mounted;
}

int unifrog_platform_wait_for_storage(void)
{
   int stable = 0;
   uint32_t wait_start_ms = unifrog_perf_time_ms();

   for (int i = 0; i < STORAGE_MAX_POLLS; i++) {
      uint32_t poll_start_ms = unifrog_perf_time_ms();
      uint32_t mount_done_ms;
      int mount_ret;
      int root_ready;
      int p1_ready;
      int p2_ready;

      mount_ret = unifrog_platform_mount_storage();
      mount_done_ms = unifrog_perf_time_ms();

      root_ready = directory_has_entries(UNIFROG_SD_ROOT);
      p1_ready = directory_has_entries("/media/mmcblk0p1");
      p2_ready = directory_has_entries("/media/mmcblk0p2");

      if (root_ready || p1_ready || p2_ready)
         stable++;
      else
         stable = 0;

      unifrog_log("unifrog storage poll=%d mount_ret=%d mount_ms=%lu check_ms=%lu total_ms=%lu root=%d p1=%d p2=%d stable=%d/%d mask=0x%lx\n",
         i, mount_ret,
         (unsigned long)(mount_done_ms - poll_start_ms),
         (unsigned long)(unifrog_perf_time_ms() - mount_done_ms),
         (unsigned long)(unifrog_perf_time_ms() - wait_start_ms),
         root_ready, p1_ready, p2_ready, stable,
         STORAGE_STABLE_SAMPLES,
         (unsigned long)storage_mounted_mask);

      if (stable >= STORAGE_STABLE_SAMPLES) {
         unifrog_log("unifrog storage ready polls=%d total_ms=%lu attempts=%lu mask=0x%lx\n",
            i + 1,
            (unsigned long)(unifrog_perf_time_ms() - wait_start_ms),
            (unsigned long)storage_mount_attempts,
            (unsigned long)storage_mounted_mask);
         return 0;
      }

      msleep(STORAGE_POLL_MS);
   }

   unifrog_log("unifrog storage timeout total_ms=%lu attempts=%lu mask=0x%lx stable=%d/%d\n",
      (unsigned long)(unifrog_perf_time_ms() - wait_start_ms),
      (unsigned long)storage_mount_attempts,
      (unsigned long)storage_mounted_mask,
      stable, STORAGE_STABLE_SAMPLES);
   return -1;
}

void unifrog_platform_idle_forever(void)
{
   for (;;)
      usleep(1000);
}
