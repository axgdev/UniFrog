	.set noreorder
	.set noat
	.set mips32

	.text
	.balign 16
	.globl unifrog_chainload_trampoline
	.ent unifrog_chainload_trampoline
unifrog_chainload_trampoline:
	mfc0	$at, $12
	li	$v0, 0xfffffffe
	and	$at, $at, $v0
	mtc0	$at, $12
	nop
	nop
	move	$t0, $a0
	move	$t1, $a1
	move	$t2, $a2
	move	$t3, $a3
	lw	$t4, 16($sp)

1:
	beqz	$t2, 2f
	nop
	lbu	$t5, 0($t1)
	addiu	$t1, $t1, 1
	sb	$t5, 0($t0)
	addiu	$t0, $t0, 1
	addiu	$t2, $t2, -1
	b	1b
	nop

2:
	li	$a0, 0x80001000
	subu	$a1, $t0, $a0
	jalr	$t3
	nop

	jr	$t4
	nop
	.end unifrog_chainload_trampoline

	.globl unifrog_chainload_trampoline_end
unifrog_chainload_trampoline_end:
