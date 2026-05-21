#ifndef UNIFROG_UI_H
#define UNIFROG_UI_H

#include <stddef.h>
#include <stdint.h>

#include <unifrog/fb.h>

#ifdef __cplusplus
extern "C" {
#endif

enum unifrog_ui_button {
   UNIFROG_UI_UP = 0,
   UNIFROG_UI_DOWN,
   UNIFROG_UI_LEFT,
   UNIFROG_UI_RIGHT,
   UNIFROG_UI_A,
   UNIFROG_UI_B,
   UNIFROG_UI_X,
   UNIFROG_UI_Y,
   UNIFROG_UI_L,
   UNIFROG_UI_R,
   UNIFROG_UI_SELECT,
   UNIFROG_UI_START,
   UNIFROG_UI_BUTTON_COUNT,
};

#define UNIFROG_UI_BUTTON_MASK(button) (1u << (button))

struct unifrog_ui {
   struct unifrog_fb fb;
   struct unifrog_ge ge;
   unsigned draw_buffer;
   int frame_open;
   int ge_ready;
   uint32_t buttons;
   uint32_t previous_buttons;
   uint32_t repeat_button;
   uint32_t next_repeat_ms;
};

struct unifrog_ui_theme {
   uint16_t background;
   uint16_t panel;
   uint16_t focus;
   uint16_t text;
   uint16_t muted;
   uint16_t accent;
   uint16_t danger;
};

int unifrog_ui_open(struct unifrog_ui *ui, int preserve_logo);
void unifrog_ui_close(struct unifrog_ui *ui);
void unifrog_ui_begin(struct unifrog_ui *ui, uint16_t color);
void unifrog_ui_present(struct unifrog_ui *ui);
void unifrog_ui_wait(struct unifrog_ui *ui);
struct unifrog_surface unifrog_ui_surface(struct unifrog_ui *ui);
void unifrog_ui_rect(struct unifrog_ui *ui, int x, int y, int w, int h,
   uint16_t color);
void unifrog_ui_text(struct unifrog_ui *ui, int x, int y, const char *text,
   uint16_t color, int scale);
void unifrog_ui_text_clipped(struct unifrog_ui *ui, int x, int y, int max_chars,
   const char *text, uint16_t color, int scale);
void unifrog_ui_header(struct unifrog_ui *ui,
   const struct unifrog_ui_theme *theme, const char *title,
   const char *detail);
void unifrog_ui_footer(struct unifrog_ui *ui,
   const struct unifrog_ui_theme *theme, const char *left,
   const char *right);
void unifrog_ui_list_row(struct unifrog_ui *ui,
   const struct unifrog_ui_theme *theme, int y, const char *label,
   const char *detail, int focused);
void unifrog_ui_message(struct unifrog_ui *ui,
   const struct unifrog_ui_theme *theme, const char *title,
   const char *line1, const char *line2);
void unifrog_ui_progress(struct unifrog_ui *ui,
   const struct unifrog_ui_theme *theme, const char *title,
   const char *line1, const char *line2, unsigned percent);
uint32_t unifrog_ui_poll(struct unifrog_ui *ui);
int unifrog_ui_pressed(const struct unifrog_ui *ui, enum unifrog_ui_button b);
int unifrog_ui_down(const struct unifrog_ui *ui, enum unifrog_ui_button b);
int unifrog_ui_repeated(struct unifrog_ui *ui, enum unifrog_ui_button b,
   uint32_t now_ms, uint32_t delay_ms, uint32_t interval_ms);
const struct unifrog_ui_theme *unifrog_ui_default_theme(void);

#ifdef __cplusplus
}
#endif

#endif
