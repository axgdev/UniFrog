#include <unifrog/platform.h>

#include <stdbool.h>
#include <stdint.h>
#include <errno.h>
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/unistd.h>

#include <fastboot/handoff.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <kernel/delay.h>
#include <kernel/io.h>
#include <hcuapi/pinpad.h>
#include <unifrog/exception_record.h>
#include <unifrog/hc_mmc_host.h>
#include <unifrog/log.h>
#include <unifrog/paths.h>
#include <unifrog/perf.h>

/* Private SF2000 storage mount, recovery, and SD runtime profile helpers. */
#define STORAGE_STABLE_SAMPLES 2
#define STORAGE_MAX_POLLS 100
#define STORAGE_POLL_MS 100
#define STORAGE_ACTIVITY_MOUNT_VFAT 0x5101u
#define STORAGE_ACTIVITY_MOUNT_NTFS 0x5102u
#define STORAGE_ACTIVITY_MOUNT_DONE 0x5103u
#define STORAGE_ACTIVITY_WAIT_DEFERRED 0x5104u
#define SD_RUNTIME_QUIESCE_MS 100u
#define SD_RUNTIME_ENUMERATING_GRACE_MS 5000u
#define SD_RUNTIME_FILEUART_FAULT_TICKS 5000u
#define SD_RUNTIME_LOG_QUIET_MS 2000u
#define SD_RUNTIME_LOG_FAULT_MS 5000u
#define SD_RUNTIME_CLAIM_WAIT_MS 1500u
#define SD_RUNTIME_CLAIM_WAIT_STEP_MS 25u
#define SD_RUNTIME_RECOVERY_GRACE_ATTEMPTS 48u
#define SD_RUNTIME_RECOVERY_GRACE_DELAY_MS 125u
#define SD_RUNTIME_REDETECT_SETTLE_MS 250u
#define SD_RUNTIME_HARD_RECOVERY_FIRST_ATTEMPT 8u
#define SD_RUNTIME_HARD_RECOVERY_SECOND_ATTEMPT 24u
#define SD_RUNTIME_HARD_RECOVERY_SETTLE_MS 500u

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
static unsigned storage_log_suspend_depth;
static unsigned storage_ready_debug_logged;
static unsigned storage_mount_failed_debug_count;

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
extern void fileuart_set_storage_suspended(int suspended);
extern void fileuart_note_storage_unstable(unsigned ticks);
extern unsigned long GPIOLCTRL;
extern unsigned long PINMUXL;
extern void __mmc_claim_host(void *host, void *abort);
extern void mmc_release_host(void *host);
extern int mmc_app_set_bus_width(void *card, unsigned int width);
extern int mmc_sd_switch(void *card, int mode, int group, int value,
   unsigned char *status);
extern int mmc_sd_switch_hs(void *card);
extern unsigned int mmc_sd_get_max_clock(void *card);
extern void mmc_set_clock(void *host, unsigned int hz);
extern void mmc_set_bus_width(void *host, unsigned int width);
extern void mmc_set_timing(void *host, unsigned int timing);
extern void mmc_detect_change(void *host, unsigned long delay_ms);
extern void mmc_detach_bus(void *host);
extern void mmc_power_off(void *host);
extern void hc_mmc_ip_reset(void);
extern void *sg_next(void *sg);

#ifndef MNT_FORCE
#define MNT_FORCE 1u
#endif

#define SD_DEVICE_DRIVER_DATA_OFFSET 64u
#define SD_MMC_HOST_PARENT_OFFSET 0u
#define SD_MMC_HOST_OPS_OFFSET 196u
#define SD_MMC_HOST_F_MAX_OFFSET 208u
#define SD_MMC_HOST_CAPS_OFFSET 256u
#define SD_MMC_HOST_CAPS2_OFFSET 260u
#define SD_MMC_HOST_PM_CAPS_OFFSET 264u
#define SD_MMC_HOST_MAX_SEG_SIZE_OFFSET 268u
#define SD_MMC_HOST_MAX_SEGS_OFFSET 272u
#define SD_MMC_HOST_MAX_REQ_SIZE_OFFSET 276u
#define SD_MMC_HOST_MAX_BLK_SIZE_OFFSET 280u
#define SD_MMC_HOST_MAX_BLK_COUNT_OFFSET 284u
#define SD_MMC_HOST_IOS_OFFSET 292u
#define SD_MMC_HOST_CLAIMED_OFFSET 308u
#define SD_MMC_HOST_CARD_OFFSET 392u
#define SD_MMC_HOST_CLAIMED_TASK_OFFSET 404u
#define SD_MMC_HOST_CLAIM_COUNT_OFFSET 408u
#define SD_MMC_HOST_BUS_OPS_OFFSET 472u
#define SD_MMC_HOST_BUS_REFS_OFFSET 476u
#define SD_MMC_HOST_ACTUAL_CLOCK_OFFSET 532u
#define SD_HC_HOST_OFFSET 576u
#define SD_HC_HOST_IOBASE_OFFSET (SD_HC_HOST_OFFSET + 4u)
#define SD_HC_HOST_PDATA_OFFSET (SD_HC_HOST_OFFSET + 16u)
#define SD_HC_HOST_OPS_OFFSET (SD_HC_HOST_OFFSET + 20u)
#define SD_HC_HOST_MMC_OFFSET (SD_HC_HOST_OFFSET + 24u)
#define SD_HC_HOST_CURRENT_MRQ_OFFSET (SD_HC_HOST_OFFSET + 28u)
#define SD_HC_HOST_CURRENT_CMD_OFFSET (SD_HC_HOST_OFFSET + 32u)
#define SD_HC_HOST_USE_PIO_OFFSET (SD_HC_HOST_OFFSET + 36u)
#define SD_MMC_HOST_OPS_REQUEST_OFFSET 8u
#define SD_MMC_HOST_OPS_SET_IOS_OFFSET 12u
#define SD_MMC_HOST_OPS_GET_CD_OFFSET 20u
#define SD_HC_HOST_HW_OPS_FORCE_CLOCK_OFFSET 12u
#define SD_HC_HOST_HW_OPS_SET_CMD_OFFSET 16u
#define SD_HC_HOST_HW_OPS_START_CMD_OFFSET 20u
#define SD_HC_HOST_HW_OPS_SET_DMA_OFFSET 36u
#define SD_HC_HOST_HW_OPS_ENABLE_IRQ_OFFSET 56u
#define SD_HC_HOST_HW_OPS_GET_CLEAR_IRQ_OFFSET 64u
/*
 * Reverse-engineered from libmmchosthc15.a: hc_mmc_probe fills this compact
 * platform-data block from the DTS, and hc_mmc_get_cd treats flag bit 3 as
 * broken-cd/non-removable.  Keep those fields visible because false
 * card-removal is indistinguishable from a flaky SD contact at higher layers.
 */
#define SD_HC_PDATA_BUS_WIDTH_OFFSET 0u
#define SD_HC_PDATA_FLAGS_OFFSET 4u
#define SD_HC_PDATA_BUS_HZ_OFFSET 8u
#define SD_HC_PDATA_CARD_DETECT_DELAY_OFFSET 12u
#define SD_HC_PDATA_MAX_FREQUENCY_OFFSET 16u
#define SD_HC_PDATA_CAPS_OFFSET 20u
#define SD_HC_PDATA_FLAG_BROKEN_CD (1u << 3)
#define SD_MMC_CARD_STATE_OFFSET 204u
#define SD_MMC_BUS_OP_SUSPEND_OFFSET 12u
#define SD_MMC_BUS_OP_RESUME_OFFSET 16u
#define SD_MMC_BUS_OP_REMOVE_OFFSET 0u
#define SD_MMC_REQUEST_CMD_OFFSET 4u
#define SD_MMC_REQUEST_DATA_OFFSET 8u
#define SD_MMC_REQUEST_STOP_OFFSET 12u
#define SD_MMC_COMMAND_OPCODE_OFFSET 0u
#define SD_MMC_COMMAND_ARG_OFFSET 4u
#define SD_MMC_COMMAND_FLAGS_OFFSET 24u
#define SD_MMC_COMMAND_RETRIES_OFFSET 28u
#define SD_MMC_COMMAND_ERROR_OFFSET 32u
#define SD_MMC_DATA_TIMEOUT_NS_OFFSET 0u
#define SD_MMC_DATA_TIMEOUT_CLKS_OFFSET 4u
#define SD_MMC_DATA_BLKSZ_OFFSET 8u
#define SD_MMC_DATA_BLOCKS_OFFSET 12u
#define SD_MMC_DATA_ERROR_OFFSET 16u
#define SD_MMC_DATA_FLAGS_OFFSET 20u
#define SD_MMC_DATA_BYTES_XFERED_OFFSET 24u
#define SD_MMC_DATA_STOP_OFFSET 28u
#define SD_MMC_DATA_SG_LEN_OFFSET 36u
#define SD_MMC_DATA_SG_OFFSET 44u

#define SD_SG_PAGE_LINK_OFFSET 0u
#define SD_SG_OFFSET_OFFSET 4u
#define SD_SG_LENGTH_OFFSET 8u
#define SD_SG_DMA_ADDRESS_OFFSET 12u
#define SD_SG_END_FLAG 2u
#define SD_SG_PAGE_MASK 0xfffff000u

#define SD_MMC_TRACE_COUNT 32u
#define SD_MMC_BOUNCE_MAX_SEGS 16u
#define SD_MMC_BOUNCE_BYTES (32u * 1024u)
#define SD_MMC_DMA_TO_DEVICE 1u
#define SD_MMC_DMA_FROM_DEVICE 2u

#define SD_HC_REG_CLKDIV_LO 0x03u
#define SD_HC_REG_BUS_WIDTH 0x0bu
#define SD_HC_REG_CLKDIV_HI 0x34u
#define SD_HC_REG_TIMING 0x50u

#define SD_SOC_CLOCK_GATE0_REG 0xb8800060u
#define SD_SOC_CLOCK_GATE1_REG 0xb8800064u
#define SD_SOC_CLOCK_GATE2_REG 0xb8800094u
#define SD_SOC_RESET_GATE0_REG 0xb8800080u
#define SD_SOC_RESET_GATE1_REG 0xb8800084u
#define SD_SOC_SDIO_CLOCK_GATE0_BIT (1u << 1)
#define SD_SOC_SDIO_CLOCK_GATE2_BITS (3u << 18)
#define SD_SOC_SDIO_RESET_GATE1_BIT (1u << 18)

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
#define SD_MMC_CAP_UHS_MASK \
   (SD_MMC_CAP_UHS_SDR12 | SD_MMC_CAP_UHS_SDR25 | \
    SD_MMC_CAP_UHS_SDR50 | SD_MMC_CAP_UHS_SDR104 | \
    SD_MMC_CAP_UHS_DDR50)
#define SD_MMC_CAP_DIRECT_LEGACY_FORBIDDEN \
   (SD_MMC_CAP_MMC_HIGHSPEED | SD_MMC_CAP_SD_HIGHSPEED | \
    SD_MMC_CAP_8_BIT_DATA | SD_MMC_CAP_UHS_MASK)
#define SD_MMC_CAP2_NO_1_8V (1u << 19)
#define SD_MMC_BUS_WIDTH_1 0u
#define SD_MMC_BUS_WIDTH_4 2u
#define SD_MMC_TIMING_LEGACY 0u
#define SD_MMC_TIMING_SD_HS 2u

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
   uintptr_t parent;
   uintptr_t pdata;
   uint32_t f_max;
   uint32_t caps;
   uint32_t caps2;
   uint32_t pm_caps;
   uint32_t pdata_bus_width;
   uint32_t pdata_flags;
   uint32_t pdata_bus_hz;
   uint32_t pdata_card_detect_delay;
   uint32_t pdata_max_frequency;
   uint32_t pdata_caps;
   int saved;
   int forced_broken_cd;
   char active_profile[16];
};

struct sd_runtime_switch_diag {
   int hs_attempted;
   int hs_ret;
   uint32_t hs_max_clock;
   uint32_t hs_before_actual;
   uint32_t hs_after_actual;
   uint32_t hs_before_timing;
   uint32_t hs_after_timing;
};

static struct sd_runtime_boot_state sd_runtime_boot;
static struct sd_runtime_switch_diag sd_runtime_last_switch;
static unifrog_platform_storage_stage_cb storage_stage_cb;
static void *storage_stage_userdata;
typedef int (*sd_runtime_bus_op_fn)(void *host);
typedef void (*sd_runtime_bus_remove_fn)(void *host);
static uintptr_t sd_runtime_find_host(void);
static int sd_runtime_ptr_valid(uintptr_t ptr);
void unifrog_platform_sd_debug_dump(const char *tag);

struct sd_mmc_trace_entry {
   uint32_t seq;
   uint32_t ts_ms;
   uintptr_t host;
   uintptr_t mrq;
   uintptr_t cmd;
   uintptr_t data;
   uintptr_t stop;
   uintptr_t data_stop;
   uint32_t actual;
   uint32_t ios_clock;
   uint32_t opcode;
   uint32_t arg;
   uint32_t flags;
   int32_t cmd_error;
   uint32_t retries;
   uint32_t blocks;
   uint32_t blksz;
   uint32_t data_flags;
   uint32_t bytes_xfered;
   uint32_t expected_bytes;
   uint32_t partial_progress;
   uint32_t sg_len;
   uintptr_t sg;
   uint32_t sg0_page_link;
   uint32_t sg0_offset;
   uint32_t sg0_length;
   uint32_t sg0_dma_address;
   int32_t data_error;
   uint32_t stop_opcode;
   int32_t stop_error;
};

static volatile uint32_t sd_mmc_diag_enabled;
static uint32_t sd_mmc_diag_depth;
static uint32_t sd_mmc_diag_begin_count;
static uint32_t sd_mmc_diag_request_count;
static volatile uint32_t sd_mmc_trace_seq;
static struct sd_mmc_trace_entry sd_mmc_trace[SD_MMC_TRACE_COUNT];
static uint32_t sd_mmc_trace_last_dump_seq;
static uint32_t sd_mmc_trace_last_summary_seq;
static uint32_t sd_mmc_diag_short_count;
static uint32_t sd_mmc_diag_short_last_opcode;
static uint32_t sd_mmc_diag_short_last_blocks;
static uint32_t sd_mmc_diag_short_last_blksz;
static uint32_t sd_mmc_diag_short_last_bytes;
static uint32_t sd_mmc_diag_short_last_expected;
static uint32_t sd_mmc_diag_partial_count;
static uint32_t sd_mmc_diag_partial_last_opcode;
static uint32_t sd_mmc_diag_partial_last_blocks;
static uint32_t sd_mmc_diag_partial_last_blksz;
static uint32_t sd_mmc_diag_partial_last_bytes;
static uint32_t sd_mmc_diag_partial_last_expected;
static uint32_t sd_mmc_diag_cmd_error_count;
static uint32_t sd_mmc_diag_cmd_error_last_opcode;
static int32_t sd_mmc_diag_cmd_error_last;
static uint32_t sd_mmc_diag_data_error_count;
static uint32_t sd_mmc_diag_data_error_last_opcode;
static int32_t sd_mmc_diag_data_error_last;
static uint32_t sd_mmc_diag_stop_error_count;
static uint32_t sd_mmc_diag_stop_error_last_opcode;
static int32_t sd_mmc_diag_stop_error_last;
static uint32_t sd_mmc_bounce_map_count;
static uint32_t sd_mmc_bounce_read_count;
static uint32_t sd_mmc_bounce_write_count;
static uint32_t sd_mmc_bounce_fallback_count;
static uint32_t sd_mmc_bounce_conflict_count;
static uint32_t sd_mmc_bounce_abort_count;
static uint32_t sd_mmc_bounce_last_nents;
static uint32_t sd_mmc_bounce_last_total;
static uint32_t sd_mmc_bounce_last_direction;

struct sd_mmc_bounce_sg {
   uintptr_t addr;
   uint32_t page_link;
   uint32_t offset;
   uint32_t length;
   uint32_t dma_address;
};

struct sd_mmc_bounce_state {
   volatile uint32_t active;
   void *dev;
   uintptr_t sglist;
   uint32_t nents;
   uint32_t mapped_nents;
   uint32_t direction;
   uint32_t total;
   struct sd_mmc_bounce_sg sg[SD_MMC_BOUNCE_MAX_SEGS];
};

static struct sd_mmc_bounce_state sd_mmc_bounce;
static uint8_t sd_mmc_bounce_buffer[SD_MMC_BOUNCE_BYTES]
   __attribute__((aligned(4096)));

static void storage_update_log_disk_available(void)
{
   unifrog_log_set_disk_available(storage_mounted_mask != 0);
}

static const struct sd_runtime_profile sd_runtime_profiles[] = {
   {
      "safe",
      25000000u,
      0u,
      SD_MMC_CAP_SPEED_MASK,
      SD_MMC_CAP2_NO_1_8V,
      0u,
   },
   {
      "wide1",
      1000000u,
      SD_MMC_CAP_4_BIT_DATA,
      SD_MMC_CAP_SPEED_MASK,
      SD_MMC_CAP2_NO_1_8V,
      0u,
   },
   {
      "wide2",
      2000000u,
      SD_MMC_CAP_4_BIT_DATA,
      SD_MMC_CAP_SPEED_MASK,
      SD_MMC_CAP2_NO_1_8V,
      0u,
   },
   {
      "wide4",
      4000000u,
      SD_MMC_CAP_4_BIT_DATA,
      SD_MMC_CAP_SPEED_MASK,
      SD_MMC_CAP2_NO_1_8V,
      0u,
   },
   {
      "wide8",
      8000000u,
      SD_MMC_CAP_4_BIT_DATA,
      SD_MMC_CAP_SPEED_MASK,
      SD_MMC_CAP2_NO_1_8V,
      0u,
   },
   {
      "wide10",
      10000000u,
      SD_MMC_CAP_4_BIT_DATA,
      SD_MMC_CAP_SPEED_MASK,
      SD_MMC_CAP2_NO_1_8V,
      0u,
   },
   {
      "wide12",
      12000000u,
      SD_MMC_CAP_4_BIT_DATA,
      SD_MMC_CAP_SPEED_MASK,
      SD_MMC_CAP2_NO_1_8V,
      0u,
   },
   {
      "wide14",
      14000000u,
      SD_MMC_CAP_4_BIT_DATA,
      SD_MMC_CAP_SPEED_MASK,
      SD_MMC_CAP2_NO_1_8V,
      0u,
   },
   {
      "wide16",
      16000000u,
      SD_MMC_CAP_4_BIT_DATA,
      SD_MMC_CAP_SPEED_MASK,
      SD_MMC_CAP2_NO_1_8V,
      0u,
   },
   {
      "wide18",
      18000000u,
      SD_MMC_CAP_4_BIT_DATA,
      SD_MMC_CAP_SPEED_MASK,
      SD_MMC_CAP2_NO_1_8V,
      0u,
   },
   {
      "wide20",
      20000000u,
      SD_MMC_CAP_4_BIT_DATA,
      SD_MMC_CAP_SPEED_MASK,
      SD_MMC_CAP2_NO_1_8V,
      0u,
   },
   {
      "wide22",
      22000000u,
      SD_MMC_CAP_4_BIT_DATA,
      SD_MMC_CAP_SPEED_MASK,
      SD_MMC_CAP2_NO_1_8V,
      0u,
   },
   {
      "wide24",
      24000000u,
      SD_MMC_CAP_4_BIT_DATA,
      SD_MMC_CAP_SPEED_MASK,
      SD_MMC_CAP2_NO_1_8V,
      0u,
   },
   {
      "wide25",
      25000000u,
      SD_MMC_CAP_4_BIT_DATA,
      SD_MMC_CAP_SPEED_MASK,
      SD_MMC_CAP2_NO_1_8V,
      0u,
   },
   {
      "wide37",
      37000000u,
      SD_MMC_CAP_4_BIT_DATA | SD_MMC_CAP_SD_HIGHSPEED,
      SD_MMC_CAP_SPEED_MASK,
      SD_MMC_CAP2_NO_1_8V,
      0u,
   },
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

static uint32_t sd_mmio_read32(uintptr_t base, size_t offset)
{
   return *(volatile const uint32_t *)(base + offset);
}

static uint8_t sd_mmio_read8(uintptr_t base, size_t offset)
{
   return *(volatile const uint8_t *)(base + offset);
}

static void sd_runtime_log_soc_gates(const char *tag)
{
   uint32_t clk0 = REG32_READ(SD_SOC_CLOCK_GATE0_REG);
   uint32_t clk1 = REG32_READ(SD_SOC_CLOCK_GATE1_REG);
   uint32_t clk2 = REG32_READ(SD_SOC_CLOCK_GATE2_REG);
   uint32_t rst0 = REG32_READ(SD_SOC_RESET_GATE0_REG);
   uint32_t rst1 = REG32_READ(SD_SOC_RESET_GATE1_REG);
   uint32_t sdio_gate_bit = (clk0 & SD_SOC_SDIO_CLOCK_GATE0_BIT) ? 1u : 0u;
   uint32_t sdio_aux_bits = clk2 & SD_SOC_SDIO_CLOCK_GATE2_BITS;
   uint32_t sdio_reset_asserted =
      (rst1 & SD_SOC_SDIO_RESET_GATE1_BIT) ? 1u : 0u;

   unifrog_log("unifrog sd gates tag=%s clk0=0x%08lx clk1=0x%08lx "
          "clk2=0x%08lx rst0=0x%08lx rst1=0x%08lx sdio_gate_bit=%u "
          "sdio_clock_enabled=%u sdio_aux_bits=0x%08lx "
          "sdio_reset_asserted=%u\n",
      tag ? tag : "",
      (unsigned long)clk0,
      (unsigned long)clk1,
      (unsigned long)clk2,
      (unsigned long)rst0,
      (unsigned long)rst1,
      sdio_gate_bit,
      sdio_gate_bit ? 0u : 1u,
      (unsigned long)sdio_aux_bits,
      sdio_reset_asserted);
}

static uint8_t sd_pinmux_l_byte(pinpad_e pin)
{
   if (pin < 32)
      return REG8_READ((uint32_t)&PINMUXL + pin);
   return 0xff;
}

static void sd_runtime_log_pins(const char *tag)
{
   unifrog_log("unifrog sd pins tag=%s gpl0c=0x%08lx gpl10=0x%08lx gpl14=0x%08lx "
          "pmL16=%02x pmL17=%02x pmL18=%02x pmL19=%02x pmL20=%02x "
          "pmL21=%02x pmL22=%02x in16=%d in17=%d in18=%d in19=%d "
          "in20=%d in21=%d in22=%d\n",
      tag ? tag : "",
      (unsigned long)REG32_READ((uint32_t)&GPIOLCTRL + 0x0c),
      (unsigned long)REG32_READ((uint32_t)&GPIOLCTRL + 0x10),
      (unsigned long)REG32_READ((uint32_t)&GPIOLCTRL + 0x14),
      (unsigned)sd_pinmux_l_byte(PINPAD_L16),
      (unsigned)sd_pinmux_l_byte(PINPAD_L17),
      (unsigned)sd_pinmux_l_byte(PINPAD_L18),
      (unsigned)sd_pinmux_l_byte(PINPAD_L19),
      (unsigned)sd_pinmux_l_byte(PINPAD_L20),
      (unsigned)sd_pinmux_l_byte(PINPAD_L21),
      (unsigned)sd_pinmux_l_byte(PINPAD_L22),
      (int)((REG32_READ((uint32_t)&GPIOLCTRL + 0x0cu) >> PINPAD_L16) & 1u),
      (int)((REG32_READ((uint32_t)&GPIOLCTRL + 0x0cu) >> PINPAD_L17) & 1u),
      (int)((REG32_READ((uint32_t)&GPIOLCTRL + 0x0cu) >> PINPAD_L18) & 1u),
      (int)((REG32_READ((uint32_t)&GPIOLCTRL + 0x0cu) >> PINPAD_L19) & 1u),
      (int)((REG32_READ((uint32_t)&GPIOLCTRL + 0x0cu) >> PINPAD_L20) & 1u),
      (int)((REG32_READ((uint32_t)&GPIOLCTRL + 0x0cu) >> PINPAD_L21) & 1u),
      (int)((REG32_READ((uint32_t)&GPIOLCTRL + 0x0cu) >> PINPAD_L22) & 1u));
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
   unifrog_perf_cache_flush((const void *)diag, sizeof(*diag));

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

static uint16_t sd_read_u16(uintptr_t base, size_t offset)
{
   uint16_t value;

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

static int sd_runtime_ptr_valid(uintptr_t ptr)
{
   return ptr >= 0x80000000u && ptr < 0x90000000u && (ptr & 3u) == 0;
}

int __real_dma_map_sg(void *dev, void *sglist, int nents, int direction);
void __real_dma_unmap_sg(void *dev, void *sglist, int nents, int direction);

/*
 * Source-owned bridge for the binary HC15xx host driver: it advertises
 * multi-SG DMA to libmmc but programs only one DMA address. After the SD host
 * is discovered, repair only that host by collapsing supported multi-SG
 * transfers into a single aligned buffer. Verbose request tracing stays gated
 * by explicit diagnostics.
 */
static uintptr_t sd_sg_virt(uintptr_t sg)
{
   return (sd_read_u32(sg, SD_SG_PAGE_LINK_OFFSET) & SD_SG_PAGE_MASK) +
      sd_read_u32(sg, SD_SG_OFFSET_OFFSET);
}

static uint32_t sd_mmc_bounce_collect(uintptr_t sglist, uint32_t nents)
{
   void *cur = (void *)sglist;
   uint32_t total = 0;

   for (uint32_t i = 0; i < nents; i++) {
      uintptr_t sg = (uintptr_t)cur;
      uint32_t length;
      uintptr_t virt;

      if (!sd_runtime_ptr_valid(sg))
         return 0;
      length = sd_read_u32(sg, SD_SG_LENGTH_OFFSET);
      virt = sd_sg_virt(sg);
      if (length == 0 || !sd_runtime_ptr_valid(virt) ||
          ((virt | length) & 31u))
         return 0;
      if (total > SD_MMC_BOUNCE_BYTES ||
          length > SD_MMC_BOUNCE_BYTES - total)
         return 0;
      sd_mmc_bounce.sg[i].addr = sg;
      sd_mmc_bounce.sg[i].page_link =
         sd_read_u32(sg, SD_SG_PAGE_LINK_OFFSET);
      sd_mmc_bounce.sg[i].offset = sd_read_u32(sg, SD_SG_OFFSET_OFFSET);
      sd_mmc_bounce.sg[i].length = length;
      sd_mmc_bounce.sg[i].dma_address =
         sd_read_u32(sg, SD_SG_DMA_ADDRESS_OFFSET);
      total += length;
      if (i + 1u < nents) {
         cur = sg_next(cur);
         if (!cur)
            return 0;
      }
   }

   return total;
}

static int sd_mmc_bounce_should_repair(void *dev)
{
   uintptr_t dev_addr = (uintptr_t)dev;

   if (sd_mmc_diag_enabled)
      return 1;
   if (!sd_runtime_boot.saved || !sd_runtime_boot.parent || !dev_addr)
      return 0;
   return dev_addr == sd_runtime_boot.parent;
}

static void sd_mmc_bounce_restore(uintptr_t sglist)
{
   (void)sglist;
   for (uint32_t i = 0; i < sd_mmc_bounce.nents; i++) {
      uintptr_t sg = sd_mmc_bounce.sg[i].addr;

      sd_write_u32(sg, SD_SG_PAGE_LINK_OFFSET,
         sd_mmc_bounce.sg[i].page_link);
      sd_write_u32(sg, SD_SG_OFFSET_OFFSET, sd_mmc_bounce.sg[i].offset);
      sd_write_u32(sg, SD_SG_LENGTH_OFFSET, sd_mmc_bounce.sg[i].length);
      sd_write_u32(sg, SD_SG_DMA_ADDRESS_OFFSET,
         sd_mmc_bounce.sg[i].dma_address);
   }
}

static void sd_mmc_bounce_clear(void)
{
   sd_mmc_bounce.active = 0;
   sd_mmc_bounce.dev = NULL;
   sd_mmc_bounce.sglist = 0;
   sd_mmc_bounce.nents = 0;
   sd_mmc_bounce.mapped_nents = 0;
   sd_mmc_bounce.direction = 0;
   sd_mmc_bounce.total = 0;
}

static void sd_mmc_bounce_abort(const char *tag)
{
   uintptr_t sglist = sd_mmc_bounce.sglist;

   if (!sd_mmc_bounce.active)
      return;

   unifrog_log("unifrog sd bounce_abort tag=%s sg=0x%08lx nents=%lu dir=%lu total=%lu\n",
      tag ? tag : "", (unsigned long)sglist,
      (unsigned long)sd_mmc_bounce.nents,
      (unsigned long)sd_mmc_bounce.direction,
      (unsigned long)sd_mmc_bounce.total);
   if (sd_runtime_ptr_valid(sglist))
      sd_mmc_bounce_restore(sglist);
   sd_mmc_bounce_clear();
   sd_mmc_bounce_abort_count++;
}

static void sd_mmc_bounce_copy_to_buffer(uintptr_t sglist)
{
   uint32_t offset = 0;

   (void)sglist;
   for (uint32_t i = 0; i < sd_mmc_bounce.nents; i++) {
      uint32_t length = sd_mmc_bounce.sg[i].length;
      uintptr_t virt =
         (sd_mmc_bounce.sg[i].page_link & SD_SG_PAGE_MASK) +
         sd_mmc_bounce.sg[i].offset;

      memcpy(sd_mmc_bounce_buffer + offset, (const void *)virt, length);
      offset += length;
   }
   unifrog_perf_cache_flush(sd_mmc_bounce_buffer, sd_mmc_bounce.total);
}

static void sd_mmc_bounce_copy_from_buffer(uintptr_t sglist)
{
   uint32_t offset = 0;

   (void)sglist;
   for (uint32_t i = 0; i < sd_mmc_bounce.nents; i++) {
      uint32_t length = sd_mmc_bounce.sg[i].length;
      uintptr_t virt =
         (sd_mmc_bounce.sg[i].page_link & SD_SG_PAGE_MASK) +
         sd_mmc_bounce.sg[i].offset;

      memcpy((void *)virt, sd_mmc_bounce_buffer + offset, length);
      unifrog_perf_cache_flush((const void *)virt, length);
      offset += length;
   }
}

static int __attribute__((noinline)) sd_mmc_bounce_map(void *dev,
   void *sglist, int nents, int direction)
{
   uintptr_t sglist_addr = (uintptr_t)sglist;
   uintptr_t first;
   uintptr_t buffer = (uintptr_t)sd_mmc_bounce_buffer;
   uint32_t total;
   int mapped;

   sd_mmc_bounce_last_nents = nents > 0 ? (uint32_t)nents : 0;
   sd_mmc_bounce_last_direction = direction > 0 ? (uint32_t)direction : 0;

   if (nents <= 1)
      return __real_dma_map_sg(dev, sglist, nents, direction);

   if (nents > (int)SD_MMC_BOUNCE_MAX_SEGS ||
       (direction != (int)SD_MMC_DMA_TO_DEVICE &&
        direction != (int)SD_MMC_DMA_FROM_DEVICE) ||
       !sd_runtime_ptr_valid(sglist_addr)) {
      sd_mmc_bounce_fallback_count++;
      return __real_dma_map_sg(dev, sglist, nents, direction);
   }
   if (sd_mmc_bounce.active) {
      sd_mmc_bounce_conflict_count++;
      sd_mmc_bounce_fallback_count++;
      return __real_dma_map_sg(dev, sglist, nents, direction);
   }

   total = sd_mmc_bounce_collect(sglist_addr, (uint32_t)nents);
   sd_mmc_bounce_last_total = total;
   if (total == 0) {
      sd_mmc_bounce_fallback_count++;
      return __real_dma_map_sg(dev, sglist, nents, direction);
   }
   sd_mmc_bounce.dev = dev;
   sd_mmc_bounce.sglist = sglist_addr;
   sd_mmc_bounce.nents = (uint32_t)nents;
   sd_mmc_bounce.mapped_nents = 1;
   sd_mmc_bounce.direction = (uint32_t)direction;
   sd_mmc_bounce.total = total;

   if (direction == (int)SD_MMC_DMA_TO_DEVICE)
      sd_mmc_bounce_copy_to_buffer(sglist_addr);
   else
      unifrog_perf_cache_invalidate(sd_mmc_bounce_buffer, total);

   first = sd_mmc_bounce.sg[0].addr;
   sd_write_u32(first, SD_SG_PAGE_LINK_OFFSET,
      (uint32_t)(buffer & SD_SG_PAGE_MASK) | SD_SG_END_FLAG);
   sd_write_u32(first, SD_SG_OFFSET_OFFSET, (uint32_t)(buffer & 0xfffu));
   sd_write_u32(first, SD_SG_LENGTH_OFFSET, total);
   sd_write_u32(first, SD_SG_DMA_ADDRESS_OFFSET, 0);

   mapped = __real_dma_map_sg(dev, sglist, 1, direction);
   if (mapped != 1) {
      sd_mmc_bounce_restore(sglist_addr);
      sd_mmc_bounce_clear();
      sd_mmc_bounce_fallback_count++;
      return mapped;
   }

   sd_mmc_bounce.active = 1;
   sd_mmc_bounce_map_count++;
   if (direction == (int)SD_MMC_DMA_TO_DEVICE)
      sd_mmc_bounce_write_count++;
   else
      sd_mmc_bounce_read_count++;

   return 1;
}

static void __attribute__((noinline)) sd_mmc_bounce_unmap(void *dev,
   void *sglist, int nents, int direction)
{
   uintptr_t sglist_addr = (uintptr_t)sglist;

   if (sd_mmc_bounce.active && sglist_addr == sd_mmc_bounce.sglist) {
      __real_dma_unmap_sg(dev, sglist,
         sd_mmc_bounce.mapped_nents ? (int)sd_mmc_bounce.mapped_nents : 1,
         (int)sd_mmc_bounce.direction);
      if (sd_mmc_bounce.direction == SD_MMC_DMA_FROM_DEVICE) {
         unifrog_perf_cache_invalidate(sd_mmc_bounce_buffer,
            sd_mmc_bounce.total);
         sd_mmc_bounce_copy_from_buffer(sglist_addr);
      }
      sd_mmc_bounce_restore(sglist_addr);
      sd_mmc_bounce_clear();
      return;
   }

   __real_dma_unmap_sg(dev, sglist, nents, direction);
}

int __wrap_dma_map_sg(void *dev, void *sglist, int nents, int direction)
{
   if (sd_mmc_bounce_should_repair(dev))
      return sd_mmc_bounce_map(dev, sglist, nents, direction);
   return __real_dma_map_sg(dev, sglist, nents, direction);
}

void __wrap_dma_unmap_sg(void *dev, void *sglist, int nents, int direction)
{
   if (sd_mmc_diag_enabled || sd_mmc_bounce.active)
      sd_mmc_bounce_unmap(dev, sglist, nents, direction);
   else
      __real_dma_unmap_sg(dev, sglist, nents, direction);
}

void __real_mmc_request_done(void *host, void *mrq);

static void __attribute__((noinline)) sd_mmc_request_done_trace(void *host,
   void *mrq)
{
   uintptr_t host_addr = (uintptr_t)host;
   uintptr_t mrq_addr = (uintptr_t)mrq;
   uintptr_t cmd;
   uintptr_t data;
   uintptr_t stop;
   uintptr_t data_stop = 0;
   uint32_t seq;
   struct sd_mmc_trace_entry *entry;

   cmd = sd_runtime_ptr_valid(mrq_addr) ?
      sd_read_ptr(mrq_addr, SD_MMC_REQUEST_CMD_OFFSET) : 0u;
   data = sd_runtime_ptr_valid(mrq_addr) ?
      sd_read_ptr(mrq_addr, SD_MMC_REQUEST_DATA_OFFSET) : 0u;
   stop = sd_runtime_ptr_valid(mrq_addr) ?
      sd_read_ptr(mrq_addr, SD_MMC_REQUEST_STOP_OFFSET) : 0u;
   seq = ++sd_mmc_trace_seq;
   sd_mmc_diag_request_count++;
   entry = &sd_mmc_trace[seq % SD_MMC_TRACE_COUNT];

   memset(entry, 0, sizeof(*entry));
   entry->seq = seq;
   entry->ts_ms = unifrog_perf_time_ms();
   entry->host = host_addr;
   entry->mrq = mrq_addr;
   entry->cmd = cmd;
   entry->data = data;
   if (host_addr) {
      entry->actual = sd_read_u32(host_addr,
         SD_MMC_HOST_ACTUAL_CLOCK_OFFSET);
      entry->ios_clock = sd_read_u32(host_addr, SD_MMC_HOST_IOS_OFFSET);
   }
   if (sd_runtime_ptr_valid(cmd)) {
      entry->opcode = sd_read_u32(cmd, SD_MMC_COMMAND_OPCODE_OFFSET);
      entry->arg = sd_read_u32(cmd, SD_MMC_COMMAND_ARG_OFFSET);
      entry->flags = sd_read_u32(cmd, SD_MMC_COMMAND_FLAGS_OFFSET);
      entry->retries = sd_read_u32(cmd, SD_MMC_COMMAND_RETRIES_OFFSET);
      entry->cmd_error = (int32_t)sd_read_u32(cmd,
         SD_MMC_COMMAND_ERROR_OFFSET);
      if (entry->cmd_error != 0) {
         sd_mmc_diag_cmd_error_count++;
         sd_mmc_diag_cmd_error_last_opcode = entry->opcode;
         sd_mmc_diag_cmd_error_last = entry->cmd_error;
      }
   }
   if (sd_runtime_ptr_valid(data)) {
      data_stop = sd_read_ptr(data, SD_MMC_DATA_STOP_OFFSET);
      if (!sd_runtime_ptr_valid(stop) && sd_runtime_ptr_valid(data_stop))
         stop = data_stop;
      entry->blksz = sd_read_u32(data, SD_MMC_DATA_BLKSZ_OFFSET);
      entry->blocks = sd_read_u32(data, SD_MMC_DATA_BLOCKS_OFFSET);
      entry->data_flags = sd_read_u32(data, SD_MMC_DATA_FLAGS_OFFSET);
      entry->bytes_xfered = sd_read_u32(data,
         SD_MMC_DATA_BYTES_XFERED_OFFSET);
      entry->data_error = (int32_t)sd_read_u32(data,
         SD_MMC_DATA_ERROR_OFFSET);
      if (entry->data_error != 0) {
         sd_mmc_diag_data_error_count++;
         sd_mmc_diag_data_error_last_opcode = entry->opcode;
         sd_mmc_diag_data_error_last = entry->data_error;
      }
      if (entry->blocks != 0 && entry->blksz != 0)
         entry->expected_bytes = entry->blocks * entry->blksz;
      entry->sg_len = sd_read_u32(data, SD_MMC_DATA_SG_LEN_OFFSET);
      entry->sg = sd_read_ptr(data, SD_MMC_DATA_SG_OFFSET);
      if (sd_runtime_ptr_valid(entry->sg)) {
         entry->sg0_page_link = sd_read_u32(entry->sg,
            SD_SG_PAGE_LINK_OFFSET);
         entry->sg0_offset = sd_read_u32(entry->sg, SD_SG_OFFSET_OFFSET);
         entry->sg0_length = sd_read_u32(entry->sg, SD_SG_LENGTH_OFFSET);
         entry->sg0_dma_address = sd_read_u32(entry->sg,
            SD_SG_DMA_ADDRESS_OFFSET);
      }
   }
   entry->stop = stop;
   entry->data_stop = data_stop;
   if (sd_runtime_ptr_valid(stop)) {
      entry->stop_opcode = sd_read_u32(stop, SD_MMC_COMMAND_OPCODE_OFFSET);
      entry->stop_error = (int32_t)sd_read_u32(stop,
         SD_MMC_COMMAND_ERROR_OFFSET);
      if (entry->stop_error != 0) {
         sd_mmc_diag_stop_error_count++;
         sd_mmc_diag_stop_error_last_opcode = entry->stop_opcode;
         sd_mmc_diag_stop_error_last = entry->stop_error;
      }
   }
   if (entry->expected_bytes != 0 &&
       entry->bytes_xfered < entry->expected_bytes) {
      int request_error = entry->cmd_error != 0 ||
         entry->data_error != 0 || entry->stop_error != 0;

      /*
       * The HCRTOS host can leave bytes_xfered at the current SG segment
       * size for successful multi-block DMA.  A zero-byte transfer or any
       * command/data/stop error is the failure signal we need to chase.
       */
      if (request_error || entry->bytes_xfered == 0) {
         sd_mmc_diag_short_count++;
         sd_mmc_diag_short_last_opcode = entry->opcode;
         sd_mmc_diag_short_last_blocks = entry->blocks;
         sd_mmc_diag_short_last_blksz = entry->blksz;
         sd_mmc_diag_short_last_bytes = entry->bytes_xfered;
         sd_mmc_diag_short_last_expected = entry->expected_bytes;
      } else {
         entry->partial_progress = 1;
         sd_mmc_diag_partial_count++;
         sd_mmc_diag_partial_last_opcode = entry->opcode;
         sd_mmc_diag_partial_last_blocks = entry->blocks;
         sd_mmc_diag_partial_last_blksz = entry->blksz;
         sd_mmc_diag_partial_last_bytes = entry->bytes_xfered;
         sd_mmc_diag_partial_last_expected = entry->expected_bytes;
      }
   }

   __real_mmc_request_done(host, mrq);
}

void __wrap_mmc_request_done(void *host, void *mrq)
{
   if (sd_mmc_diag_enabled) {
      sd_mmc_request_done_trace(host, mrq);
      return;
   }
   __real_mmc_request_done(host, mrq);
}

static int sd_runtime_mmio_valid(uintptr_t ptr)
{
   return ptr >= 0xb8000000u && ptr < 0xc0000000u && (ptr & 3u) == 0;
}

static int sd_runtime_host_enumerating(uintptr_t host)
{
   if (!host)
      return 0;
   return !sd_read_ptr(host, SD_MMC_HOST_CARD_OFFSET) &&
      (sd_read_u8(host, SD_MMC_HOST_CLAIMED_OFFSET) ||
       sd_read_u32(host, SD_MMC_HOST_CLAIM_COUNT_OFFSET));
}

static int sd_runtime_host_claimed(uintptr_t host)
{
   if (!host)
      return 0;
   return sd_read_u8(host, SD_MMC_HOST_CLAIMED_OFFSET) ||
      sd_read_u32(host, SD_MMC_HOST_CLAIM_COUNT_OFFSET);
}

static uintptr_t sd_runtime_current_task(void)
{
   return (uintptr_t)xTaskGetCurrentTaskHandle();
}

static const char *sd_runtime_task_name(uintptr_t task)
{
   if (!sd_runtime_ptr_valid(task))
      return "";
   return pcTaskGetName((TaskHandle_t)task);
}

static void sd_runtime_delay_ms(unsigned ms)
{
   uint32_t start;

   if (!ms)
      return;
   if (xTaskGetSchedulerState() != taskSCHEDULER_SUSPENDED) {
      msleep(ms);
      return;
   }

   start = unifrog_perf_time_ms();
   while ((uint32_t)(unifrog_perf_time_ms() - start) < ms)
      ;
}

static int sd_runtime_wait_unclaimed(const char *operation, const char *stage,
   uintptr_t host)
{
   uint32_t start_ms = unifrog_perf_time_ms();
   unsigned waits = 0;

   while (sd_runtime_host_claimed(host) &&
          (unifrog_perf_time_ms() - start_ms) < SD_RUNTIME_CLAIM_WAIT_MS) {
      if (waits == 0) {
         uintptr_t owner = sd_read_ptr(host, SD_MMC_HOST_CLAIMED_TASK_OFFSET);
         uintptr_t current = sd_runtime_current_task();

         unifrog_log("unifrog sd runtime wait_unclaimed begin operation=%s stage=%s "
                "claim=0x%02lx count=%lu owner=0x%08lx current=0x%08lx "
                "owner_current=%u owner_name=%s current_name=%s actual=%lu\n",
            operation ? operation : "",
            stage ? stage : "",
            (unsigned long)sd_read_u8(host, SD_MMC_HOST_CLAIMED_OFFSET),
            (unsigned long)sd_read_u32(host, SD_MMC_HOST_CLAIM_COUNT_OFFSET),
            (unsigned long)owner,
            (unsigned long)current,
            owner && owner == current ? 1u : 0u,
            sd_runtime_task_name(owner),
            sd_runtime_task_name(current),
            (unsigned long)sd_read_u32(host, SD_MMC_HOST_ACTUAL_CLOCK_OFFSET));
      }
      waits++;
      sd_runtime_delay_ms(SD_RUNTIME_CLAIM_WAIT_STEP_MS);
   }

   if (waits) {
      uintptr_t owner = sd_read_ptr(host, SD_MMC_HOST_CLAIMED_TASK_OFFSET);
      uintptr_t current = sd_runtime_current_task();

      unifrog_log("unifrog sd runtime wait_unclaimed done operation=%s stage=%s "
             "ret=%d waits=%u total_ms=%lu claim=0x%02lx count=%lu "
             "owner=0x%08lx current=0x%08lx owner_current=%u owner_name=%s "
             "current_name=%s actual=%lu\n",
         operation ? operation : "",
         stage ? stage : "",
         sd_runtime_host_claimed(host) ? -EBUSY : 0,
         waits,
         (unsigned long)(unifrog_perf_time_ms() - start_ms),
         (unsigned long)sd_read_u8(host, SD_MMC_HOST_CLAIMED_OFFSET),
         (unsigned long)sd_read_u32(host, SD_MMC_HOST_CLAIM_COUNT_OFFSET),
         (unsigned long)owner,
         (unsigned long)current,
         owner && owner == current ? 1u : 0u,
         sd_runtime_task_name(owner),
         sd_runtime_task_name(current),
         (unsigned long)sd_read_u32(host, SD_MMC_HOST_ACTUAL_CLOCK_OFFSET));
   }

   return sd_runtime_host_claimed(host) ? -EBUSY : 0;
}

static void sd_runtime_log_registers(const char *tag, uintptr_t host)
{
   uintptr_t iobase;
   uintptr_t pdata;
   uint32_t source_hz = 0;
   uint32_t decoded_hz = 0;
   uint32_t div;

   if (!host)
      return;

   iobase = sd_read_ptr(host, SD_HC_HOST_IOBASE_OFFSET);
   if (!sd_runtime_mmio_valid(iobase)) {
      unifrog_log("unifrog sd regs tag=%s host=0x%08lx iobase=0x%08lx invalid=1\n",
         tag ? tag : "", (unsigned long)host, (unsigned long)iobase);
      return;
   }

   div = (uint32_t)sd_mmio_read8(iobase, SD_HC_REG_CLKDIV_LO) |
      ((uint32_t)sd_mmio_read8(iobase, SD_HC_REG_CLKDIV_HI) << 8);
   pdata = sd_read_ptr(host, SD_HC_HOST_PDATA_OFFSET);
   if (sd_runtime_ptr_valid(pdata))
      source_hz = sd_read_u32(pdata, SD_HC_PDATA_BUS_HZ_OFFSET);
   if (!source_hz)
      source_hz = sd_runtime_boot.pdata_bus_hz;
   if (source_hz) {
      if (div > 1u) {
         uint32_t denom = (div - 1u) * 2u;

         decoded_hz = denom ? (source_hz + denom - 1u) / denom : 0u;
      } else {
         decoded_hz = source_hz;
      }
   }
   unifrog_log("unifrog sd regs tag=%s host=0x%08lx iobase=0x%08lx "
          "clkdiv=%lu src=%lu decoded=%lu clklo=0x%02lx clkhi=0x%02lx "
          "bus=0x%02lx timing=0x%02lx "
          "r00=0x%08lx r04=0x%08lx r08=0x%08lx r0c=0x%08lx "
          "r10=0x%08lx r14=0x%08lx r18=0x%08lx r1c=0x%08lx "
          "r20=0x%08lx r24=0x%08lx r28=0x%08lx r2c=0x%08lx "
          "r30=0x%08lx r34=0x%08lx r38=0x%08lx r3c=0x%08lx "
          "r40=0x%08lx r44=0x%08lx r48=0x%08lx r4c=0x%08lx r50=0x%08lx\n",
      tag ? tag : "",
      (unsigned long)host,
      (unsigned long)iobase,
      (unsigned long)div,
      (unsigned long)source_hz,
      (unsigned long)decoded_hz,
      (unsigned long)sd_mmio_read8(iobase, SD_HC_REG_CLKDIV_LO),
      (unsigned long)sd_mmio_read8(iobase, SD_HC_REG_CLKDIV_HI),
      (unsigned long)sd_mmio_read8(iobase, SD_HC_REG_BUS_WIDTH),
      (unsigned long)sd_mmio_read8(iobase, SD_HC_REG_TIMING),
      (unsigned long)sd_mmio_read32(iobase, 0x00u),
      (unsigned long)sd_mmio_read32(iobase, 0x04u),
      (unsigned long)sd_mmio_read32(iobase, 0x08u),
      (unsigned long)sd_mmio_read32(iobase, 0x0cu),
      (unsigned long)sd_mmio_read32(iobase, 0x10u),
      (unsigned long)sd_mmio_read32(iobase, 0x14u),
      (unsigned long)sd_mmio_read32(iobase, 0x18u),
      (unsigned long)sd_mmio_read32(iobase, 0x1cu),
      (unsigned long)sd_mmio_read32(iobase, 0x20u),
      (unsigned long)sd_mmio_read32(iobase, 0x24u),
      (unsigned long)sd_mmio_read32(iobase, 0x28u),
      (unsigned long)sd_mmio_read32(iobase, 0x2cu),
      (unsigned long)sd_mmio_read32(iobase, 0x30u),
      (unsigned long)sd_mmio_read32(iobase, 0x34u),
      (unsigned long)sd_mmio_read32(iobase, 0x38u),
      (unsigned long)sd_mmio_read32(iobase, 0x3cu),
      (unsigned long)sd_mmio_read32(iobase, 0x40u),
      (unsigned long)sd_mmio_read32(iobase, 0x44u),
      (unsigned long)sd_mmio_read32(iobase, 0x48u),
      (unsigned long)sd_mmio_read32(iobase, 0x4cu),
      (unsigned long)sd_mmio_read32(iobase, 0x50u));
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

int unifrog_platform_sd_profile_allowed(const char *profile)
{
   return sd_runtime_profile_by_name(profile) ? 1 : 0;
}

const char *unifrog_platform_sd_active_profile(void)
{
   if (sd_runtime_boot.saved &&
       strcmp(sd_runtime_boot.active_profile, "boot") != 0)
      return sd_runtime_boot.active_profile;
   return UNIFROG_SD_MODE;
}

int unifrog_platform_sd_describe(char *detail, size_t detail_size)
{
   uintptr_t host;
   uintptr_t pdata;
   uintptr_t owner;
   uintptr_t current;
   uintptr_t mrq;
   uintptr_t cmd;
   uint32_t ios_clock;

   if (!detail || detail_size == 0)
      return -1;
   detail[0] = '\0';
   host = sd_runtime_find_host();
   if (!host) {
      snprintf(detail, detail_size,
         "active=%s build=%s host=missing", unifrog_platform_sd_active_profile(),
         UNIFROG_SD_MODE);
      return -1;
   }

   pdata = sd_read_ptr(host, SD_HC_HOST_PDATA_OFFSET);
   owner = sd_read_ptr(host, SD_MMC_HOST_CLAIMED_TASK_OFFSET);
   current = sd_runtime_current_task();
   mrq = sd_read_ptr(host, SD_HC_HOST_CURRENT_MRQ_OFFSET);
   cmd = sd_read_ptr(host, SD_HC_HOST_CURRENT_CMD_OFFSET);
   ios_clock = sd_read_u32(host, SD_MMC_HOST_IOS_OFFSET);
   snprintf(detail, detail_size,
      "active=%s build=%s fmax=%lu actual=%lu ios_clock=%lu "
      "bus_width=%u timing=%u signal=%u caps=%08lx caps2=%08lx "
      "pm_caps=%08lx max_seg=%lu max_segs=%lu max_req=%lu "
      "max_blk=%lu max_blks=%lu pdata_busw=%lu pdata_flags=%08lx "
      "pdata_delay=%lu pdata_bus=%lu pdata_max=%lu pdata_caps=%08lx "
      "claim=0x%02lx count=%lu owner=0x%08lx current=0x%08lx "
      "owner_current=%u owner_name=%s current_name=%s mrq=0x%08lx "
      "cmd=0x%08lx",
      unifrog_platform_sd_active_profile(),
      UNIFROG_SD_MODE,
      (unsigned long)sd_read_u32(host, SD_MMC_HOST_F_MAX_OFFSET),
      (unsigned long)sd_read_u32(host, SD_MMC_HOST_ACTUAL_CLOCK_OFFSET),
      (unsigned long)ios_clock,
      (unsigned)sd_read_u8(host, SD_MMC_HOST_IOS_OFFSET + 9u),
      (unsigned)sd_read_u8(host, SD_MMC_HOST_IOS_OFFSET + 10u),
      (unsigned)sd_read_u8(host, SD_MMC_HOST_IOS_OFFSET + 11u),
      (unsigned long)sd_read_u32(host, SD_MMC_HOST_CAPS_OFFSET),
      (unsigned long)sd_read_u32(host, SD_MMC_HOST_CAPS2_OFFSET),
      (unsigned long)sd_read_u32(host, SD_MMC_HOST_PM_CAPS_OFFSET),
      (unsigned long)sd_read_u32(host, SD_MMC_HOST_MAX_SEG_SIZE_OFFSET),
      (unsigned long)sd_read_u16(host, SD_MMC_HOST_MAX_SEGS_OFFSET),
      (unsigned long)sd_read_u32(host, SD_MMC_HOST_MAX_REQ_SIZE_OFFSET),
      (unsigned long)sd_read_u32(host, SD_MMC_HOST_MAX_BLK_SIZE_OFFSET),
      (unsigned long)sd_read_u32(host, SD_MMC_HOST_MAX_BLK_COUNT_OFFSET),
      (unsigned long)(pdata ? sd_read_u32(pdata,
         SD_HC_PDATA_BUS_WIDTH_OFFSET) : 0u),
      (unsigned long)(pdata ? sd_read_u32(pdata,
         SD_HC_PDATA_FLAGS_OFFSET) : 0u),
      (unsigned long)(pdata ? sd_read_u32(pdata,
         SD_HC_PDATA_CARD_DETECT_DELAY_OFFSET) : 0u),
      (unsigned long)(pdata ? sd_read_u32(pdata,
         SD_HC_PDATA_BUS_HZ_OFFSET) : 0u),
      (unsigned long)(pdata ? sd_read_u32(pdata,
         SD_HC_PDATA_MAX_FREQUENCY_OFFSET) : 0u),
      (unsigned long)(pdata ? sd_read_u32(pdata,
         SD_HC_PDATA_CAPS_OFFSET) : 0u),
      (unsigned long)sd_read_u8(host, SD_MMC_HOST_CLAIMED_OFFSET),
      (unsigned long)sd_read_u32(host, SD_MMC_HOST_CLAIM_COUNT_OFFSET),
      (unsigned long)owner,
      (unsigned long)current,
      owner && owner == current ? 1u : 0u,
      sd_runtime_task_name(owner),
      sd_runtime_task_name(current),
      (unsigned long)mrq,
      (unsigned long)cmd);
   return 0;
}

static int sd_runtime_host_valid(uintptr_t host, struct device *dev)
{
   const struct hc_mmc_host_hw_ops *ops;

   if (!host || (host & 3u) != 0)
      return 0;
   if (sd_read_ptr(host, SD_MMC_HOST_PARENT_OFFSET) != (uintptr_t)dev)
      return 0;
   ops = (const struct hc_mmc_host_hw_ops *)
      sd_read_ptr(host, SD_HC_HOST_OPS_OFFSET);
   if (!src_hc_mmc_host_ops_known(ops))
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

static int sd_runtime_fdt_has_broken_cd(void)
{
   int node = fdt_get_node_offset_by_path("/hcrtos/mmc");

   if (node < 0)
      node = fdt_get_node_offset_by_path("/mmc");
   return node >= 0 && fdt_property_read_bool(node, "broken-cd");
}

static void sd_runtime_save_boot(uintptr_t host)
{
   uintptr_t pdata;

   if (sd_runtime_boot.saved)
      return;

   pdata = sd_read_ptr(host, SD_HC_HOST_PDATA_OFFSET);
   sd_runtime_boot.host = host;
   sd_runtime_boot.parent = sd_read_ptr(host, SD_MMC_HOST_PARENT_OFFSET);
   sd_runtime_boot.pdata = pdata;
   sd_runtime_boot.f_max = sd_read_u32(host, SD_MMC_HOST_F_MAX_OFFSET);
   sd_runtime_boot.caps = sd_read_u32(host, SD_MMC_HOST_CAPS_OFFSET);
   sd_runtime_boot.caps2 = sd_read_u32(host, SD_MMC_HOST_CAPS2_OFFSET);
   sd_runtime_boot.pm_caps = sd_read_u32(host, SD_MMC_HOST_PM_CAPS_OFFSET);
   if (pdata) {
      uint32_t pdata_flags = sd_read_u32(pdata, SD_HC_PDATA_FLAGS_OFFSET);

      if ((pdata_flags & SD_HC_PDATA_FLAG_BROKEN_CD) == 0 &&
          sd_runtime_fdt_has_broken_cd()) {
         pdata_flags |= SD_HC_PDATA_FLAG_BROKEN_CD;
         sd_write_u32(pdata, SD_HC_PDATA_FLAGS_OFFSET, pdata_flags);
         sd_runtime_boot.forced_broken_cd = 1;
      }
      sd_runtime_boot.pdata_bus_width =
         sd_read_u32(pdata, SD_HC_PDATA_BUS_WIDTH_OFFSET);
      sd_runtime_boot.pdata_flags =
         sd_read_u32(pdata, SD_HC_PDATA_FLAGS_OFFSET);
      sd_runtime_boot.pdata_bus_hz =
         sd_read_u32(pdata, SD_HC_PDATA_BUS_HZ_OFFSET);
      sd_runtime_boot.pdata_card_detect_delay =
         sd_read_u32(pdata, SD_HC_PDATA_CARD_DETECT_DELAY_OFFSET);
      sd_runtime_boot.pdata_max_frequency =
         sd_read_u32(pdata, SD_HC_PDATA_MAX_FREQUENCY_OFFSET);
      sd_runtime_boot.pdata_caps =
         sd_read_u32(pdata, SD_HC_PDATA_CAPS_OFFSET);
   }
   snprintf(sd_runtime_boot.active_profile,
      sizeof(sd_runtime_boot.active_profile), "boot");
   sd_runtime_boot.saved = 1;

   unifrog_log("unifrog sd runtime boot host=0x%08lx parent=0x%08lx pdata=0x%08lx fmax=%lu "
          "caps=0x%08lx caps2=0x%08lx pdata_busw=%lu "
          "pdata_flags=0x%08lx pdata_delay=%lu pdata_bus=%lu "
          "pdata_max=%lu pdata_caps=0x%08lx broken_cd=%lu forced_cd=%lu "
          "max_seg=%lu max_segs=%lu max_req=%lu "
          "max_blk=%lu max_blks=%lu mmc_diag=%lu req=%lu short=%lu "
          "partial=%lu last=%lu/%lu/%lu/%lu/%lu "
          "partial_last=%lu/%lu/%lu/%lu/%lu "
          "mode=%s\n",
      (unsigned long)host,
      (unsigned long)sd_runtime_boot.parent,
      (unsigned long)pdata,
      (unsigned long)sd_runtime_boot.f_max,
      (unsigned long)sd_runtime_boot.caps,
      (unsigned long)sd_runtime_boot.caps2,
      (unsigned long)sd_runtime_boot.pdata_bus_width,
      (unsigned long)sd_runtime_boot.pdata_flags,
      (unsigned long)sd_runtime_boot.pdata_card_detect_delay,
      (unsigned long)sd_runtime_boot.pdata_bus_hz,
      (unsigned long)sd_runtime_boot.pdata_max_frequency,
      (unsigned long)sd_runtime_boot.pdata_caps,
      (unsigned long)((sd_runtime_boot.pdata_flags &
         SD_HC_PDATA_FLAG_BROKEN_CD) ? 1u : 0u),
      (unsigned long)sd_runtime_boot.forced_broken_cd,
      (unsigned long)sd_read_u32(host, SD_MMC_HOST_MAX_SEG_SIZE_OFFSET),
      (unsigned long)sd_read_u16(host, SD_MMC_HOST_MAX_SEGS_OFFSET),
      (unsigned long)sd_read_u32(host, SD_MMC_HOST_MAX_REQ_SIZE_OFFSET),
      (unsigned long)sd_read_u32(host, SD_MMC_HOST_MAX_BLK_SIZE_OFFSET),
      (unsigned long)sd_read_u32(host, SD_MMC_HOST_MAX_BLK_COUNT_OFFSET),
      (unsigned long)sd_mmc_diag_enabled,
      (unsigned long)sd_mmc_diag_request_count,
      (unsigned long)sd_mmc_diag_short_count,
      (unsigned long)sd_mmc_diag_partial_count,
      (unsigned long)sd_mmc_diag_short_last_opcode,
      (unsigned long)sd_mmc_diag_short_last_blocks,
      (unsigned long)sd_mmc_diag_short_last_blksz,
      (unsigned long)sd_mmc_diag_short_last_bytes,
      (unsigned long)sd_mmc_diag_short_last_expected,
      (unsigned long)sd_mmc_diag_partial_last_opcode,
      (unsigned long)sd_mmc_diag_partial_last_blocks,
      (unsigned long)sd_mmc_diag_partial_last_blksz,
      (unsigned long)sd_mmc_diag_partial_last_bytes,
      (unsigned long)sd_mmc_diag_partial_last_expected,
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
   int written;

   if (!detail || detail_size == 0)
      return;
   written = snprintf(detail, detail_size,
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
   if (sd_runtime_last_switch.hs_attempted &&
       written > 0 && (size_t)written < detail_size) {
      snprintf(detail + written, detail_size - (size_t)written,
         " hs=%d hsclk=%lu>%lu hst=%lu>%lu hsmax=%lu",
         sd_runtime_last_switch.hs_ret,
         (unsigned long)sd_runtime_last_switch.hs_before_actual,
         (unsigned long)sd_runtime_last_switch.hs_after_actual,
         (unsigned long)sd_runtime_last_switch.hs_before_timing,
         (unsigned long)sd_runtime_last_switch.hs_after_timing,
         (unsigned long)sd_runtime_last_switch.hs_max_clock);
   }
}

static int storage_sanity_join_path(char *out, size_t out_size,
   const char *target, const char *rel)
{
   int written;

   if (!out || out_size == 0 || !target || !target[0] || !rel || !rel[0])
      return -1;
   written = snprintf(out, out_size, "%s/%s", target, rel);
   return written > 0 && (size_t)written < out_size ? 0 : -1;
}

static int storage_sanity_read_path(const char *tag, const char *path)
{
   unsigned char buf[512];
   unsigned long checksum = 5381u;
   size_t got;
   FILE *file;
   int open_errno;

   errno = 0;
   file = fopen(path, "rb");
   open_errno = errno;
   if (!file) {
      if (open_errno != ENOENT)
         unifrog_log("unifrog storage sanity tag=%s path=%s ret=-1 errno=%d stage=open\n",
            tag ? tag : "", path, open_errno);
      return -1;
   }

   errno = 0;
   got = fread(buf, 1, sizeof(buf), file);
   open_errno = errno;
   for (size_t i = 0; i < got; i++)
      checksum = ((checksum << 5) + checksum) ^ buf[i];
   fclose(file);

   if (got == 0) {
      unifrog_log("unifrog storage sanity tag=%s path=%s ret=-1 errno=%d stage=read got=0\n",
         tag ? tag : "", path, open_errno);
      return -1;
   }

   unifrog_log("unifrog storage sanity tag=%s path=%s ret=0 bytes=%lu checksum=0x%08lx\n",
      tag ? tag : "", path, (unsigned long)got, checksum);
   return 0;
}

static int storage_sanity_read_target(const char *tag, const char *target,
   unsigned required_passes)
{
   static const char *const rels[] = {
      "unifrog/manifest.ini",
      "unifrog/firmware/unifrog.bin",
      "unifrog/unifrog.ini",
      "unifrog/bytecode-manifest.txt",
      "firmware/unifrog.bin",
      "bios/bisrv.asd",
      "bisrv.asd",
   };
   unsigned pass = 0;
   unsigned fail = 0;
   char path[288];

   if (!required_passes)
      required_passes = 1;
   if (!target || !target[0])
      return -1;

   for (unsigned i = 0; i < sizeof(rels) / sizeof(rels[0]); i++) {
      if (storage_sanity_join_path(path, sizeof(path), target, rels[i]) != 0) {
         fail++;
         continue;
      }
      if (storage_sanity_read_path(tag, path) == 0)
         pass++;
      else
         fail++;
      if (pass >= required_passes)
         break;
   }

   unifrog_log("unifrog storage sanity tag=%s target=%s pass=%lu fail=%lu need=%lu\n",
      tag ? tag : "", target, (unsigned long)pass, (unsigned long)fail,
      (unsigned long)required_passes);
   return pass >= required_passes ? 0 : -1;
}

static int sd_runtime_sanity_check(const char *profile)
{
   int ret = storage_sanity_read_target(profile, UNIFROG_SD_ROOT, 2);

   unifrog_log_sync("sd_profile sanity profile=%s ret=%d",
      profile ? profile : "", ret);
   return ret;
}

static int sd_runtime_unmount_storage(const char *tag)
{
   unsigned mounted_mask = storage_mounted_mask;
   int ret = 0;

   for (int i = (int)(sizeof(storage_mounts) / sizeof(storage_mounts[0])) - 1;
        i >= 0; i--) {
      int unmount_ret;
      int unmount_errno;
      char stage[48];

      snprintf(stage, sizeof(stage), "unmount %s",
         storage_mounts[i].target);
      if (mounted_mask && !(mounted_mask & (1u << i))) {
         sd_runtime_stage(tag, stage, 1);
         unifrog_log("unifrog sd runtime unmount tag=%s target=%s skipped=1 "
                "reason=not_mounted mask=0x%lx\n",
            tag ? tag : "",
            storage_mounts[i].target,
            (unsigned long)storage_mounted_mask);
         continue;
      }
      sd_runtime_stage(tag, stage, 0);
      errno = 0;
      unmount_ret = umount2(storage_mounts[i].target, MNT_FORCE);
      unmount_errno = errno;

      if (unmount_ret == 0 ||
          (unmount_ret != 0 && unmount_errno == EINVAL) ||
          (unmount_ret != 0 && unmount_errno == ENOENT)) {
         storage_mounted_mask &= ~(1u << i);
         storage_update_log_disk_available();
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

static void sd_runtime_profile_target(const struct sd_runtime_profile *profile,
   uint32_t *f_max_out, uint32_t *caps_out, uint32_t *caps2_out)
{
   uint32_t f_max = sd_runtime_boot.f_max;
   uint32_t caps = sd_runtime_boot.caps;
   uint32_t caps2 = sd_runtime_boot.caps2;

   if (profile) {
      f_max = profile->f_max;
      caps &= ~profile->clear_caps;
      caps |= profile->set_caps;
      caps2 &= ~profile->clear_caps2;
      caps2 |= profile->set_caps2;
   }

   if (f_max_out)
      *f_max_out = f_max;
   if (caps_out)
      *caps_out = caps;
   if (caps2_out)
      *caps2_out = caps2;
}

static void sd_runtime_write_target(uintptr_t host,
   const struct sd_runtime_profile *profile, int clear_ios)
{
   uintptr_t pdata = sd_read_ptr(host, SD_HC_HOST_PDATA_OFFSET);
   uint32_t f_max;
   uint32_t caps;
   uint32_t caps2;

   sd_runtime_profile_target(profile, &f_max, &caps, &caps2);

   sd_write_u32(host, SD_MMC_HOST_F_MAX_OFFSET, f_max);
   sd_write_u32(host, SD_MMC_HOST_CAPS_OFFSET, caps);
   sd_write_u32(host, SD_MMC_HOST_CAPS2_OFFSET, caps2);
   sd_write_u32(host, SD_MMC_HOST_PM_CAPS_OFFSET, sd_runtime_boot.pm_caps);
   if (clear_ios)
      sd_runtime_clear_ios(host);

   if (pdata) {
      sd_write_u32(pdata, SD_HC_PDATA_BUS_WIDTH_OFFSET,
         sd_runtime_boot.pdata_bus_width);
      sd_write_u32(pdata, SD_HC_PDATA_FLAGS_OFFSET,
         sd_runtime_boot.pdata_flags);
      sd_write_u32(pdata, SD_HC_PDATA_CARD_DETECT_DELAY_OFFSET,
         sd_runtime_boot.pdata_card_detect_delay);
      if (!profile) {
         sd_write_u32(pdata, SD_HC_PDATA_BUS_HZ_OFFSET,
            sd_runtime_boot.pdata_bus_hz);
         sd_write_u32(pdata, SD_HC_PDATA_MAX_FREQUENCY_OFFSET,
            sd_runtime_boot.pdata_max_frequency);
         sd_write_u32(pdata, SD_HC_PDATA_CAPS_OFFSET,
            sd_runtime_boot.pdata_caps);
      } else {
         sd_write_u32(pdata, SD_HC_PDATA_MAX_FREQUENCY_OFFSET, f_max);
         sd_write_u32(pdata, SD_HC_PDATA_CAPS_OFFSET, caps);
      }
   }
}

static void sd_runtime_write_profile(uintptr_t host,
   const struct sd_runtime_profile *profile)
{
   sd_runtime_write_target(host, profile, 1);
}

static void sd_runtime_write_boot(uintptr_t host)
{
   sd_runtime_write_target(host, NULL, 1);
}

static int sd_runtime_caps_direct_legacy(uint32_t caps)
{
   return (caps & SD_MMC_CAP_DIRECT_LEGACY_FORBIDDEN) == 0;
}

static int sd_runtime_reconfigure_legacy_direct(const char *operation,
   uintptr_t host, const struct sd_runtime_profile *profile)
{
   const char *target = profile ? profile->name : "boot";
   uintptr_t card;
   uint32_t target_f_max;
   uint32_t target_caps;
   uint32_t target_caps2;
   uint32_t current_caps;
   uint32_t current_timing;
   uint32_t current_actual;
   uint32_t current_ios_clock;
   uint32_t current_width;
   uint32_t target_width;
   int ret = 0;

   if (!host || !sd_runtime_boot.saved)
      return -EOPNOTSUPP;

   sd_runtime_profile_target(profile, &target_f_max, &target_caps,
      &target_caps2);
   if (!sd_runtime_caps_direct_legacy(target_caps))
      return -EOPNOTSUPP;

   card = sd_read_ptr(host, SD_MMC_HOST_CARD_OFFSET);
   current_caps = sd_read_u32(host, SD_MMC_HOST_CAPS_OFFSET);
   current_timing = sd_read_u8(host, SD_MMC_HOST_IOS_OFFSET + 10u);
   current_actual = sd_read_u32(host, SD_MMC_HOST_ACTUAL_CLOCK_OFFSET);
   current_ios_clock = sd_read_u32(host, SD_MMC_HOST_IOS_OFFSET);
   current_width = sd_read_u8(host, SD_MMC_HOST_IOS_OFFSET + 9u);
   if (!card || !current_actual || !current_ios_clock ||
       current_timing != SD_MMC_TIMING_LEGACY ||
       !sd_runtime_caps_direct_legacy(current_caps))
      return -EOPNOTSUPP;

   target_width = (target_caps & SD_MMC_CAP_4_BIT_DATA) ?
      SD_MMC_BUS_WIDTH_4 : SD_MMC_BUS_WIDTH_1;

   /*
    * Reverse engineering shows mmc_sd_suspend powers the card off, and
    * mmc_sd_resume then runs the full mmc_sd_init_card path whenever card
    * state bit 0x40 is set.  The 0253 logs stopped inside that resume path
    * while switching from wide25 to safe.  For legacy 1-bit/4-bit profiles we
    * can avoid that risky power-cycle: ACMD6 plus host set_ios is all the
    * vendor init path uses for the same bus-width change.
    */
   unifrog_log_sync("sd_profile direct begin operation=%s target=%s host=0x%08lx card=0x%08lx width=%lu>%lu clock=%lu>%lu caps=0x%08lx>0x%08lx caps2=0x%08lx>0x%08lx",
      operation ? operation : "", target, (unsigned long)host,
      (unsigned long)card, (unsigned long)current_width,
      (unsigned long)target_width, (unsigned long)current_actual,
      (unsigned long)target_f_max, (unsigned long)current_caps,
      (unsigned long)target_caps,
      (unsigned long)sd_read_u32(host, SD_MMC_HOST_CAPS2_OFFSET),
      (unsigned long)target_caps2);
   sd_runtime_stage(operation, "legacy direct begin", 0);
   sd_runtime_log_registers("legacy_direct_begin", host);

   ret = sd_runtime_wait_unclaimed(operation, "legacy direct wait", host);
   if (ret != 0) {
      sd_runtime_stage(operation, "legacy direct wait failed", ret);
      return ret;
   }

   __mmc_claim_host((void *)host, NULL);
   sd_runtime_write_target(host, profile, 0);
   mmc_set_timing((void *)host, SD_MMC_TIMING_LEGACY);
   if (target_width == SD_MMC_BUS_WIDTH_1)
      mmc_set_clock((void *)host, target_f_max);
   if (current_width != target_width) {
      ret = mmc_app_set_bus_width((void *)card, target_width);
      if (ret == 0)
         mmc_set_bus_width((void *)host, target_width);
   }
   if (ret == 0) {
      mmc_set_clock((void *)host, target_f_max);
   }
   mmc_release_host((void *)host);

   sd_runtime_delay_ms(50);
   sd_runtime_log_registers("legacy_direct_done", host);
   sd_runtime_stage(operation, "legacy direct done", ret);
   unifrog_log_sync("sd_profile direct done operation=%s target=%s ret=%d actual=%lu ios_clock=%lu width=%lu timing=%lu caps=0x%08lx caps2=0x%08lx",
      operation ? operation : "", target, ret,
      (unsigned long)sd_read_u32(host, SD_MMC_HOST_ACTUAL_CLOCK_OFFSET),
      (unsigned long)sd_read_u32(host, SD_MMC_HOST_IOS_OFFSET),
      (unsigned long)sd_read_u8(host, SD_MMC_HOST_IOS_OFFSET + 9u),
      (unsigned long)sd_read_u8(host, SD_MMC_HOST_IOS_OFFSET + 10u),
      (unsigned long)sd_read_u32(host, SD_MMC_HOST_CAPS_OFFSET),
      (unsigned long)sd_read_u32(host, SD_MMC_HOST_CAPS2_OFFSET));
   return ret;
}

static int sd_runtime_profile_needs_sd_hs(
   const struct sd_runtime_profile *profile)
{
   if (!profile)
      return 0;
   if ((profile->set_caps & SD_MMC_CAP_SD_HIGHSPEED) == 0)
      return 0;
   if (profile->set_caps &
       (SD_MMC_CAP_UHS_SDR12 | SD_MMC_CAP_UHS_SDR25 |
        SD_MMC_CAP_UHS_SDR50 | SD_MMC_CAP_UHS_SDR104 |
        SD_MMC_CAP_UHS_DDR50))
      return 0;
   return 1;
}

static int sd_runtime_reconfigure_sd_hs_direct(const char *operation,
   uintptr_t host, const struct sd_runtime_profile *profile)
{
   const char *target = profile ? profile->name : "";
   uintptr_t card;
   uint32_t target_f_max;
   uint32_t target_caps;
   uint32_t target_caps2;
   uint32_t current_actual;
   uint32_t current_ios_clock;
   uint32_t current_width;
   uint32_t before_timing;
   uint32_t target_width;
   unsigned int max_clock;
   int switch_ret;
   int width_ret = 0;
   int ret;

   if (!host || !sd_runtime_boot.saved ||
       !sd_runtime_profile_needs_sd_hs(profile))
      return -EOPNOTSUPP;

   card = sd_read_ptr(host, SD_MMC_HOST_CARD_OFFSET);
   current_actual = sd_read_u32(host, SD_MMC_HOST_ACTUAL_CLOCK_OFFSET);
   current_ios_clock = sd_read_u32(host, SD_MMC_HOST_IOS_OFFSET);
   current_width = sd_read_u8(host, SD_MMC_HOST_IOS_OFFSET + 9u);
   before_timing = sd_read_u8(host, SD_MMC_HOST_IOS_OFFSET + 10u);
   if (!card || !current_actual || !current_ios_clock)
      return -EOPNOTSUPP;

   sd_runtime_profile_target(profile, &target_f_max, &target_caps,
      &target_caps2);
   target_width = (target_caps & SD_MMC_CAP_4_BIT_DATA) ?
      SD_MMC_BUS_WIDTH_4 : SD_MMC_BUS_WIDTH_1;

   /*
    * 0263 showed wide50/wide losing the card during vendor suspend/resume:
    * actual clock collapsed and bus ops/card temporarily disappeared.  The
    * original init path only does CMD6 high-speed switch, host timing/clock,
    * and optional ACMD6 bus width for non-UHS SD high-speed, so keep that
    * transition in source and leave the heavier path for UHS experiments.
    */
   unifrog_log_sync("sd_profile hs_direct begin operation=%s target=%s host=0x%08lx card=0x%08lx width=%lu>%lu clock=%lu>%lu caps=0x%08lx caps2=0x%08lx",
      operation ? operation : "", target, (unsigned long)host,
      (unsigned long)card, (unsigned long)current_width,
      (unsigned long)target_width, (unsigned long)current_actual,
      (unsigned long)target_f_max, (unsigned long)target_caps,
      (unsigned long)target_caps2);
   sd_runtime_stage(operation, "sd high-speed direct begin", 0);
   sd_runtime_log_registers("hs_direct_begin", host);

   ret = sd_runtime_wait_unclaimed(operation, "sd high-speed direct wait",
      host);
   if (ret != 0) {
      sd_runtime_stage(operation, "sd high-speed direct wait failed", ret);
      return ret;
   }

   sd_runtime_last_switch.hs_attempted = 1;
   sd_runtime_last_switch.hs_ret = -EIO;
   sd_runtime_last_switch.hs_max_clock = 0;
   sd_runtime_last_switch.hs_before_actual = current_actual;
   sd_runtime_last_switch.hs_after_actual = current_actual;
   sd_runtime_last_switch.hs_before_timing = before_timing;
   sd_runtime_last_switch.hs_after_timing = before_timing;

   __mmc_claim_host((void *)host, NULL);
   sd_runtime_write_target(host, profile, 0);
   if (target_width == SD_MMC_BUS_WIDTH_1 && current_width != target_width) {
      width_ret = mmc_app_set_bus_width((void *)card, target_width);
      if (width_ret == 0)
         mmc_set_bus_width((void *)host, target_width);
   }
   switch_ret = mmc_sd_switch_hs((void *)card);
   if (switch_ret > 0)
      mmc_set_timing((void *)host, SD_MMC_TIMING_SD_HS);
   max_clock = mmc_sd_get_max_clock((void *)card);
   if (max_clock == 0 || max_clock > target_f_max)
      max_clock = target_f_max;
   if (target_width == SD_MMC_BUS_WIDTH_4 && current_width != target_width) {
      width_ret = mmc_app_set_bus_width((void *)card, target_width);
      if (width_ret == 0)
         mmc_set_bus_width((void *)host, target_width);
   }
   if (switch_ret >= 0 && width_ret == 0)
      mmc_set_clock((void *)host, max_clock);
   mmc_release_host((void *)host);

   sd_runtime_delay_ms(50);
   sd_runtime_last_switch.hs_ret = switch_ret < 0 ? switch_ret : width_ret;
   sd_runtime_last_switch.hs_max_clock = max_clock;
   sd_runtime_last_switch.hs_after_actual =
      sd_read_u32(host, SD_MMC_HOST_ACTUAL_CLOCK_OFFSET);
   sd_runtime_last_switch.hs_after_timing =
      sd_read_u8(host, SD_MMC_HOST_IOS_OFFSET + 10u);

   if (switch_ret < 0)
      ret = switch_ret;
   else if (width_ret != 0)
      ret = width_ret;
   else if (sd_runtime_last_switch.hs_after_actual == 0 ||
            sd_read_u32(host, SD_MMC_HOST_IOS_OFFSET) == 0)
      ret = -EAGAIN;
   else
      ret = 0;

   sd_runtime_log_registers("hs_direct_done", host);
   sd_runtime_stage(operation, "sd high-speed direct done", ret);
   unifrog_log_sync("sd_profile hs_direct done operation=%s target=%s ret=%d switch_ret=%d width_ret=%d requested=%lu max=%lu actual=%lu ios_clock=%lu width=%lu timing=%lu",
      operation ? operation : "", target, ret, switch_ret, width_ret,
      (unsigned long)target_f_max, (unsigned long)max_clock,
      (unsigned long)sd_runtime_last_switch.hs_after_actual,
      (unsigned long)sd_read_u32(host, SD_MMC_HOST_IOS_OFFSET),
      (unsigned long)sd_read_u8(host, SD_MMC_HOST_IOS_OFFSET + 9u),
      (unsigned long)sd_runtime_last_switch.hs_after_timing);
   return ret;
}

static int sd_runtime_reconfigure_sd_hs_to_legacy_direct(
   const char *operation, uintptr_t host,
   const struct sd_runtime_profile *profile)
{
   const char *target = profile ? profile->name : "boot";
   uintptr_t card;
   uint32_t target_f_max;
   uint32_t target_caps;
   uint32_t current_caps;
   uint32_t current_actual;
   uint32_t current_ios_clock;
   uint32_t current_width;
   uint32_t before_timing;
   uint32_t target_width;
   unsigned char status[64];
   int switch_ret;
   int width_ret = 0;
   int ret;

   if (!host || !sd_runtime_boot.saved)
      return -EOPNOTSUPP;

   sd_runtime_profile_target(profile, &target_f_max, &target_caps, NULL);
   current_caps = sd_read_u32(host, SD_MMC_HOST_CAPS_OFFSET);
   if (!sd_runtime_caps_direct_legacy(target_caps) ||
       (current_caps & SD_MMC_CAP_SD_HIGHSPEED) == 0 ||
       (current_caps & SD_MMC_CAP_UHS_MASK) != 0)
      return -EOPNOTSUPP;

   card = sd_read_ptr(host, SD_MMC_HOST_CARD_OFFSET);
   current_actual = sd_read_u32(host, SD_MMC_HOST_ACTUAL_CLOCK_OFFSET);
   current_ios_clock = sd_read_u32(host, SD_MMC_HOST_IOS_OFFSET);
   current_width = sd_read_u8(host, SD_MMC_HOST_IOS_OFFSET + 9u);
   before_timing = sd_read_u8(host, SD_MMC_HOST_IOS_OFFSET + 10u);
   if (!card || !current_actual || !current_ios_clock)
      return -EOPNOTSUPP;

   target_width = (target_caps & SD_MMC_CAP_4_BIT_DATA) ?
      SD_MMC_BUS_WIDTH_4 : SD_MMC_BUS_WIDTH_1;

   unifrog_log_sync("sd_profile hs_legacy_direct begin operation=%s target=%s host=0x%08lx card=0x%08lx width=%lu>%lu clock=%lu>%lu caps=0x%08lx>0x%08lx",
      operation ? operation : "", target, (unsigned long)host,
      (unsigned long)card, (unsigned long)current_width,
      (unsigned long)target_width, (unsigned long)current_actual,
      (unsigned long)target_f_max, (unsigned long)current_caps,
      (unsigned long)target_caps);
   sd_runtime_stage(operation, "sd high-speed legacy direct begin", 0);
   sd_runtime_log_registers("hs_legacy_direct_begin", host);

   ret = sd_runtime_wait_unclaimed(operation,
      "sd high-speed legacy direct wait", host);
   if (ret != 0) {
      sd_runtime_stage(operation,
         "sd high-speed legacy direct wait failed", ret);
      return ret;
   }

   sd_runtime_last_switch.hs_attempted = 1;
   sd_runtime_last_switch.hs_ret = -EIO;
   sd_runtime_last_switch.hs_max_clock = target_f_max;
   sd_runtime_last_switch.hs_before_actual = current_actual;
   sd_runtime_last_switch.hs_after_actual = current_actual;
   sd_runtime_last_switch.hs_before_timing = before_timing;
   sd_runtime_last_switch.hs_after_timing = before_timing;

   memset(status, 0, sizeof(status));
   __mmc_claim_host((void *)host, NULL);
   sd_runtime_write_target(host, profile, 0);
   switch_ret = mmc_sd_switch((void *)card, 1, 0, 0, status);
   if (switch_ret == 0 && (status[16] & 0x0fu) != 0)
      switch_ret = -EIO;
   if (switch_ret == 0) {
      mmc_set_timing((void *)host, SD_MMC_TIMING_LEGACY);
      if (target_width == SD_MMC_BUS_WIDTH_1)
         mmc_set_clock((void *)host, target_f_max);
      if (current_width != target_width) {
         width_ret = mmc_app_set_bus_width((void *)card, target_width);
         if (width_ret == 0)
            mmc_set_bus_width((void *)host, target_width);
      }
      if (width_ret == 0)
         mmc_set_clock((void *)host, target_f_max);
   }
   mmc_release_host((void *)host);

   sd_runtime_delay_ms(50);
   sd_runtime_last_switch.hs_ret = switch_ret != 0 ? switch_ret : width_ret;
   sd_runtime_last_switch.hs_after_actual =
      sd_read_u32(host, SD_MMC_HOST_ACTUAL_CLOCK_OFFSET);
   sd_runtime_last_switch.hs_after_timing =
      sd_read_u8(host, SD_MMC_HOST_IOS_OFFSET + 10u);

   if (switch_ret != 0)
      ret = switch_ret;
   else if (width_ret != 0)
      ret = width_ret;
   else if (sd_runtime_last_switch.hs_after_actual == 0 ||
            sd_read_u32(host, SD_MMC_HOST_IOS_OFFSET) == 0)
      ret = -EAGAIN;
   else
      ret = 0;

   sd_runtime_log_registers("hs_legacy_direct_done", host);
   sd_runtime_stage(operation, "sd high-speed legacy direct done", ret);
   unifrog_log_sync("sd_profile hs_legacy_direct done operation=%s target=%s ret=%d switch_ret=%d width_ret=%d actual=%lu ios_clock=%lu width=%lu timing=%lu status16=0x%02x",
      operation ? operation : "", target, ret, switch_ret, width_ret,
      (unsigned long)sd_runtime_last_switch.hs_after_actual,
      (unsigned long)sd_read_u32(host, SD_MMC_HOST_IOS_OFFSET),
      (unsigned long)sd_read_u8(host, SD_MMC_HOST_IOS_OFFSET + 9u),
      (unsigned long)sd_runtime_last_switch.hs_after_timing,
      (unsigned)status[16]);
   return ret;
}

static int sd_runtime_finish_sd_hs(const char *operation, uintptr_t host,
   const struct sd_runtime_profile *profile)
{
   uintptr_t card = sd_read_ptr(host, SD_MMC_HOST_CARD_OFFSET);
   uint32_t before_actual = sd_read_u32(host,
      SD_MMC_HOST_ACTUAL_CLOCK_OFFSET);
   uint32_t before_timing = sd_read_u8(host, SD_MMC_HOST_IOS_OFFSET + 10u);
   unsigned int max_clock;
   int switch_ret;

   sd_runtime_last_switch.hs_attempted = 1;
   sd_runtime_last_switch.hs_ret = -ENODEV;
   sd_runtime_last_switch.hs_max_clock = 0;
   sd_runtime_last_switch.hs_before_actual = before_actual;
   sd_runtime_last_switch.hs_after_actual = before_actual;
   sd_runtime_last_switch.hs_before_timing = before_timing;
   sd_runtime_last_switch.hs_after_timing = before_timing;

   if (!card) {
      unifrog_log_sync("sd_profile hs_finish operation=%s ret=-1 reason=no_card",
         operation ? operation : "");
      return -ENODEV;
   }

   unifrog_log_sync("sd_profile hs_finish begin operation=%s profile=%s card=0x%08lx actual=%lu timing=%lu",
      operation ? operation : "", profile ? profile->name : "",
      (unsigned long)card, (unsigned long)before_actual,
      (unsigned long)before_timing);
   __mmc_claim_host((void *)host, NULL);
   switch_ret = mmc_sd_switch_hs((void *)card);
   if (switch_ret > 0)
      mmc_set_timing((void *)host, SD_MMC_TIMING_SD_HS);
   max_clock = mmc_sd_get_max_clock((void *)card);
   if (max_clock == 0 || max_clock > profile->f_max)
      max_clock = profile->f_max;
   mmc_set_clock((void *)host, max_clock);
   mmc_release_host((void *)host);
   sd_runtime_delay_ms(50);
   sd_runtime_last_switch.hs_ret = switch_ret;
   sd_runtime_last_switch.hs_max_clock = max_clock;
   sd_runtime_last_switch.hs_after_actual =
      sd_read_u32(host, SD_MMC_HOST_ACTUAL_CLOCK_OFFSET);
   sd_runtime_last_switch.hs_after_timing =
      sd_read_u8(host, SD_MMC_HOST_IOS_OFFSET + 10u);

   unifrog_log_sync("sd_profile hs_finish done operation=%s profile=%s switch_ret=%d requested=%lu max=%lu before_actual=%lu actual=%lu timing=%lu",
      operation ? operation : "", profile ? profile->name : "",
      switch_ret, (unsigned long)profile->f_max, (unsigned long)max_clock,
      (unsigned long)before_actual,
      (unsigned long)sd_read_u32(host, SD_MMC_HOST_ACTUAL_CLOCK_OFFSET),
      (unsigned long)sd_read_u8(host, SD_MMC_HOST_IOS_OFFSET + 10u));
   sd_runtime_log_registers("hs_finish_done", host);
   return switch_ret < 0 ? switch_ret : 0;
}

static int sd_runtime_call_bus_op(const char *operation, const char *stage,
   uintptr_t host, size_t op_offset)
{
   char reg_tag[64];
   uintptr_t ops = sd_read_ptr(host, SD_MMC_HOST_BUS_OPS_OFFSET);
   uintptr_t card = sd_read_ptr(host, SD_MMC_HOST_CARD_OFFSET);
   uintptr_t fn = ops ? sd_read_ptr(ops, op_offset) : 0;
   uintptr_t owner = sd_read_ptr(host, SD_MMC_HOST_CLAIMED_TASK_OFFSET);
   uintptr_t current = sd_runtime_current_task();
   uint32_t before_state = card ?
      sd_read_u32(card, SD_MMC_CARD_STATE_OFFSET) : 0u;
   uint32_t before_actual =
      sd_read_u32(host, SD_MMC_HOST_ACTUAL_CLOCK_OFFSET);
   int ret;

   snprintf(reg_tag, sizeof(reg_tag), "%s_%s_begin",
      operation ? operation : "", stage ? stage : "");

   unifrog_log("unifrog sd runtime bus_op begin operation=%s stage=%s "
          "host=0x%08lx ops=0x%08lx fn=0x%08lx card=0x%08lx "
          "card_state=0x%08lx claim=0x%02lx owner=0x%08lx count=%lu "
          "current=0x%08lx owner_current=%u owner_name=%s "
          "current_name=%s caps=0x%08lx caps2=0x%08lx actual=%lu\n",
      operation ? operation : "",
      stage ? stage : "",
      (unsigned long)host,
      (unsigned long)ops,
      (unsigned long)fn,
      (unsigned long)card,
      (unsigned long)before_state,
      (unsigned long)sd_read_u8(host, SD_MMC_HOST_CLAIMED_OFFSET),
      (unsigned long)owner,
      (unsigned long)sd_read_u32(host, SD_MMC_HOST_CLAIM_COUNT_OFFSET),
      (unsigned long)current,
      owner && owner == current ? 1u : 0u,
      sd_runtime_task_name(owner),
      sd_runtime_task_name(current),
      (unsigned long)sd_read_u32(host, SD_MMC_HOST_CAPS_OFFSET),
      (unsigned long)sd_read_u32(host, SD_MMC_HOST_CAPS2_OFFSET),
      (unsigned long)before_actual);
   sd_runtime_log_registers(reg_tag, host);

   if (sd_runtime_wait_unclaimed(operation, stage, host) != 0) {
      owner = sd_read_ptr(host, SD_MMC_HOST_CLAIMED_TASK_OFFSET);
      current = sd_runtime_current_task();
      snprintf(reg_tag, sizeof(reg_tag), "%s_%s_claimed",
         operation ? operation : "", stage ? stage : "");
      sd_runtime_log_registers(reg_tag, host);
      unifrog_log("unifrog sd runtime bus_op blocked operation=%s stage=%s "
             "reason=host_claimed host=0x%08lx claim=0x%02lx "
             "owner=0x%08lx count=%lu current=0x%08lx owner_current=%u "
             "owner_name=%s current_name=%s card=0x%08lx actual=%lu\n",
         operation ? operation : "",
         stage ? stage : "",
         (unsigned long)host,
         (unsigned long)sd_read_u8(host, SD_MMC_HOST_CLAIMED_OFFSET),
         (unsigned long)owner,
         (unsigned long)sd_read_u32(host, SD_MMC_HOST_CLAIM_COUNT_OFFSET),
         (unsigned long)current,
         owner && owner == current ? 1u : 0u,
         sd_runtime_task_name(owner),
         sd_runtime_task_name(current),
         (unsigned long)sd_read_ptr(host, SD_MMC_HOST_CARD_OFFSET),
         (unsigned long)sd_read_u32(host, SD_MMC_HOST_ACTUAL_CLOCK_OFFSET));
      return -EBUSY;
   }

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
   snprintf(reg_tag, sizeof(reg_tag), "%s_%s_done",
      operation ? operation : "", stage ? stage : "");
   sd_runtime_log_registers(reg_tag, host);
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

static int sd_runtime_reclock_unclocked_host(const char *operation,
   uintptr_t host);

static int sd_runtime_profile_switch_ready(const char *operation,
   uintptr_t host, char *detail, size_t detail_size)
{
   uintptr_t card = host ? sd_read_ptr(host, SD_MMC_HOST_CARD_OFFSET) : 0;
   uintptr_t ops = host ? sd_read_ptr(host, SD_MMC_HOST_BUS_OPS_OFFSET) : 0;
   uint32_t actual = host ? sd_read_u32(host,
      SD_MMC_HOST_ACTUAL_CLOCK_OFFSET) : 0;
   uint32_t ios_clock = host ? sd_read_u32(host, SD_MMC_HOST_IOS_OFFSET) : 0;

   if (card && ops && actual && ios_clock)
      return 0;

   if (card && ops && (!actual || !ios_clock)) {
      (void)sd_runtime_reclock_unclocked_host(operation, host);
      card = sd_read_ptr(host, SD_MMC_HOST_CARD_OFFSET);
      ops = sd_read_ptr(host, SD_MMC_HOST_BUS_OPS_OFFSET);
      actual = sd_read_u32(host, SD_MMC_HOST_ACTUAL_CLOCK_OFFSET);
      ios_clock = sd_read_u32(host, SD_MMC_HOST_IOS_OFFSET);
      if (card && ops && actual && ios_clock)
         return 0;
   }

   if (detail && detail_size)
      snprintf(detail, detail_size,
         "%s card=0x%08lx ops=0x%08lx actual=%lu ios_clock=%lu active=%s",
         card ? (ops ? "host not clocked" : "bus ops not ready") :
            "card not ready",
         (unsigned long)card, (unsigned long)ops, (unsigned long)actual,
         (unsigned long)ios_clock,
         unifrog_platform_sd_active_profile());
   unifrog_log("unifrog sd runtime switch blocked operation=%s reason=%s "
          "host=0x%08lx card=0x%08lx ops=0x%08lx actual=%lu ios_clock=%lu active=%s\n",
      operation ? operation : "",
      !card ? "card_not_ready" : (!ops ? "missing_bus_ops" :
         "host_not_clocked"),
      (unsigned long)host,
      (unsigned long)card,
      (unsigned long)ops,
      (unsigned long)actual,
      (unsigned long)ios_clock,
      unifrog_platform_sd_active_profile());
   return -EAGAIN;
}

static int sd_runtime_debug_tag_interesting(const char *tag)
{
   if (!tag)
      return 0;
   return strstr(tag, "error") != NULL ||
      strstr(tag, "failed") != NULL ||
      strstr(tag, "fault") != NULL ||
      strcmp(tag, "mount_failed") == 0;
}

static void sd_mmc_trace_dump(const char *tag)
{
   uint32_t seq = sd_mmc_trace_seq;
   uint32_t first;
   uint32_t tail_first;
   uint32_t printed = 0;

   if (!sd_mmc_diag_enabled && !sd_runtime_debug_tag_interesting(tag))
      return;
   if (seq == 0 || seq == sd_mmc_trace_last_dump_seq)
      return;

   first = seq > SD_MMC_TRACE_COUNT ? seq - SD_MMC_TRACE_COUNT + 1u : 1u;
   tail_first = seq > 8u ? seq - 8u + 1u : first;
   unifrog_log("unifrog sd mmc_trace tag=%s diag=%lu seq=%lu "
          "last_dump=%lu count=%lu tail_first=%lu req=%lu short=%lu "
          "partial=%lu cmd_err=%lu data_err=%lu stop_err=%lu\n",
      tag ? tag : "",
      (unsigned long)sd_mmc_diag_enabled,
      (unsigned long)seq,
      (unsigned long)sd_mmc_trace_last_dump_seq,
      (unsigned long)(seq - first + 1u),
      (unsigned long)tail_first,
      (unsigned long)sd_mmc_diag_request_count,
      (unsigned long)sd_mmc_diag_short_count,
      (unsigned long)sd_mmc_diag_partial_count,
      (unsigned long)sd_mmc_diag_cmd_error_count,
      (unsigned long)sd_mmc_diag_data_error_count,
      (unsigned long)sd_mmc_diag_stop_error_count);
   for (uint32_t cur = first; cur <= seq; cur++) {
      const struct sd_mmc_trace_entry *entry =
         &sd_mmc_trace[cur % SD_MMC_TRACE_COUNT];
      int short_accounting;
      int request_error;

      if (entry->seq != cur)
         continue;
      short_accounting = entry->expected_bytes != 0 &&
         entry->bytes_xfered < entry->expected_bytes &&
         !entry->partial_progress;
      request_error = short_accounting || entry->cmd_error != 0 ||
         entry->data_error != 0 || entry->stop_error != 0;
      if (!request_error && cur < tail_first && printed >= 4u)
         continue;
      if (printed >= 20u)
         continue;
      unifrog_log("unifrog sd mmc_req seq=%lu ts=%lu host=0x%08lx "
             "actual=%lu ios_clock=%lu mrq=0x%08lx cmd=0x%08lx "
             "op=%lu arg=0x%08lx flags=0x%08lx retries=%lu cmd_err=%ld "
             "data=0x%08lx blksz=%lu blocks=%lu data_flags=0x%08lx "
             "bytes=%lu expected=%lu partial=%lu data_err=%ld "
             "sg=0x%08lx sg_len=%lu "
             "sg0_page=0x%08lx sg0_off=%lu sg0_len=%lu sg0_dma=0x%08lx "
             "stop=0x%08lx data_stop=0x%08lx stop_op=%lu stop_err=%ld\n",
         (unsigned long)entry->seq,
         (unsigned long)entry->ts_ms,
         (unsigned long)entry->host,
         (unsigned long)entry->actual,
         (unsigned long)entry->ios_clock,
         (unsigned long)entry->mrq,
         (unsigned long)entry->cmd,
         (unsigned long)entry->opcode,
         (unsigned long)entry->arg,
         (unsigned long)entry->flags,
         (unsigned long)entry->retries,
         (long)entry->cmd_error,
         (unsigned long)entry->data,
         (unsigned long)entry->blksz,
         (unsigned long)entry->blocks,
         (unsigned long)entry->data_flags,
         (unsigned long)entry->bytes_xfered,
         (unsigned long)entry->expected_bytes,
         (unsigned long)entry->partial_progress,
         (long)entry->data_error,
         (unsigned long)entry->sg,
         (unsigned long)entry->sg_len,
         (unsigned long)entry->sg0_page_link,
         (unsigned long)entry->sg0_offset,
         (unsigned long)entry->sg0_length,
         (unsigned long)entry->sg0_dma_address,
         (unsigned long)entry->stop,
         (unsigned long)entry->data_stop,
         (unsigned long)entry->stop_opcode,
         (long)entry->stop_error);
      printed++;
   }
   sd_mmc_trace_last_dump_seq = seq;
}

static void sd_mmc_trace_summary(const char *tag)
{
   uint32_t seq = sd_mmc_trace_seq;

   if (!sd_mmc_diag_enabled || seq == sd_mmc_trace_last_summary_seq)
      return;
   unifrog_log("unifrog sd mmc_summary tag=%s diag=%lu seq=%lu "
          "last_detail=%lu req=%lu short=%lu partial=%lu "
          "cmd_err=%lu/%lu/%ld data_err=%lu/%lu/%ld "
          "stop_err=%lu/%lu/%ld bounce=%lu read=%lu write=%lu "
          "fallback=%lu conflict=%lu abort=%lu active=%lu "
          "last_nents=%lu last_total=%lu last_dir=%lu\n",
      tag ? tag : "",
      (unsigned long)sd_mmc_diag_enabled,
      (unsigned long)seq,
      (unsigned long)sd_mmc_trace_last_dump_seq,
      (unsigned long)sd_mmc_diag_request_count,
      (unsigned long)sd_mmc_diag_short_count,
      (unsigned long)sd_mmc_diag_partial_count,
      (unsigned long)sd_mmc_diag_cmd_error_count,
      (unsigned long)sd_mmc_diag_cmd_error_last_opcode,
      (long)sd_mmc_diag_cmd_error_last,
      (unsigned long)sd_mmc_diag_data_error_count,
      (unsigned long)sd_mmc_diag_data_error_last_opcode,
      (long)sd_mmc_diag_data_error_last,
      (unsigned long)sd_mmc_diag_stop_error_count,
      (unsigned long)sd_mmc_diag_stop_error_last_opcode,
      (long)sd_mmc_diag_stop_error_last,
      (unsigned long)sd_mmc_bounce_map_count,
      (unsigned long)sd_mmc_bounce_read_count,
      (unsigned long)sd_mmc_bounce_write_count,
      (unsigned long)sd_mmc_bounce_fallback_count,
      (unsigned long)sd_mmc_bounce_conflict_count,
      (unsigned long)sd_mmc_bounce_abort_count,
      (unsigned long)sd_mmc_bounce.active,
      (unsigned long)sd_mmc_bounce_last_nents,
      (unsigned long)sd_mmc_bounce_last_total,
      (unsigned long)sd_mmc_bounce_last_direction);
   sd_mmc_trace_last_summary_seq = seq;
}

void unifrog_platform_sd_mmc_diag_begin(const char *tag)
{
   if (sd_mmc_diag_depth++ != 0)
      return;

   sd_mmc_diag_enabled = 0;
   sd_mmc_bounce_abort("diag_begin_stale");
   sd_mmc_diag_begin_count++;
   sd_mmc_diag_request_count = 0;
   sd_mmc_diag_short_count = 0;
   sd_mmc_diag_short_last_opcode = 0;
   sd_mmc_diag_short_last_blocks = 0;
   sd_mmc_diag_short_last_blksz = 0;
   sd_mmc_diag_short_last_bytes = 0;
   sd_mmc_diag_short_last_expected = 0;
   sd_mmc_diag_partial_count = 0;
   sd_mmc_diag_partial_last_opcode = 0;
   sd_mmc_diag_partial_last_blocks = 0;
   sd_mmc_diag_partial_last_blksz = 0;
   sd_mmc_diag_partial_last_bytes = 0;
   sd_mmc_diag_partial_last_expected = 0;
   sd_mmc_diag_cmd_error_count = 0;
   sd_mmc_diag_cmd_error_last_opcode = 0;
   sd_mmc_diag_cmd_error_last = 0;
   sd_mmc_diag_data_error_count = 0;
   sd_mmc_diag_data_error_last_opcode = 0;
   sd_mmc_diag_data_error_last = 0;
   sd_mmc_diag_stop_error_count = 0;
   sd_mmc_diag_stop_error_last_opcode = 0;
   sd_mmc_diag_stop_error_last = 0;
   sd_mmc_bounce_map_count = 0;
   sd_mmc_bounce_read_count = 0;
   sd_mmc_bounce_write_count = 0;
   sd_mmc_bounce_fallback_count = 0;
   sd_mmc_bounce_conflict_count = 0;
   sd_mmc_bounce_abort_count = 0;
   sd_mmc_bounce_last_nents = 0;
   sd_mmc_bounce_last_total = 0;
   sd_mmc_bounce_last_direction = 0;
   sd_mmc_trace_seq = 0;
   sd_mmc_trace_last_dump_seq = 0;
   sd_mmc_trace_last_summary_seq = 0;
   memset(sd_mmc_trace, 0, sizeof(sd_mmc_trace));
   sd_mmc_diag_enabled = 1;
   unifrog_log("unifrog sd mmc_diag begin tag=%s begin_count=%lu\n",
      tag ? tag : "", (unsigned long)sd_mmc_diag_begin_count);
}

void unifrog_platform_sd_mmc_diag_checkpoint(const char *tag)
{
   if (!sd_mmc_diag_enabled)
      return;
   sd_mmc_trace_dump(tag ? tag : "checkpoint");
}

void unifrog_platform_sd_mmc_diag_checkpoint_summary(const char *tag)
{
   if (!sd_mmc_diag_enabled)
      return;
   sd_mmc_trace_summary(tag ? tag : "checkpoint");
}

void unifrog_platform_sd_mmc_diag_end(const char *tag)
{
   if (sd_mmc_diag_depth == 0)
      return;
   sd_mmc_diag_depth--;
   if (sd_mmc_diag_depth != 0)
      return;

   sd_mmc_trace_dump(tag ? tag : "end");
   sd_mmc_bounce_abort(tag ? tag : "end");
   unifrog_log("unifrog sd mmc_diag end tag=%s req=%lu short=%lu "
          "partial=%lu last_op=%lu last_blocks=%lu last_blksz=%lu "
          "last_bytes=%lu last_expected=%lu partial_last=%lu/%lu/%lu/%lu/%lu "
          "cmd_err=%lu/%lu/%ld data_err=%lu/%lu/%ld "
          "stop_err=%lu/%lu/%ld bounce=%lu read=%lu write=%lu fallback=%lu "
          "conflict=%lu abort=%lu active=%lu last_nents=%lu last_total=%lu "
          "last_dir=%lu\n",
      tag ? tag : "",
      (unsigned long)sd_mmc_diag_request_count,
      (unsigned long)sd_mmc_diag_short_count,
      (unsigned long)sd_mmc_diag_partial_count,
      (unsigned long)sd_mmc_diag_short_last_opcode,
      (unsigned long)sd_mmc_diag_short_last_blocks,
      (unsigned long)sd_mmc_diag_short_last_blksz,
      (unsigned long)sd_mmc_diag_short_last_bytes,
      (unsigned long)sd_mmc_diag_short_last_expected,
      (unsigned long)sd_mmc_diag_partial_last_opcode,
      (unsigned long)sd_mmc_diag_partial_last_blocks,
      (unsigned long)sd_mmc_diag_partial_last_blksz,
      (unsigned long)sd_mmc_diag_partial_last_bytes,
      (unsigned long)sd_mmc_diag_partial_last_expected,
      (unsigned long)sd_mmc_diag_cmd_error_count,
      (unsigned long)sd_mmc_diag_cmd_error_last_opcode,
      (long)sd_mmc_diag_cmd_error_last,
      (unsigned long)sd_mmc_diag_data_error_count,
      (unsigned long)sd_mmc_diag_data_error_last_opcode,
      (long)sd_mmc_diag_data_error_last,
      (unsigned long)sd_mmc_diag_stop_error_count,
      (unsigned long)sd_mmc_diag_stop_error_last_opcode,
      (long)sd_mmc_diag_stop_error_last,
      (unsigned long)sd_mmc_bounce_map_count,
      (unsigned long)sd_mmc_bounce_read_count,
      (unsigned long)sd_mmc_bounce_write_count,
      (unsigned long)sd_mmc_bounce_fallback_count,
      (unsigned long)sd_mmc_bounce_conflict_count,
      (unsigned long)sd_mmc_bounce_abort_count,
      (unsigned long)sd_mmc_bounce.active,
      (unsigned long)sd_mmc_bounce_last_nents,
      (unsigned long)sd_mmc_bounce_last_total,
      (unsigned long)sd_mmc_bounce_last_direction);
   sd_mmc_diag_enabled = 0;
}

static int sd_runtime_host_needs_redetect(uintptr_t host, uintptr_t card,
   uintptr_t ops, uint32_t actual, uint32_t ios_clock)
{
   (void)host;
   return !card || !ops || actual == 0 || ios_clock == 0;
}

static int sd_runtime_request_redetect(const char *tag, uintptr_t host,
   unsigned attempt)
{
   uintptr_t card;
   uintptr_t ops;
   uint32_t actual;
   uint32_t ios_clock;

   if (!host)
      return 0;

   card = sd_read_ptr(host, SD_MMC_HOST_CARD_OFFSET);
   ops = sd_read_ptr(host, SD_MMC_HOST_BUS_OPS_OFFSET);
   actual = sd_read_u32(host, SD_MMC_HOST_ACTUAL_CLOCK_OFFSET);
   ios_clock = sd_read_u32(host, SD_MMC_HOST_IOS_OFFSET);
   if (!sd_runtime_host_needs_redetect(host, card, ops, actual, ios_clock))
      return 0;

   unifrog_log_sync("sd_profile redetect tag=%s attempt=%lu host=0x%08lx card=0x%08lx ops=0x%08lx active=%s actual=%lu ios_clock=%lu",
      tag ? tag : "", (unsigned long)attempt, (unsigned long)host,
      (unsigned long)card, (unsigned long)ops,
      unifrog_platform_sd_active_profile(),
      (unsigned long)actual, (unsigned long)ios_clock);
   sd_runtime_log_registers("redetect_request", host);
   mmc_detect_change((void *)host, 0);
   return 1;
}

static int sd_runtime_reclock_legacy_direct(const char *operation,
   uintptr_t host)
{
   uintptr_t card;
   uint32_t caps;
   uint32_t f_max;
   uint32_t target_width;
   int ret;

   if (!host)
      return -ENODEV;

   card = sd_read_ptr(host, SD_MMC_HOST_CARD_OFFSET);
   caps = sd_read_u32(host, SD_MMC_HOST_CAPS_OFFSET);
   f_max = sd_read_u32(host, SD_MMC_HOST_F_MAX_OFFSET);
   if (!card || !sd_runtime_caps_direct_legacy(caps))
      return -EOPNOTSUPP;
   if (f_max == 0 && sd_runtime_boot.saved)
      f_max = sd_runtime_boot.f_max;
   if (f_max == 0)
      return -EINVAL;

   target_width = (caps & SD_MMC_CAP_4_BIT_DATA) ?
      SD_MMC_BUS_WIDTH_4 : SD_MMC_BUS_WIDTH_1;

   unifrog_log_sync("sd_profile reclock_direct begin tag=%s host=0x%08lx card=0x%08lx card_state=0x%08lx width=%lu fmax=%lu caps=0x%08lx actual=%lu ios_clock=%lu",
      operation ? operation : "", (unsigned long)host,
      (unsigned long)card,
      (unsigned long)sd_read_u32(card, SD_MMC_CARD_STATE_OFFSET),
      (unsigned long)target_width, (unsigned long)f_max,
      (unsigned long)caps,
      (unsigned long)sd_read_u32(host, SD_MMC_HOST_ACTUAL_CLOCK_OFFSET),
      (unsigned long)sd_read_u32(host, SD_MMC_HOST_IOS_OFFSET));
   sd_runtime_log_registers("reclock_direct_begin", host);

   ret = sd_runtime_wait_unclaimed(operation, "reclock direct wait", host);
   if (ret != 0)
      return ret;

   __mmc_claim_host((void *)host, NULL);
   mmc_set_timing((void *)host, SD_MMC_TIMING_LEGACY);
   if (target_width == SD_MMC_BUS_WIDTH_4)
      mmc_set_bus_width((void *)host, target_width);
   mmc_set_clock((void *)host, f_max);
   ret = mmc_app_set_bus_width((void *)card, target_width);
   if (ret == 0) {
      mmc_set_timing((void *)host, SD_MMC_TIMING_LEGACY);
      mmc_set_bus_width((void *)host, target_width);
      mmc_set_clock((void *)host, f_max);
   }
   mmc_release_host((void *)host);
   sd_runtime_delay_ms(50);

   sd_runtime_log_registers("reclock_direct_done", host);
   unifrog_log_sync("sd_profile reclock_direct done tag=%s ret=%d actual=%lu ios_clock=%lu width=%lu timing=%lu",
      operation ? operation : "", ret,
      (unsigned long)sd_read_u32(host, SD_MMC_HOST_ACTUAL_CLOCK_OFFSET),
      (unsigned long)sd_read_u32(host, SD_MMC_HOST_IOS_OFFSET),
      (unsigned long)sd_read_u8(host, SD_MMC_HOST_IOS_OFFSET + 9u),
      (unsigned long)sd_read_u8(host, SD_MMC_HOST_IOS_OFFSET + 10u));
   if (ret != 0)
      return ret;
   return sd_read_u32(host, SD_MMC_HOST_ACTUAL_CLOCK_OFFSET) &&
      sd_read_u32(host, SD_MMC_HOST_IOS_OFFSET) ? 0 : -EAGAIN;
}

static int sd_runtime_reclock_unclocked_host(const char *operation,
   uintptr_t host)
{
   uintptr_t card;
   uintptr_t ops;
   uint32_t actual;
   uint32_t ios_clock;
   int requested;

   if (!host)
      return -ENODEV;

   card = sd_read_ptr(host, SD_MMC_HOST_CARD_OFFSET);
   ops = sd_read_ptr(host, SD_MMC_HOST_BUS_OPS_OFFSET);
   actual = sd_read_u32(host, SD_MMC_HOST_ACTUAL_CLOCK_OFFSET);
   ios_clock = sd_read_u32(host, SD_MMC_HOST_IOS_OFFSET);
   if (!card || !ops)
      return -ENODEV;
   if (actual && ios_clock)
      return 0;

   unifrog_log_sync("sd_profile reclock begin tag=%s host=0x%08lx card=0x%08lx ops=0x%08lx actual=%lu ios_clock=%lu active=%s",
      operation ? operation : "", (unsigned long)host,
      (unsigned long)card, (unsigned long)ops,
      (unsigned long)actual, (unsigned long)ios_clock,
      unifrog_platform_sd_active_profile());
   sd_runtime_log_registers("reclock_begin", host);

   requested = 0;
   if (sd_runtime_reclock_legacy_direct(operation, host) == 0)
      requested = 2;
   else {
      requested = sd_runtime_request_redetect(operation, host, 0);
      sd_runtime_delay_ms(SD_RUNTIME_REDETECT_SETTLE_MS);
   }

   card = sd_read_ptr(host, SD_MMC_HOST_CARD_OFFSET);
   ops = sd_read_ptr(host, SD_MMC_HOST_BUS_OPS_OFFSET);
   actual = sd_read_u32(host, SD_MMC_HOST_ACTUAL_CLOCK_OFFSET);
   ios_clock = sd_read_u32(host, SD_MMC_HOST_IOS_OFFSET);
   sd_runtime_log_registers("reclock_done", host);
   unifrog_log_sync("sd_profile reclock done tag=%s requested=%d host=0x%08lx card=0x%08lx ops=0x%08lx actual=%lu ios_clock=%lu active=%s",
      operation ? operation : "", requested, (unsigned long)host,
      (unsigned long)card, (unsigned long)ops,
      (unsigned long)actual, (unsigned long)ios_clock,
      unifrog_platform_sd_active_profile());

   return (card && ops && actual && ios_clock) ? 0 : -EAGAIN;
}

static int sd_runtime_recovery_attempt_needs_hard_reset(unsigned attempt)
{
   return attempt == SD_RUNTIME_HARD_RECOVERY_FIRST_ATTEMPT ||
      attempt == SD_RUNTIME_HARD_RECOVERY_SECOND_ATTEMPT;
}

static int sd_runtime_force_card_redetect(const char *tag, uintptr_t host,
   const struct sd_runtime_profile *profile, unsigned attempt)
{
   uintptr_t ops;
   uintptr_t remove_fn;
   uintptr_t card;
   uint32_t bus_refs;
   const char *target = profile ? profile->name : "boot";
   int ret;

   if (!host)
      return -ENODEV;

   ret = sd_runtime_wait_unclaimed(tag, "hard redetect wait", host);
   if (ret != 0) {
      unifrog_log_sync("sd_profile hard_redetect skip tag=%s attempt=%lu reason=claimed ret=%d",
         tag ? tag : "", (unsigned long)attempt, ret);
      return ret;
   }

   ops = sd_read_ptr(host, SD_MMC_HOST_BUS_OPS_OFFSET);
   remove_fn = ops ? sd_read_ptr(ops, SD_MMC_BUS_OP_REMOVE_OFFSET) : 0;
   card = sd_read_ptr(host, SD_MMC_HOST_CARD_OFFSET);
   bus_refs = sd_read_u32(host, SD_MMC_HOST_BUS_REFS_OFFSET);

   unifrog_log_sync("sd_profile hard_redetect begin tag=%s attempt=%lu target=%s host=0x%08lx card=0x%08lx ops=0x%08lx remove=0x%08lx refs=%lu active=%s",
      tag ? tag : "", (unsigned long)attempt, target,
      (unsigned long)host, (unsigned long)card, (unsigned long)ops,
      (unsigned long)remove_fn, (unsigned long)bus_refs,
      unifrog_platform_sd_active_profile());
   sd_runtime_stage(tag, "hard redetect begin", 0);
   sd_runtime_log_registers("hard_redetect_begin", host);

   if (card && remove_fn) {
      ((sd_runtime_bus_remove_fn)remove_fn)((void *)host);
      sd_runtime_delay_ms(20);
   }

   if (ops && bus_refs) {
      __mmc_claim_host((void *)host, NULL);
      mmc_detach_bus((void *)host);
      mmc_power_off((void *)host);
      mmc_release_host((void *)host);
   }

   if (profile) {
      sd_runtime_write_profile(host, profile);
      snprintf(sd_runtime_boot.active_profile,
         sizeof(sd_runtime_boot.active_profile), "%s", profile->name);
   } else {
      sd_runtime_write_boot(host);
      snprintf(sd_runtime_boot.active_profile,
         sizeof(sd_runtime_boot.active_profile), "boot");
   }

   /*
    * hc_mmc_ip_reset is exported by libmmchosthc15.a.  Reverse engineering
    * shows it toggles RESET_GATE1 bit 18 with short delays, which is the
    * vendor driver's own SDIO IP reset primitive.
    */
   hc_mmc_ip_reset();
   sd_runtime_delay_ms(10);
   sd_runtime_log_soc_gates("hard_redetect_after_ip_reset");
   sd_runtime_log_registers("hard_redetect_after_ip_reset", host);
   mmc_detect_change((void *)host, 0);
   sd_runtime_stage(tag, "hard redetect done", 0);
   sd_runtime_delay_ms(SD_RUNTIME_HARD_RECOVERY_SETTLE_MS);
   unifrog_log_sync("sd_profile hard_redetect done tag=%s attempt=%lu target=%s card=0x%08lx active=%s",
      tag ? tag : "", (unsigned long)attempt, target,
      (unsigned long)sd_read_ptr(host, SD_MMC_HOST_CARD_OFFSET),
      unifrog_platform_sd_active_profile());
   return 0;
}

static int sd_runtime_reconfigure_host(const char *operation, uintptr_t host,
   const struct sd_runtime_profile *profile)
{
   int ret;

   sd_runtime_log_registers("reconfigure_begin", host);
   ret = sd_runtime_reconfigure_legacy_direct(operation, host, profile);
   if (ret != -EOPNOTSUPP)
      return ret;

   ret = sd_runtime_reconfigure_sd_hs_direct(operation, host, profile);
   if (ret != -EOPNOTSUPP)
      return ret;

   ret = sd_runtime_reconfigure_sd_hs_to_legacy_direct(operation, host,
      profile);
   if (ret != -EOPNOTSUPP)
      return ret;

   sd_runtime_stage(operation, "bus suspend begin", 0);
   ret = sd_runtime_call_bus_op(operation, "suspend", host,
      SD_MMC_BUS_OP_SUSPEND_OFFSET);
   sd_runtime_stage(operation, "bus suspend done", ret);
   if (ret != 0) {
      if (!profile && ret == -ENOSYS) {
         int reclock_ret;

         sd_runtime_write_boot(host);
         snprintf(sd_runtime_boot.active_profile,
            sizeof(sd_runtime_boot.active_profile), "boot");
         sd_runtime_stage(operation, "boot profile forced", ret);
         sd_runtime_log_registers("after_forced_boot_profile", host);
         unifrog_log("unifrog sd runtime reconfigure operation=%s "
                "fallback=force_boot reason=missing_bus_ops\n",
            operation ? operation : "");

         reclock_ret = sd_runtime_reclock_legacy_direct(operation, host);
         sd_runtime_stage(operation, "boot profile reclock", reclock_ret);
         if (reclock_ret == 0) {
            snprintf(sd_runtime_boot.active_profile,
               sizeof(sd_runtime_boot.active_profile), "boot");
            return 0;
         }

         (void)sd_runtime_request_redetect(operation, host, 0);
         sd_runtime_delay_ms(SD_RUNTIME_REDETECT_SETTLE_MS);
         return 0;
      }
      return ret;
   }
   sd_runtime_log_registers("after_suspend", host);

   if (profile) {
      sd_runtime_write_profile(host, profile);
      sd_runtime_stage(operation, "profile written", 0);
   } else {
      sd_runtime_write_boot(host);
      sd_runtime_stage(operation, "boot profile written", 0);
   }
   sd_runtime_log_registers("after_profile_write", host);

   sd_runtime_stage(operation, "bus resume begin", 0);
   ret = sd_runtime_call_bus_op(operation, "resume", host,
      SD_MMC_BUS_OP_RESUME_OFFSET);
   sd_runtime_stage(operation, "bus resume done", ret);
   if (ret != 0)
      return ret;

   sd_runtime_delay_ms(100);
   sd_runtime_log_registers("after_resume_settle", host);
   if (sd_runtime_profile_needs_sd_hs(profile)) {
      sd_runtime_stage(operation, "sd high-speed begin", 0);
      ret = sd_runtime_finish_sd_hs(operation, host, profile);
      sd_runtime_stage(operation, "sd high-speed done", ret);
      if (ret != 0)
         return ret;
   }
   return 0;
}

void unifrog_platform_sd_debug_dump(const char *tag)
{
   uintptr_t host = sd_runtime_find_host();
   uintptr_t card;
   uintptr_t pdata;
   uintptr_t mrq;
   uintptr_t cmd;
   uintptr_t req_cmd;
   uintptr_t data;
   uintptr_t stop;
   uintptr_t data_stop = 0;
   uintptr_t owner;
   uintptr_t current;
   uintptr_t mmc_ops;
   uintptr_t hc_ops;
   uintptr_t iobase;
   uint32_t force_clk = 0;
   uint32_t ios_clock;
   uint32_t cmd_opcode = 0;
   uint32_t cmd_arg = 0;
   uint32_t cmd_flags = 0;
   uint32_t cmd_retries = 0;
   uint32_t cmd_error = 0;
   uint32_t data_timeout_ns = 0;
   uint32_t data_timeout_clks = 0;
   uint32_t data_blksz = 0;
   uint32_t data_blocks = 0;
   uint32_t data_error = 0;

   if (!host) {
      unifrog_log("unifrog sd debug tag=%s host=missing\n",
         tag ? tag : "");
      return;
   }
   sd_runtime_log_pins(tag ? tag : "debug");
   sd_runtime_log_soc_gates(tag ? tag : "debug");
   sd_runtime_log_registers(tag ? tag : "debug", host);

   card = sd_read_ptr(host, SD_MMC_HOST_CARD_OFFSET);
   pdata = sd_read_ptr(host, SD_HC_HOST_PDATA_OFFSET);
   mrq = sd_read_ptr(host, SD_HC_HOST_CURRENT_MRQ_OFFSET);
   cmd = sd_read_ptr(host, SD_HC_HOST_CURRENT_CMD_OFFSET);
   req_cmd = sd_runtime_ptr_valid(mrq) ?
      sd_read_ptr(mrq, SD_MMC_REQUEST_CMD_OFFSET) : 0u;
   data = sd_runtime_ptr_valid(mrq) ?
      sd_read_ptr(mrq, SD_MMC_REQUEST_DATA_OFFSET) : 0u;
   stop = sd_runtime_ptr_valid(mrq) ?
      sd_read_ptr(mrq, SD_MMC_REQUEST_STOP_OFFSET) : 0u;
   if (!sd_runtime_ptr_valid(cmd))
      cmd = req_cmd;
   owner = sd_read_ptr(host, SD_MMC_HOST_CLAIMED_TASK_OFFSET);
   current = sd_runtime_current_task();
   mmc_ops = sd_read_ptr(host, SD_MMC_HOST_OPS_OFFSET);
   hc_ops = sd_read_ptr(host, SD_HC_HOST_OPS_OFFSET);
   iobase = sd_read_ptr(host, SD_HC_HOST_IOBASE_OFFSET);
   if (sd_runtime_mmio_valid(iobase))
      force_clk = sd_mmio_read8(iobase, SD_HC_REG_BUS_WIDTH) & 1u;
   if (sd_runtime_ptr_valid(cmd)) {
      cmd_opcode = sd_read_u32(cmd, SD_MMC_COMMAND_OPCODE_OFFSET);
      cmd_arg = sd_read_u32(cmd, SD_MMC_COMMAND_ARG_OFFSET);
      cmd_flags = sd_read_u32(cmd, SD_MMC_COMMAND_FLAGS_OFFSET);
      cmd_retries = sd_read_u32(cmd, SD_MMC_COMMAND_RETRIES_OFFSET);
      cmd_error = sd_read_u32(cmd, SD_MMC_COMMAND_ERROR_OFFSET);
   }
   if (sd_runtime_ptr_valid(data)) {
      data_stop = sd_read_ptr(data, SD_MMC_DATA_STOP_OFFSET);
      data_timeout_ns = sd_read_u32(data, SD_MMC_DATA_TIMEOUT_NS_OFFSET);
      data_timeout_clks = sd_read_u32(data, SD_MMC_DATA_TIMEOUT_CLKS_OFFSET);
      data_blksz = sd_read_u32(data, SD_MMC_DATA_BLKSZ_OFFSET);
      data_blocks = sd_read_u32(data, SD_MMC_DATA_BLOCKS_OFFSET);
      data_error = sd_read_u32(data, SD_MMC_DATA_ERROR_OFFSET);
      if (!sd_runtime_ptr_valid(stop) && sd_runtime_ptr_valid(data_stop))
         stop = data_stop;
   }
   ios_clock = sd_read_u32(host, SD_MMC_HOST_IOS_OFFSET);

   unifrog_log("unifrog sd mmc_ops tag=%s mmc_ops=0x%08lx "
          "request=0x%08lx set_ios=0x%08lx get_cd=0x%08lx "
          "hc_ops=0x%08lx force_clk=%lu force_fn=0x%08lx "
          "set_cmd=0x%08lx start_cmd=0x%08lx set_dma=0x%08lx "
          "enable_irq=0x%08lx get_irq=0x%08lx\n",
      tag ? tag : "",
      (unsigned long)mmc_ops,
      (unsigned long)(sd_runtime_ptr_valid(mmc_ops) ?
         sd_read_ptr(mmc_ops, SD_MMC_HOST_OPS_REQUEST_OFFSET) : 0u),
      (unsigned long)(sd_runtime_ptr_valid(mmc_ops) ?
         sd_read_ptr(mmc_ops, SD_MMC_HOST_OPS_SET_IOS_OFFSET) : 0u),
      (unsigned long)(sd_runtime_ptr_valid(mmc_ops) ?
         sd_read_ptr(mmc_ops, SD_MMC_HOST_OPS_GET_CD_OFFSET) : 0u),
      (unsigned long)hc_ops,
      (unsigned long)force_clk,
      (unsigned long)(sd_runtime_ptr_valid(hc_ops) ?
         sd_read_ptr(hc_ops, SD_HC_HOST_HW_OPS_FORCE_CLOCK_OFFSET) : 0u),
      (unsigned long)(sd_runtime_ptr_valid(hc_ops) ?
         sd_read_ptr(hc_ops, SD_HC_HOST_HW_OPS_SET_CMD_OFFSET) : 0u),
      (unsigned long)(sd_runtime_ptr_valid(hc_ops) ?
         sd_read_ptr(hc_ops, SD_HC_HOST_HW_OPS_START_CMD_OFFSET) : 0u),
      (unsigned long)(sd_runtime_ptr_valid(hc_ops) ?
         sd_read_ptr(hc_ops, SD_HC_HOST_HW_OPS_SET_DMA_OFFSET) : 0u),
      (unsigned long)(sd_runtime_ptr_valid(hc_ops) ?
         sd_read_ptr(hc_ops, SD_HC_HOST_HW_OPS_ENABLE_IRQ_OFFSET) : 0u),
      (unsigned long)(sd_runtime_ptr_valid(hc_ops) ?
         sd_read_ptr(hc_ops, SD_HC_HOST_HW_OPS_GET_CLEAR_IRQ_OFFSET) : 0u));

   unifrog_log("unifrog sd debug tag=%s active=%s host=0x%08lx card=0x%08lx "
          "card_state=0x%08lx fmax=%lu actual=%lu caps=0x%08lx "
          "caps2=0x%08lx pm_caps=0x%08lx max_seg=%lu max_segs=%lu "
          "max_req=%lu max_blk=%lu max_blks=%lu ios_clock=%lu "
          "ios=%u/%u/%u/%u/%u "
          "claim=0x%02lx owner=0x%08lx count=%lu current=0x%08lx "
          "owner_current=%u owner_name=%s current_name=%s pdata=0x%08lx "
          "pdata_busw=%lu pdata_flags=0x%08lx pdata_delay=%lu "
          "pdata_bus=%lu pdata_max=%lu pdata_caps=0x%08lx broken_cd=%u "
          "mrq=0x%08lx cmd=0x%08lx req_cmd=0x%08lx data=0x%08lx "
          "stop=0x%08lx data_stop=0x%08lx "
          "cmd_op=%lu cmd_arg=0x%08lx cmd_flags=0x%08lx cmd_retries=%lu "
          "cmd_error=%ld data_blksz=%lu data_blocks=%lu "
          "data_timeout=%lu/%lu data_error=%ld "
          "mount_mask=0x%lx attempts=%lu\n",
      tag ? tag : "",
      sd_runtime_boot.active_profile,
      (unsigned long)host,
      (unsigned long)card,
      (unsigned long)(card ? sd_read_u32(card, SD_MMC_CARD_STATE_OFFSET) : 0u),
      (unsigned long)sd_read_u32(host, SD_MMC_HOST_F_MAX_OFFSET),
      (unsigned long)sd_read_u32(host, SD_MMC_HOST_ACTUAL_CLOCK_OFFSET),
      (unsigned long)sd_read_u32(host, SD_MMC_HOST_CAPS_OFFSET),
      (unsigned long)sd_read_u32(host, SD_MMC_HOST_CAPS2_OFFSET),
      (unsigned long)sd_read_u32(host, SD_MMC_HOST_PM_CAPS_OFFSET),
      (unsigned long)sd_read_u32(host, SD_MMC_HOST_MAX_SEG_SIZE_OFFSET),
      (unsigned long)sd_read_u16(host, SD_MMC_HOST_MAX_SEGS_OFFSET),
      (unsigned long)sd_read_u32(host, SD_MMC_HOST_MAX_REQ_SIZE_OFFSET),
      (unsigned long)sd_read_u32(host, SD_MMC_HOST_MAX_BLK_SIZE_OFFSET),
      (unsigned long)sd_read_u32(host, SD_MMC_HOST_MAX_BLK_COUNT_OFFSET),
      (unsigned long)ios_clock,
      (unsigned)sd_read_u8(host, SD_MMC_HOST_IOS_OFFSET + 9u),
      (unsigned)sd_read_u8(host, SD_MMC_HOST_IOS_OFFSET + 10u),
      (unsigned)sd_read_u8(host, SD_MMC_HOST_IOS_OFFSET + 11u),
      (unsigned)sd_read_u8(host, SD_MMC_HOST_IOS_OFFSET + 12u),
      (unsigned)sd_read_u8(host, SD_MMC_HOST_IOS_OFFSET + 13u),
      (unsigned long)sd_read_u8(host, SD_MMC_HOST_CLAIMED_OFFSET),
      (unsigned long)owner,
      (unsigned long)sd_read_u32(host, SD_MMC_HOST_CLAIM_COUNT_OFFSET),
      (unsigned long)current,
      owner && owner == current ? 1u : 0u,
      sd_runtime_task_name(owner),
      sd_runtime_task_name(current),
      (unsigned long)pdata,
      (unsigned long)(pdata ? sd_read_u32(pdata,
         SD_HC_PDATA_BUS_WIDTH_OFFSET) : 0u),
      (unsigned long)(pdata ? sd_read_u32(pdata,
         SD_HC_PDATA_FLAGS_OFFSET) : 0u),
      (unsigned long)(pdata ? sd_read_u32(pdata,
         SD_HC_PDATA_CARD_DETECT_DELAY_OFFSET) : 0u),
      (unsigned long)(pdata ? sd_read_u32(pdata,
         SD_HC_PDATA_BUS_HZ_OFFSET) : 0u),
      (unsigned long)(pdata ? sd_read_u32(pdata,
         SD_HC_PDATA_MAX_FREQUENCY_OFFSET) : 0u),
      (unsigned long)(pdata ? sd_read_u32(pdata,
         SD_HC_PDATA_CAPS_OFFSET) : 0u),
      (unsigned)((pdata && (sd_read_u32(pdata, SD_HC_PDATA_FLAGS_OFFSET) &
         SD_HC_PDATA_FLAG_BROKEN_CD)) ? 1u : 0u),
      (unsigned long)mrq,
      (unsigned long)cmd,
      (unsigned long)req_cmd,
      (unsigned long)data,
      (unsigned long)stop,
      (unsigned long)data_stop,
      (unsigned long)cmd_opcode,
      (unsigned long)cmd_arg,
      (unsigned long)cmd_flags,
      (unsigned long)cmd_retries,
      (long)(int32_t)cmd_error,
      (unsigned long)data_blksz,
      (unsigned long)data_blocks,
      (unsigned long)data_timeout_ns,
      (unsigned long)data_timeout_clks,
      (long)(int32_t)data_error,
      (unsigned long)storage_mounted_mask,
      (unsigned long)storage_mount_attempts);
   unifrog_log("unifrog sd mmc_diag enabled=%lu depth=%lu req=%lu "
          "short=%lu partial=%lu last_op=%lu "
          "last_blocks=%lu last_blksz=%lu last_bytes=%lu expected=%lu "
          "partial_last=%lu/%lu/%lu/%lu/%lu "
          "cmd_err=%lu/%lu/%ld data_err=%lu/%lu/%ld "
          "stop_err=%lu/%lu/%ld host_limits=%lu/%lu/%lu/%lu "
          "bounce=%lu/%lu/%lu/%lu/%lu "
          "active=%lu last=%lu/%lu/%lu\n",
      (unsigned long)sd_mmc_diag_enabled,
      (unsigned long)sd_mmc_diag_depth,
      (unsigned long)sd_mmc_diag_request_count,
      (unsigned long)sd_mmc_diag_short_count,
      (unsigned long)sd_mmc_diag_partial_count,
      (unsigned long)sd_mmc_diag_short_last_opcode,
      (unsigned long)sd_mmc_diag_short_last_blocks,
      (unsigned long)sd_mmc_diag_short_last_blksz,
      (unsigned long)sd_mmc_diag_short_last_bytes,
      (unsigned long)sd_mmc_diag_short_last_expected,
      (unsigned long)sd_mmc_diag_partial_last_opcode,
      (unsigned long)sd_mmc_diag_partial_last_blocks,
      (unsigned long)sd_mmc_diag_partial_last_blksz,
      (unsigned long)sd_mmc_diag_partial_last_bytes,
      (unsigned long)sd_mmc_diag_partial_last_expected,
      (unsigned long)sd_mmc_diag_cmd_error_count,
      (unsigned long)sd_mmc_diag_cmd_error_last_opcode,
      (long)sd_mmc_diag_cmd_error_last,
      (unsigned long)sd_mmc_diag_data_error_count,
      (unsigned long)sd_mmc_diag_data_error_last_opcode,
      (long)sd_mmc_diag_data_error_last,
      (unsigned long)sd_mmc_diag_stop_error_count,
      (unsigned long)sd_mmc_diag_stop_error_last_opcode,
      (long)sd_mmc_diag_stop_error_last,
      (unsigned long)sd_read_u32(host, SD_MMC_HOST_MAX_SEG_SIZE_OFFSET),
      (unsigned long)sd_read_u16(host, SD_MMC_HOST_MAX_SEGS_OFFSET),
      (unsigned long)sd_read_u32(host, SD_MMC_HOST_MAX_REQ_SIZE_OFFSET),
      (unsigned long)sd_read_u32(host, SD_MMC_HOST_MAX_BLK_COUNT_OFFSET),
      (unsigned long)sd_mmc_bounce_map_count,
      (unsigned long)sd_mmc_bounce_read_count,
      (unsigned long)sd_mmc_bounce_write_count,
      (unsigned long)sd_mmc_bounce_fallback_count,
      (unsigned long)sd_mmc_bounce_conflict_count,
      (unsigned long)sd_mmc_bounce.active,
      (unsigned long)sd_mmc_bounce_last_nents,
      (unsigned long)sd_mmc_bounce_last_total,
      (unsigned long)sd_mmc_bounce_last_direction);
   unifrog_log("unifrog sd debug checkpoint tag=%s active=%s actual=%lu claim=0x%02lx count=%lu owner=0x%08lx current=0x%08lx owner_current=%u owner_name=%s current_name=%s mrq=0x%08lx cmd_op=%lu cmd_arg=0x%08lx cmd_error=%ld data_blksz=%lu data_blocks=%lu data_error=%ld mount_mask=0x%lx attempts=%lu\n",
      tag ? tag : "",
      sd_runtime_boot.active_profile,
      (unsigned long)sd_read_u32(host, SD_MMC_HOST_ACTUAL_CLOCK_OFFSET),
      (unsigned long)sd_read_u8(host, SD_MMC_HOST_CLAIMED_OFFSET),
      (unsigned long)sd_read_u32(host, SD_MMC_HOST_CLAIM_COUNT_OFFSET),
      (unsigned long)owner,
      (unsigned long)current,
      owner && owner == current ? 1u : 0u,
      sd_runtime_task_name(owner),
      sd_runtime_task_name(current),
      (unsigned long)mrq,
      (unsigned long)cmd_opcode,
      (unsigned long)cmd_arg,
      (long)(int32_t)cmd_error,
      (unsigned long)data_blksz,
      (unsigned long)data_blocks,
      (long)(int32_t)data_error,
      (unsigned long)storage_mounted_mask,
      (unsigned long)storage_mount_attempts);
   sd_mmc_trace_dump(tag);
}

/* Private SF2000 storage mount, remount, and runtime-profile public hooks. */
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

static int directory_has_child(const char *root, const char *name)
{
   char path[256];
   struct stat st;

   if (!root || !name)
      return 0;
   snprintf(path, sizeof(path), "%s/%s", root, name);
   return stat(path, &st) == 0;
}

static int storage_target_scan(const char *target, unsigned *entries_out,
   unsigned *package_out, unsigned *user_out, unsigned *markers_out)
{
   DIR *dir;
   struct dirent *ent;
   unsigned entries = 0;
   unsigned package = 0;
   unsigned user = 0;
   unsigned markers = 0;

   if (entries_out)
      *entries_out = 0;
   if (package_out)
      *package_out = 0;
   if (user_out)
      *user_out = 0;
   if (markers_out)
      *markers_out = 0;
   if (!target)
      return -1;
   dir = opendir(target);
   if (!dir)
      return -1;
   while ((ent = readdir(dir)) != NULL) {
      const char *name = ent->d_name;

      if (name[0] == '.' &&
         (name[1] == '\0' || (name[1] == '.' && name[2] == '\0')))
         continue;
      entries++;
      if (strcmp(name, "bios") == 0 || strcmp(name, "unifrog") == 0 ||
          strcmp(name, "unifrog_data") == 0 || strcmp(name, "System Volume Information") == 0) {
         package++;
         continue;
      }
      if (strcmp(name, "ROMS") == 0 || strcmp(name, "ARCHIVE") == 0 ||
          strcmp(name, "roms") == 0 || strcmp(name, "archive") == 0)
         markers++;
      user++;
   }
   closedir(dir);
   if (entries_out)
      *entries_out = entries;
   if (package_out)
      *package_out = package;
   if (user_out)
      *user_out = user;
   if (markers_out)
      *markers_out = markers;
   return 0;
}

static void storage_clear_package_staging_mountpoint(const char *target)
{
   unsigned entries = 0;
   unsigned package = 0;
   unsigned user = 0;
   unsigned markers = 0;
   char staging[288];
   int ret;

   if (!target || storage_target_scan(target, &entries, &package, &user,
       &markers) != 0 || entries == 0 || user || markers || entries != package)
      return;

   /*
    * output/sdcard is a package template, not proof that the user's SD root is
    * mounted.  If it leaked into the canonical mountpoint, move it aside so the
    * real block device can be mounted on the path all runtime code uses.
    */
   snprintf(staging, sizeof(staging), "%s.staging", target);
   (void)rmdir(staging);
   errno = 0;
   ret = rename(target, staging);
   if (ret == 0)
      (void)mkdir(target, 0777);
   unifrog_log("unifrog storage mount package_staging target=%s staging=%s ret=%d errno=%d entries=%u package=%u\n",
      target, staging, ret, errno, entries, package);
}

static int storage_target_looks_ready(const char *target)
{
   unsigned entries;
   unsigned package;
   unsigned user;
   unsigned markers;

   if (storage_target_scan(target, &entries, &package, &user, &markers) != 0 ||
       entries == 0)
      return 0;

   /*
    * The firmware image and update package can contain a minimal staging tree
    * under /media/mmcblk0 before the SD mount is actually live.  That tree has
    * only distro-owned directories (bios, unifrog, unifrog_data).  Do not adopt
    * it as storage: doing so hides mount failures and makes the browser look as
    * if the user's ROM folders disappeared.
    */
   return markers > 0 || user > 0 || directory_has_child(target, "bisrv.asd");
}

static int storage_any_target_readable(const char *tag, unsigned required_passes)
{
   for (unsigned i = 0;
        i < sizeof(storage_mounts) / sizeof(storage_mounts[0]); i++) {
      if (!(storage_mounted_mask & (1u << i)) &&
          !storage_target_looks_ready(storage_mounts[i].target))
         continue;
      if (storage_sanity_read_target(tag, storage_mounts[i].target,
          required_passes) == 0)
         return 0;
   }
   return -1;
}

static int storage_adopt_existing_mount(unsigned index, uint32_t start_ms,
   const char *reason)
{
   unsigned entries = 0;
   unsigned package = 0;
   unsigned user = 0;
   unsigned markers = 0;

   if (index >= sizeof(storage_mounts) / sizeof(storage_mounts[0]) ||
       storage_target_scan(storage_mounts[index].target, &entries, &package,
          &user, &markers) != 0)
      return -1;
   if (!storage_target_looks_ready(storage_mounts[index].target)) {
      if (entries)
         unifrog_log("unifrog storage mount target=%s not_ready reason=%s entries=%u package=%u user=%u markers=%u\n",
            storage_mounts[index].target, reason ? reason : "", entries,
            package, user, markers);
      return -1;
   }

   storage_mounted_mask |= 1u << index;
   storage_update_log_disk_available();
   unifrog_log("unifrog storage mount target=%s adopted=1 reason=%s ms=%lu mask=0x%lx entries=%u package=%u user=%u markers=%u\n",
      storage_mounts[index].target, reason ? reason : "",
      (unsigned long)(unifrog_perf_time_ms() - start_ms),
      (unsigned long)storage_mounted_mask, entries, package, user, markers);
   unifrog_platform_sd_debug_dump(reason ? reason : "mount_adopted");
   unifrog_log("unifrog storage mount adopted_checkpoint target=%s reason=%s mask=0x%lx attempts=%lu\n",
      storage_mounts[index].target, reason ? reason : "",
      (unsigned long)storage_mounted_mask,
      (unsigned long)storage_mount_attempts);
   return 0;
}

int unifrog_platform_mount_storage(void)
{
   int mounted = -1;
   uint32_t start_ms = unifrog_perf_time_ms();
   uintptr_t host = sd_runtime_find_host();

   (void)mkdir("/media", 0777);

   if (sd_runtime_host_enumerating(host)) {
      if (storage_mount_failed_debug_count == 0) {
         storage_mount_failed_debug_count++;
         unifrog_platform_sd_debug_dump("mount_wait_enumerating");
      }
      unifrog_log("unifrog storage mount wait_enumerating host=0x%08lx claim=0x%02lx count=%lu attempts=%lu\n",
         (unsigned long)host,
         (unsigned long)sd_read_u8(host, SD_MMC_HOST_CLAIMED_OFFSET),
         (unsigned long)sd_read_u32(host, SD_MMC_HOST_CLAIM_COUNT_OFFSET),
         (unsigned long)storage_mount_attempts);
      storage_update_log_disk_available();
      return -EAGAIN;
   }

   for (unsigned i = 0; i < sizeof(storage_mounts) / sizeof(storage_mounts[0]); i++) {
      uint32_t target_start_ms;
      int had_entries;
      int vfat_ret;
      int vfat_errno = 0;
      int ntfs_ret;
      int ntfs_errno = 0;

      (void)mkdir(storage_mounts[i].target, 0777);
      storage_clear_package_staging_mountpoint(storage_mounts[i].target);

      target_start_ms = unifrog_perf_time_ms();
      had_entries = directory_has_entries(storage_mounts[i].target);
      if (storage_mounted_mask & (1u << i)) {
         if (had_entries && storage_target_looks_ready(storage_mounts[i].target)) {
            unifrog_log("unifrog storage mount target=%s already_adopted=1 ms=%lu mask=0x%lx\n",
               storage_mounts[i].target,
               (unsigned long)(unifrog_perf_time_ms() - target_start_ms),
               (unsigned long)storage_mounted_mask);
            storage_update_log_disk_available();
            return 0;
         }
         unifrog_log("unifrog storage mount stale_mask target=%s bit=%u mask=0x%lx\n",
            storage_mounts[i].target, i, (unsigned long)storage_mounted_mask);
         storage_mounted_mask &= ~(1u << i);
         storage_update_log_disk_available();
      }

      if (had_entries &&
          storage_adopt_existing_mount(i, start_ms, "pre_mount_ready") == 0)
         return 0;

      storage_mount_attempts++;
      target_start_ms = unifrog_perf_time_ms();
      errno = 0;
      vfat_ret = mount(storage_mounts[i].dev, storage_mounts[i].target,
         "vfat", 0, NULL);
      vfat_errno = errno;
      if (vfat_ret == 0) {
         storage_mounted_mask |= 1u << i;
         storage_update_log_disk_available();
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
         storage_update_log_disk_available();
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
      if (had_entries &&
          storage_adopt_existing_mount(i, start_ms, "mount_failed_ready") == 0)
         return 0;
   }

   unifrog_log("unifrog storage mount summary ret=%d ms=%lu mask=0x%lx attempts=%lu\n",
      mounted, (unsigned long)(unifrog_perf_time_ms() - start_ms),
      (unsigned long)storage_mounted_mask,
      (unsigned long)storage_mount_attempts);
   if (mounted != 0) {
      storage_mount_failed_debug_count++;
      if (storage_mount_failed_debug_count == 1 ||
         (storage_mount_failed_debug_count % 8u) == 0) {
         unifrog_platform_sd_debug_dump("mount_failed");
         unifrog_log("unifrog storage mount failed_checkpoint ms=%lu mask=0x%lx attempts=%lu\n",
            (unsigned long)(unifrog_perf_time_ms() - start_ms),
            (unsigned long)storage_mounted_mask,
            (unsigned long)storage_mount_attempts);
      }
   } else {
      storage_mount_failed_debug_count = 0;
   }
   storage_update_log_disk_available();
   return mounted;
}

int unifrog_platform_storage_ready(void)
{
   for (unsigned i = 0; i < sizeof(storage_mounts) / sizeof(storage_mounts[0]); i++) {
      if ((storage_mounted_mask & (1u << i)) &&
          storage_target_looks_ready(storage_mounts[i].target)) {
         storage_update_log_disk_available();
         return 1;
      }
      if (storage_adopt_existing_mount(i, unifrog_perf_time_ms(),
            "ready_probe") == 0)
         return 1;
   }
   storage_update_log_disk_available();
   return 0;
}

static int sd_runtime_recovery_needs_grace(const char *tag)
{
   if (!tag || !tag[0])
      return 0;
   return strcmp(tag, "restore") == 0 ||
      strcmp(tag, "io_fault") == 0 ||
      strstr(tag, "rollback") != NULL ||
      strstr(tag, "fault") != NULL ||
      strstr(tag, "error") != NULL;
}

int unifrog_platform_recover_storage(const char *tag, unsigned attempts,
   unsigned delay_ms)
{
   uint32_t start_ms = unifrog_perf_time_ms();
   uintptr_t host = 0;
   unsigned attempt = 0;
   int use_grace = sd_runtime_recovery_needs_grace(tag);
   int enumerating_seen = 0;
   unsigned hard_redetects = 0;

   if (attempts == 0)
      attempts = 1;
   if (unifrog_platform_storage_ready()) {
      unifrog_log("unifrog storage recover tag=%s ready=1 attempts=0 total_ms=0 mask=0x%lx\n",
         tag ? tag : "", (unsigned long)storage_mounted_mask);
      return 0;
   }
   sd_mmc_bounce_abort(tag ? tag : "recover");

   while (attempt < attempts ||
          ((use_grace || enumerating_seen) &&
           (unifrog_perf_time_ms() - start_ms) <
              SD_RUNTIME_ENUMERATING_GRACE_MS)) {
      int mount_ret = unifrog_platform_mount_storage();
      int ready = unifrog_platform_storage_ready();
      int enumerating = mount_ret == -EAGAIN;

      attempt++;
      host = sd_runtime_find_host();
      if (!ready && host && (attempt == 1u || (attempt % 8u) == 0u)) {
         if (sd_runtime_request_redetect(tag, host, attempt))
            sd_runtime_delay_ms(SD_RUNTIME_REDETECT_SETTLE_MS);
      }
      if (!ready && host && use_grace &&
          sd_runtime_recovery_attempt_needs_hard_reset(attempt)) {
         if (sd_runtime_force_card_redetect(tag, host, NULL, attempt) == 0)
            hard_redetects++;
      }
      if (enumerating)
         enumerating_seen = 1;
      unifrog_log("unifrog storage recover tag=%s attempt=%u mount_ret=%d ready=%d enumerating=%d grace=%d hard=%lu total_ms=%lu mask=0x%lx\n",
         tag ? tag : "", attempt, mount_ret, ready, enumerating, use_grace,
         (unsigned long)hard_redetects,
         (unsigned long)(unifrog_perf_time_ms() - start_ms),
         (unsigned long)storage_mounted_mask);
      if (ready)
         return 0;
      if (!enumerating && !use_grace && attempt >= attempts)
         break;
      if (delay_ms)
         sd_runtime_delay_ms(delay_ms);
      else if (enumerating)
         sd_runtime_delay_ms(STORAGE_POLL_MS);
   }

   unifrog_log("unifrog storage recover tag=%s failed attempts=%u requested=%u total_ms=%lu mask=0x%lx\n",
      tag ? tag : "", attempt, attempts,
      (unsigned long)(unifrog_perf_time_ms() - start_ms),
      (unsigned long)storage_mounted_mask);
   return -1;
}

int unifrog_platform_recover_storage_after_io_error(const char *tag,
   unsigned attempts, unsigned delay_ms)
{
   const char *safe_tag = tag && tag[0] ? tag : "io_fault";
   const struct sd_runtime_profile *safe_profile;
   uintptr_t host;
   uint32_t start_ms = unifrog_perf_time_ms();
   int ret = -1;
   unsigned hard_redetects = 0;

   if (attempts == 0)
      attempts = 24;
   if (delay_ms == 0)
      delay_ms = 250;

   unifrog_platform_note_storage_unstable(SD_RUNTIME_FILEUART_FAULT_TICKS);
   unifrog_platform_set_storage_log_suspended(1);
   unifrog_log_sync("storage io_fault begin tag=%s attempts=%lu delay=%lums mask=0x%lx active=%s",
      safe_tag, (unsigned long)attempts, (unsigned long)delay_ms,
      (unsigned long)storage_mounted_mask, unifrog_platform_sd_active_profile());
   sd_runtime_stage(safe_tag, "io fault unmount begin", 0);
   (void)sd_runtime_unmount_storage(safe_tag);
   sd_runtime_stage(safe_tag, "io fault unmount done", 0);
   sd_mmc_bounce_abort(safe_tag);
   sd_runtime_delay_ms(SD_RUNTIME_QUIESCE_MS);

   safe_profile = sd_runtime_profile_by_name("safe");
   host = sd_runtime_find_host();
   if (safe_profile && host &&
       strcmp(unifrog_platform_sd_active_profile(), "safe") != 0) {
      int safe_ret;

      sd_runtime_stage(safe_tag, "io fault safe begin", 0);
      safe_ret = sd_runtime_reconfigure_host(safe_tag, host, safe_profile);
      sd_runtime_stage(safe_tag, "io fault safe done", safe_ret);
      if (safe_ret == 0)
         snprintf(sd_runtime_boot.active_profile,
            sizeof(sd_runtime_boot.active_profile), "safe");
      unifrog_log("unifrog storage io_fault safe_profile tag=%s ret=%d active=%s\n",
         safe_tag, safe_ret, unifrog_platform_sd_active_profile());
      sd_runtime_delay_ms(SD_RUNTIME_QUIESCE_MS);
   }

   for (unsigned i = 0; i < attempts; i++) {
      int mount_ret = unifrog_platform_mount_storage();
      int ready = unifrog_platform_storage_ready();
      int sane = ready && storage_any_target_readable(safe_tag, 1) == 0;

      unifrog_log("unifrog storage io_fault tag=%s attempt=%u mount_ret=%d ready=%d sane=%d total_ms=%lu mask=0x%lx\n",
         safe_tag, i + 1u, mount_ret, ready, sane ? 1 : 0,
         (unsigned long)(unifrog_perf_time_ms() - start_ms),
         (unsigned long)storage_mounted_mask);
      if (sane) {
         ret = 0;
         break;
      }

      if (ready && i + 1u < attempts) {
         sd_runtime_stage(safe_tag, "io fault stale unmount", -EIO);
         (void)sd_runtime_unmount_storage(safe_tag);
      }
      host = sd_runtime_find_host();
      if (!sane && host && (i == 0 || ((i + 1u) % 4u) == 0u)) {
         if (sd_runtime_request_redetect(safe_tag, host, i + 1u))
            sd_runtime_delay_ms(SD_RUNTIME_REDETECT_SETTLE_MS);
      }
      if (!sane && host &&
          sd_runtime_recovery_attempt_needs_hard_reset(i + 1u)) {
         if (sd_runtime_force_card_redetect(safe_tag, host, safe_profile,
               i + 1u) == 0)
            hard_redetects++;
      }
      if (i + 1u < attempts)
         sd_runtime_delay_ms(delay_ms);
   }

   if (ret == 0)
      unifrog_log_note_storage_quiet(SD_RUNTIME_LOG_QUIET_MS);
   else
      unifrog_platform_note_storage_unstable(SD_RUNTIME_FILEUART_FAULT_TICKS);
   unifrog_log_sync("storage io_fault done tag=%s ret=%d attempts=%lu hard=%lu total_ms=%lu mask=0x%lx",
      safe_tag, ret, (unsigned long)attempts,
      (unsigned long)hard_redetects,
      (unsigned long)(unifrog_perf_time_ms() - start_ms),
      (unsigned long)storage_mounted_mask);
   unifrog_platform_set_storage_log_suspended(0);
   return ret;
}

static int sd_runtime_rollback_failed_switch(const char *profile,
   const char *reason, uintptr_t host, unsigned mount_attempts,
   unsigned mount_delay_ms, char *detail, size_t detail_size)
{
   uint32_t start_ms = unifrog_perf_time_ms();
   int restore_ret;
   int mount_ret = mount_attempts ? -ENODEV : 0;
   int sanity_ret = mount_attempts ? -ENODEV : 0;

   if (detail && detail_size)
      detail[0] = '\0';
   if (!host)
      return -ENODEV;

   if (mount_attempts) {
      if (mount_attempts < SD_RUNTIME_RECOVERY_GRACE_ATTEMPTS)
         mount_attempts = SD_RUNTIME_RECOVERY_GRACE_ATTEMPTS;
      if (mount_delay_ms < SD_RUNTIME_RECOVERY_GRACE_DELAY_MS)
         mount_delay_ms = SD_RUNTIME_RECOVERY_GRACE_DELAY_MS;
   }

   unifrog_log_sync("sd_profile switch rollback begin profile=%s reason=%s active=%s actual=%lu",
      profile ? profile : "", reason ? reason : "",
      sd_runtime_boot.active_profile,
      (unsigned long)sd_read_u32(host, SD_MMC_HOST_ACTUAL_CLOCK_OFFSET));
   sd_mmc_bounce_abort("switch_rollback");
   sd_runtime_stage(profile, "rollback boot begin", 0);
   restore_ret = sd_runtime_reconfigure_host("switch_rollback", host, NULL);
   sd_runtime_stage(profile, "rollback boot done", restore_ret);
   if (restore_ret == 0)
      snprintf(sd_runtime_boot.active_profile,
         sizeof(sd_runtime_boot.active_profile), "boot");
   sd_runtime_delay_ms(SD_RUNTIME_QUIESCE_MS);

   if (mount_attempts) {
      sd_runtime_stage(profile, "rollback recover begin", restore_ret);
      mount_ret = unifrog_platform_recover_storage("switch_rollback",
         mount_attempts, mount_delay_ms);
      sd_runtime_stage(profile, "rollback recover done", mount_ret);
   }

   sd_runtime_log_registers("switch_rollback_after_recover", host);
   if (restore_ret == 0 && mount_ret == 0)
      sanity_ret = sd_runtime_sanity_check("boot");

   sd_runtime_format_detail("boot", host, mount_ret,
      unifrog_perf_time_ms() - start_ms, detail, detail_size);
   unifrog_log_sync("sd_profile switch rollback done profile=%s reason=%s ret=%d ms=%lu actual=%lu restore_ret=%d mount_ret=%d sanity_ret=%d detail=%s",
      profile ? profile : "", reason ? reason : "",
      (restore_ret == 0 && mount_ret == 0 && sanity_ret == 0) ? 0 : -1,
      (unsigned long)(unifrog_perf_time_ms() - start_ms),
      (unsigned long)sd_read_u32(host, SD_MMC_HOST_ACTUAL_CLOCK_OFFSET),
      restore_ret, mount_ret, sanity_ret, detail ? detail : "");
   return (restore_ret == 0 && mount_ret == 0 && sanity_ret == 0) ? 0 : -1;
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
   int sanity_ret = mount_attempts ? -ENODEV : 0;
   char failed_detail[384];
   char rollback_detail[384];

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

   memset(&sd_runtime_last_switch, 0, sizeof(sd_runtime_last_switch));
   start_ms = unifrog_perf_time_ms();
   unifrog_log_sync("sd_profile switch begin profile=%s host=0x%08lx card=0x%08lx",
      profile, (unsigned long)host,
      (unsigned long)sd_read_ptr(host, SD_MMC_HOST_CARD_OFFSET));
   unifrog_log("unifrog sd runtime switch begin profile=%s host=0x%08lx "
          "card=0x%08lx caps=0x%08lx caps2=0x%08lx\n",
      profile,
      (unsigned long)host,
      (unsigned long)sd_read_ptr(host, SD_MMC_HOST_CARD_OFFSET),
      (unsigned long)sd_read_u32(host, SD_MMC_HOST_CAPS_OFFSET),
      (unsigned long)sd_read_u32(host, SD_MMC_HOST_CAPS2_OFFSET));
   sd_runtime_log_registers("switch_begin", host);

   switch_ret = sd_runtime_profile_switch_ready(profile, host, detail,
      detail_size);
   if (switch_ret != 0) {
      unifrog_log_sync("sd_profile switch fail profile=%s reason=not_ready ret=%d detail=%s",
         profile, switch_ret, detail ? detail : "");
      return -1;
   }

   sd_runtime_stage(profile, "unmount begin", 0);
   unmount_ret = sd_runtime_unmount_storage(profile);
   if (unmount_ret != 0) {
      fileuart_note_storage_unstable(SD_RUNTIME_FILEUART_FAULT_TICKS);
      unifrog_log_note_storage_quiet(SD_RUNTIME_LOG_FAULT_MS);
      sd_runtime_format_detail(profile, host, -1,
         unifrog_perf_time_ms() - start_ms, detail, detail_size);
      unifrog_log("unifrog sd runtime switch profile=%s ret=-1 "
             "reason=unmount_failed ms=%lu\n",
         profile, (unsigned long)(unifrog_perf_time_ms() - start_ms));
      unifrog_log_sync("sd_profile switch fail profile=%s reason=unmount ms=%lu",
         profile, (unsigned long)(unifrog_perf_time_ms() - start_ms));
      sd_runtime_stage(profile, "unmount failed", unmount_ret);
      return -1;
   }
   sd_runtime_stage(profile, "unmount done", 0);
   sd_runtime_stage(profile, "settle after unmount", 0);
   sd_runtime_delay_ms(SD_RUNTIME_QUIESCE_MS);

   switch_ret = sd_runtime_reconfigure_host(profile, host, runtime_profile);
   if (switch_ret != 0) {
      int rollback_ret;

      fileuart_note_storage_unstable(SD_RUNTIME_FILEUART_FAULT_TICKS);
      unifrog_log_note_storage_quiet(SD_RUNTIME_LOG_FAULT_MS);
      sd_runtime_format_detail(profile, host, -1,
         unifrog_perf_time_ms() - start_ms, failed_detail,
         sizeof(failed_detail));
      rollback_ret = sd_runtime_rollback_failed_switch(profile,
         "reconfigure", host, mount_attempts, mount_delay_ms,
         rollback_detail, sizeof(rollback_detail));
      if (detail && detail_size)
         snprintf(detail, detail_size, "%s rollback=%d %s",
            failed_detail, rollback_ret, rollback_detail);
      unifrog_log("unifrog sd runtime switch profile=%s ret=-1 "
             "reason=reconfigure_failed switch_ret=%d rollback_ret=%d "
             "ms=%lu\n",
         profile, switch_ret,
         rollback_ret,
         (unsigned long)(unifrog_perf_time_ms() - start_ms));
      unifrog_log_sync("sd_profile switch fail profile=%s reason=reconfigure ret=%d rollback=%d ms=%lu detail=%s",
         profile, switch_ret, rollback_ret,
         (unsigned long)(unifrog_perf_time_ms() - start_ms),
         detail ? detail : failed_detail);
      return -1;
   }
   snprintf(sd_runtime_boot.active_profile,
      sizeof(sd_runtime_boot.active_profile), "%s", profile);

   if (mount_attempts) {
      sd_runtime_stage(profile, "recover begin", 0);
      mount_ret = unifrog_platform_recover_storage(profile,
         mount_attempts, mount_delay_ms);
      sd_runtime_stage(profile, "recover done", mount_ret);
   }
   sd_runtime_log_registers("after_recover", host);

   if (mount_ret == 0) {
      sd_runtime_stage(profile, "sanity begin", 0);
      sanity_ret = sd_runtime_sanity_check(profile);
      sd_runtime_stage(profile, "sanity done", sanity_ret);
      if (sanity_ret != 0) {
         fileuart_note_storage_unstable(SD_RUNTIME_FILEUART_FAULT_TICKS);
         unifrog_log_note_storage_quiet(SD_RUNTIME_LOG_FAULT_MS);
      }
   }

   if (mount_ret == 0 && sanity_ret == 0) {
      unifrog_log_note_storage_quiet(SD_RUNTIME_LOG_QUIET_MS);
   }
   if (mount_ret != 0 || sanity_ret != 0) {
      const char *reason = mount_ret != 0 ? "mount" : "sanity";
      int rollback_ret;

      fileuart_note_storage_unstable(SD_RUNTIME_FILEUART_FAULT_TICKS);
      unifrog_log_note_storage_quiet(SD_RUNTIME_LOG_FAULT_MS);
      sd_runtime_format_detail(profile, host, mount_ret,
         unifrog_perf_time_ms() - start_ms, failed_detail,
         sizeof(failed_detail));
      rollback_ret = sd_runtime_rollback_failed_switch(profile, reason,
         host, mount_attempts, mount_delay_ms, rollback_detail,
         sizeof(rollback_detail));
      if (detail && detail_size)
         snprintf(detail, detail_size, "%s rollback=%d %s",
            failed_detail, rollback_ret, rollback_detail);
      unifrog_log("unifrog sd runtime switch done profile=%s ret=-1 ms=%lu "
             "caps=0x%08lx caps2=0x%08lx actual=%lu mount_ret=%d "
             "sanity_ret=%d rollback_ret=%d\n",
         profile,
         (unsigned long)(unifrog_perf_time_ms() - start_ms),
         (unsigned long)sd_read_u32(host, SD_MMC_HOST_CAPS_OFFSET),
         (unsigned long)sd_read_u32(host, SD_MMC_HOST_CAPS2_OFFSET),
         (unsigned long)sd_read_u32(host, SD_MMC_HOST_ACTUAL_CLOCK_OFFSET),
         mount_ret, sanity_ret, rollback_ret);
      unifrog_log_sync("sd_profile switch done profile=%s ret=-1 ms=%lu actual=%lu mount_ret=%d sanity_ret=%d rollback_ret=%d detail=%s",
         profile, (unsigned long)(unifrog_perf_time_ms() - start_ms),
         (unsigned long)sd_read_u32(host, SD_MMC_HOST_ACTUAL_CLOCK_OFFSET),
         mount_ret, sanity_ret, rollback_ret,
         detail ? detail : failed_detail);
      return -1;
   }
   sd_runtime_format_detail(profile, host, mount_ret,
      unifrog_perf_time_ms() - start_ms, detail, detail_size);
   unifrog_log("unifrog sd runtime switch done profile=%s ret=%d ms=%lu "
          "caps=0x%08lx caps2=0x%08lx actual=%lu mount_ret=%d sanity_ret=%d\n",
      profile,
      (mount_ret == 0 && sanity_ret == 0) ? 0 : -1,
      (unsigned long)(unifrog_perf_time_ms() - start_ms),
      (unsigned long)sd_read_u32(host, SD_MMC_HOST_CAPS_OFFSET),
      (unsigned long)sd_read_u32(host, SD_MMC_HOST_CAPS2_OFFSET),
      (unsigned long)sd_read_u32(host, SD_MMC_HOST_ACTUAL_CLOCK_OFFSET),
      mount_ret, sanity_ret);
   unifrog_log_sync("sd_profile switch done profile=%s ret=%d ms=%lu actual=%lu mount_ret=%d sanity_ret=%d detail=%s",
      profile, (mount_ret == 0 && sanity_ret == 0) ? 0 : -1,
      (unsigned long)(unifrog_perf_time_ms() - start_ms),
      (unsigned long)sd_read_u32(host, SD_MMC_HOST_ACTUAL_CLOCK_OFFSET),
      mount_ret, sanity_ret, detail ? detail : "");

   return 0;
}

int unifrog_platform_sd_restore_boot(unsigned mount_attempts,
   unsigned mount_delay_ms, char *detail, size_t detail_size)
{
   uintptr_t host;
   uint32_t start_ms;
   int unmount_ret;
   int restore_ret = 0;
   int mount_ret = 0;
   int sanity_ret = mount_attempts ? -ENODEV : 0;

   host = sd_runtime_find_host();
   if (!host) {
      if (detail && detail_size)
         snprintf(detail, detail_size, "mmc host not found");
      unifrog_log("unifrog sd runtime restore ret=-1 reason=no_host\n");
      return -1;
   }
   if (!sd_runtime_boot.saved)
      sd_runtime_save_boot(host);

   memset(&sd_runtime_last_switch, 0, sizeof(sd_runtime_last_switch));
   start_ms = unifrog_perf_time_ms();
   unifrog_log_sync("sd_profile restore begin host=0x%08lx active=%s",
      (unsigned long)host, sd_runtime_boot.active_profile);
   unifrog_log("unifrog sd runtime restore begin host=0x%08lx active=%s\n",
      (unsigned long)host, sd_runtime_boot.active_profile);
   sd_runtime_log_registers("restore_begin", host);

   sd_runtime_stage("restore", "unmount begin", 0);
   unmount_ret = sd_runtime_unmount_storage("restore");
   if (unmount_ret != 0) {
      fileuart_note_storage_unstable(SD_RUNTIME_FILEUART_FAULT_TICKS);
      unifrog_log_note_storage_quiet(SD_RUNTIME_LOG_FAULT_MS);
      sd_runtime_format_detail("restore", host, -1,
         unifrog_perf_time_ms() - start_ms, detail, detail_size);
      unifrog_log("unifrog sd runtime restore ret=-1 reason=unmount_failed "
             "ms=%lu\n",
         (unsigned long)(unifrog_perf_time_ms() - start_ms));
      unifrog_log_sync("sd_profile restore fail reason=unmount ms=%lu",
         (unsigned long)(unifrog_perf_time_ms() - start_ms));
      sd_runtime_stage("restore", "unmount failed", unmount_ret);
      return -1;
   }
   sd_runtime_stage("restore", "unmount done", 0);
   sd_mmc_bounce_abort("restore");
   sd_runtime_stage("restore", "settle after unmount", 0);
   sd_runtime_delay_ms(SD_RUNTIME_QUIESCE_MS);

   if (strcmp(sd_runtime_boot.active_profile, "boot") != 0) {
      restore_ret = sd_runtime_reconfigure_host("restore", host, NULL);
      if (restore_ret == 0)
         snprintf(sd_runtime_boot.active_profile,
            sizeof(sd_runtime_boot.active_profile), "boot");
   } else {
      sd_runtime_stage("restore", "host already boot", 0);
   }

   if (mount_attempts) {
      if (mount_attempts < SD_RUNTIME_RECOVERY_GRACE_ATTEMPTS)
         mount_attempts = SD_RUNTIME_RECOVERY_GRACE_ATTEMPTS;
      if (mount_delay_ms < SD_RUNTIME_RECOVERY_GRACE_DELAY_MS)
         mount_delay_ms = SD_RUNTIME_RECOVERY_GRACE_DELAY_MS;
      sd_runtime_stage("restore", "recover begin", restore_ret);
      mount_ret = unifrog_platform_recover_storage("restore",
         mount_attempts, mount_delay_ms);
      sd_runtime_stage("restore", "recover done", mount_ret);
   }
   sd_runtime_log_registers("restore_after_recover", host);

   if (restore_ret == 0 && mount_ret == 0) {
      sd_runtime_stage("restore", "sanity begin", 0);
      sanity_ret = sd_runtime_sanity_check("boot");
      sd_runtime_stage("restore", "sanity done", sanity_ret);
   }

   if (restore_ret != 0 || mount_ret != 0 || sanity_ret != 0) {
      fileuart_note_storage_unstable(SD_RUNTIME_FILEUART_FAULT_TICKS);
      unifrog_log_note_storage_quiet(SD_RUNTIME_LOG_FAULT_MS);
   }
   if (restore_ret == 0 && mount_ret == 0 && sanity_ret == 0)
      unifrog_log_note_storage_quiet(SD_RUNTIME_LOG_QUIET_MS);
   sd_runtime_format_detail("boot", host, mount_ret,
      unifrog_perf_time_ms() - start_ms, detail, detail_size);
   unifrog_log("unifrog sd runtime restore done ret=%d ms=%lu caps=0x%08lx "
          "caps2=0x%08lx actual=%lu restore_ret=%d mount_ret=%d sanity_ret=%d\n",
      (restore_ret == 0 && mount_ret == 0 && sanity_ret == 0) ? 0 : -1,
      (unsigned long)(unifrog_perf_time_ms() - start_ms),
      (unsigned long)sd_read_u32(host, SD_MMC_HOST_CAPS_OFFSET),
      (unsigned long)sd_read_u32(host, SD_MMC_HOST_CAPS2_OFFSET),
      (unsigned long)sd_read_u32(host, SD_MMC_HOST_ACTUAL_CLOCK_OFFSET),
      restore_ret,
      mount_ret,
      sanity_ret);
   unifrog_log_sync("sd_profile restore done ret=%d ms=%lu actual=%lu restore_ret=%d mount_ret=%d sanity_ret=%d detail=%s",
      (restore_ret == 0 && mount_ret == 0 && sanity_ret == 0) ? 0 : -1,
      (unsigned long)(unifrog_perf_time_ms() - start_ms),
      (unsigned long)sd_read_u32(host, SD_MMC_HOST_ACTUAL_CLOCK_OFFSET),
      restore_ret, mount_ret, sanity_ret, detail ? detail : "");

   return (restore_ret == 0 && mount_ret == 0 && sanity_ret == 0) ? 0 : -1;
}

void unifrog_platform_set_storage_stage_callback(
   unifrog_platform_storage_stage_cb cb, void *userdata)
{
   storage_stage_cb = cb;
   storage_stage_userdata = userdata;
}

void unifrog_platform_set_storage_log_suspended(int suspended)
{
   if (suspended) {
      if (storage_log_suspend_depth < 0xffffffffu)
         storage_log_suspend_depth++;
   } else if (storage_log_suspend_depth > 0) {
      storage_log_suspend_depth--;
   }
   suspended = storage_log_suspend_depth > 0 ? 1 : 0;
   fileuart_set_storage_suspended(suspended);
   unifrog_log_set_disk_suspended(suspended);
   unifrog_log("unifrog storage log_suspended=%d depth=%u\n",
      suspended, storage_log_suspend_depth);
}

void unifrog_platform_note_storage_unstable(unsigned ticks)
{
   fileuart_note_storage_unstable(ticks);
   unifrog_log_note_storage_quiet(ticks);
   unifrog_log("unifrog storage unstable quiet_ticks=%u\n", ticks);
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
      int ready;
      int readable;

      mount_ret = unifrog_platform_mount_storage();
      mount_done_ms = unifrog_perf_time_ms();

      root_ready = storage_target_looks_ready(UNIFROG_SD_ROOT);
      p1_ready = storage_target_looks_ready("/media/mmcblk0p1");
      p2_ready = storage_target_looks_ready("/media/mmcblk0p2");
      ready = unifrog_platform_storage_ready();
      readable = ready && storage_any_target_readable("wait", 1) == 0;

      if (readable)
         stable++;
      else
         stable = 0;

      unifrog_log("unifrog storage poll=%d mount_ret=%d ready=%d readable=%d mount_ms=%lu check_ms=%lu total_ms=%lu root=%d p1=%d p2=%d stable=%d/%d mask=0x%lx\n",
         i, mount_ret, ready, readable,
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
         if (!storage_ready_debug_logged) {
            storage_ready_debug_logged = 1;
            unifrog_platform_sd_debug_dump("storage_ready");
         }
         return 0;
      }

      sd_runtime_delay_ms(STORAGE_POLL_MS);
   }

   unifrog_log("unifrog storage timeout total_ms=%lu attempts=%lu mask=0x%lx stable=%d/%d\n",
      (unsigned long)(unifrog_perf_time_ms() - wait_start_ms),
      (unsigned long)storage_mount_attempts,
      (unsigned long)storage_mounted_mask,
      stable, STORAGE_STABLE_SAMPLES);
   return -1;
}
