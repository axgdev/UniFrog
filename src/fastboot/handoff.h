#ifndef UNIFROG_FASTBOOT_HANDOFF_H
#define UNIFROG_FASTBOOT_HANDOFF_H

#define FASTBOOT_CHUNK_ADDR 0x87fe7000u
#define FASTBOOT_STAGE1_ADDR 0x87ff7000u
#define FASTBOOT_STACK_ADDR 0x87fff000u
#define FASTBOOT_DIAG_ADDR ((volatile struct fastboot_diag *)0x87fffe00u)
#define FASTBOOT_HANDOFF_ADDR ((volatile struct fastboot_handoff *)0x87ffff00u)
#define FASTBOOT_HANDOFF_MAGIC 0x55464248u
#define FASTBOOT_DIAG_MAGIC 0x55464244u
#define FASTBOOT_HANDOFF_PATH_BYTES 96u
#define FASTBOOT_PINMUX_R05_ADDR 0xb88004e5
#define FASTBOOT_GPIOR_OUTPUT_REG 0xb88000f4
#define FASTBOOT_GPIOR_DIR_REG 0xb88000f8
#define FASTBOOT_BACKLIGHT_R05_MASK 0x20

#ifndef __ASSEMBLER__

#include <stdint.h>

struct fastboot_handoff {
   uint32_t magic;
   char path[FASTBOOT_HANDOFF_PATH_BYTES];
};

struct fastboot_diag {
   uint32_t magic;
   uint32_t stage_addr;
   uint32_t event;
   int32_t result;
   char path[FASTBOOT_HANDOFF_PATH_BYTES];
};

#endif

#endif
