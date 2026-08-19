#ifndef UNIFROG_DEVICE_H
#define UNIFROG_DEVICE_H

#ifdef __cplusplus
extern "C" {
#endif

enum unifrog_device_board {
   UNIFROG_DEVICE_BOARD_AUTO = 0,
   UNIFROG_DEVICE_BOARD_SF2000,
   UNIFROG_DEVICE_BOARD_GB300,
};

enum unifrog_device_panel {
   UNIFROG_DEVICE_PANEL_UNKNOWN = 0,
   UNIFROG_DEVICE_PANEL_SF2000,
   UNIFROG_DEVICE_PANEL_GB300,
};

unsigned long unifrog_device_lcd_panel_id(void);
enum unifrog_device_panel unifrog_device_panel(void);
const char *unifrog_device_panel_name(enum unifrog_device_panel panel);

int unifrog_device_set_board_override(const char *name);
const char *unifrog_device_board_override_name(void);
void unifrog_device_note_input_profile(int uses_gb300_stock_bits,
   const char *reason);
enum unifrog_device_board unifrog_device_board(void);
const char *unifrog_device_board_name(enum unifrog_device_board board);
const char *unifrog_device_variant_name(void);
int unifrog_device_uses_gb300_quirks(void);

#ifdef __cplusplus
}
#endif

#endif
