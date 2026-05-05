#include <unifrog/platform.h>

#include <stdbool.h>
#include <stdint.h>
#include <errno.h>
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/unistd.h>

#include <hcuapi/gpio.h>
#include <hcuapi/pinpad.h>
#include <kernel/delay.h>

#include <cpu_func.h>
#include <fastboot/handoff.h>
#include <unifrog/boot_logo.h>
#include <unifrog/boot_trace.h>
#include <unifrog/log.h>
#include <unifrog/perf.h>
#include <unifrog/runtime.h>

#define CLOCK_GATE0_REG 0xb8800060u
#define CLOCK_GATE1_REG 0xb8800064u
#define RESET_GATE0_REG 0xb8800080u
#define RESET_GATE1_REG 0xb8800084u

extern unsigned long PINMUXL;
extern unsigned long PINMUXR;

#define STORAGE_STABLE_SAMPLES 1
#define STORAGE_MAX_POLLS 100
#define STORAGE_POLL_MS 100
#define SD_RUNTIME_QUIESCE_MS 100u

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
extern int umount2(const char *target, unsigned int flags);

struct bus_type;
struct device;

extern struct bus_type platform_bus_type;
extern int bus_for_each_dev(struct bus_type *bus, struct device *start,
   void *data, int (*fn)(struct device *dev, void *data));
extern const char hc_mmc_host_hw_ops[];
extern void fileuart_set_storage_suspended(int suspended);

#ifndef MNT_FORCE
#define MNT_FORCE 1u
#endif

#define SD_DEVICE_DRIVER_DATA_OFFSET 64u
#define SD_MMC_HOST_PARENT_OFFSET 0u
#define SD_MMC_HOST_F_MAX_OFFSET 208u
#define SD_MMC_HOST_CAPS_OFFSET 256u
#define SD_MMC_HOST_CAPS2_OFFSET 260u
#define SD_MMC_HOST_PM_CAPS_OFFSET 264u
#define SD_MMC_HOST_IOS_OFFSET 292u
#define SD_MMC_HOST_CLAIMED_OFFSET 308u
#define SD_MMC_HOST_CARD_OFFSET 392u
#define SD_MMC_HOST_CLAIMED_TASK_OFFSET 404u
#define SD_MMC_HOST_CLAIM_COUNT_OFFSET 408u
#define SD_MMC_HOST_BUS_OPS_OFFSET 472u
#define SD_MMC_HOST_ACTUAL_CLOCK_OFFSET 532u
#define SD_HC_HOST_OFFSET 576u
#define SD_HC_HOST_PDATA_OFFSET (SD_HC_HOST_OFFSET + 16u)
#define SD_HC_HOST_OPS_OFFSET (SD_HC_HOST_OFFSET + 20u)
#define SD_HC_HOST_MMC_OFFSET (SD_HC_HOST_OFFSET + 24u)
#define SD_HC_PDATA_BUS_HZ_OFFSET 8u
#define SD_HC_PDATA_MAX_FREQUENCY_OFFSET 16u
#define SD_HC_PDATA_CAPS_OFFSET 20u
#define SD_MMC_CARD_STATE_OFFSET 204u
#define SD_MMC_BUS_OP_SUSPEND_OFFSET 12u
#define SD_MMC_BUS_OP_RESUME_OFFSET 16u

#define SD_MMC_CAP_4_BIT_DATA (1u << 0)
#define SD_MMC_CAP_MMC_HIGHSPEED (1u << 1)
#define SD_MMC_CAP_SD_HIGHSPEED (1u << 2)
#define SD_MMC_CAP_8_BIT_DATA (1u << 6)
#define SD_MMC_CAP_UHS_SDR12 (1u << 15)
#define SD_MMC_CAP_UHS_SDR25 (1u << 16)
#define SD_MMC_CAP_UHS_SDR50 (1u << 17)
#define SD_MMC_CAP_UHS_SDR104 (1u << 18)
#define SD_MMC_CAP_UHS_DDR50 (1u << 19)
#define SD_MMC_CAP_SPEED_MASK \
   (SD_MMC_CAP_4_BIT_DATA | SD_MMC_CAP_8_BIT_DATA | \
    SD_MMC_CAP_MMC_HIGHSPEED | SD_MMC_CAP_SD_HIGHSPEED | \
    SD_MMC_CAP_UHS_SDR12 | SD_MMC_CAP_UHS_SDR25 | \
    SD_MMC_CAP_UHS_SDR50 | SD_MMC_CAP_UHS_SDR104 | \
    SD_MMC_CAP_UHS_DDR50)
#define SD_MMC_CAP2_NO_1_8V (1u << 19)

struct sd_runtime_profile {
   const char *name;
   uint32_t f_max;
   uint32_t set_caps;
   uint32_t clear_caps;
   uint32_t set_caps2;
   uint32_t clear_caps2;
};

struct sd_runtime_boot_state {
   uintptr_t host;
   uintptr_t pdata;
   uint32_t f_max;
   uint32_t caps;
   uint32_t caps2;
   uint32_t pm_caps;
   uint32_t pdata_bus_hz;
   uint32_t pdata_max_frequency;
   uint32_t pdata_caps;
   int saved;
   char active_profile[16];
};

static struct sd_runtime_boot_state sd_runtime_boot;
static unifrog_platform_storage_stage_cb storage_stage_cb;
static void *storage_stage_userdata;
typedef int (*sd_runtime_bus_op_fn)(void *host);

static const struct sd_runtime_profile sd_runtime_profiles[] = {
   {
      "hs1",
      198000000u,
      SD_MMC_CAP_SD_HIGHSPEED,
      SD_MMC_CAP_SPEED_MASK,
      SD_MMC_CAP2_NO_1_8V,
      0u,
   },
   {
      "wide50",
      50000000u,
      SD_MMC_CAP_4_BIT_DATA | SD_MMC_CAP_SD_HIGHSPEED,
      SD_MMC_CAP_SPEED_MASK,
      SD_MMC_CAP2_NO_1_8V,
      0u,
   },
   {
      "wide",
      198000000u,
      SD_MMC_CAP_4_BIT_DATA | SD_MMC_CAP_SD_HIGHSPEED,
      SD_MMC_CAP_SPEED_MASK,
      SD_MMC_CAP2_NO_1_8V,
      0u,
   },
   {
      "uhs12",
      50000000u,
      SD_MMC_CAP_4_BIT_DATA | SD_MMC_CAP_SD_HIGHSPEED |
         SD_MMC_CAP_UHS_SDR12,
      SD_MMC_CAP_SPEED_MASK,
      0u,
      SD_MMC_CAP2_NO_1_8V,
   },
   {
      "uhs25",
      99000000u,
      SD_MMC_CAP_4_BIT_DATA | SD_MMC_CAP_SD_HIGHSPEED |
         SD_MMC_CAP_UHS_SDR12 | SD_MMC_CAP_UHS_SDR25,
      SD_MMC_CAP_SPEED_MASK,
      0u,
      SD_MMC_CAP2_NO_1_8V,
   },
   {
      "uhs",
      198000000u,
      SD_MMC_CAP_4_BIT_DATA | SD_MMC_CAP_SD_HIGHSPEED |
         SD_MMC_CAP_UHS_SDR12 | SD_MMC_CAP_UHS_SDR25 |
         SD_MMC_CAP_UHS_SDR50,
      SD_MMC_CAP_SPEED_MASK,
      0u,
      SD_MMC_CAP2_NO_1_8V,
   },
};

static uint32_t sd_read_u32(uintptr_t base, size_t offset)
{
   uint32_t value;

   memcpy(&value, (const void *)(base + offset), sizeof(value));
   return value;
}

static uint32_t sd_runtime_hash(const char *text)
{
   uint32_t hash = 2166136261u;

   if (!text)
      return hash;
   while (*text) {
      hash ^= (uint8_t)*text++;
      hash *= 16777619u;
   }
   return hash;
}

static void sd_runtime_stage(const char *operation, const char *stage,
   int result)
{
   volatile struct fastboot_diag *diag = FASTBOOT_DIAG_ADDR;
   char detail[FASTBOOT_HANDOFF_PATH_BYTES];

   snprintf(detail, sizeof(detail), "sd:%s:%s",
      operation ? operation : "",
      stage ? stage : "");

   diag->magic = FASTBOOT_DIAG_MAGIC;
   diag->stage_addr = 0x53445254u;
   diag->event = sd_runtime_hash(detail);
   diag->result = result;
   memset((void *)diag->path, 0, sizeof(diag->path));
   snprintf((char *)diag->path, sizeof(diag->path), "%s", detail);
   cache_flush((void *)diag, sizeof(*diag));

   unifrog_log("unifrog sd runtime stage operation=%s stage=%s result=%d\n",
      operation ? operation : "",
      stage ? stage : "",
      result);

   if (storage_stage_cb)
      storage_stage_cb(storage_stage_userdata, operation, stage);
}

void unifrog_platform_storage_diag_note(const char *operation,
   const char *stage)
{
   sd_runtime_stage(operation, stage, 0);
}

static void sd_write_u32(uintptr_t base, size_t offset, uint32_t value)
{
   memcpy((void *)(base + offset), &value, sizeof(value));
}

static uint8_t sd_read_u8(uintptr_t base, size_t offset)
{
   uint8_t value;

   memcpy(&value, (const void *)(base + offset), sizeof(value));
   return value;
}

static void sd_write_u8(uintptr_t base, size_t offset, uint8_t value)
{
   memcpy((void *)(base + offset), &value, sizeof(value));
}

static uintptr_t sd_read_ptr(uintptr_t base, size_t offset)
{
   uintptr_t value;

   memcpy(&value, (const void *)(base + offset), sizeof(value));
   return value;
}

static const struct sd_runtime_profile *sd_runtime_profile_by_name(
   const char *name)
{
   if (!name || !name[0])
      return NULL;
   for (unsigned i = 0;
        i < sizeof(sd_runtime_profiles) / sizeof(sd_runtime_profiles[0]);
        i++) {
      if (strcmp(name, sd_runtime_profiles[i].name) == 0)
         return &sd_runtime_profiles[i];
   }
   return NULL;
}

static int sd_runtime_host_valid(uintptr_t host, struct device *dev)
{
   if (!host || (host & 3u) != 0)
      return 0;
   if (sd_read_ptr(host, SD_MMC_HOST_PARENT_OFFSET) != (uintptr_t)dev)
      return 0;
   if (sd_read_ptr(host, SD_HC_HOST_OPS_OFFSET) !=
       (uintptr_t)hc_mmc_host_hw_ops)
      return 0;
   if (sd_read_ptr(host, SD_HC_HOST_MMC_OFFSET) != host)
      return 0;
   return 1;
}

struct sd_find_host_context {
   uintptr_t host;
   struct device *dev;
};

static int sd_runtime_find_host_cb(struct device *dev, void *data)
{
   struct sd_find_host_context *ctx = data;
   uintptr_t host;

   if (!dev || !ctx)
      return 0;

   host = sd_read_ptr((uintptr_t)dev, SD_DEVICE_DRIVER_DATA_OFFSET);
   if (!sd_runtime_host_valid(host, dev))
      return 0;

   ctx->host = host;
   ctx->dev = dev;
   return 1;
}

static void sd_runtime_save_boot(uintptr_t host)
{
   uintptr_t pdata;

   if (sd_runtime_boot.saved)
      return;

   pdata = sd_read_ptr(host, SD_HC_HOST_PDATA_OFFSET);
   sd_runtime_boot.host = host;
   sd_runtime_boot.pdata = pdata;
   sd_runtime_boot.f_max = sd_read_u32(host, SD_MMC_HOST_F_MAX_OFFSET);
   sd_runtime_boot.caps = sd_read_u32(host, SD_MMC_HOST_CAPS_OFFSET);
   sd_runtime_boot.caps2 = sd_read_u32(host, SD_MMC_HOST_CAPS2_OFFSET);
   sd_runtime_boot.pm_caps = sd_read_u32(host, SD_MMC_HOST_PM_CAPS_OFFSET);
   if (pdata) {
      sd_runtime_boot.pdata_bus_hz =
         sd_read_u32(pdata, SD_HC_PDATA_BUS_HZ_OFFSET);
      sd_runtime_boot.pdata_max_frequency =
         sd_read_u32(pdata, SD_HC_PDATA_MAX_FREQUENCY_OFFSET);
      sd_runtime_boot.pdata_caps =
         sd_read_u32(pdata, SD_HC_PDATA_CAPS_OFFSET);
   }
   snprintf(sd_runtime_boot.active_profile,
      sizeof(sd_runtime_boot.active_profile), "boot");
   sd_runtime_boot.saved = 1;

   unifrog_log("unifrog sd runtime boot host=0x%08lx pdata=0x%08lx fmax=%lu "
          "caps=0x%08lx caps2=0x%08lx pdata_bus=%lu pdata_max=%lu "
          "pdata_caps=0x%08lx mode=%s\n",
      (unsigned long)host,
      (unsigned long)pdata,
      (unsigned long)sd_runtime_boot.f_max,
      (unsigned long)sd_runtime_boot.caps,
      (unsigned long)sd_runtime_boot.caps2,
      (unsigned long)sd_runtime_boot.pdata_bus_hz,
      (unsigned long)sd_runtime_boot.pdata_max_frequency,
      (unsigned long)sd_runtime_boot.pdata_caps,
      UNIFROG_SD_MODE);
}

static uintptr_t sd_runtime_find_host(void)
{
   struct sd_find_host_context ctx;

   memset(&ctx, 0, sizeof(ctx));
   (void)bus_for_each_dev(&platform_bus_type, NULL, &ctx,
      sd_runtime_find_host_cb);
   if (!ctx.host)
      return 0;
   sd_runtime_save_boot(ctx.host);
   return ctx.host;
}

static void sd_runtime_format_detail(const char *name, uintptr_t host,
   int mount_ret, uint32_t ms, char *detail, size_t detail_size)
{
   const char *active = name ? name : "";

   if (!detail || detail_size == 0)
      return;
   snprintf(detail, detail_size,
      "%s fmax=%lu actual=%lu caps=%08lx caps2=%08lx ios=%u/%u/%u mount=%d %lums",
      active,
      (unsigned long)sd_read_u32(host, SD_MMC_HOST_F_MAX_OFFSET),
      (unsigned long)sd_read_u32(host, SD_MMC_HOST_ACTUAL_CLOCK_OFFSET),
      (unsigned long)sd_read_u32(host, SD_MMC_HOST_CAPS_OFFSET),
      (unsigned long)sd_read_u32(host, SD_MMC_HOST_CAPS2_OFFSET),
      (unsigned)sd_read_u8(host, SD_MMC_HOST_IOS_OFFSET + 9u),
      (unsigned)sd_read_u8(host, SD_MMC_HOST_IOS_OFFSET + 10u),
      (unsigned)sd_read_u8(host, SD_MMC_HOST_IOS_OFFSET + 11u),
      mount_ret,
      (unsigned long)ms);
}

static int sd_runtime_unmount_storage(const char *tag)
{
   int ret = 0;

   for (int i = (int)(sizeof(storage_mounts) / sizeof(storage_mounts[0])) - 1;
        i >= 0; i--) {
      int unmount_ret;
      int unmount_errno;
      char stage[48];

      snprintf(stage, sizeof(stage), "unmount %s",
         storage_mounts[i].target);
      sd_runtime_stage(tag, stage, 0);
      errno = 0;
      unmount_ret = umount2(storage_mounts[i].target, MNT_FORCE);
      unmount_errno = errno;

      if (unmount_ret == 0 ||
          (unmount_ret != 0 && unmount_errno == EINVAL) ||
          (unmount_ret != 0 && unmount_errno == ENOENT)) {
         storage_mounted_mask &= ~(1u << i);
      } else {
         ret = -1;
      }

      unifrog_log("unifrog sd runtime unmount tag=%s target=%s ret=%d errno=%d "
             "flags=0x%lx mask=0x%lx\n",
         tag ? tag : "",
         storage_mounts[i].target,
         unmount_ret,
         unmount_errno,
         (unsigned long)MNT_FORCE,
         (unsigned long)storage_mounted_mask);
      sd_runtime_stage(tag, stage, unmount_ret == 0 ? 0 : -unmount_errno);
   }

   return ret;
}

static void sd_runtime_clear_ios(uintptr_t host)
{
   sd_write_u32(host, SD_MMC_HOST_IOS_OFFSET, 0);
   sd_write_u8(host, SD_MMC_HOST_IOS_OFFSET + 9u, 0);
   sd_write_u8(host, SD_MMC_HOST_IOS_OFFSET + 10u, 0);
   sd_write_u8(host, SD_MMC_HOST_IOS_OFFSET + 11u, 0);
   sd_write_u8(host, SD_MMC_HOST_IOS_OFFSET + 12u, 0);
   sd_write_u32(host, SD_MMC_HOST_ACTUAL_CLOCK_OFFSET, 0);
}

static void sd_runtime_write_profile(uintptr_t host,
   const struct sd_runtime_profile *profile)
{
   uintptr_t pdata = sd_read_ptr(host, SD_HC_HOST_PDATA_OFFSET);
   uint32_t caps = sd_runtime_boot.caps;
   uint32_t caps2 = sd_runtime_boot.caps2;

   caps &= ~profile->clear_caps;
   caps |= profile->set_caps;
   caps2 &= ~profile->clear_caps2;
   caps2 |= profile->set_caps2;

   sd_write_u32(host, SD_MMC_HOST_F_MAX_OFFSET, profile->f_max);
   sd_write_u32(host, SD_MMC_HOST_CAPS_OFFSET, caps);
   sd_write_u32(host, SD_MMC_HOST_CAPS2_OFFSET, caps2);
   sd_write_u32(host, SD_MMC_HOST_PM_CAPS_OFFSET, sd_runtime_boot.pm_caps);
   sd_runtime_clear_ios(host);

   if (pdata) {
      sd_write_u32(pdata, SD_HC_PDATA_MAX_FREQUENCY_OFFSET, profile->f_max);
      sd_write_u32(pdata, SD_HC_PDATA_CAPS_OFFSET, caps);
   }
}

static void sd_runtime_write_boot(uintptr_t host)
{
   uintptr_t pdata = sd_read_ptr(host, SD_HC_HOST_PDATA_OFFSET);

   sd_write_u32(host, SD_MMC_HOST_F_MAX_OFFSET, sd_runtime_boot.f_max);
   sd_write_u32(host, SD_MMC_HOST_CAPS_OFFSET, sd_runtime_boot.caps);
   sd_write_u32(host, SD_MMC_HOST_CAPS2_OFFSET, sd_runtime_boot.caps2);
   sd_write_u32(host, SD_MMC_HOST_PM_CAPS_OFFSET, sd_runtime_boot.pm_caps);
   sd_runtime_clear_ios(host);

   if (pdata) {
      sd_write_u32(pdata, SD_HC_PDATA_BUS_HZ_OFFSET,
         sd_runtime_boot.pdata_bus_hz);
      sd_write_u32(pdata, SD_HC_PDATA_MAX_FREQUENCY_OFFSET,
         sd_runtime_boot.pdata_max_frequency);
      sd_write_u32(pdata, SD_HC_PDATA_CAPS_OFFSET,
         sd_runtime_boot.pdata_caps);
   }
}

static int sd_runtime_call_bus_op(const char *operation, const char *stage,
   uintptr_t host, size_t op_offset)
{
   uintptr_t ops = sd_read_ptr(host, SD_MMC_HOST_BUS_OPS_OFFSET);
   uintptr_t card = sd_read_ptr(host, SD_MMC_HOST_CARD_OFFSET);
   uintptr_t fn = ops ? sd_read_ptr(ops, op_offset) : 0;
   uint32_t before_state = card ?
      sd_read_u32(card, SD_MMC_CARD_STATE_OFFSET) : 0u;
   uint32_t before_actual =
      sd_read_u32(host, SD_MMC_HOST_ACTUAL_CLOCK_OFFSET);
   int ret;

   unifrog_log("unifrog sd runtime bus_op begin operation=%s stage=%s "
          "host=0x%08lx ops=0x%08lx fn=0x%08lx card=0x%08lx "
          "card_state=0x%08lx claim=0x%02lx owner=0x%08lx count=%lu "
          "caps=0x%08lx caps2=0x%08lx actual=%lu\n",
      operation ? operation : "",
      stage ? stage : "",
      (unsigned long)host,
      (unsigned long)ops,
      (unsigned long)fn,
      (unsigned long)card,
      (unsigned long)before_state,
      (unsigned long)sd_read_u8(host, SD_MMC_HOST_CLAIMED_OFFSET),
      (unsigned long)sd_read_ptr(host, SD_MMC_HOST_CLAIMED_TASK_OFFSET),
      (unsigned long)sd_read_u32(host, SD_MMC_HOST_CLAIM_COUNT_OFFSET),
      (unsigned long)sd_read_u32(host, SD_MMC_HOST_CAPS_OFFSET),
      (unsigned long)sd_read_u32(host, SD_MMC_HOST_CAPS2_OFFSET),
      (unsigned long)before_actual);

   if (!fn) {
      unifrog_log("unifrog sd runtime bus_op missing operation=%s stage=%s "
             "offset=%lu\n",
         operation ? operation : "",
         stage ? stage : "",
         (unsigned long)op_offset);
      return -ENOSYS;
   }

   ret = ((sd_runtime_bus_op_fn)fn)((void *)host);
   card = sd_read_ptr(host, SD_MMC_HOST_CARD_OFFSET);
   unifrog_log("unifrog sd runtime bus_op done operation=%s stage=%s ret=%d "
          "card=0x%08lx card_state=0x%08lx caps=0x%08lx caps2=0x%08lx "
          "actual=%lu\n",
      operation ? operation : "",
      stage ? stage : "",
      ret,
      (unsigned long)card,
      (unsigned long)(card ? sd_read_u32(card, SD_MMC_CARD_STATE_OFFSET) : 0u),
      (unsigned long)sd_read_u32(host, SD_MMC_HOST_CAPS_OFFSET),
      (unsigned long)sd_read_u32(host, SD_MMC_HOST_CAPS2_OFFSET),
      (unsigned long)sd_read_u32(host, SD_MMC_HOST_ACTUAL_CLOCK_OFFSET));
   return ret;
}

static int sd_runtime_reconfigure_host(const char *operation, uintptr_t host,
   const struct sd_runtime_profile *profile)
{
   int ret;

   sd_runtime_stage(operation, "bus suspend begin", 0);
   ret = sd_runtime_call_bus_op(operation, "suspend", host,
      SD_MMC_BUS_OP_SUSPEND_OFFSET);
   sd_runtime_stage(operation, "bus suspend done", ret);
   if (ret != 0)
      return ret;

   if (profile) {
      sd_runtime_write_profile(host, profile);
      sd_runtime_stage(operation, "profile written", 0);
   } else {
      sd_runtime_write_boot(host);
      sd_runtime_stage(operation, "boot profile written", 0);
   }

   sd_runtime_stage(operation, "bus resume begin", 0);
   ret = sd_runtime_call_bus_op(operation, "resume", host,
      SD_MMC_BUS_OP_RESUME_OFFSET);
   sd_runtime_stage(operation, "bus resume done", ret);
   if (ret != 0)
      return ret;

   msleep(100);
   return 0;
}

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
   if (!unifrog_boot_logo_is_active()) {
      gpio_configure(PINPAD_R05, GPIO_DIR_OUTPUT);
      gpio_set_output(PINPAD_R05, true);
   }

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
   unifrog_boot_trace_log("platform.init_board");
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
      if (storage_mounted_mask & (1u << i)) {
         unifrog_log("unifrog storage mount stale_mask target=%s bit=%u mask=0x%lx\n",
            storage_mounts[i].target, i, (unsigned long)storage_mounted_mask);
         storage_mounted_mask &= ~(1u << i);
      }

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

int unifrog_platform_storage_ready(void)
{
   return directory_has_entries(UNIFROG_SD_ROOT) ||
      directory_has_entries("/media/mmcblk0p1") ||
      directory_has_entries("/media/mmcblk0p2");
}

int unifrog_platform_recover_storage(const char *tag, unsigned attempts,
   unsigned delay_ms)
{
   uint32_t start_ms = unifrog_perf_time_ms();

   if (attempts == 0)
      attempts = 1;
   if (unifrog_platform_storage_ready()) {
      unifrog_log("unifrog storage recover tag=%s ready=1 attempts=0 total_ms=0 mask=0x%lx\n",
         tag ? tag : "", (unsigned long)storage_mounted_mask);
      return 0;
   }

   for (unsigned i = 0; i < attempts; i++) {
      int mount_ret = unifrog_platform_mount_storage();
      int ready = unifrog_platform_storage_ready();

      unifrog_log("unifrog storage recover tag=%s attempt=%u mount_ret=%d ready=%d total_ms=%lu mask=0x%lx\n",
         tag ? tag : "", i + 1u, mount_ret, ready,
         (unsigned long)(unifrog_perf_time_ms() - start_ms),
         (unsigned long)storage_mounted_mask);
      if (ready)
         return 0;
      if (delay_ms)
         msleep(delay_ms);
   }

   unifrog_log("unifrog storage recover tag=%s failed attempts=%u total_ms=%lu mask=0x%lx\n",
      tag ? tag : "", attempts,
      (unsigned long)(unifrog_perf_time_ms() - start_ms),
      (unsigned long)storage_mounted_mask);
   return -1;
}

int unifrog_platform_sd_runtime_supported(void)
{
   return sd_runtime_find_host() ? 1 : 0;
}

int unifrog_platform_sd_apply_profile(const char *profile,
   unsigned mount_attempts, unsigned mount_delay_ms, char *detail,
   size_t detail_size)
{
   const struct sd_runtime_profile *runtime_profile;
   uintptr_t host;
   uint32_t start_ms;
   int unmount_ret;
   int switch_ret;
   int mount_ret = 0;

   runtime_profile = sd_runtime_profile_by_name(profile);
   if (!runtime_profile) {
      if (detail && detail_size)
         snprintf(detail, detail_size, "unknown profile %s",
            profile ? profile : "");
      return -1;
   }

   host = sd_runtime_find_host();
   if (!host) {
      if (detail && detail_size)
         snprintf(detail, detail_size, "mmc host not found");
      unifrog_log("unifrog sd runtime switch profile=%s ret=-1 reason=no_host\n",
         profile);
      return -1;
   }

   start_ms = unifrog_perf_time_ms();
   unifrog_log("unifrog sd runtime switch begin profile=%s host=0x%08lx "
          "card=0x%08lx caps=0x%08lx caps2=0x%08lx\n",
      profile,
      (unsigned long)host,
      (unsigned long)sd_read_ptr(host, SD_MMC_HOST_CARD_OFFSET),
      (unsigned long)sd_read_u32(host, SD_MMC_HOST_CAPS_OFFSET),
      (unsigned long)sd_read_u32(host, SD_MMC_HOST_CAPS2_OFFSET));

   sd_runtime_stage(profile, "unmount begin", 0);
   unmount_ret = sd_runtime_unmount_storage(profile);
   if (unmount_ret != 0) {
      sd_runtime_format_detail(profile, host, -1,
         unifrog_perf_time_ms() - start_ms, detail, detail_size);
      unifrog_log("unifrog sd runtime switch profile=%s ret=-1 "
             "reason=unmount_failed ms=%lu\n",
         profile, (unsigned long)(unifrog_perf_time_ms() - start_ms));
      sd_runtime_stage(profile, "unmount failed", unmount_ret);
      return -1;
   }
   sd_runtime_stage(profile, "unmount done", 0);
   sd_runtime_stage(profile, "settle after unmount", 0);
   msleep(SD_RUNTIME_QUIESCE_MS);

   switch_ret = sd_runtime_reconfigure_host(profile, host, runtime_profile);
   if (switch_ret != 0) {
      sd_runtime_format_detail(profile, host, -1,
         unifrog_perf_time_ms() - start_ms, detail, detail_size);
      unifrog_log("unifrog sd runtime switch profile=%s ret=-1 "
             "reason=reconfigure_failed switch_ret=%d ms=%lu\n",
         profile, switch_ret,
         (unsigned long)(unifrog_perf_time_ms() - start_ms));
      if (mount_attempts) {
         sd_runtime_stage(profile, "recover begin", switch_ret);
         mount_ret = unifrog_platform_recover_storage(profile,
            mount_attempts, mount_delay_ms);
         sd_runtime_stage(profile, "recover done", mount_ret);
      }
      return -1;
   }

   if (mount_attempts) {
      sd_runtime_stage(profile, "recover begin", 0);
      mount_ret = unifrog_platform_recover_storage(profile,
         mount_attempts, mount_delay_ms);
      sd_runtime_stage(profile, "recover done", mount_ret);
   }

   snprintf(sd_runtime_boot.active_profile,
      sizeof(sd_runtime_boot.active_profile), "%s", profile);
   sd_runtime_format_detail(profile, host, mount_ret,
      unifrog_perf_time_ms() - start_ms, detail, detail_size);
   unifrog_log("unifrog sd runtime switch done profile=%s ret=%d ms=%lu "
          "caps=0x%08lx caps2=0x%08lx actual=%lu mount_ret=%d\n",
      profile,
      mount_ret == 0 ? 0 : -1,
      (unsigned long)(unifrog_perf_time_ms() - start_ms),
      (unsigned long)sd_read_u32(host, SD_MMC_HOST_CAPS_OFFSET),
      (unsigned long)sd_read_u32(host, SD_MMC_HOST_CAPS2_OFFSET),
      (unsigned long)sd_read_u32(host, SD_MMC_HOST_ACTUAL_CLOCK_OFFSET),
      mount_ret);

   return mount_ret == 0 ? 0 : -1;
}

int unifrog_platform_sd_restore_boot(unsigned mount_attempts,
   unsigned mount_delay_ms, char *detail, size_t detail_size)
{
   uintptr_t host;
   uint32_t start_ms;
   int unmount_ret;
   int restore_ret = 0;
   int mount_ret = 0;

   host = sd_runtime_find_host();
   if (!host) {
      if (detail && detail_size)
         snprintf(detail, detail_size, "mmc host not found");
      unifrog_log("unifrog sd runtime restore ret=-1 reason=no_host\n");
      return -1;
   }
   if (!sd_runtime_boot.saved)
      sd_runtime_save_boot(host);

   start_ms = unifrog_perf_time_ms();
   unifrog_log("unifrog sd runtime restore begin host=0x%08lx active=%s\n",
      (unsigned long)host, sd_runtime_boot.active_profile);

   sd_runtime_stage("restore", "unmount begin", 0);
   unmount_ret = sd_runtime_unmount_storage("restore");
   if (unmount_ret != 0) {
      sd_runtime_format_detail("restore", host, -1,
         unifrog_perf_time_ms() - start_ms, detail, detail_size);
      unifrog_log("unifrog sd runtime restore ret=-1 reason=unmount_failed "
             "ms=%lu\n",
         (unsigned long)(unifrog_perf_time_ms() - start_ms));
      sd_runtime_stage("restore", "unmount failed", unmount_ret);
      return -1;
   }
   sd_runtime_stage("restore", "unmount done", 0);
   sd_runtime_stage("restore", "settle after unmount", 0);
   msleep(SD_RUNTIME_QUIESCE_MS);

   if (strcmp(sd_runtime_boot.active_profile, "boot") != 0) {
      restore_ret = sd_runtime_reconfigure_host("restore", host, NULL);
   } else {
      sd_runtime_stage("restore", "host already boot", 0);
   }

   if (mount_attempts) {
      sd_runtime_stage("restore", "recover begin", restore_ret);
      mount_ret = unifrog_platform_recover_storage("restore",
         mount_attempts, mount_delay_ms);
      sd_runtime_stage("restore", "recover done", mount_ret);
   }

   if (restore_ret == 0)
      snprintf(sd_runtime_boot.active_profile,
         sizeof(sd_runtime_boot.active_profile), "boot");
   sd_runtime_format_detail("boot", host, mount_ret,
      unifrog_perf_time_ms() - start_ms, detail, detail_size);
   unifrog_log("unifrog sd runtime restore done ret=%d ms=%lu caps=0x%08lx "
          "caps2=0x%08lx actual=%lu restore_ret=%d mount_ret=%d\n",
      (restore_ret == 0 && mount_ret == 0) ? 0 : -1,
      (unsigned long)(unifrog_perf_time_ms() - start_ms),
      (unsigned long)sd_read_u32(host, SD_MMC_HOST_CAPS_OFFSET),
      (unsigned long)sd_read_u32(host, SD_MMC_HOST_CAPS2_OFFSET),
      (unsigned long)sd_read_u32(host, SD_MMC_HOST_ACTUAL_CLOCK_OFFSET),
      restore_ret,
      mount_ret);

   return (restore_ret == 0 && mount_ret == 0) ? 0 : -1;
}

void unifrog_platform_set_storage_stage_callback(
   unifrog_platform_storage_stage_cb cb, void *userdata)
{
   storage_stage_cb = cb;
   storage_stage_userdata = userdata;
}

void unifrog_platform_set_storage_log_suspended(int suspended)
{
   fileuart_set_storage_suspended(suspended);
   unifrog_log("unifrog storage log_suspended=%d\n", suspended ? 1 : 0);
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
