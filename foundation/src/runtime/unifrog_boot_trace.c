#include <unifrog/boot_trace.h>

#include <fastboot/handoff.h>
#include <unifrog/log.h>

static uint32_t boot_trace_logged;

uint32_t unifrog_boot_trace_r05_state(void)
{
   uint32_t state = *(volatile uint8_t *)(uintptr_t)FASTBOOT_PINMUX_R05_ADDR;

   if (*(volatile uint32_t *)(uintptr_t)FASTBOOT_GPIOR_DIR_REG &
       FASTBOOT_BACKLIGHT_R05_MASK)
      state |= 0x100u;
   if (*(volatile uint32_t *)(uintptr_t)FASTBOOT_GPIOR_OUTPUT_REG &
       FASTBOOT_BACKLIGHT_R05_MASK)
      state |= 0x200u;
   return state;
}

void unifrog_boot_trace_mark(uint32_t event, uint32_t arg0,
   uint32_t arg1, uint32_t arg2)
{
   volatile struct fastboot_trace *trace = FASTBOOT_TRACE_ADDR;
   uint32_t index;

   if (trace->magic != FASTBOOT_TRACE_MAGIC ||
       trace->version != FASTBOOT_TRACE_VERSION) {
      trace->magic = FASTBOOT_TRACE_MAGIC;
      trace->version = FASTBOOT_TRACE_VERSION;
      trace->count = 0;
      trace->dropped = 0;
      boot_trace_logged = 0;
   }

   if (trace->count >= FASTBOOT_TRACE_ENTRIES) {
      trace->dropped++;
      return;
   }

   index = trace->count++;
   trace->entries[index].seq = index;
   trace->entries[index].event = event;
   trace->entries[index].arg0 = arg0;
   trace->entries[index].arg1 = arg1;
   trace->entries[index].arg2 = arg2;
   trace->entries[index].r05_state = unifrog_boot_trace_r05_state();
}

static const char *boot_trace_name(uint32_t event)
{
   switch (event) {
   case FASTBOOT_TRACE_STUB_BACKLIGHT_OFF:
      return "fastboot.stub.backlight_off";
   case FASTBOOT_TRACE_STAGE1_START:
      return "fastboot.stage1.start";
   case FASTBOOT_TRACE_STAGE1_BACKLIGHT_OFF:
      return "fastboot.stage1.backlight_off";
   case FASTBOOT_TRACE_STAGE1_MOUNT_RESULT:
      return "fastboot.stage1.mount_result";
   case FASTBOOT_TRACE_STAGE1_HANDOFF_RESULT:
      return "fastboot.stage1.handoff_result";
   case FASTBOOT_TRACE_STAGE1_LOAD_START:
      return "fastboot.stage1.load_start";
   case FASTBOOT_TRACE_STAGE1_LOAD_DONE:
      return "fastboot.stage1.load_done";
   case FASTBOOT_TRACE_STAGE1_ASD_START:
      return "fastboot.stage1.asd_start";
   case FASTBOOT_TRACE_STAGE1_ASD_DONE:
      return "fastboot.stage1.asd_done";
   case FASTBOOT_TRACE_STAGE1_JUMP:
      return "fastboot.stage1.jump";
   case FASTBOOT_TRACE_STAGE1_FAIL:
      return "fastboot.stage1.fail";
   case FASTBOOT_TRACE_STAGE1_INPUT_OVERRIDE:
      return "fastboot.stage1.input_override";
   case FASTBOOT_TRACE_STAGE1_DEFAULT_RESULT:
      return "fastboot.stage1.default_result";
   case FASTBOOT_TRACE_STAGE1_HW_GPIO_L:
      return "fastboot.stage1.hw_gpio_l";
   case FASTBOOT_TRACE_STAGE1_HW_GPIO_R:
      return "fastboot.stage1.hw_gpio_r";
   case FASTBOOT_TRACE_STAGE1_HW_PINMUX:
      return "fastboot.stage1.hw_pinmux";
   case FASTBOOT_TRACE_STAGE1_INPUT_SF2000_RAW:
      return "fastboot.stage1.input_sf2000_raw";
   case FASTBOOT_TRACE_STAGE1_INPUT_GB300_RAW:
      return "fastboot.stage1.input_gb300_raw";
   case FASTBOOT_TRACE_STAGE1_BOARD_DETECT:
      return "fastboot.stage1.board_detect";
   case FASTBOOT_TRACE_UNIFROG_MAIN_START:
      return "unifrog.main.start";
   case FASTBOOT_TRACE_UNIFROG_APP_MAIN_START:
      return "unifrog.app_main.start";
   case FASTBOOT_TRACE_UNIFROG_MODULE_INIT_BEGIN:
      return "unifrog.module_init.begin";
   case FASTBOOT_TRACE_UNIFROG_MODULE_INIT_DONE:
      return "unifrog.module_init.done";
   case FASTBOOT_TRACE_UNIFROG_FRONTEND_THREAD_START:
      return "unifrog.frontend_thread.start";
   case FASTBOOT_TRACE_UNIFROG_BOARD_INIT_BEGIN:
      return "unifrog.board_init.begin";
   case FASTBOOT_TRACE_UNIFROG_BOARD_INIT_DONE:
      return "unifrog.board_init.done";
   case FASTBOOT_TRACE_UNIFROG_STORAGE_DONE:
      return "unifrog.storage.done";
   case FASTBOOT_TRACE_UNIFROG_LOG_RESET_DONE:
      return "unifrog.log_reset.done";
   case FASTBOOT_TRACE_UNIFROG_FRONTEND_BEGIN:
      return "unifrog.frontend.begin";
   case FASTBOOT_TRACE_UNIFROG_FB_OPEN_BEGIN:
      return "unifrog.fb_open.begin";
   case FASTBOOT_TRACE_UNIFROG_FB_CLEAR_DONE:
      return "unifrog.fb_clear.done";
   case FASTBOOT_TRACE_UNIFROG_BOOT_LOGO_DONE:
      return "unifrog.boot_logo.done";
   case FASTBOOT_TRACE_UNIFROG_INPUT_LOCAL_DONE:
      return "unifrog.input.local_done";
   case FASTBOOT_TRACE_UNIFROG_INPUT_WIRELESS_DONE:
      return "unifrog.input.wireless_done";
   case FASTBOOT_TRACE_UNIFROG_INPUT_CLEAR_DONE:
      return "unifrog.input.clear_done";
   case FASTBOOT_TRACE_UNIFROG_GE_OPEN_DONE:
      return "unifrog.ge.open_done";
   case FASTBOOT_TRACE_UNIFROG_GE_FILL_SETUP:
      return "unifrog.ge.fill_setup";
   case FASTBOOT_TRACE_UNIFROG_GE_STATE_DONE:
      return "unifrog.ge.state_done";
   case FASTBOOT_TRACE_UNIFROG_GE_SUBMIT_BEGIN:
      return "unifrog.ge.submit_begin";
   case FASTBOOT_TRACE_UNIFROG_GE_SUBMIT_DONE:
      return "unifrog.ge.submit_done";
   case FASTBOOT_TRACE_UNIFROG_GE_CLOCK:
      return "unifrog.ge.clock";
   case FASTBOOT_TRACE_UNIFROG_SOURCE_MMC_BOOTSTRAP_BEGIN:
      return "unifrog.source_mmc.bootstrap_begin";
   case FASTBOOT_TRACE_UNIFROG_SOURCE_MMC_BOOTSTRAP_DONE:
      return "unifrog.source_mmc.bootstrap_done";
   case FASTBOOT_TRACE_UNIFROG_SOURCE_MMC_DRIVER_REGISTER_DONE:
      return "unifrog.source_mmc.driver_register_done";
   case FASTBOOT_TRACE_UNIFROG_SOURCE_MMC_DEVICE_REGISTER_BEGIN:
      return "unifrog.source_mmc.device_register_begin";
   case FASTBOOT_TRACE_UNIFROG_SOURCE_MMC_DEVICE_REGISTER_DONE:
      return "unifrog.source_mmc.device_register_done";
   case FASTBOOT_TRACE_UNIFROG_SOURCE_MMC_PROBE_BEGIN:
      return "unifrog.source_mmc.probe_begin";
   case FASTBOOT_TRACE_UNIFROG_SOURCE_MMC_PROBE_DONE:
      return "unifrog.source_mmc.probe_done";
   case FASTBOOT_TRACE_SDK_PWM_PROBE_BEGIN:
      return "sdk.pwm.probe_begin";
   case FASTBOOT_TRACE_SDK_PWM_PINMUX_ACTIVE:
      return "sdk.pwm.pinmux_active";
   case FASTBOOT_TRACE_SDK_PWM_REGISTER_DONE:
      return "sdk.pwm.register_done";
   case FASTBOOT_TRACE_SDK_BACKLIGHT_PROBE_BEGIN:
      return "sdk.backlight.probe_begin";
   case FASTBOOT_TRACE_SDK_BACKLIGHT_DEFAULT_OFF:
      return "sdk.backlight.default_off";
   case FASTBOOT_TRACE_SDK_BACKLIGHT_PWM_DUTY:
      return "sdk.backlight.pwm_duty";
   case FASTBOOT_TRACE_SDK_BACKLIGHT_GPIO_STATUS:
      return "sdk.backlight.gpio_status";
   case FASTBOOT_TRACE_SDK_BACKLIGHT_WRITE:
      return "sdk.backlight.write";
   case FASTBOOT_TRACE_SDK_LCD_PROBE_BEGIN:
      return "sdk.lcd.probe_begin";
   case FASTBOOT_TRACE_SDK_LCD_DISPLAY_BEGIN:
      return "sdk.lcd.display_begin";
   case FASTBOOT_TRACE_SDK_LCD_RESET_DONE:
      return "sdk.lcd.reset_done";
   case FASTBOOT_TRACE_SDK_LCD_PANEL_PROBE_DONE:
      return "sdk.lcd.panel_probe_done";
   case FASTBOOT_TRACE_SDK_LCD_INIT_SEQ_DONE:
      return "sdk.lcd.init_seq_done";
   case FASTBOOT_TRACE_SDK_LCD_DISPON:
      return "sdk.lcd.dispon";
   case FASTBOOT_TRACE_SDK_LCD_RGB_ON:
      return "sdk.lcd.rgb_on";
   case FASTBOOT_TRACE_SDK_FB_PROBE_BEGIN:
      return "sdk.fb.probe_begin";
   case FASTBOOT_TRACE_SDK_FB_ALLOC_DONE:
      return "sdk.fb.alloc_done";
   case FASTBOOT_TRACE_SDK_FB_REGISTER_DONE:
      return "sdk.fb.register_done";
   case FASTBOOT_TRACE_SDK_FB_GMA_OFF:
      return "sdk.fb.gma_off";
   default:
      return "unknown";
   }
}

static const char *fastboot_board_name(uint32_t board)
{
   switch (board) {
   case FASTBOOT_DEVICE_BOARD_SF2000:
      return "sf2000";
   case FASTBOOT_DEVICE_BOARD_GB300:
      return "gb300";
   default:
      return "unknown";
   }
}

static uint32_t unpack8(uint32_t value, unsigned index)
{
   return (value >> (index * 8u)) & 0xffu;
}

int unifrog_boot_trace_fastboot_board(uint32_t *board, uint32_t *marks,
   uint32_t *scores)
{
   volatile struct fastboot_trace *trace = FASTBOOT_TRACE_ADDR;
   uint32_t count;

   if (trace->magic != FASTBOOT_TRACE_MAGIC ||
       trace->version != FASTBOOT_TRACE_VERSION)
      return 0;

   count = trace->count;
   if (count > FASTBOOT_TRACE_ENTRIES)
      count = FASTBOOT_TRACE_ENTRIES;

   for (uint32_t i = 0; i < count; i++) {
      volatile struct fastboot_trace_entry *entry = &trace->entries[i];

      if (entry->event != FASTBOOT_TRACE_STAGE1_BOARD_DETECT)
         continue;
      if (board)
         *board = entry->arg0;
      if (marks)
         *marks = entry->arg1;
      if (scores)
         *scores = entry->arg2;
      return 1;
   }

   return 0;
}

void unifrog_boot_trace_log_hardware_fingerprint(const char *tag)
{
   volatile struct fastboot_trace *trace = FASTBOOT_TRACE_ADDR;
   uint32_t count;
   uint32_t sf_and = 0;
   uint32_t sf_or = 0;
   uint32_t sf_last = 0;
   uint32_t gb_and = 0;
   uint32_t gb_or = 0;
   uint32_t gb_last = 0;
   uint32_t l_in = 0;
   uint32_t l_out = 0;
   uint32_t l_dir = 0;
   uint32_t r_in = 0;
   uint32_t r_out = 0;
   uint32_t r_dir = 0;
   uint32_t pinmux_l15_l23_l25 = 0;
   uint32_t pinmux_l26_l29 = 0;
   uint32_t pinmux_r05_r07 = 0;
   uint32_t detected_board = FASTBOOT_DEVICE_BOARD_UNKNOWN;
   uint32_t detected_marks = 0;
   uint32_t detected_scores = 0;
   int have_sf = 0;
   int have_gb = 0;
   int have_l = 0;
   int have_r = 0;
   int have_pinmux = 0;
   int have_board = 0;

   if (trace->magic != FASTBOOT_TRACE_MAGIC ||
       trace->version != FASTBOOT_TRACE_VERSION)
      return;

   count = trace->count;
   if (count > FASTBOOT_TRACE_ENTRIES)
      count = FASTBOOT_TRACE_ENTRIES;

   for (uint32_t i = 0; i < count; i++) {
      volatile struct fastboot_trace_entry *entry = &trace->entries[i];

      switch (entry->event) {
      case FASTBOOT_TRACE_STAGE1_HW_GPIO_L:
         l_in = entry->arg0;
         l_out = entry->arg1;
         l_dir = entry->arg2;
         have_l = 1;
         break;
      case FASTBOOT_TRACE_STAGE1_HW_GPIO_R:
         r_in = entry->arg0;
         r_out = entry->arg1;
         r_dir = entry->arg2;
         have_r = 1;
         break;
      case FASTBOOT_TRACE_STAGE1_HW_PINMUX:
         pinmux_l15_l23_l25 = entry->arg0;
         pinmux_l26_l29 = entry->arg1;
         pinmux_r05_r07 = entry->arg2;
         have_pinmux = 1;
         break;
      case FASTBOOT_TRACE_STAGE1_INPUT_SF2000_RAW:
         sf_and = entry->arg0;
         sf_or = entry->arg1;
         sf_last = entry->arg2;
         have_sf = 1;
         break;
      case FASTBOOT_TRACE_STAGE1_INPUT_GB300_RAW:
         gb_and = entry->arg0;
         gb_or = entry->arg1;
         gb_last = entry->arg2;
         have_gb = 1;
         break;
      case FASTBOOT_TRACE_STAGE1_BOARD_DETECT:
         detected_board = entry->arg0;
         detected_marks = entry->arg1;
         detected_scores = entry->arg2;
         have_board = 1;
         break;
      default:
         break;
      }
   }

   if (!have_sf && !have_gb && !have_l && !have_r && !have_pinmux &&
       !have_board)
      return;

   unifrog_log("unifrog hw_fingerprint fastboot tag=%s have_sf=%d have_gb=%d have_gpio_l=%d have_gpio_r=%d have_pinmux=%d have_board=%d "
         "board=%s board_id=%lu marks=0x%08lx score_sf=%lu score_gb=%lu "
         "sf2000_and=0x%08lx sf2000_or=0x%08lx sf2000_last=0x%08lx "
         "gb300_and=0x%08lx gb300_or=0x%08lx gb300_last=0x%08lx "
         "l_in=0x%08lx l_out=0x%08lx l_dir=0x%08lx "
         "r_in=0x%08lx r_out=0x%08lx r_dir=0x%08lx "
         "pm_l15=%02lx pm_l23=%02lx pm_l24=%02lx pm_l25=%02lx "
         "pm_l26=%02lx pm_l27=%02lx pm_l28=%02lx pm_l29=%02lx "
         "pm_r05=%02lx pm_r07=%02lx\n",
      tag ? tag : "",
      have_sf, have_gb, have_l, have_r, have_pinmux, have_board,
      fastboot_board_name(detected_board), (unsigned long)detected_board,
      (unsigned long)detected_marks,
      (unsigned long)((detected_scores >> 16) & 0xffu),
      (unsigned long)(detected_scores & 0xffu),
      (unsigned long)sf_and, (unsigned long)sf_or,
      (unsigned long)sf_last, (unsigned long)gb_and,
      (unsigned long)gb_or, (unsigned long)gb_last,
      (unsigned long)l_in, (unsigned long)l_out, (unsigned long)l_dir,
      (unsigned long)r_in, (unsigned long)r_out, (unsigned long)r_dir,
      (unsigned long)unpack8(pinmux_l15_l23_l25, 0),
      (unsigned long)unpack8(pinmux_l15_l23_l25, 1),
      (unsigned long)unpack8(pinmux_l15_l23_l25, 2),
      (unsigned long)unpack8(pinmux_l15_l23_l25, 3),
      (unsigned long)unpack8(pinmux_l26_l29, 0),
      (unsigned long)unpack8(pinmux_l26_l29, 1),
      (unsigned long)unpack8(pinmux_l26_l29, 2),
      (unsigned long)unpack8(pinmux_l26_l29, 3),
      (unsigned long)unpack8(pinmux_r05_r07, 0),
      (unsigned long)unpack8(pinmux_r05_r07, 1));
}

void unifrog_boot_trace_log(const char *tag)
{
   volatile struct fastboot_trace *trace = FASTBOOT_TRACE_ADDR;
   uint32_t count;

   if (trace->magic != FASTBOOT_TRACE_MAGIC ||
       trace->version != FASTBOOT_TRACE_VERSION)
      return;

   count = trace->count;
   if (count > FASTBOOT_TRACE_ENTRIES)
      count = FASTBOOT_TRACE_ENTRIES;

   unifrog_log("unifrog boot_trace tag=%s count=%lu logged=%lu dropped=%lu r05=0x%03lx\n",
      tag ? tag : "",
      (unsigned long)count,
      (unsigned long)boot_trace_logged,
      (unsigned long)trace->dropped,
      (unsigned long)unifrog_boot_trace_r05_state());

   while (boot_trace_logged < count) {
      volatile struct fastboot_trace_entry *entry =
         &trace->entries[boot_trace_logged];
      unifrog_log("unifrog boot_trace seq=%lu event=%lu name=%s arg0=0x%08lx arg1=0x%08lx arg2=0x%08lx r05=0x%03lx\n",
         (unsigned long)entry->seq,
         (unsigned long)entry->event,
         boot_trace_name(entry->event),
         (unsigned long)entry->arg0,
         (unsigned long)entry->arg1,
         (unsigned long)entry->arg2,
         (unsigned long)entry->r05_state);
      boot_trace_logged++;
   }
}
