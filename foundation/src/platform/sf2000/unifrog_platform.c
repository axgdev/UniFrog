#include <unifrog/platform.h>

#include <stdbool.h>
#include <stdint.h>
#include <errno.h>
#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/unistd.h>

#include <hcuapi/gpio.h>
#include <hcuapi/pinpad.h>
#include <kernel/delay.h>

#include <cpu_func.h>
#include <fastboot/handoff.h>
#include <unifrog/build_info.h>
#include <unifrog/boot_logo.h>
#include <unifrog/boot_trace.h>
#include <unifrog/device.h>
#include <unifrog/exception_record.h>
#include <unifrog/log.h>
#include <unifrog/perf.h>
#include <unifrog/runtime.h>

#define CLOCK_GATE0_REG 0xb8800060u
#define CLOCK_GATE1_REG 0xb8800064u
#define CLOCK_GATE2_REG 0xb8800094u
#define RESET_GATE0_REG 0xb8800080u
#define RESET_GATE1_REG 0xb8800084u
#define SDIO_CLOCK_GATE0_BIT (1u << 1)
#define SDIO_CLOCK_GATE2_BITS (3u << 18)
#define SDIO_RESET_GATE1_BIT (1u << 18)

extern unsigned long PINMUXL;
extern unsigned long PINMUXR;

#define GPIO_R_OUTPUT_REG ((volatile uint32_t *)0xb88000f4u)
#define GPIO_R_DIR_REG ((volatile uint32_t *)0xb88000f8u)
#define GPIO_R07_MASK (1u << 7)
#define GPIO_L_INPUT_REG ((volatile uint32_t *)0xb8800050u)
#define GPIO_L_OUTPUT_REG ((volatile uint32_t *)0xb8800054u)
#define GPIO_L_DIR_REG ((volatile uint32_t *)0xb8800058u)
#define GPIO_R_INPUT_REG ((volatile uint32_t *)0xb88000f0u)

extern int fdt_get_node_offset_by_path(const char *path);
extern int fdt_get_property_u_32_index(int offset, const char *name, int index,
   uint32_t *outval);
extern int fdt_get_property_string_index(int offset, const char *name, int index,
   const char **outval);
extern bool fdt_property_read_bool(int offset, const char *propname);
#if UNIFROG_MMC_HOST_SOURCE_DEFAULT
extern int hc_mmc_source_bootstrap(void);
#endif

static uint32_t read_reg32(uint32_t addr)
{
   return *(volatile uint32_t *)addr;
}

static void log_clock_gate_state(const char *tag)
{
   uint32_t clk0 = read_reg32(CLOCK_GATE0_REG);
   uint32_t clk1 = read_reg32(CLOCK_GATE1_REG);
   uint32_t clk2 = read_reg32(CLOCK_GATE2_REG);
   uint32_t rst0 = read_reg32(RESET_GATE0_REG);
   uint32_t rst1 = read_reg32(RESET_GATE1_REG);
   uint32_t sdio_gate_bit = (clk0 & SDIO_CLOCK_GATE0_BIT) ? 1u : 0u;
   uint32_t sdio_aux_bits = clk2 & SDIO_CLOCK_GATE2_BITS;
   uint32_t sdio_reset_asserted =
      (rst1 & SDIO_RESET_GATE1_BIT) ? 1u : 0u;

   unifrog_log("unifrog clock gates tag=%s clk0=0x%08lx clk1=0x%08lx "
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

static unsigned pinmux_l_byte(pinpad_e pin)
{
   if (pin < 32)
      return ((volatile unsigned char *)&PINMUXL)[pin];
   return 0xffu;
}

static unsigned pinmux_r_byte(pinpad_e pin)
{
   if (pin >= 64 && pin < 96)
      return ((volatile unsigned char *)&PINMUXR)[pin - 64];
   return 0xffu;
}

static void log_hardware_fingerprint(const char *tag)
{
   unifrog_log("unifrog hw_fingerprint runtime tag=%s board=%s panel=%s variant=%s lcd_id=0x%06lx "
          "l_in=0x%08lx l_out=0x%08lx l_dir=0x%08lx "
          "r_in=0x%08lx r_out=0x%08lx r_dir=0x%08lx "
          "pm_l15=%02x pm_l23=%02x pm_l24=%02x pm_l25=%02x "
          "pm_l26=%02x pm_l27=%02x pm_l28=%02x pm_l29=%02x "
          "pm_r05=%02x pm_r07=%02x\n",
      tag ? tag : "",
      unifrog_device_board_name(unifrog_device_board()),
      unifrog_device_panel_name(unifrog_device_panel()),
      unifrog_device_variant_name(),
      unifrog_device_lcd_panel_id(),
      (unsigned long)*GPIO_L_INPUT_REG,
      (unsigned long)*GPIO_L_OUTPUT_REG,
      (unsigned long)*GPIO_L_DIR_REG,
      (unsigned long)*GPIO_R_INPUT_REG,
      (unsigned long)*GPIO_R_OUTPUT_REG,
      (unsigned long)*GPIO_R_DIR_REG,
      pinmux_l_byte(PINPAD_L15),
      pinmux_l_byte(PINPAD_L23),
      pinmux_l_byte(PINPAD_L24),
      pinmux_l_byte(PINPAD_L25),
      pinmux_l_byte(PINPAD_L26),
      pinmux_l_byte(PINPAD_L27),
      pinmux_l_byte(PINPAD_L28),
      pinmux_l_byte(PINPAD_L29),
      pinmux_r_byte(PINPAD_R05),
      pinmux_r_byte(PINPAD_R07));
}

static void init_board_gpios(void)
{
   *GPIO_R_OUTPUT_REG |= GPIO_R07_MASK;
   *GPIO_R_DIR_REG |= GPIO_R07_MASK;
   ((volatile unsigned char *)&PINMUXR)[PINPAD_R07 - 64] = 0;

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
   uint32_t max_frequency = 0;
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
   (void)fdt_get_property_u_32_index(node, "max-frequency", 0,
      &max_frequency);

   unifrog_log("unifrog storage config mode=%s experimental=%d node=%d status=%s clock=%lu max=%lu "
          "bus-width=%lu cap-highspeed=%d supports-highspeed=%d "
          "uhs-sdr12=%d uhs-sdr25=%d uhs-sdr50=%d no-1v8=%d broken-cd=%d\n",
      UNIFROG_SD_MODE,
      UNIFROG_SD_EXPERIMENTAL,
      node, status,
      (unsigned long)clock,
      (unsigned long)max_frequency,
      (unsigned long)bus_width,
      fdt_property_read_bool(node, "cap-sd-highspeed") ? 1 : 0,
      fdt_property_read_bool(node, "supports-highspeed") ? 1 : 0,
      fdt_property_read_bool(node, "sd-uhs-sdr12") ? 1 : 0,
      fdt_property_read_bool(node, "sd-uhs-sdr25") ? 1 : 0,
      fdt_property_read_bool(node, "sd-uhs-sdr50") ? 1 : 0,
      fdt_property_read_bool(node, "no-support_1_8v") ? 1 : 0,
      fdt_property_read_bool(node, "broken-cd") ? 1 : 0);
}

static const char *fastboot_diag_name(uint32_t event)
{
   switch (event) {
   case FASTBOOT_DIAG_STAGE1_START: return "stage1_start";
   case FASTBOOT_DIAG_MOUNT: return "mount";
   case FASTBOOT_DIAG_HANDOFF_FOUND: return "handoff_found";
   case FASTBOOT_DIAG_HANDOFF_BOOT: return "handoff_boot";
   case FASTBOOT_DIAG_HANDOFF_MISSING: return "handoff_missing";
   case FASTBOOT_DIAG_RAW_OPEN: return "raw_open";
   case FASTBOOT_DIAG_RAW_OPEN_FAILED: return "raw_open_failed";
   case FASTBOOT_DIAG_RAW_SKIP_FAILED: return "raw_skip_failed";
   case FASTBOOT_DIAG_RAW_LOADED: return "raw_loaded";
   case FASTBOOT_DIAG_RAW_LOAD_FAILED: return "raw_load_failed";
   case FASTBOOT_DIAG_RAW_TOO_LARGE: return "raw_too_large";
   case FASTBOOT_DIAG_ASD_OPEN: return "asd_open";
   case FASTBOOT_DIAG_ASD_OPEN_FAILED: return "asd_open_failed";
   case FASTBOOT_DIAG_ASD_SIZE_INVALID: return "asd_size_invalid";
   case FASTBOOT_DIAG_ASD_SKIP_FAILED: return "asd_skip_failed";
   case FASTBOOT_DIAG_ASD_LOAD_FAILED: return "asd_load_failed";
   case FASTBOOT_DIAG_ASD_LOADED: return "asd_loaded";
   case FASTBOOT_DIAG_JUMP: return "jump";
   case FASTBOOT_DIAG_FAIL: return "no_bootable_payload";
   case FASTBOOT_DIAG_HANDOFF_REQUEST: return "handoff_request";
   case FASTBOOT_DIAG_RESET_REQUEST: return "reset_request";
   case FASTBOOT_DIAG_DIRECT_REQUEST: return "direct_request";
   default: return "unknown";
   }
}

static int fastboot_diag_failed(uint32_t event, int32_t result)
{
   if (result < 0 || event == FASTBOOT_DIAG_FAIL)
      return 1;
   if (event == FASTBOOT_DIAG_MOUNT)
      return result != 0;
   return event == FASTBOOT_DIAG_RAW_OPEN_FAILED ||
      event == FASTBOOT_DIAG_RAW_SKIP_FAILED ||
      event == FASTBOOT_DIAG_RAW_LOAD_FAILED ||
      event == FASTBOOT_DIAG_RAW_TOO_LARGE ||
      event == FASTBOOT_DIAG_ASD_OPEN_FAILED ||
      event == FASTBOOT_DIAG_ASD_SIZE_INVALID ||
      event == FASTBOOT_DIAG_ASD_SKIP_FAILED ||
      event == FASTBOOT_DIAG_ASD_LOAD_FAILED;
}

static void log_fastboot_diag(void)
{
   volatile struct fastboot_diag *diag = FASTBOOT_DIAG_ADDR;

   if (diag->magic != FASTBOOT_DIAG_MAGIC)
      return;

   unifrog_log_at(fastboot_diag_failed(diag->event, diag->result) ?
         UNIFROG_LOG_ERROR : UNIFROG_LOG_INFO, "fastboot",
      "stage=0x%08lx event=%lu event_name=%s result=%ld path=%s",
      (unsigned long)diag->stage_addr,
      (unsigned long)diag->event,
      fastboot_diag_name(diag->event),
      (long)diag->result,
      (const char *)diag->path);
   diag->magic = 0;
}

void unifrog_platform_init_board(void)
{
#if UNIFROG_MMC_HOST_SOURCE_DEFAULT
   int source_mmc_ret = 0;
#endif

   log_fastboot_diag();
   unifrog_boot_trace_log("platform.init_board");
   log_clock_gate_state("board_init_begin");
   log_hardware_fingerprint("pre_gpio_init");
   init_board_gpios();
   log_storage_config();
#if UNIFROG_MMC_HOST_SOURCE_DEFAULT
   unifrog_log("unifrog source_mmc bootstrap begin time_ms=%lu\n",
      (unsigned long)unifrog_perf_time_ms());
   unifrog_exception_activity_set(UNIFROG_ACTIVITY_PHASE_FRONTEND_STORAGE,
      unifrog_exception_activity_hash("source_mmc.bootstrap"), 0,
      unifrog_perf_time_ms());
   unifrog_boot_trace_mark(FASTBOOT_TRACE_UNIFROG_SOURCE_MMC_BOOTSTRAP_BEGIN,
      unifrog_perf_time_ms(), 0, 0);
   source_mmc_ret = hc_mmc_source_bootstrap();
   unifrog_boot_trace_mark(FASTBOOT_TRACE_UNIFROG_SOURCE_MMC_BOOTSTRAP_DONE,
      unifrog_perf_time_ms(), (uint32_t)source_mmc_ret, 0);
   unifrog_log("unifrog source_mmc bootstrap ret=%d\n", source_mmc_ret);
#endif
   /*
    * Vendor builds have already probed the MMC host during module init; source
    * builds bootstrap it here after the early logo path is active. Rewriting
    * global gate/reset registers still races card enumeration, so board init
    * only observes them and leaves ownership with the individual drivers.
    */
   log_clock_gate_state("board_init_done");
}

void unifrog_platform_idle_forever(void)
{
   for (;;)
      usleep(1000);
}

__attribute__((weak)) void fileuart_note_storage_unstable(unsigned ticks)
{
   (void)ticks;
}

__attribute__((weak)) void fileuart_get_debug_status(uint32_t *pending,
   uint32_t *suspended, uint32_t *quiet_ticks, uint32_t *dirty_bytes)
{
   if (pending)
      *pending = 0;
   if (suspended)
      *suspended = 0;
   if (quiet_ticks)
      *quiet_ticks = 0;
   if (dirty_bytes)
      *dirty_bytes = 0;
}

void unifrog_platform_debug_status(struct unifrog_platform_debug_status *status)
{
   if (!status)
      return;
   memset(status, 0, sizeof(*status));
   fileuart_get_debug_status(&status->pending, &status->suspended,
      &status->quiet_ticks, &status->dirty_bytes);
}

int unifrog_platform_debug_write(const void *data, size_t size,
   unsigned repeat)
{
   const unsigned char *bytes = data;
   int fd;
   int ret = 0;

   if ((!data && size) || !repeat)
      return -EINVAL;
   fd = open("/dev/fileuart", O_WRONLY);
   if (fd < 0)
      return errno ? -errno : -EIO;
   for (unsigned i = 0; i < repeat && ret == 0; i++) {
      size_t offset = 0;

      while (offset < size) {
         ssize_t wrote = write(fd, bytes + offset, size - offset);

         if (wrote <= 0) {
            ret = errno ? -errno : -EIO;
            break;
         }
         offset += (size_t)wrote;
      }
   }
   if (close(fd) != 0 && ret == 0)
      ret = errno ? -errno : -EIO;
   return ret;
}
