#include <unifrog/input.h>

#include <stdint.h>
#include <stdio.h>
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

#define MSYSIO_BASE 0xb8800000u
#define RF_REG50 (MSYSIO_BASE + 0x50)
#define RF_REG54 (MSYSIO_BASE + 0x54)
#define RF_REG58 (MSYSIO_BASE + 0x58)
#define RF_REG354 (MSYSIO_BASE + 0x354)
#define RF_REG358 (MSYSIO_BASE + 0x358)
#define RF_BIT_DATA 0x08000000u
#define RF_BIT_CLOCK 0x10000000u
#define RF_BIT_CS 0x20000000u
#define RF_STOCK_POLL_R50 0x2f808103u

#define printf unifrog_log

extern unsigned long arch_local_irq_save(void);
extern void arch_local_irq_restore(unsigned long flags);
extern unsigned long PINMUXL;
extern unsigned long PINMUXB;
extern unsigned long GPIOLCTRL;
extern unsigned long GPIOBCTRL;

static uint32_t wireless_state[UNIFROG_INPUT_MAX_PORTS];
static uint32_t wireless_prev_state[UNIFROG_INPUT_MAX_PORTS];
static uint32_t wireless_raw[UNIFROG_INPUT_MAX_PORTS];
static unsigned wireless_timeout[UNIFROG_INPUT_MAX_PORTS];
static int wireless_initialized;
static unsigned wireless_rf_channel_index;
static unsigned wireless_rf_empty_polls;
static unsigned wireless_rf_poll_count;
static unsigned wireless_rf_status_log_count;
static unsigned wireless_rf_state_log_count;
static int wireless_rf_bus_ok;
static uint8_t wireless_last_status;

#define RF_IO_DELAY_US 2u
#define RF_IDLE_POLL_SUSPEND 32768u

static void rf_stock_rx_reset(void);
static void rf_stock_rx_ack(void);
static void rf_stock_hw_preamble(void);

static void rf_pinmux_gpio_l24_l29(void)
{
   pinmux_configure(PINPAD_L23, PINMUX_L23_GPIO);
   pinmux_configure(PINPAD_L24, PINMUX_L24_GPIO);
   pinmux_configure(PINPAD_L25, PINMUX_L25_GPIO);
   pinmux_configure(PINPAD_L26, PINMUX_L26_GPIO);
   pinmux_configure(PINPAD_L27, PINMUX_L27_GPIO);
   pinmux_configure(PINPAD_L28, PINMUX_L28_GPIO);
   pinmux_configure(PINPAD_L29, PINMUX_L29_GPIO);
   pinmux_configure(PINPAD_B03, PINMUX_B03_GPIO);
   pinmux_configure(PINPAD_B15, 0);
   gpio_configure(PINPAD_B15, GPIO_DIR_INPUT);
}

static void rf_write32_sync(uint32_t addr, uint32_t value)
{
   REG32_WRITE(addr, value);
   for (volatile unsigned i = 0; i < 64; i++) {
      if (REG32_READ(addr) == value)
         break;
   }
}

static void rf_set_bits(uint32_t addr, uint32_t clear_mask, uint32_t set_mask)
{
   uint32_t value = REG32_READ(addr);
   value &= ~clear_mask;
   value |= set_mask;
   rf_write32_sync(addr, value);
}

static uint8_t rf_pinmux_byte(pinpad_e pin)
{
   if (pin < 32)
      return REG8_READ((uint32_t)&PINMUXL + pin);
   if (pin < 64)
      return REG8_READ((uint32_t)&PINMUXB + pin - 32);
   return 0xff;
}

void unifrog_input_log_wireless_sdio_state(const char *tag)
{
   uint32_t r50 = REG32_READ(RF_REG50);

   printf("unifrog wireless sdio tag=%s r50=0x%08lx stock_delta=0x%08lx pmL16=%02x pmL17=%02x pmL18=%02x pmL19=%02x pmL20=%02x pmL21=%02x pmL22=%02x in16=%d in20=%d in21=%d\n",
      tag,
      (unsigned long)r50,
      (unsigned long)(r50 ^ RF_STOCK_POLL_R50),
      (unsigned)rf_pinmux_byte(PINPAD_L16),
      (unsigned)rf_pinmux_byte(PINPAD_L17),
      (unsigned)rf_pinmux_byte(PINPAD_L18),
      (unsigned)rf_pinmux_byte(PINPAD_L19),
      (unsigned)rf_pinmux_byte(PINPAD_L20),
      (unsigned)rf_pinmux_byte(PINPAD_L21),
      (unsigned)rf_pinmux_byte(PINPAD_L22),
      gpio_get_input(PINPAD_L16),
      gpio_get_input(PINPAD_L20),
      gpio_get_input(PINPAD_L21));
}

static void rf_force_stock_pinmux_words(void)
{
   for (unsigned i = 23; i < 31; i++)
      REG8_WRITE((uint32_t)&PINMUXL + i, 0);
   REG32_WRITE((uint32_t)&PINMUXB, 0x00000001u);
}

static void rf_force_stock_board_shadow(void)
{
   rf_force_stock_pinmux_words();
   REG32_WRITE((uint32_t)&GPIOLCTRL + 0x0c, 0x2f808103u);
   REG32_WRITE((uint32_t)&GPIOLCTRL + 0x10, 0x2b4085b3u);
   REG32_WRITE((uint32_t)&GPIOLCTRL + 0x14, 0x390004feu);
   REG32_WRITE((uint32_t)&GPIOBCTRL + 0x0c, 0x00000000u);
   REG32_WRITE((uint32_t)&GPIOBCTRL + 0x10, 0x00000000u);
   REG32_WRITE((uint32_t)&GPIOBCTRL + 0x14, 0x00000008u);
   rf_write32_sync(RF_REG54, 0x2b4085b3u);
   rf_write32_sync(RF_REG58, 0x390004feu);
   rf_write32_sync(RF_REG354, 0x00088383u);
   rf_write32_sync(RF_REG358, 0x00087effu);
   usleep(2000);
}

static void rf_force_stock_full_init_setup(void)
{
   rf_force_stock_pinmux_words();
   REG32_WRITE((uint32_t)&GPIOLCTRL + 0x0c, 0x37808103u);
   REG32_WRITE((uint32_t)&GPIOLCTRL + 0x10, 0x3fc085b3u);
   REG32_WRITE((uint32_t)&GPIOLCTRL + 0x14, 0x000004feu);
   REG32_WRITE((uint32_t)&GPIOBCTRL + 0x0c, 0x00000000u);
   REG32_WRITE((uint32_t)&GPIOBCTRL + 0x10, 0x00000000u);
   REG32_WRITE((uint32_t)&GPIOBCTRL + 0x14, 0x00000008u);
   rf_write32_sync(RF_REG54, 0x3fc085b3u);
   rf_write32_sync(RF_REG58, 0x000004feu);
   rf_write32_sync(RF_REG354, 0x00088303u);
   rf_write32_sync(RF_REG358, 0x00087effu);
   usleep(2000);
}

static void rf_force_stock_program_idle_shadow(void)
{
   rf_force_stock_pinmux_words();
   REG32_WRITE((uint32_t)&GPIOLCTRL + 0x0c, 0x2f808103u);
   REG32_WRITE((uint32_t)&GPIOLCTRL + 0x10, 0x2fc085b3u);
   REG32_WRITE((uint32_t)&GPIOLCTRL + 0x14, 0x390004feu);
   REG32_WRITE((uint32_t)&GPIOBCTRL + 0x0c, 0x00000000u);
   REG32_WRITE((uint32_t)&GPIOBCTRL + 0x10, 0x00000000u);
   REG32_WRITE((uint32_t)&GPIOBCTRL + 0x14, 0x00000008u);
   rf_write32_sync(RF_REG54, 0x2fc085b3u);
   rf_write32_sync(RF_REG58, 0x390004feu);
   rf_write32_sync(RF_REG354, 0x00088383u);
   rf_write32_sync(RF_REG358, 0x00087effu);
   usleep(2000);
}

static void rf_io_delay(void)
{
   unifrog_perf_delay_us(RF_IO_DELAY_US);
}

static void rf_shift_byte(uint8_t value)
{
   for (unsigned i = 0; i < 8; i++) {
      uint8_t bit = (value & 0x80) != 0;

      rf_set_bits(RF_REG54, RF_BIT_CLOCK, 0);
      rf_io_delay();
      rf_set_bits(RF_REG54, RF_BIT_DATA, bit ? RF_BIT_DATA : 0);
      rf_io_delay();
      rf_set_bits(RF_REG54, RF_BIT_CLOCK, RF_BIT_CLOCK);
      rf_io_delay();
      value <<= 1;
   }

   rf_set_bits(RF_REG54, RF_BIT_CLOCK, 0);
   rf_io_delay();
   rf_set_bits(RF_REG54, RF_BIT_DATA, RF_BIT_DATA);
   rf_io_delay();
}

static uint8_t rf_read_byte(void)
{
   uint8_t value = 0;

   for (unsigned i = 0; i < 8; i++) {
      rf_set_bits(RF_REG54, RF_BIT_CLOCK, 0);
      rf_io_delay();
      value <<= 1;
      rf_set_bits(RF_REG54, RF_BIT_CLOCK, RF_BIT_CLOCK);
      rf_io_delay();
      if (REG32_READ(RF_REG50) & RF_BIT_DATA)
         value |= 1;
   }

   rf_set_bits(RF_REG54, RF_BIT_CLOCK, 0);
   rf_io_delay();
   return value;
}

static void rf_write_reg(uint8_t reg, uint8_t value)
{
   unsigned long flags = arch_local_irq_save();

   rf_set_bits(RF_REG58, RF_BIT_DATA, RF_BIT_DATA);
   rf_set_bits(RF_REG54, RF_BIT_CS, 0);
   rf_io_delay();
   rf_shift_byte(reg);
   rf_shift_byte(value);
   rf_set_bits(RF_REG54, RF_BIT_CS, RF_BIT_CS);
   rf_set_bits(RF_REG58, RF_BIT_DATA, RF_BIT_DATA);
   rf_set_bits(RF_REG54, RF_BIT_DATA, RF_BIT_DATA);
   arch_local_irq_restore(flags);
}

static uint8_t rf_read_reg(uint8_t reg)
{
   uint8_t value;
   unsigned long flags = arch_local_irq_save();

   rf_set_bits(RF_REG58, RF_BIT_DATA, RF_BIT_DATA);
   rf_set_bits(RF_REG54, RF_BIT_CS, 0);
   rf_io_delay();
   rf_shift_byte(reg);
   rf_set_bits(RF_REG58, RF_BIT_DATA, 0);
   rf_io_delay();
   value = rf_read_byte();
   rf_set_bits(RF_REG54, RF_BIT_CS, RF_BIT_CS);
   rf_set_bits(RF_REG58, RF_BIT_DATA, RF_BIT_DATA);
   rf_set_bits(RF_REG54, RF_BIT_DATA, RF_BIT_DATA);
   arch_local_irq_restore(flags);
   return value;
}

static void rf_read_buf(uint8_t reg, uint8_t *buf, unsigned len)
{
   unsigned long flags = arch_local_irq_save();

   rf_set_bits(RF_REG58, RF_BIT_DATA, RF_BIT_DATA);
   rf_set_bits(RF_REG54, RF_BIT_CS, 0);
   rf_io_delay();
   rf_shift_byte(reg);
   rf_set_bits(RF_REG58, RF_BIT_DATA, 0);
   rf_io_delay();
   for (unsigned i = 0; i < len; i++)
      buf[i] = rf_read_byte();
   rf_set_bits(RF_REG54, RF_BIT_CS, RF_BIT_CS);
   rf_set_bits(RF_REG58, RF_BIT_DATA, RF_BIT_DATA);
   rf_set_bits(RF_REG54, RF_BIT_DATA, RF_BIT_DATA);
   arch_local_irq_restore(flags);
}

static void rf_write_buf(uint8_t reg, const uint8_t *buf, unsigned len)
{
   unsigned long flags = arch_local_irq_save();

   rf_set_bits(RF_REG58, RF_BIT_DATA, RF_BIT_DATA);
   rf_set_bits(RF_REG54, RF_BIT_CS, 0);
   rf_io_delay();
   rf_shift_byte(reg);
   for (unsigned i = 0; i < len; i++)
      rf_shift_byte(buf[i]);
   rf_set_bits(RF_REG54, RF_BIT_CS, RF_BIT_CS);
   rf_set_bits(RF_REG58, RF_BIT_DATA, RF_BIT_DATA);
   rf_set_bits(RF_REG54, RF_BIT_DATA, RF_BIT_DATA);
   arch_local_irq_restore(flags);
}

static uint32_t rf_decode_stock_buttons(uint32_t raw)
{
   uint32_t out = 0;

   if (raw & 0x0020) out |= UNIFROG_BUTTON_MASK(UNIFROG_BUTTON_SELECT);
   if (raw & 0x0010) out |= UNIFROG_BUTTON_MASK(UNIFROG_BUTTON_START);
   if (raw & 0x0008) out |= UNIFROG_BUTTON_MASK(UNIFROG_BUTTON_UP);
   if (raw & 0x0004) out |= UNIFROG_BUTTON_MASK(UNIFROG_BUTTON_DOWN);
   if (raw & 0x0002) out |= UNIFROG_BUTTON_MASK(UNIFROG_BUTTON_LEFT);
   if (raw & 0x0001) out |= UNIFROG_BUTTON_MASK(UNIFROG_BUTTON_RIGHT);
   if (raw & 0x0080) out |= UNIFROG_BUTTON_MASK(UNIFROG_BUTTON_A);
   if (raw & 0x0040) out |= UNIFROG_BUTTON_MASK(UNIFROG_BUTTON_B);
   if (raw & 0x0800) out |= UNIFROG_BUTTON_MASK(UNIFROG_BUTTON_L);
   if (raw & 0x4000) out |= UNIFROG_BUTTON_MASK(UNIFROG_BUTTON_X);
   if (raw & 0x2000) out |= UNIFROG_BUTTON_MASK(UNIFROG_BUTTON_Y);
   if (raw & 0x1000) out |= UNIFROG_BUTTON_MASK(UNIFROG_BUTTON_R);
   return out;
}

static unsigned rf_decode_stock_port(uint8_t status, uint32_t raw)
{
   unsigned pipe = (status >> 1) & 0x07u;

   if (pipe == 0)
      return 0;
   if (pipe == 1)
      return 1;
   return (raw & 0x8000u) ? 1u : 0u;
}

static void rf_force_stock_idle_regs(void)
{
   rf_write32_sync(RF_REG54, 0x2fc085b3u);
   rf_write32_sync(RF_REG58, 0x390004feu);
   rf_write32_sync(RF_REG354, 0x00088383u);
   rf_write32_sync(RF_REG358, 0x00087effu);
   usleep(20000);
}

static void rf_force_stock_rx_idle_regs(void)
{
   rf_write32_sync(RF_REG54, 0x2b4085b3u);
   rf_write32_sync(RF_REG58, 0x390004feu);
   rf_write32_sync(RF_REG354, 0x00088383u);
   rf_write32_sync(RF_REG358, 0x00087effu);
}

static void rf_stock_hw_preamble(void)
{
   rf_set_bits(RF_REG58, 0x02000000u, 0);
   rf_set_bits(RF_REG58, 0x00800000u, 0);
   rf_set_bits(RF_REG58, 0x04000000u, 0);
   rf_set_bits(RF_REG358, 0x00008000u, 0);
   rf_set_bits(RF_REG358, 0, 0x00000080u);
   rf_set_bits(RF_REG354, 0, 0x00000080u);
   rf_set_bits(RF_REG58, 0, 0x01000000u);
   rf_set_bits(RF_REG54, 0, 0x01000000u);
   rf_set_bits(RF_REG58, 0, 0x20000000u);
   rf_set_bits(RF_REG54, 0, 0x20000000u);
   rf_set_bits(RF_REG58, 0, 0x10000000u);
   rf_set_bits(RF_REG54, 0x10000000u, 0);
   rf_set_bits(RF_REG58, 0, 0x08000000u);
   rf_set_bits(RF_REG54, 0, 0x08000000u);
   usleep(20000);
}

static void rf_board_enable_defaults(void)
{
   static const struct {
      pinpad_e pin;
      int high;
   } pins[] = {
      { PINPAD_B02, 1 },
      { PINPAD_L24, 0 },
      { PINPAD_L25, 1 },
      { PINPAD_L26, 1 },
      { PINPAD_L27, 0 },
   };

   for (unsigned i = 0; i < ARRAY_SIZE(pins); i++) {
      pinmux_configure(pins[i].pin, 0);
      gpio_configure(pins[i].pin, GPIO_DIR_OUTPUT);
      gpio_set_output(pins[i].pin, pins[i].high != 0);
   }
   usleep(50000);
}

static void rf_stock_ready_pin_state(void)
{
   pinmux_configure(PINPAD_L15, PINMUX_L15_GPIO);
   gpio_configure(PINPAD_L15, GPIO_DIR_INPUT);
}

static void rf_log_regs(const char *tag, uint8_t test)
{
   printf("unifrog wireless rf variant=%s test=0x%02x r50=0x%08lx r54=0x%08lx r58=0x%08lx\n",
      tag,
      (unsigned)test,
      (unsigned long)REG32_READ(RF_REG50),
      (unsigned long)REG32_READ(RF_REG54),
      (unsigned long)REG32_READ(RF_REG58));
}

static uint8_t wireless_init_sequence_stock_exact(const char *tag)
{
   uint8_t test;

   rf_stock_hw_preamble();
   rf_write_reg(0x53, 0x5a);
   usleep(20000);
   rf_write_reg(0x53, 0xa5);
   usleep(20000);
   rf_write_reg(0x25, 0xa5);
   usleep(50000);
   test = rf_read_reg(0x05);
   rf_log_regs(tag, test);
   return test;
}

static void rf_write_table(const uint8_t (*table)[2], unsigned count)
{
   for (unsigned i = 0; i < count; i++)
      rf_write_reg(table[i][0], table[i][1]);
}

static void rf_stock_radio_config(void)
{
   static const uint8_t rf_3f[] = { 0x0a, 0x6d, 0x67, 0x9c, 0x46 };
   static const uint8_t rf_3e[] = { 0xf6, 0x37, 0x5d };
   static const uint8_t rf_2a[] = { 0xdc, 0xa8, 0xf3, 0x6b, 0x74 };
   static const uint8_t rf_2b[] = { 0xb2, 0x9d, 0x59, 0x4f, 0xe3 };
   static const uint8_t config[][2] = {
      {0x39, 0x01},
      {0x20, 0x8e},
      {0x21, 0x03},
      {0x22, 0x03},
      {0x23, 0x03},
      {0x24, 0x02},
      {0x31, 0x02},
      {0x32, 0x02},
      {0x3c, 0x00},
      {0x26, 0x3f},
   };
   uint8_t id;

   rf_force_stock_idle_regs();
   rf_write_reg(0x3d, 0x20);
   rf_write_reg(0xfc, 0x00);
   rf_write_reg(0xe1, 0x00);
   rf_write_reg(0xe2, 0x00);
   rf_write_reg(0x27, 0x70);
   rf_write_buf(0x3f, rf_3f, sizeof(rf_3f));
   rf_write_buf(0x3e, rf_3e, sizeof(rf_3e));
   rf_write_table(config, ARRAY_SIZE(config));
   rf_write_buf(0x2a, rf_2a, sizeof(rf_2a));
   rf_write_buf(0x2b, rf_2b, sizeof(rf_2b));
   rf_stock_rx_reset();
   usleep(40000);
   wireless_rf_channel_index = 0;
   wireless_rf_empty_polls = 0;
   rf_write_reg(0x25, 0x04);
   rf_force_stock_rx_idle_regs();
   wireless_last_status = rf_read_reg(0x07);
   id = rf_read_reg(0x05);
   printf("unifrog wireless rf stock config status=0x%02x id05=0x%02x ch25=0x04 r50=0x%08lx r54=0x%08lx r58=0x%08lx\n",
      (unsigned)wireless_last_status,
      (unsigned)id,
      (unsigned long)REG32_READ(RF_REG50),
      (unsigned long)REG32_READ(RF_REG54),
      (unsigned long)REG32_READ(RF_REG58));
}

static void rf_stock_rx_reset(void)
{
   rf_write_reg(0xfc, 0x00);
   rf_write_reg(0xe1, 0x00);
   rf_write_reg(0xe2, 0x00);
   rf_write_reg(0x20, 0x8f);
   usleep(10000);
   rf_write_reg(0xfd, 0x00);
}

static void rf_stock_rx_ack(void)
{
   rf_write_reg(0xe2, 0x00);
   rf_write_reg(0x27, 0x70);
   rf_write_reg(0xfd, 0x00);
}

static void rf_stock_next_channel(void)
{
   static const uint8_t channels[] = { 0x04, 0x1d, 0x31, 0x4f };

   wireless_rf_channel_index = (wireless_rf_channel_index + 1) % ARRAY_SIZE(channels);
   rf_write_reg(0x25, channels[wireless_rf_channel_index]);
}

void unifrog_input_wireless_init(void)
{
   uint8_t test;

   if (wireless_initialized)
      return;

   rf_board_enable_defaults();
   rf_force_stock_board_shadow();

   rf_pinmux_gpio_l24_l29();
   rf_stock_ready_pin_state();
   rf_force_stock_full_init_setup();
   test = wireless_init_sequence_stock_exact("stock_full_init");
   rf_force_stock_program_idle_shadow();

   if (test == 0xa5) {
      rf_stock_ready_pin_state();
      rf_force_stock_program_idle_shadow();
      rf_stock_radio_config();
      rf_stock_ready_pin_state();
      rf_force_stock_board_shadow();
      rf_force_stock_rx_idle_regs();
      wireless_rf_bus_ok = 1;
      wireless_initialized = 1;
      printf("unifrog wireless rf init ok variant=stock_full_init\n");
      (void)unifrog_log_flush();
   } else {
      wireless_rf_bus_ok = 0;
      wireless_initialized = 1;
      printf("unifrog wireless rf init selftest_fail last=0x%02x\n", (unsigned)test);
      (void)unifrog_log_flush();
      unifrog_input_restore_local_bus();
   }
}

void unifrog_input_wireless_reset(void)
{
   memset(wireless_state, 0, sizeof(wireless_state));
   memset(wireless_prev_state, 0, sizeof(wireless_prev_state));
   memset(wireless_raw, 0, sizeof(wireless_raw));
   memset(wireless_timeout, 0, sizeof(wireless_timeout));
   wireless_initialized = 0;
   wireless_rf_bus_ok = 0;
   wireless_rf_channel_index = 0;
   wireless_rf_empty_polls = 0;
   wireless_rf_poll_count = 0;
   wireless_rf_status_log_count = 0;
   wireless_rf_state_log_count = 0;
   wireless_last_status = 0;
}

void unifrog_input_wireless_clear(void)
{
   memset(wireless_state, 0, sizeof(wireless_state));
   memset(wireless_prev_state, 0, sizeof(wireless_prev_state));
   memset(wireless_raw, 0, sizeof(wireless_raw));
   memset(wireless_timeout, 0, sizeof(wireless_timeout));
   wireless_last_status = 0;
}

int unifrog_input_wireless_available(void)
{
   return wireless_initialized == 1 && wireless_rf_bus_ok;
}

int unifrog_input_wireless_initialized(void)
{
   return wireless_initialized;
}

int unifrog_input_wireless_bus_ok(void)
{
   return wireless_rf_bus_ok;
}

unsigned unifrog_input_wireless_channel_index(void)
{
   return wireless_rf_channel_index;
}

uint32_t unifrog_input_wireless_buttons(unsigned port)
{
   if (port >= ARRAY_SIZE(wireless_state))
      return 0;
   return wireless_state[port];
}

uint32_t unifrog_input_wireless_raw(unsigned port)
{
   if (port >= ARRAY_SIZE(wireless_raw))
      return 0;
   return wireless_raw[port];
}

uint32_t unifrog_input_wireless_all_buttons(void)
{
   uint32_t mask = 0;

   for (unsigned port = 0; port < ARRAY_SIZE(wireless_state); port++)
      mask |= wireless_state[port];
   return mask;
}

unsigned unifrog_input_wireless_timeout(unsigned port)
{
   if (port >= ARRAY_SIZE(wireless_timeout))
      return 0;
   return wireless_timeout[port];
}

uint8_t unifrog_input_wireless_status(void)
{
   if (!unifrog_input_wireless_available())
      return 0;
   return wireless_last_status;
}

void unifrog_input_wireless_prepare_poll(void)
{
   rf_pinmux_gpio_l24_l29();
   rf_stock_ready_pin_state();
}

void unifrog_input_wireless_poll_once(void)
{
   uint8_t status;

   if (!unifrog_input_wireless_available())
      return;

   status = rf_read_reg(0x07);
   wireless_last_status = status;
   if ((wireless_rf_poll_count++ % 8192) == 0 &&
       wireless_rf_status_log_count < 8) {
      printf("unifrog wireless poll status=0x%02x ch_index=%u count=%u\n",
         (unsigned)status, wireless_rf_channel_index, wireless_rf_poll_count);
      wireless_rf_status_log_count++;
   }
   if (status & 0x40) {
      uint8_t pkt[2] = {0, 0};
      unsigned port;
      uint32_t raw;

      rf_write_reg(0xfc, 0x00);
      rf_read_buf(0x61, pkt, 2);
      rf_stock_rx_ack();
      wireless_rf_empty_polls = 0;
      rf_stock_next_channel();

      raw = ((uint32_t)pkt[0] << 8) | ((~pkt[1]) & 0xffu);
      port = rf_decode_stock_port(status, raw);
      if (port < ARRAY_SIZE(wireless_state)) {
         wireless_raw[port] = raw;
         wireless_state[port] = rf_decode_stock_buttons(raw);
         wireless_timeout[port] = 0;
      }
   } else {
      for (unsigned port = 0; port < ARRAY_SIZE(wireless_state); port++) {
         if (wireless_timeout[port] < 90)
            wireless_timeout[port]++;
         else
            wireless_state[port] = 0;
      }
      if (++wireless_rf_empty_polls >= 2) {
         wireless_rf_empty_polls = 0;
         rf_stock_next_channel();
      }
      if (wireless_rf_poll_count >= RF_IDLE_POLL_SUSPEND &&
          wireless_rf_status_log_count >= 8) {
         printf("unifrog wireless idle_keepalive polls=%u status=0x%02x ch_index=%u\n",
            wireless_rf_poll_count, (unsigned)status,
            wireless_rf_channel_index);
         wireless_rf_poll_count = 0;
         wireless_rf_status_log_count = 0;
         rf_stock_ready_pin_state();
         rf_force_stock_program_idle_shadow();
         rf_stock_radio_config();
      }
   }

   for (unsigned port = 0; port < ARRAY_SIZE(wireless_state); port++) {
      if (wireless_state[port] != wireless_prev_state[port]) {
         if (wireless_rf_state_log_count < 16) {
         printf("unifrog wireless p%u raw=0x%04lx state=0x%08lx status=0x%02x pkt_timeout=%u\n",
            port + 1,
            (unsigned long)wireless_raw[port],
            (unsigned long)wireless_state[port],
            (unsigned)status,
            wireless_timeout[port]);
            wireless_rf_state_log_count++;
         }
         wireless_prev_state[port] = wireless_state[port];
      }
   }
}

void unifrog_input_wireless_poll(void)
{
   if (!unifrog_input_wireless_available())
      return;

   unifrog_input_wireless_prepare_poll();
   rf_force_stock_board_shadow();
   for (unsigned i = 0; i < 4; i++)
      unifrog_input_wireless_poll_once();
   unifrog_input_restore_local_bus();
}

int unifrog_input_wireless_receive_window(const char *tag, uint8_t channel,
   unsigned duration_ms, unsigned poll_delay_us)
{
   uint32_t start_ms;
   unsigned polls = 0;
   unsigned ready = 0;

   if (!unifrog_input_wireless_available())
      return 0;

   unifrog_input_wireless_prepare_poll();
   rf_force_stock_rx_idle_regs();
   rf_write_reg(0x25, channel);
   wireless_rf_channel_index = 0;
   wireless_last_status = rf_read_reg(0x07);
   printf("WIRELESS_DIAG window start tag=%s ch=0x%02x duration_ms=%u delay_us=%u status0=0x%02x\n",
      tag, channel, duration_ms, poll_delay_us, (unsigned)wireless_last_status);
   start_ms = unifrog_perf_time_ms();
   while ((uint32_t)(unifrog_perf_time_ms() - start_ms) <= duration_ms) {
      uint8_t status = rf_read_reg(0x07);
      wireless_last_status = status;
      polls++;
      if (status & 0x40) {
         uint8_t pkt[2] = {0, 0};
         uint32_t raw;
         unsigned port;

         rf_write_reg(0xfc, 0x00);
         rf_read_buf(0x61, pkt, 2);
         rf_stock_rx_ack();
         raw = ((uint32_t)pkt[0] << 8) | ((~pkt[1]) & 0xffu);
         port = rf_decode_stock_port(status, raw);
         if (port < ARRAY_SIZE(wireless_state)) {
            wireless_raw[port] = raw;
            wireless_state[port] = rf_decode_stock_buttons(raw);
            wireless_timeout[port] = 0;
         }
         ready++;
         printf("WIRELESS_DIAG window packet tag=%s ch=0x%02x status=0x%02x pkt=%02x:%02x raw=0x%04lx port=%u state=0x%08lx\n",
            tag, channel, (unsigned)status, (unsigned)pkt[0], (unsigned)pkt[1],
            (unsigned long)raw, port + 1,
            port < ARRAY_SIZE(wireless_state) ? (unsigned long)wireless_state[port] : 0ul);
      }
      usleep(poll_delay_us);
   }

   wireless_last_status = rf_read_reg(0x07);
   printf("WIRELESS_DIAG window end tag=%s ch=0x%02x polls=%u ready=%u final_status=0x%02x\n",
      tag, channel, polls, ready, (unsigned)wireless_last_status);
   unifrog_input_restore_local_bus();
   return ready > 0;
}
