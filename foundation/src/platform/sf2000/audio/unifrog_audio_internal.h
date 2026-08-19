#ifndef UNIFROG_AUDIO_INTERNAL_H
#define UNIFROG_AUDIO_INTERNAL_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include <hcuapi/avsync.h>
#include <hcuapi/audsink.h>
#include <hcuapi/gpio.h>
#include <hcuapi/pinpad.h>
#include <hcuapi/pinmux.h>
#include <hcuapi/snd.h>

#include <unifrog/audio.h>
#include <unifrog/log.h>

#define printf unifrog_log

#define GPIO_L_OUTPUT ((volatile uint32_t *)0xb8800054u)
#define GPIO_L_DIR ((volatile uint32_t *)0xb8800058u)
#define GPIO_R_OUTPUT ((volatile uint32_t *)0xb88000f4u)
#define GPIO_R_DIR ((volatile uint32_t *)0xb88000f8u)
#define GPIO_L15_MASK (1u << 15)
#define GPIO_R07_MASK (1u << 7)
#define UNIFROG_AUDIO_ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

#define GB300_ROUTE_PROBE_RATE 44100u
#define GB300_ROUTE_PROBE_CHANNELS 2u
#define GB300_ROUTE_PROBE_PERIOD_BYTES 4096u
#define GB300_ROUTE_PROBE_PERIODS 8u
#define GB300_ROUTE_PROBE_FRAMES 8192u
#define GB300_ROUTE_PROBE_XFERS 4u
#define GB300_ROUTE_PROBE_ROUTE_PAUSE_US 420000u
#ifndef UNIFROG_AUDIO_GB300_ROUTE_PROBE_ONCE
#define UNIFROG_AUDIO_GB300_ROUTE_PROBE_ONCE 0
#endif
#define GB300_SND_PERIOD_BYTES 3072u
#define GB300_SND_PERIODS 40u
#define GB300_SND_START_THRESHOLD 2u
#define GB300_GATE_PROBE_VOLUME 100u
#define GB300_GATE_PROBE_STAGE_XFERS 4u
#define GB300_GATE_PROBE_STAGE_PAUSE_US 450000u
#define GB300_CONTROL_SWEEP_XFERS 5u
#define GB300_CONTROL_SWEEP_PAUSE_US 250000u
#define SND0_BASE ((volatile uint32_t *)0xb880a000u)
#define SND0_UNDERRUN_FADE_REG (0x3cu / sizeof(uint32_t))
#define SND0_UNDERRUN_FADE_BIT 0x40u

#define I2SO_PLATFORM_STATUS_OFF 4u
#define I2SO_PLATFORM_PINMUX_MUTE_OFF 176u
#define I2SO_PLATFORM_MUTE_POLAR_OFF 180u
#define I2SO_PLATFORM_VOLUME_OFF 181u
#define I2SO_PLATFORM_U8(off) \
   (*(volatile uint8_t *)(void *)(i2so_platform_dev + (off)))
#define I2SO_PLATFORM_PINMUX_MUTE \
   (*(struct pinmux_setting **)(void *)(i2so_platform_dev + \
      I2SO_PLATFORM_PINMUX_MUTE_OFF))

enum audio_gate {
   AUDIO_GATE_SF2000_R07,
   AUDIO_GATE_GB300_L15,
};

extern unsigned char i2so_platform_dev[] __attribute__((weak));

int current_audio_uses_gb300_gate(void);
enum audio_gate current_audio_gate(void);
const char *audio_gate_name(enum audio_gate gate);
void gb300_i2so_platform_log(const char *tag);
unsigned system_audio_volume(void);
void set_reg_level(volatile uint32_t *dir, volatile uint32_t *out,
   uint32_t mask, int high);
void set_stock_audio_output_gate(int enabled);
int ensure_audio_drivers(void);
int configure_neutral_audio_controls_fd(int fd, const char *tag);
unsigned read_pinmux(pinpad_e pin);

#endif
