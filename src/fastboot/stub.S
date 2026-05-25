	.set noreorder
	.set noat
	.set mips32

#include "handoff.h"

	.section .text.start, "ax"
	.globl _start
	.ent _start
_start:
	/* Keep fastboot board-neutral: only blank the known backlight gate. */
	li	$t4, FASTBOOT_GPIOR_OUTPUT_REG
	lw	$t5, 0($t4)
	ori	$t5, $t5, FASTBOOT_BACKLIGHT_R05_MASK
	sw	$t5, 0($t4)

	li	$t4, FASTBOOT_GPIOR_DIR_REG
	lw	$t5, 0($t4)
	ori	$t5, $t5, FASTBOOT_BACKLIGHT_R05_MASK
	sw	$t5, 0($t4)

	li	$t4, FASTBOOT_PINMUX_R05_ADDR
	sb	$zero, 0($t4)

	li	$t4, FASTBOOT_TRACE_BASE
	li	$t5, FASTBOOT_TRACE_MAGIC
	sw	$t5, FASTBOOT_TRACE_MAGIC_OFFSET($t4)
	li	$t5, FASTBOOT_TRACE_VERSION
	sw	$t5, FASTBOOT_TRACE_VERSION_OFFSET($t4)
	li	$t5, 1
	sw	$t5, FASTBOOT_TRACE_COUNT_OFFSET($t4)
	sw	$zero, FASTBOOT_TRACE_DROPPED_OFFSET($t4)

	li	$t4, FASTBOOT_TRACE_BASE + FASTBOOT_TRACE_ENTRY_OFFSET
	sw	$zero, FASTBOOT_TRACE_ENTRY_SEQ_OFFSET($t4)
	li	$t5, FASTBOOT_TRACE_STUB_BACKLIGHT_OFF
	sw	$t5, FASTBOOT_TRACE_ENTRY_EVENT_OFFSET($t4)
	sw	$zero, FASTBOOT_TRACE_ENTRY_ARG0_OFFSET($t4)
	sw	$zero, FASTBOOT_TRACE_ENTRY_ARG1_OFFSET($t4)
	sw	$zero, FASTBOOT_TRACE_ENTRY_ARG2_OFFSET($t4)
	li	$t5, 0x300
	sw	$t5, FASTBOOT_TRACE_ENTRY_R05_OFFSET($t4)

	la	$t0, __stage1_blob_start
	la	$t1, __stage1_blob_end
	li	$t2, FASTBOOT_STAGE1_ADDR

1:
	beq	$t0, $t1, 2f
	nop
	lw	$t3, 0($t0)
	addiu	$t0, $t0, 4
	sw	$t3, 0($t2)
	b	1b
	addiu	$t2, $t2, 4

2:
	li	$t9, FASTBOOT_STAGE1_ADDR
	jr	$t9
	nop
	.end _start

	.section .rodata, "a"
	.balign 16
	.globl __stage1_blob_start
__stage1_blob_start:
	.incbin FASTBOOT_STAGE_BIN_PATH
	.balign 16
	.globl __stage1_blob_end
__stage1_blob_end:
