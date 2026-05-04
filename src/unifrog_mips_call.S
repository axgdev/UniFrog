#include <regdef.h>

   .set noreorder
   .set noat

   .text

   .globl unifrog_mips_call0
   .ent unifrog_mips_call0
unifrog_mips_call0:
   addiu sp, sp, -24
   sw    ra, 20(sp)
   sw    gp, 16(sp)
   move  t9, a1
   move  gp, a0
   jalr  t9
   nop
   lw    gp, 16(sp)
   lw    ra, 20(sp)
   jr    ra
   addiu sp, sp, 24
   .end unifrog_mips_call0

   .globl unifrog_mips_call1
   .ent unifrog_mips_call1
unifrog_mips_call1:
   addiu sp, sp, -24
   sw    ra, 20(sp)
   sw    gp, 16(sp)
   move  t9, a1
   move  gp, a0
   move  a0, a2
   jalr  t9
   nop
   lw    gp, 16(sp)
   lw    ra, 20(sp)
   jr    ra
   addiu sp, sp, 24
   .end unifrog_mips_call1

   .globl unifrog_mips_call2
   .ent unifrog_mips_call2
unifrog_mips_call2:
   addiu sp, sp, -24
   sw    ra, 20(sp)
   sw    gp, 16(sp)
   move  t9, a1
   move  gp, a0
   move  a0, a2
   move  a1, a3
   jalr  t9
   nop
   lw    gp, 16(sp)
   lw    ra, 20(sp)
   jr    ra
   addiu sp, sp, 24
   .end unifrog_mips_call2

   .globl unifrog_mips_call3
   .ent unifrog_mips_call3
unifrog_mips_call3:
   addiu sp, sp, -32
   sw    ra, 28(sp)
   sw    gp, 24(sp)
   move  t9, a1
   move  gp, a0
   move  a0, a2
   move  a1, a3
   lw    a2, 48(sp)
   jalr  t9
   nop
   lw    gp, 24(sp)
   lw    ra, 28(sp)
   jr    ra
   addiu sp, sp, 32
   .end unifrog_mips_call3
