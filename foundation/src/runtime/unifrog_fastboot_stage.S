	.set noreorder

	.section .rodata.fastboot_stage, "a"
	.balign 16
	.globl unifrog_fastboot_stage_start
unifrog_fastboot_stage_start:
	.incbin FASTBOOT_STAGE_BIN_PATH
	.globl unifrog_fastboot_stage_end
unifrog_fastboot_stage_end:
	.balign 16
