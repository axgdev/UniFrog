#ifndef UNIFROG_INPUT_H
#define UNIFROG_INPUT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define UNIFROG_INPUT_MAX_PORTS 2u

/* Button masks are normalized across local and wireless controllers. */
enum unifrog_button {
   UNIFROG_BUTTON_R = 0,
   UNIFROG_BUTTON_Y,
   UNIFROG_BUTTON_X,
   UNIFROG_BUTTON_L,
   UNIFROG_BUTTON_A,
   UNIFROG_BUTTON_B,
   UNIFROG_BUTTON_SELECT,
   UNIFROG_BUTTON_START,
   UNIFROG_BUTTON_UP,
   UNIFROG_BUTTON_DOWN,
   UNIFROG_BUTTON_LEFT,
   UNIFROG_BUTTON_RIGHT,
   UNIFROG_BUTTON_COUNT,
};

#define UNIFROG_BUTTON_MASK(button) (1u << (button))

struct unifrog_input_snapshot {
   uint32_t buttons;
   uint32_t previous_buttons;
   uint32_t local_buttons;
   uint32_t local_raw;
   uint32_t wireless_buttons[UNIFROG_INPUT_MAX_PORTS];
   uint32_t wireless_raw[UNIFROG_INPUT_MAX_PORTS];
   unsigned wireless_timeout[UNIFROG_INPUT_MAX_PORTS];
   int wireless_initialized;
   int wireless_bus_ok;
   unsigned wireless_channel_index;
   uint8_t wireless_status;
};

void unifrog_input_init(void);
void unifrog_input_clear(void);
void unifrog_input_recover_core_transition(const char *tag);
void unifrog_input_recover_after_core(void);
void unifrog_input_save_previous(void);
void unifrog_input_poll(void);
void unifrog_input_poll_with_wireless_divisor(unsigned wireless_divisor);
uint32_t unifrog_input_poll_local_raw(void);
uint32_t unifrog_input_poll_local_direct_buttons(void);
uint32_t unifrog_input_buttons(void);
uint32_t unifrog_input_menu_buttons(void);
uint32_t unifrog_input_previous_buttons(void);
uint32_t unifrog_input_local_buttons(void);
uint32_t unifrog_input_local_raw(void);
int unifrog_input_uses_stock_bits(void);
int unifrog_input_down(enum unifrog_button button);
int unifrog_input_pressed(enum unifrog_button button);
int unifrog_input_menu_pressed(enum unifrog_button button);
const char *unifrog_input_button_name(enum unifrog_button button);
void unifrog_input_snapshot(struct unifrog_input_snapshot *snapshot);

void unifrog_input_wireless_reset(void);
void unifrog_input_wireless_clear(void);
void unifrog_input_wireless_init(void);
int unifrog_input_wireless_available(void);
int unifrog_input_wireless_initialized(void);
int unifrog_input_wireless_bus_ok(void);
unsigned unifrog_input_wireless_channel_index(void);
uint32_t unifrog_input_wireless_buttons(unsigned port);
uint32_t unifrog_input_wireless_raw(unsigned port);
uint32_t unifrog_input_wireless_all_buttons(void);
unsigned unifrog_input_wireless_timeout(unsigned port);
/* Last RF status byte observed by initialization, polling, or diagnostics. */
uint8_t unifrog_input_wireless_status(void);
void unifrog_input_wireless_poll(void);
void unifrog_input_wireless_poll_once(void);
void unifrog_input_wireless_prepare_poll(void);
void unifrog_input_restore_local_bus(void);
void unifrog_input_log_local_bus_state(const char *tag);
void unifrog_input_log_wireless_sdio_state(const char *tag);
int unifrog_input_wireless_receive_window(const char *tag, uint8_t channel,
   unsigned duration_ms, unsigned poll_delay_us);

#ifdef __cplusplus
}
#endif

#endif
