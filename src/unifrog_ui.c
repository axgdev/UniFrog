#include <unifrog/ui.h>

#include <stdio.h>
#include <string.h>

#include <kernel/lib/console.h>

#include <unifrog/boot_logo.h>
#include <unifrog/gfx.h>
#include <unifrog/input.h>
#include <unifrog/perf.h>
#include <unifrog/text.h>

static const struct unifrog_ui_theme default_theme = {
   UNIFROG_RGB565(8, 9, 12),
   UNIFROG_RGB565(20, 22, 29),
   UNIFROG_RGB565(52, 104, 132),
   UNIFROG_RGB565(238, 241, 232),
   UNIFROG_RGB565(151, 159, 157),
   UNIFROG_RGB565(238, 188, 70),
   UNIFROG_RGB565(214, 72, 77),
};

static void ui_set_handoff_buffers(struct unifrog_fb *fb, int preserve_logo)
{
   if (preserve_logo && fb->buffer_count >= 2)
      return;
   if (unifrog_fb_set_buffer_count(fb, 2) != 0)
      (void)unifrog_fb_set_buffer_count(fb, 1);
}

static uint32_t ui_button_mask(uint32_t buttons)
{
   uint32_t out = 0;

   if (buttons & UNIFROG_BUTTON_MASK(UNIFROG_BUTTON_UP))
      out |= UNIFROG_UI_BUTTON_MASK(UNIFROG_UI_UP);
   if (buttons & UNIFROG_BUTTON_MASK(UNIFROG_BUTTON_DOWN))
      out |= UNIFROG_UI_BUTTON_MASK(UNIFROG_UI_DOWN);
   if (buttons & UNIFROG_BUTTON_MASK(UNIFROG_BUTTON_LEFT))
      out |= UNIFROG_UI_BUTTON_MASK(UNIFROG_UI_LEFT);
   if (buttons & UNIFROG_BUTTON_MASK(UNIFROG_BUTTON_RIGHT))
      out |= UNIFROG_UI_BUTTON_MASK(UNIFROG_UI_RIGHT);
   if (buttons & UNIFROG_BUTTON_MASK(UNIFROG_BUTTON_A))
      out |= UNIFROG_UI_BUTTON_MASK(UNIFROG_UI_A);
   if (buttons & UNIFROG_BUTTON_MASK(UNIFROG_BUTTON_B))
      out |= UNIFROG_UI_BUTTON_MASK(UNIFROG_UI_B);
   if (buttons & UNIFROG_BUTTON_MASK(UNIFROG_BUTTON_X))
      out |= UNIFROG_UI_BUTTON_MASK(UNIFROG_UI_X);
   if (buttons & UNIFROG_BUTTON_MASK(UNIFROG_BUTTON_Y))
      out |= UNIFROG_UI_BUTTON_MASK(UNIFROG_UI_Y);
   if (buttons & UNIFROG_BUTTON_MASK(UNIFROG_BUTTON_L))
      out |= UNIFROG_UI_BUTTON_MASK(UNIFROG_UI_L);
   if (buttons & UNIFROG_BUTTON_MASK(UNIFROG_BUTTON_R))
      out |= UNIFROG_UI_BUTTON_MASK(UNIFROG_UI_R);
   if (buttons & UNIFROG_BUTTON_MASK(UNIFROG_BUTTON_SELECT))
      out |= UNIFROG_UI_BUTTON_MASK(UNIFROG_UI_SELECT);
   if (buttons & UNIFROG_BUTTON_MASK(UNIFROG_BUTTON_START))
      out |= UNIFROG_UI_BUTTON_MASK(UNIFROG_UI_START);
   return out;
}

const struct unifrog_ui_theme *unifrog_ui_default_theme(void)
{
   return &default_theme;
}

int unifrog_ui_open(struct unifrog_ui *ui, int preserve_logo)
{
   unsigned flags;

   if (!ui)
      return -1;
   memset(ui, 0, sizeof(*ui));
   flags = preserve_logo ? UNIFROG_FB_OPEN_PRESERVE : UNIFROG_FB_OPEN_DEFAULT;
   if (unifrog_fb_open(&ui->fb, flags) != 0)
      return -1;
   if (unifrog_ge_open(&ui->ge) == 0) {
      ui->ge_ready = 1;
      (void)unifrog_ge_set_fast_clock(&ui->ge);
   }
   ui_set_handoff_buffers(&ui->fb, preserve_logo);
   ui->draw_buffer = ui->fb.current_buffer;
   if (preserve_logo) {
      unifrog_boot_logo_release_early();
      (void)unifrog_boot_logo_present(&ui->fb, "frontend");
   }
   return 0;
}

void unifrog_ui_close(struct unifrog_ui *ui)
{
   if (!ui)
      return;
   unifrog_ge_close(&ui->ge);
   unifrog_fb_close(&ui->fb);
   memset(ui, 0, sizeof(*ui));
}

struct unifrog_surface unifrog_ui_surface(struct unifrog_ui *ui)
{
   return unifrog_fb_surface_for_buffer(&ui->fb, ui->draw_buffer);
}

void unifrog_ui_begin(struct unifrog_ui *ui, uint16_t color)
{
   struct unifrog_surface surface;

   if (!ui || ui->frame_open)
      return;
   ui->draw_buffer = ui->fb.current_buffer;
   if (ui->fb.buffer_count > 1)
      ui->draw_buffer = (ui->fb.current_buffer + 1u) % ui->fb.buffer_count;
   ui->frame_open = 1;
   surface = unifrog_ui_surface(ui);
   if (ui->ge_ready) {
      struct unifrog_ge_surface dst =
         unifrog_fb_ge_surface_for_buffer(&ui->fb, ui->draw_buffer);
      struct unifrog_ge_rect rect = {
         0, 0, (int)surface.width, (int)surface.height
      };
      uint32_t r = ((uint32_t)((color >> 11) & 0x1fu) * 255u + 15u) / 31u;
      uint32_t g = ((uint32_t)((color >> 5) & 0x3fu) * 255u + 31u) / 63u;
      uint32_t b = ((uint32_t)(color & 0x1fu) * 255u + 15u) / 31u;

      if (unifrog_ge_fill(&ui->ge, &dst, &rect,
          0xff000000u | (r << 16) | (g << 8) | b) == 0 &&
          unifrog_ge_sync(&ui->ge) == 0)
         return;
   }
   unifrog_gfx_fill_rect(&surface, 0, 0, surface.width, surface.height, color);
}

void unifrog_ui_present(struct unifrog_ui *ui)
{
   if (!ui)
      return;
   if (!ui->frame_open) {
      (void)unifrog_fb_wait_vsync(&ui->fb);
      return;
   }
   if (ui->ge_ready)
      (void)unifrog_ge_sync(&ui->ge);
   unifrog_fb_flush_buffer(&ui->fb, ui->draw_buffer);
   (void)unifrog_fb_pan(&ui->fb, ui->draw_buffer);
   ui->frame_open = 0;
}

void unifrog_ui_wait(struct unifrog_ui *ui)
{
   if (ui)
      (void)unifrog_fb_wait_vsync(&ui->fb);
}

void unifrog_ui_rect(struct unifrog_ui *ui, int x, int y, int w, int h,
   uint16_t color)
{
   struct unifrog_surface surface;

   if (!ui)
      return;
   surface = unifrog_ui_surface(ui);
   unifrog_gfx_fill_rect(&surface, x, y, w, h, color);
}

void unifrog_ui_text(struct unifrog_ui *ui, int x, int y, const char *text,
   uint16_t color, int scale)
{
   struct unifrog_surface surface;

   if (!ui)
      return;
   surface = unifrog_ui_surface(ui);
   unifrog_gfx_draw_text(&surface, x, y, text ? text : "", color, scale);
}

void unifrog_ui_text_clipped(struct unifrog_ui *ui, int x, int y,
   int max_chars, const char *text, uint16_t color, int scale)
{
   char clipped[96];
   size_t len;

   if (!text)
      text = "";
   if (max_chars <= 0) {
      unifrog_ui_text(ui, x, y, "", color, scale);
      return;
   }
   if ((size_t)max_chars >= sizeof(clipped))
      max_chars = (int)sizeof(clipped) - 1;
   len = strlen(text);
   if (len <= (size_t)max_chars) {
      unifrog_ui_text(ui, x, y, text, color, scale);
      return;
   }
   memcpy(clipped, text, (size_t)max_chars);
   clipped[max_chars] = '\0';
   if (max_chars >= 3) {
      clipped[max_chars - 3] = '.';
      clipped[max_chars - 2] = '.';
      clipped[max_chars - 1] = '.';
   }
   unifrog_ui_text(ui, x, y, clipped, color, scale);
}

void unifrog_ui_header(struct unifrog_ui *ui,
   const struct unifrog_ui_theme *theme, const char *title,
   const char *detail)
{
   if (!theme)
      theme = &default_theme;
   unifrog_ui_rect(ui, 0, 0, (int)ui->fb.width, 36, theme->panel);
   unifrog_ui_text_clipped(ui, 10, 8, 30, title, theme->text, 1);
   if (detail && detail[0])
      unifrog_ui_text_clipped(ui, 214, 8, 16, detail, theme->muted, 1);
}

void unifrog_ui_footer(struct unifrog_ui *ui,
   const struct unifrog_ui_theme *theme, const char *left,
   const char *right)
{
   int y;

   if (!theme)
      theme = &default_theme;
   y = (int)ui->fb.height - 22;
   unifrog_ui_rect(ui, 0, y, (int)ui->fb.width, 22, theme->panel);
   unifrog_ui_text_clipped(ui, 10, y + 7, 28, left, theme->muted, 1);
   unifrog_ui_text_clipped(ui, 220, y + 7, 14, right, theme->muted, 1);
}

void unifrog_ui_list_row(struct unifrog_ui *ui,
   const struct unifrog_ui_theme *theme, int y, const char *label,
   const char *detail, int focused)
{
   uint16_t text_color;
   uint16_t detail_color;

   if (!theme)
      theme = &default_theme;
   if (focused)
      unifrog_ui_rect(ui, 6, y - 3, (int)ui->fb.width - 12, 22, theme->focus);
   text_color = focused ? UNIFROG_RGB565(255, 255, 255) : theme->text;
   detail_color = focused ? UNIFROG_RGB565(205, 240, 235) : theme->muted;
   unifrog_ui_text_clipped(ui, 12, y + 2, 31, label, text_color, 1);
   if (detail && detail[0])
      unifrog_ui_text_clipped(ui, 224, y + 2, 13, detail, detail_color, 1);
}

void unifrog_ui_message(struct unifrog_ui *ui,
   const struct unifrog_ui_theme *theme, const char *title,
   const char *line1, const char *line2)
{
   if (!theme)
      theme = &default_theme;
   unifrog_ui_begin(ui, theme->background);
   unifrog_ui_header(ui, theme, title, "");
   unifrog_ui_rect(ui, 18, 78, (int)ui->fb.width - 36, 66, theme->panel);
   unifrog_ui_text_clipped(ui, 30, 96, 36, line1, theme->text, 1);
   unifrog_ui_text_clipped(ui, 30, 116, 36, line2, theme->muted, 1);
   unifrog_ui_present(ui);
}

void unifrog_ui_progress(struct unifrog_ui *ui,
   const struct unifrog_ui_theme *theme, const char *title,
   const char *line1, const char *line2, unsigned percent)
{
   int bar_x = 30;
   int bar_y = 134;
   int bar_w = 260;
   int bar_h = 12;
   int fill_w;
   char pct[16];

   if (!theme)
      theme = &default_theme;
   if (percent > 100u)
      percent = 100u;
   fill_w = (int)((unsigned)bar_w * percent / 100u);
   snprintf(pct, sizeof(pct), "%u%%", percent);
   unifrog_ui_begin(ui, theme->background);
   unifrog_ui_header(ui, theme, title, "");
   unifrog_ui_rect(ui, 18, 72, (int)ui->fb.width - 36, 94, theme->panel);
   unifrog_ui_text_clipped(ui, 30, 90, 36, line1, theme->text, 1);
   unifrog_ui_text_clipped(ui, 30, 110, 36, line2, theme->muted, 1);
   unifrog_ui_rect(ui, bar_x, bar_y, bar_w, bar_h, theme->background);
   if (fill_w > 0)
      unifrog_ui_rect(ui, bar_x, bar_y, fill_w, bar_h, theme->accent);
   unifrog_ui_text_clipped(ui, 148, 151, 5, pct, theme->text, 1);
   unifrog_ui_present(ui);
}

uint32_t unifrog_ui_poll(struct unifrog_ui *ui)
{
   uint32_t menu_buttons;

   if (!ui)
      return 0;
   unifrog_input_save_previous();
   unifrog_input_poll_with_wireless_divisor(1);
   menu_buttons = unifrog_input_menu_buttons();
   ui->previous_buttons = ui->buttons;
   ui->buttons = ui_button_mask(menu_buttons);
   return ui->buttons;
}

int unifrog_ui_pressed(const struct unifrog_ui *ui, enum unifrog_ui_button b)
{
   uint32_t mask = UNIFROG_UI_BUTTON_MASK(b);

   return ui && (ui->buttons & mask) && !(ui->previous_buttons & mask);
}

int unifrog_ui_down(const struct unifrog_ui *ui, enum unifrog_ui_button b)
{
   return ui && (ui->buttons & UNIFROG_UI_BUTTON_MASK(b));
}

int unifrog_ui_repeated(struct unifrog_ui *ui, enum unifrog_ui_button b,
   uint32_t now_ms, uint32_t delay_ms, uint32_t interval_ms)
{
   uint32_t mask = UNIFROG_UI_BUTTON_MASK(b);

   if (!ui)
      return 0;
   if (!(ui->buttons & mask)) {
      if (ui->repeat_button == mask) {
         ui->repeat_button = 0;
         ui->next_repeat_ms = 0;
      }
      return 0;
   }
   if (!(ui->previous_buttons & mask)) {
      ui->repeat_button = mask;
      ui->next_repeat_ms = now_ms + delay_ms;
      return 1;
   }
   if (ui->repeat_button == mask && now_ms >= ui->next_repeat_ms) {
      ui->next_repeat_ms = now_ms + interval_ms;
      return 1;
   }
   return 0;
}
