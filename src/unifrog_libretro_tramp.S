#include <regdef.h>

   .set noreorder

   .text

   .macro HOST_TRAMP name target
   .globl \name
   .ent \name
\name:
   addiu sp, sp, -24
   sw    ra, 20(sp)
   sw    gp, 16(sp)
   la    gp, _gp
   jal   \target
   nop
   lw    gp, 16(sp)
   lw    ra, 20(sp)
   jr    ra
   addiu sp, sp, 24
   .end \name
   .endm

   HOST_TRAMP unifrog_libretro_environment_trampoline, unifrog_libretro_environment_cb
   HOST_TRAMP unifrog_libretro_video_refresh_trampoline, unifrog_libretro_video_refresh_cb
   HOST_TRAMP unifrog_libretro_audio_sample_trampoline, unifrog_libretro_audio_sample_cb
   HOST_TRAMP unifrog_libretro_audio_batch_trampoline, unifrog_libretro_audio_batch_cb
   HOST_TRAMP unifrog_libretro_input_poll_trampoline, unifrog_libretro_input_poll_cb
   HOST_TRAMP unifrog_libretro_input_state_trampoline, unifrog_libretro_input_state_cb
   HOST_TRAMP unifrog_libretro_rumble_trampoline, unifrog_libretro_rumble_cb
