	.set noreorder
	.set noat
	.set mips32

#include "handoff.h"

	.section .text.start, "ax"
	.globl _stage1_start
	.ent _stage1_start
_stage1_start:
	li	$sp, FASTBOOT_STACK_ADDR
	jal	stage1_main
	nop

1:
	b	1b
	nop
	.end _stage1_start
