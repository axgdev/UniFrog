#include <regdef.h>

   .set noreorder

   .text

   .macro ABI_TRAMP name target
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

   ABI_TRAMP unifrog_abi_runtime_name_trampoline, unifrog_runtime_name
   ABI_TRAMP unifrog_abi_runtime_api_version_trampoline, unifrog_runtime_api_version
   ABI_TRAMP unifrog_abi_log_message_trampoline, unifrog_abi_log_message_cb
   ABI_TRAMP unifrog_abi_log_flush_trampoline, unifrog_log_flush
   ABI_TRAMP unifrog_abi_perf_count_trampoline, unifrog_perf_count
   ABI_TRAMP unifrog_abi_perf_elapsed_trampoline, unifrog_perf_elapsed
   ABI_TRAMP unifrog_abi_cache_flush_trampoline, unifrog_perf_cache_flush
   ABI_TRAMP unifrog_abi_cache_invalidate_trampoline, unifrog_perf_cache_invalidate
   ABI_TRAMP unifrog_abi_cache_flush_invalidate_trampoline, unifrog_perf_cache_flush_invalidate
   ABI_TRAMP unifrog_abi_memory_layout_trampoline, unifrog_abi_memory_layout
   ABI_TRAMP unifrog_abi_application_memory_slot_trampoline, unifrog_abi_application_memory_slot
   ABI_TRAMP unifrog_abi_core_load_progress_trampoline, unifrog_core_load_progress
   ABI_TRAMP unifrog_abi_malloc_trampoline, malloc
   ABI_TRAMP unifrog_abi_free_trampoline, free
   ABI_TRAMP unifrog_abi_calloc_trampoline, calloc
   ABI_TRAMP unifrog_abi_realloc_trampoline, realloc
   ABI_TRAMP unifrog_abi_memalign_trampoline, memalign
   ABI_TRAMP unifrog_abi_fopen_trampoline, fopen
   ABI_TRAMP unifrog_abi_fclose_trampoline, fclose
   ABI_TRAMP unifrog_abi_fread_trampoline, fread
   ABI_TRAMP unifrog_abi_fwrite_trampoline, fwrite
   ABI_TRAMP unifrog_abi_fseek_trampoline, fseek
   ABI_TRAMP unifrog_abi_ftell_trampoline, ftell
   .globl unifrog_abi_fseeko_trampoline
   .ent unifrog_abi_fseeko_trampoline
unifrog_abi_fseeko_trampoline:
   addiu sp, sp, -32
   sw    ra, 28(sp)
   sw    gp, 24(sp)
   /* o32 passes fseeko(FILE *, int64_t, int) whence on the caller stack. */
   lw    t0, 48(sp)
   sw    t0, 16(sp)
   la    gp, _gp
   jal   fseeko
   nop
   lw    gp, 24(sp)
   lw    ra, 28(sp)
   jr    ra
   addiu sp, sp, 32
   .end unifrog_abi_fseeko_trampoline
   ABI_TRAMP unifrog_abi_ftello_trampoline, ftello
   ABI_TRAMP unifrog_abi_fflush_trampoline, fflush
   ABI_TRAMP unifrog_abi_setvbuf_trampoline, setvbuf
   ABI_TRAMP unifrog_abi_remove_trampoline, remove
   ABI_TRAMP unifrog_abi_rename_trampoline, rename
   ABI_TRAMP unifrog_abi_time_trampoline, time
   ABI_TRAMP unifrog_abi_localtime_trampoline, localtime
   ABI_TRAMP unifrog_abi_gettimeofday_trampoline, gettimeofday
   ABI_TRAMP unifrog_abi_vsnprintf_trampoline, vsnprintf
   ABI_TRAMP unifrog_abi_vfprintf_trampoline, vfprintf
   ABI_TRAMP unifrog_abi_stat_trampoline, stat
   ABI_TRAMP unifrog_abi_mkdir_trampoline, mkdir
   ABI_TRAMP unifrog_abi_opendir_trampoline, opendir
   ABI_TRAMP unifrog_abi_readdir_trampoline, readdir
   ABI_TRAMP unifrog_abi_closedir_trampoline, closedir
   ABI_TRAMP unifrog_abi_log_auto_flush_bytes_trampoline, unifrog_log_auto_flush_bytes
   ABI_TRAMP unifrog_abi_log_set_auto_flush_bytes_trampoline, unifrog_log_set_auto_flush_bytes
   ABI_TRAMP unifrog_abi_audio_set_system_output_enabled_trampoline, unifrog_audio_set_system_output_enabled
   ABI_TRAMP unifrog_abi_audio_debug_dump_trampoline, unifrog_audio_debug_dump
   ABI_TRAMP unifrog_abi_input_save_previous_trampoline, unifrog_input_save_previous
   ABI_TRAMP unifrog_abi_input_poll_with_wireless_divisor_trampoline, unifrog_input_poll_with_wireless_divisor
   ABI_TRAMP unifrog_abi_input_menu_buttons_trampoline, unifrog_input_menu_buttons
