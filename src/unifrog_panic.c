#include <stdint.h>

#include <cpu_func.h>

#include <unifrog/audio.h>
#include <unifrog/exception_record.h>
#include <unifrog/fb.h>
#include <unifrog/gfx.h>
#include <unifrog/input.h>
#include <unifrog/log.h>

static char hex_digit(unsigned value)
{
   value &= 0xfu;
   return (char)(value < 10u ? ('0' + value) : ('A' + value - 10u));
}

static void put_hex32(char *out, uint32_t value)
{
   out[0] = '0';
   out[1] = 'x';
   for (unsigned i = 0; i < 8u; i++)
      out[2 + i] = hex_digit(value >> ((7u - i) * 4u));
   out[10] = '\0';
}

static void draw_label_hex(const struct unifrog_surface *surface, int y,
   const char *label, uint32_t value)
{
   char hex[11];

   put_hex32(hex, value);
   unifrog_gfx_draw_text(surface, 18, y, label, UNIFROG_RGB565(180, 190, 205), 1);
   unifrog_gfx_draw_text(surface, 98, y, hex, UNIFROG_RGB565(250, 246, 230), 1);
}

static void draw_panic_screen(const char *title, const char *label0,
   uint32_t value0, const char *label1, uint32_t value1, const char *label2,
   uint32_t value2, const char *label3, uint32_t value3)
{
   struct unifrog_fb fb;

   if (unifrog_fb_open(&fb, UNIFROG_FB_OPEN_DEFAULT) == 0) {
      for (unsigned i = 0; i < fb.buffer_count; i++) {
         struct unifrog_surface surface =
            unifrog_fb_surface_for_buffer(&fb, i);

         unifrog_gfx_fill_rect(&surface, 0, 0, surface.width,
            surface.height, UNIFROG_RGB565(28, 6, 18));
         unifrog_gfx_fill_rect(&surface, 0, 0, surface.width, 34,
            UNIFROG_RGB565(126, 22, 46));
         unifrog_gfx_draw_text(&surface, 18, 10,
            title ? title : "UNIFROG PANIC",
            UNIFROG_RGB565(255, 245, 235), 1);
         draw_label_hex(&surface, 46, label0 ? label0 : "A", value0);
         draw_label_hex(&surface, 66, label1 ? label1 : "B", value1);
         draw_label_hex(&surface, 86, label2 ? label2 : "C", value2);
         draw_label_hex(&surface, 106, label3 ? label3 : "D", value3);
         unifrog_gfx_draw_text(&surface, 18, 136,
            "START reset after recording",
            UNIFROG_RGB565(210, 184, 144), 1);
         unifrog_fb_flush_buffer(&fb, i);
      }
      (void)unifrog_fb_pan(&fb, 0);
   }
}

static void panic_reset_system(uint32_t buttons)
{
   unifrog_audio_set_system_output_enabled(0);
   unifrog_log("unifrog panic reset buttons=0x%08lx\n",
      (unsigned long)buttons);
   (void)unifrog_log_flush_force();
   hw_watchdog_reset(1000);
   reset();
   for (;;)
      ;
}

static uint32_t panic_poll_buttons(void)
{
   return unifrog_input_poll_local_direct_buttons();
}

static void wait_for_start_reset(void)
{
   unifrog_input_restore_local_bus();
   for (;;) {
      uint32_t buttons = panic_poll_buttons();

      if (buttons & UNIFROG_BUTTON_MASK(UNIFROG_BUTTON_START))
         panic_reset_system(buttons);
      for (volatile unsigned i = 0; i < 200000u; i++)
         __asm__ volatile("");
   }
}

void unifrog_panic_screen(const char *title, uint32_t a, uint32_t b,
   uint32_t c, uint32_t d)
{
   draw_panic_screen(title, "A", a, "B", b, "C", c, "D", d);
   wait_for_start_reset();
}

void unifrog_panic_screen_labeled(const char *title, const char *label0,
   uint32_t value0, const char *label1, uint32_t value1,
   const char *label2, uint32_t value2, const char *label3, uint32_t value3)
{
   draw_panic_screen(title, label0, value0, label1, value1, label2,
      value2, label3, value3);
   wait_for_start_reset();
}

void unifrog_exception_panic(uint32_t cause, uint32_t epc, uint32_t badvaddr,
   uint32_t ra)
{
   unifrog_exception_record_store(cause, epc, badvaddr, ra);
   unifrog_log("unifrog exception cause=0x%08lx epc=0x%08lx badv=0x%08lx ra=0x%08lx\n",
      (unsigned long)cause, (unsigned long)epc, (unsigned long)badvaddr,
      (unsigned long)ra);
   (void)unifrog_log_flush_force();
   draw_panic_screen("UNIFROG EXCEPTION", "CAUSE", cause, "EPC", epc,
      "BADV", badvaddr, "RA", ra);
   wait_for_start_reset();
}

void unifrog_panic_trigger_test_exception(void)
{
   unifrog_exception_panic(0x54455354u, 0x00000000u, 0x00000002u,
      0x00000000u);
}

void unifrog_panic_trigger_cpu_exception(void)
{
   unifrog_log("unifrog panic trigger cpu exception\n");
   (void)unifrog_log_flush_force();
   __asm__ volatile(
      ".set push\n"
      ".set noreorder\n"
      "break 0x123\n"
      "nop\n"
      ".set pop\n"
      ::: "memory");
   unifrog_exception_panic(0x42524b21u, 0x00000000u, 0x00000000u,
      0x00000000u);
}
