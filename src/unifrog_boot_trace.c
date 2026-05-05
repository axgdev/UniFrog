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
   case FASTBOOT_TRACE_UNIFROG_JS_BEGIN:
      return "unifrog.js.begin";
   case FASTBOOT_TRACE_UNIFROG_FB_OPEN_BEGIN:
      return "unifrog.fb_open.begin";
   case FASTBOOT_TRACE_UNIFROG_FB_CLEAR_DONE:
      return "unifrog.fb_clear.done";
   case FASTBOOT_TRACE_UNIFROG_BOOT_LOGO_DONE:
      return "unifrog.boot_logo.done";
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
