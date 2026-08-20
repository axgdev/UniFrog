#include <stddef.h>
#include <stdint.h>

#include <unifrog/core_module.h>

#ifndef UNIFROG_MODULE_CORE_ID
#error "UNIFROG_MODULE_CORE_ID is required"
#endif

#ifndef UNIFROG_MODULE_EXTENSIONS
#define UNIFROG_MODULE_EXTENSIONS ""
#endif

#define UF_CAT2(a, b) a##_##b
#define UF_CAT(a, b) UF_CAT2(a, b)

#ifdef UNIFROG_MODULE_SYMBOL_PREFIX
#define UF_RETRO(name) UF_CAT(UNIFROG_MODULE_SYMBOL_PREFIX, name)
#else
#define UF_RETRO(name) name
#endif

extern char _gp[];
extern char __unifrog_module_start[];
extern char __unifrog_module_file_end[];
extern char __unifrog_module_end[];
extern char __unifrog_module_bss_start[];
extern char __unifrog_module_bss_end[];
extern uintptr_t __CTOR_LIST__[];
extern uintptr_t __init_array_start[];
extern uintptr_t __init_array_end[];
extern const struct unifrog_abi *unifrog_core_module_abi;

extern void UF_RETRO(retro_set_environment)(retro_environment_t cb);
extern void UF_RETRO(retro_set_video_refresh)(retro_video_refresh_t cb);
extern void UF_RETRO(retro_set_audio_sample)(retro_audio_sample_t cb);
extern void UF_RETRO(retro_set_audio_sample_batch)(retro_audio_sample_batch_t cb);
extern void UF_RETRO(retro_set_input_poll)(retro_input_poll_t cb);
extern void UF_RETRO(retro_set_input_state)(retro_input_state_t cb);
extern void UF_RETRO(retro_init)(void);
extern void UF_RETRO(retro_deinit)(void);
extern unsigned UF_RETRO(retro_api_version)(void);
extern void UF_RETRO(retro_get_system_info)(struct retro_system_info *info);
extern void UF_RETRO(retro_get_system_av_info)(struct retro_system_av_info *info);
extern void UF_RETRO(retro_set_controller_port_device)(unsigned port,
   unsigned device);
extern void UF_RETRO(retro_run)(void);
extern void UF_RETRO(retro_unload_game)(void);
extern bool UF_RETRO(retro_load_game)(const struct retro_game_info *game);
extern unsigned UF_RETRO(retro_get_region)(void);
extern size_t UF_RETRO(retro_serialize_size)(void);
extern bool UF_RETRO(retro_serialize)(void *data, size_t size);
extern bool UF_RETRO(retro_unserialize)(const void *data, size_t size);
extern void *UF_RETRO(retro_get_memory_data)(unsigned id);
extern size_t UF_RETRO(retro_get_memory_size)(unsigned id);
extern void UF_RETRO(retro_cheat_reset)(void);
extern void UF_RETRO(retro_cheat_set)(unsigned index, bool enabled,
   const char *code);

const struct unifrog_core_module_exports *unifrog_core_module_entry(
   const struct unifrog_abi *abi);

const struct unifrog_core_module_header unifrog_core_module_header
   __attribute__((section(".unifrog_module_header"), used, aligned(16))) = {
   .magic = UNIFROG_CORE_MODULE_MAGIC,
   .header_size = sizeof(struct unifrog_core_module_header),
   .format_version = UNIFROG_CORE_MODULE_FORMAT_VERSION,
   .endian_mark = UNIFROG_CORE_MODULE_ENDIAN_MARK,
   .required_abi_version = UNIFROG_CORE_MODULE_REQUIRED_ABI_VERSION,
   .flags = UNIFROG_CORE_MODULE_FLAG_FIXED_ADDRESS,
   .load_addr = (uintptr_t)__unifrog_module_start,
   .file_end_addr = (uintptr_t)__unifrog_module_file_end,
   .memory_end_addr = (uintptr_t)__unifrog_module_end,
   .bss_addr = (uintptr_t)__unifrog_module_bss_start,
   .bss_end_addr = (uintptr_t)__unifrog_module_bss_end,
   .entry_addr = (uintptr_t)unifrog_core_module_entry,
   .gp_addr = (uintptr_t)_gp,
   .core_id = UNIFROG_MODULE_CORE_ID,
   .extensions = UNIFROG_MODULE_EXTENSIONS,
   .built_abi_version = UNIFROG_ABI_VERSION,
   .required_abi_size = UNIFROG_CORE_MODULE_REQUIRED_ABI_SIZE,
   .built_abi_size = sizeof(struct unifrog_abi),
   .exports_size = sizeof(struct unifrog_core_module_exports),
};

static struct unifrog_core_module_exports exports = {
   .magic = UNIFROG_CORE_MODULE_EXPORTS_MAGIC,
   .size = sizeof(struct unifrog_core_module_exports),
   .required_abi_version = UNIFROG_CORE_MODULE_REQUIRED_ABI_VERSION,
   .flags = 0,
   .core_id = UNIFROG_MODULE_CORE_ID,
   .retro_set_environment = UF_RETRO(retro_set_environment),
   .retro_set_video_refresh = UF_RETRO(retro_set_video_refresh),
   .retro_set_audio_sample = UF_RETRO(retro_set_audio_sample),
   .retro_set_audio_sample_batch = UF_RETRO(retro_set_audio_sample_batch),
   .retro_set_input_poll = UF_RETRO(retro_set_input_poll),
   .retro_set_input_state = UF_RETRO(retro_set_input_state),
   .retro_init = UF_RETRO(retro_init),
   .retro_deinit = UF_RETRO(retro_deinit),
   .retro_api_version = UF_RETRO(retro_api_version),
   .retro_get_system_info = UF_RETRO(retro_get_system_info),
   .retro_get_system_av_info = UF_RETRO(retro_get_system_av_info),
   .retro_set_controller_port_device =
      UF_RETRO(retro_set_controller_port_device),
   .retro_run = UF_RETRO(retro_run),
   .retro_unload_game = UF_RETRO(retro_unload_game),
   .retro_load_game = UF_RETRO(retro_load_game),
   .retro_get_region = UF_RETRO(retro_get_region),
   .retro_serialize_size = UF_RETRO(retro_serialize_size),
   .retro_serialize = UF_RETRO(retro_serialize),
   .retro_unserialize = UF_RETRO(retro_unserialize),
   .retro_get_memory_data = UF_RETRO(retro_get_memory_data),
   .retro_get_memory_size = UF_RETRO(retro_get_memory_size),
   .retro_cheat_reset = UF_RETRO(retro_cheat_reset),
   .retro_cheat_set = UF_RETRO(retro_cheat_set),
};

static void module_run_ctors(void)
{
   uintptr_t ctor_count = __CTOR_LIST__[0];
   uintptr_t *init;

   for (uintptr_t i = 0; i < ctor_count; i++) {
      void (*ctor)(void) = (void (*)(void))__CTOR_LIST__[i + 1];

      if (ctor)
         ctor();
   }

   for (init = __init_array_start; init < __init_array_end; init++) {
      void (*ctor)(void) = (void (*)(void))*init;

      if (ctor)
         ctor();
   }
}

const struct unifrog_core_module_exports *unifrog_core_module_entry(
   const struct unifrog_abi *abi)
{
   static int initialized;

   if (!unifrog_abi_table_compatible(abi,
       UNIFROG_CORE_MODULE_REQUIRED_ABI_VERSION,
       UNIFROG_CORE_MODULE_REQUIRED_ABI_SIZE))
      return NULL;
   unifrog_core_module_abi = abi;
   if (!initialized) {
      module_run_ctors();
      initialized = 1;
   }
   return &exports;
}
