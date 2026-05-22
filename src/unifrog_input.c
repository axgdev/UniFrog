#include <unifrog/input.h>

#include <stdint.h>
#include <string.h>
#include <sys/unistd.h>

#include <kernel/io.h>
#include <hcuapi/gpio.h>
#include <hcuapi/pinpad.h>
#include <hcuapi/pinmux.h>

#include <unifrog/log.h>
#include <unifrog/perf.h>

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#endif

#define KEY_SHIFTER_CLK_PIN PINPAD_L24
#define KEY_SHIFTER_PL1_PIN PINPAD_L23
#define GB300_KEY_SHIFTER_CLK_PIN PINPAD_L26
#define GB300_KEY_SHIFTER_D0_PIN PINPAD_L27
#define GB300_KEY_SHIFTER_D1_PIN PINPAD_L25
#define KEY_SHIFTER_BITS 16u
#define LCD_ID_GB300 0x009306ul
#define LCD_ID_DY14 0x009307ul
#define INPUT_LOCAL_ZERO_PROBE_POLLS 128u
#define KEY_SHIFTER_LOAD_US 4u
#define KEY_SHIFTER_SETTLE_US 4u
#define KEY_SHIFTER_CLOCK_LOW_US 3u
#define KEY_SHIFTER_CLOCK_HIGH_US 3u
#define INPUT_DEBOUNCE_STABLE_POLLS 1
#define INPUT_VALID_BUTTON_MASK ((1u << UNIFROG_BUTTON_COUNT) - 1u)

static int button_state[UNIFROG_BUTTON_COUNT];
static int button_prev[UNIFROG_BUTTON_COUNT];
static int button_raw_prev[UNIFROG_BUTTON_COUNT];
static int button_raw_count[UNIFROG_BUTTON_COUNT];
static uint32_t buttons;
static uint32_t previous_buttons;
static uint32_t local_buttons;
static uint32_t local_raw;
static uint32_t menu_buttons;
static uint32_t previous_menu_buttons;
static uint32_t input_source_last_logged_local;
static uint32_t input_source_last_logged_wireless;
static uint32_t input_source_last_logged_menu;
static uint32_t input_source_last_logged_raw;
static uint32_t input_source_last_logged_norm;
static unsigned input_source_log_count;
static unsigned input_source_log_local_count;
static unsigned input_source_log_wireless_count;
static unsigned input_source_log_menu_count;
static unsigned wireless_poll_phase;
static unsigned local_menu_guard;
static uint32_t local_menu_hold_buttons;
static unsigned local_zero_probe_count;
static unsigned local_zero_probe_poll_count;
static int local_input_profile = -1;

extern unsigned long sf2000_lcd_panel_id(void) __attribute__((weak));
extern unsigned long PINMUXL;
extern unsigned long GPIOLCTRL;

enum local_input_profile {
   LOCAL_INPUT_SF2000 = 0,
   LOCAL_INPUT_STOCK_BITS,
};

static const uint8_t stock_bit_for_button[UNIFROG_BUTTON_COUNT] = {
   11, /* R */
   10, /* Y */
   12, /* X */
   13, /* L */
   14, /* A */
   0,  /* B */
   3,  /* SELECT */
   4,  /* START */
   6,  /* UP */
   7,  /* DOWN */
   5,  /* LEFT */
   1,  /* RIGHT */
};

static const uint8_t gb300_stock_bit_for_shift[KEY_SHIFTER_BITS] = {
   15, 11, 10, 12, 13, 14, 0, 3, 4, 6, 7, 5, 1, 2, 8, 9,
};

static uint32_t buttons_from_state(void)
{
   uint32_t mask = 0;

   for (unsigned i = 0; i < UNIFROG_BUTTON_COUNT; i++) {
      if (button_state[i])
         mask |= UNIFROG_BUTTON_MASK(i);
   }
   return mask;
}

static int detect_local_input_profile(void)
{
   unsigned long lcd_id = sf2000_lcd_panel_id ? sf2000_lcd_panel_id() : 0;
   int profile = LOCAL_INPUT_SF2000;

   if (lcd_id == LCD_ID_GB300 || lcd_id == LCD_ID_DY14)
      profile = LOCAL_INPUT_STOCK_BITS;

   if (profile != local_input_profile) {
      local_input_profile = profile;
      unifrog_log("unifrog input local_profile=%s lcd_id=0x%06lx\n",
         profile == LOCAL_INPUT_STOCK_BITS ? "stock_bits" : "sf2000",
         lcd_id);
   }

   return profile;
}

static uint8_t pinmux_l_byte(pinpad_e pin)
{
   if (pin < 32)
      return REG8_READ((uint32_t)&PINMUXL + pin);
   return 0xff;
}

void unifrog_input_log_local_bus_state(const char *tag)
{
   unifrog_log("unifrog input local_bus tag=%s profile=%s raw=0x%08lx local=0x%08lx gpl0c=0x%08lx gpl10=0x%08lx gpl14=0x%08lx pmL23=%02x pmL24=%02x pmL25=%02x pmL26=%02x pmL27=%02x pmL28=%02x pmL29=%02x in23=%d in24=%d in25=%d in26=%d in27=%d\n",
      tag ? tag : "",
      detect_local_input_profile() == LOCAL_INPUT_STOCK_BITS ?
         "stock_bits" : "sf2000",
      (unsigned long)local_raw,
      (unsigned long)local_buttons,
      (unsigned long)REG32_READ((uint32_t)&GPIOLCTRL + 0x0c),
      (unsigned long)REG32_READ((uint32_t)&GPIOLCTRL + 0x10),
      (unsigned long)REG32_READ((uint32_t)&GPIOLCTRL + 0x14),
      (unsigned)pinmux_l_byte(PINPAD_L23),
      (unsigned)pinmux_l_byte(PINPAD_L24),
      (unsigned)pinmux_l_byte(PINPAD_L25),
      (unsigned)pinmux_l_byte(PINPAD_L26),
      (unsigned)pinmux_l_byte(PINPAD_L27),
      (unsigned)pinmux_l_byte(PINPAD_L28),
      (unsigned)pinmux_l_byte(PINPAD_L29),
      gpio_get_input(PINPAD_L23),
      gpio_get_input(PINPAD_L24),
      gpio_get_input(PINPAD_L25),
      gpio_get_input(PINPAD_L26),
      gpio_get_input(PINPAD_L27));
}

static uint32_t normalize_local_raw(uint32_t raw)
{
   uint32_t normalized = 0;

   if (detect_local_input_profile() != LOCAL_INPUT_STOCK_BITS)
      return raw & INPUT_VALID_BUTTON_MASK;

   for (unsigned i = 0; i < UNIFROG_BUTTON_COUNT; i++) {
      if (raw & (1u << stock_bit_for_button[i]))
         normalized |= UNIFROG_BUTTON_MASK(i);
   }

   return normalized & INPUT_VALID_BUTTON_MASK;
}

void unifrog_input_restore_local_bus(void)
{
   if (detect_local_input_profile() == LOCAL_INPUT_STOCK_BITS) {
      pinmux_configure(GB300_KEY_SHIFTER_CLK_PIN, PINMUX_L26_GPIO);
      pinmux_configure(GB300_KEY_SHIFTER_D0_PIN, PINMUX_L27_GPIO);
      pinmux_configure(GB300_KEY_SHIFTER_D1_PIN, PINMUX_L25_GPIO);
      gpio_configure(GB300_KEY_SHIFTER_CLK_PIN, GPIO_DIR_OUTPUT);
      gpio_set_output(GB300_KEY_SHIFTER_CLK_PIN, 1);
      gpio_configure(GB300_KEY_SHIFTER_D0_PIN, GPIO_DIR_INPUT);
      gpio_configure(GB300_KEY_SHIFTER_D1_PIN, GPIO_DIR_INPUT);
      return;
   }

   pinmux_configure(KEY_SHIFTER_CLK_PIN, PINMUX_L24_GPIO);
   pinmux_configure(KEY_SHIFTER_PL1_PIN, PINMUX_L23_GPIO);
   gpio_configure(KEY_SHIFTER_CLK_PIN, GPIO_DIR_OUTPUT);
   gpio_set_output(KEY_SHIFTER_CLK_PIN, 1);
   gpio_configure(KEY_SHIFTER_PL1_PIN, GPIO_DIR_INPUT);
}

static uint32_t scan_gb300_local_raw(void)
{
   uint32_t raw_mask = 0;

   pinmux_configure(GB300_KEY_SHIFTER_CLK_PIN, PINMUX_L26_GPIO);
   pinmux_configure(GB300_KEY_SHIFTER_D0_PIN, PINMUX_L27_GPIO);
   pinmux_configure(GB300_KEY_SHIFTER_D1_PIN, PINMUX_L25_GPIO);
   gpio_configure(GB300_KEY_SHIFTER_CLK_PIN, GPIO_DIR_OUTPUT);
   gpio_configure(GB300_KEY_SHIFTER_D0_PIN, GPIO_DIR_OUTPUT);
   gpio_configure(GB300_KEY_SHIFTER_D1_PIN, GPIO_DIR_OUTPUT);

   /*
    * Stock GB300 joy_task briefly drives both data lines low before switching
    * them back to input. That appears to be the parallel-load phase for the
    * keypad shift register; without it the data pins stay idle.
    */
   gpio_set_output(GB300_KEY_SHIFTER_D0_PIN, 0);
   gpio_set_output(GB300_KEY_SHIFTER_D1_PIN, 0);
   gpio_set_output(GB300_KEY_SHIFTER_CLK_PIN, 0);
   unifrog_perf_delay_us(KEY_SHIFTER_LOAD_US);

   gpio_configure(GB300_KEY_SHIFTER_D0_PIN, GPIO_DIR_INPUT);
   gpio_configure(GB300_KEY_SHIFTER_D1_PIN, GPIO_DIR_INPUT);
   unifrog_perf_delay_us(KEY_SHIFTER_SETTLE_US);

   for (unsigned i = 0; i < KEY_SHIFTER_BITS; i++) {
      int raw0 = 1 ^ gpio_get_input(GB300_KEY_SHIFTER_D0_PIN);
      int raw1 = 1 ^ gpio_get_input(GB300_KEY_SHIFTER_D1_PIN);

      if (raw0 || raw1)
         raw_mask |= 1u << gb300_stock_bit_for_shift[i];

      gpio_set_output(GB300_KEY_SHIFTER_CLK_PIN, 0);
      unifrog_perf_delay_us(KEY_SHIFTER_CLOCK_LOW_US);
      gpio_set_output(GB300_KEY_SHIFTER_CLK_PIN, 1);
      unifrog_perf_delay_us(KEY_SHIFTER_CLOCK_HIGH_US);
   }

   return raw_mask;
}

static void update_button_debounce(uint32_t normalized_mask)
{
   normalized_mask &= INPUT_VALID_BUTTON_MASK;

   for (unsigned i = 0; i < UNIFROG_BUTTON_COUNT; i++) {
      int raw = (normalized_mask & UNIFROG_BUTTON_MASK(i)) != 0;

      if (raw == button_raw_prev[i]) {
         if (button_raw_count[i] < 3)
            button_raw_count[i]++;
      } else {
         button_raw_prev[i] = raw;
         button_raw_count[i] = 0;
      }

      if (button_raw_count[i] >= INPUT_DEBOUNCE_STABLE_POLLS)
         button_state[i] = raw;
   }
}

static uint32_t scan_local_buttons(int update_debounce,
   uint32_t *normalized_out)
{
   uint32_t raw_mask = 0;
   uint32_t normalized_mask;

   if (detect_local_input_profile() == LOCAL_INPUT_STOCK_BITS) {
      raw_mask = scan_gb300_local_raw();
   } else {
      gpio_configure(KEY_SHIFTER_CLK_PIN, GPIO_DIR_OUTPUT);
      gpio_set_output(KEY_SHIFTER_CLK_PIN, 1);
      gpio_configure(KEY_SHIFTER_PL1_PIN, GPIO_DIR_OUTPUT);
      gpio_set_output(KEY_SHIFTER_PL1_PIN, 0);
      unifrog_perf_delay_us(KEY_SHIFTER_LOAD_US);
      gpio_configure(KEY_SHIFTER_PL1_PIN, GPIO_DIR_INPUT);
      unifrog_perf_delay_us(KEY_SHIFTER_SETTLE_US);

      for (unsigned i = 0; i < UNIFROG_BUTTON_COUNT; i++) {
         int raw = 1 ^ gpio_get_input(KEY_SHIFTER_PL1_PIN);

         if (raw)
            raw_mask |= 1u << i;

         gpio_set_output(KEY_SHIFTER_CLK_PIN, 0);
         unifrog_perf_delay_us(KEY_SHIFTER_CLOCK_LOW_US);
         gpio_set_output(KEY_SHIFTER_CLK_PIN, 1);
         unifrog_perf_delay_us(KEY_SHIFTER_CLOCK_HIGH_US);
      }
   }

   normalized_mask = normalize_local_raw(raw_mask);
   if (normalized_out)
      *normalized_out = normalized_mask;

   if (update_debounce)
      update_button_debounce(normalized_mask);

   return raw_mask;
}

void unifrog_input_init(void)
{
   (void)detect_local_input_profile();
   unifrog_input_wireless_init();
   unifrog_input_clear();
}

void unifrog_input_clear(void)
{
   memset(button_state, 0, sizeof(button_state));
   memset(button_prev, 0, sizeof(button_prev));
   memset(button_raw_prev, 0, sizeof(button_raw_prev));
   memset(button_raw_count, 0, sizeof(button_raw_count));
   buttons = 0;
   previous_buttons = 0;
   local_buttons = 0;
   local_raw = 0;
   menu_buttons = 0;
   previous_menu_buttons = 0;
   input_source_last_logged_local = 0;
   input_source_last_logged_wireless = 0;
   input_source_last_logged_menu = 0;
   input_source_last_logged_raw = 0;
   input_source_last_logged_norm = 0;
   input_source_log_count = 0;
   input_source_log_local_count = 0;
   input_source_log_wireless_count = 0;
   input_source_log_menu_count = 0;
   wireless_poll_phase = 0;
   local_menu_guard = 0;
   local_menu_hold_buttons = 0;
   local_zero_probe_count = 0;
   local_zero_probe_poll_count = 0;
   unifrog_input_wireless_clear();
   unifrog_input_restore_local_bus();
   (void)detect_local_input_profile();
}

static void prime_local_bus_after_transition(const char *tag)
{
   uint32_t normalized = 0;

   for (unsigned i = 0; i < 4; i++) {
      unifrog_input_restore_local_bus();
      local_raw = scan_local_buttons(0, &normalized);
      usleep(1000);
   }

   local_buttons = 0;
   buttons = 0;
   menu_buttons = 0;
   previous_buttons = 0;
   previous_menu_buttons = 0;
   unifrog_log("unifrog input transition tag=%s local_raw=0x%08lx local_norm=0x%08lx wireless_init=%d bus_ok=%d\n",
      tag ? tag : "",
      (unsigned long)local_raw,
      (unsigned long)normalized,
      unifrog_input_wireless_initialized(),
      unifrog_input_wireless_bus_ok());
   unifrog_input_log_local_bus_state(tag);
}

void unifrog_input_recover_core_transition(const char *tag)
{
   unifrog_log("unifrog input recover_transition begin tag=%s\n",
      tag ? tag : "");
   unifrog_input_log_local_bus_state("recover_begin");
   unifrog_input_wireless_reset();
   unifrog_input_restore_local_bus();
   unifrog_input_wireless_init();
   unifrog_input_clear();
   prime_local_bus_after_transition(tag);
   unifrog_log("unifrog input recover_transition end tag=%s wireless_init=%d bus_ok=%d\n",
      tag ? tag : "",
      unifrog_input_wireless_initialized(),
      unifrog_input_wireless_bus_ok());
}

void unifrog_input_recover_after_core(void)
{
   unifrog_input_recover_core_transition("after_core");
}

void unifrog_input_save_previous(void)
{
   memcpy(button_prev, button_state, sizeof(button_prev));
   previous_buttons = buttons;
   previous_menu_buttons = menu_buttons;
}

void unifrog_input_poll(void)
{
   unifrog_input_poll_with_wireless_divisor(1);
}

void unifrog_input_poll_with_wireless_divisor(unsigned wireless_divisor)
{
   uint32_t local_raw_before_wireless;
   uint32_t local_norm_before_wireless = 0;
   uint32_t local_norm_after_wireless = 0;
   uint32_t local_norm;
   uint32_t local_menu_direct;
   uint32_t wireless_buttons = 0;
   int local_changed;
   int wireless_changed;
   int menu_changed;
   int raw_changed;

   unifrog_input_restore_local_bus();
   local_raw = scan_local_buttons(0, &local_norm_before_wireless);
   local_raw_before_wireless = local_raw;

   if (unifrog_input_wireless_available()) {
      int poll_wireless = 1;

      if (wireless_divisor > 1)
         poll_wireless = (wireless_poll_phase++ % wireless_divisor) == 0;
      if (poll_wireless) {
         unifrog_input_wireless_poll();
         /*
          * The RF controller poll temporarily owns the L23-L29 GPIO group.
          * Merge the pre/post-RF physical samples and update debounce once.
          * Updating debounce twice here can turn one pressed button into an
          * alternating pressed/released stream if the shared GPIO bus is still
          * settling after RF activity.
          */
         unifrog_input_restore_local_bus();
         local_raw = scan_local_buttons(0, &local_norm_after_wireless);
         local_raw |= local_raw_before_wireless;
      } else {
         unifrog_input_restore_local_bus();
      }
      wireless_buttons = unifrog_input_wireless_all_buttons() &
         INPUT_VALID_BUTTON_MASK;
   }

   local_norm_before_wireless &= INPUT_VALID_BUTTON_MASK;
   local_norm_after_wireless &= INPUT_VALID_BUTTON_MASK;
   local_norm = (local_norm_before_wireless | local_norm_after_wireless) &
      INPUT_VALID_BUTTON_MASK;
   update_button_debounce(local_norm);
   local_buttons = buttons_from_state();
   local_menu_direct = local_norm ? local_norm : local_buttons;
   buttons = (local_buttons | wireless_buttons) & INPUT_VALID_BUTTON_MASK;
   if (local_menu_direct) {
      local_menu_guard = 1;
      local_menu_hold_buttons = local_menu_direct;
      menu_buttons = local_menu_direct;
      local_zero_probe_poll_count = 0;
   } else if (local_menu_guard) {
      local_menu_guard--;
      menu_buttons = local_menu_hold_buttons;
   } else {
      local_menu_hold_buttons = 0;
      menu_buttons = buttons;
   }

   local_changed = local_buttons != input_source_last_logged_local;
   wireless_changed = wireless_buttons != input_source_last_logged_wireless;
   menu_changed = menu_buttons != input_source_last_logged_menu;
   raw_changed = local_raw != input_source_last_logged_raw ||
      local_norm != input_source_last_logged_norm;
   if ((input_source_log_count < 12 && (local_changed || wireless_changed)) ||
       (local_changed && input_source_log_local_count < 32) ||
       (raw_changed && input_source_log_local_count < 32) ||
       (wireless_changed && input_source_log_wireless_count < 24) ||
       (menu_changed && input_source_log_menu_count < 32)) {
      unifrog_log("unifrog input sources local=0x%08lx wireless=0x%08lx combined=0x%08lx menu=0x%08lx guard=%u raw0=0x%08lx raw1=0x%08lx norm0=0x%08lx norm1=0x%08lx\n",
         (unsigned long)local_buttons, (unsigned long)wireless_buttons,
         (unsigned long)buttons, (unsigned long)menu_buttons,
         local_menu_guard,
         (unsigned long)local_raw_before_wireless,
         (unsigned long)local_raw,
         (unsigned long)local_norm_before_wireless,
         (unsigned long)local_norm_after_wireless);
      input_source_last_logged_local = local_buttons;
      input_source_last_logged_wireless = wireless_buttons;
      input_source_last_logged_menu = menu_buttons;
      input_source_last_logged_raw = local_raw;
      input_source_last_logged_norm = local_norm;
      input_source_log_count++;
      if (local_changed || raw_changed)
         input_source_log_local_count++;
      if (wireless_changed)
         input_source_log_wireless_count++;
      if (menu_changed)
         input_source_log_menu_count++;
   }

   if (!local_buttons && wireless_buttons && local_zero_probe_count < 8) {
      if (++local_zero_probe_poll_count >= INPUT_LOCAL_ZERO_PROBE_POLLS) {
         local_zero_probe_poll_count = 0;
         local_zero_probe_count++;
         unifrog_input_log_local_bus_state("wireless_active_local_zero");
      }
   }
}

uint32_t unifrog_input_poll_local_raw(void)
{
   unifrog_input_restore_local_bus();
   local_raw = scan_local_buttons(1, NULL);
   local_buttons = buttons_from_state();
   buttons = local_buttons;
   menu_buttons = buttons;
   return local_raw;
}

uint32_t unifrog_input_poll_local_direct_buttons(void)
{
   uint32_t normalized = 0;

   unifrog_input_restore_local_bus();
   local_raw = scan_local_buttons(0, &normalized);
   local_buttons = normalized;
   buttons = local_buttons;
   menu_buttons = buttons;
   return normalized;
}

uint32_t unifrog_input_buttons(void)
{
   return buttons;
}

uint32_t unifrog_input_menu_buttons(void)
{
   return menu_buttons;
}

uint32_t unifrog_input_previous_buttons(void)
{
   return previous_buttons;
}

uint32_t unifrog_input_local_buttons(void)
{
   return local_buttons;
}

uint32_t unifrog_input_local_raw(void)
{
   return local_raw;
}

int unifrog_input_down(enum unifrog_button button)
{
   if (button < 0 || button >= UNIFROG_BUTTON_COUNT)
      return 0;
   return (buttons & UNIFROG_BUTTON_MASK(button)) != 0;
}

int unifrog_input_pressed(enum unifrog_button button)
{
   if (button < 0 || button >= UNIFROG_BUTTON_COUNT)
      return 0;
   return (buttons & UNIFROG_BUTTON_MASK(button)) &&
      !(previous_buttons & UNIFROG_BUTTON_MASK(button));
}

int unifrog_input_menu_pressed(enum unifrog_button button)
{
   if (button < 0 || button >= UNIFROG_BUTTON_COUNT)
      return 0;
   return (menu_buttons & UNIFROG_BUTTON_MASK(button)) &&
      !(previous_menu_buttons & UNIFROG_BUTTON_MASK(button));
}

const char *unifrog_input_button_name(enum unifrog_button button)
{
   static const char *names[] = {
      "R", "Y", "X", "L", "A", "B", "SELECT", "START",
      "UP", "DOWN", "LEFT", "RIGHT",
   };

   if (button < 0 || button >= (enum unifrog_button)ARRAY_SIZE(names))
      return "?";
   return names[button];
}

void unifrog_input_snapshot(struct unifrog_input_snapshot *snapshot)
{
   if (!snapshot)
      return;

   memset(snapshot, 0, sizeof(*snapshot));
   snapshot->buttons = buttons;
   snapshot->previous_buttons = previous_buttons;
   snapshot->local_buttons = local_buttons;
   snapshot->local_raw = local_raw;
   for (unsigned i = 0; i < UNIFROG_INPUT_MAX_PORTS; i++) {
      snapshot->wireless_buttons[i] = unifrog_input_wireless_buttons(i);
      snapshot->wireless_raw[i] = unifrog_input_wireless_raw(i);
      snapshot->wireless_timeout[i] = unifrog_input_wireless_timeout(i);
   }
   snapshot->wireless_initialized = unifrog_input_wireless_initialized();
   snapshot->wireless_bus_ok = unifrog_input_wireless_bus_ok();
   snapshot->wireless_channel_index = unifrog_input_wireless_channel_index();
   snapshot->wireless_status = unifrog_input_wireless_status();
}
