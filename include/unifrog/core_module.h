#ifndef UNIFROG_CORE_MODULE_H
#define UNIFROG_CORE_MODULE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <libretro.h>
#include <unifrog/abi.h>

#ifdef __cplusplus
extern "C" {
#endif

#define UNIFROG_CORE_MODULE_MAGIC 0x5546434fu
#define UNIFROG_CORE_MODULE_EXPORTS_MAGIC 0x55464345u
#define UNIFROG_CORE_MODULE_FORMAT_VERSION 1u
#define UNIFROG_CORE_MODULE_ENDIAN_MARK 0x12345678u

#define UNIFROG_CORE_MODULE_FLAG_FIXED_ADDRESS (1u << 0)

#define UNIFROG_CORE_MODULE_ID_MAX 32u
#define UNIFROG_CORE_MODULE_EXTENSIONS_MAX 128u

struct unifrog_core_module_header {
   uint32_t magic;
   uint32_t header_size;
   uint32_t format_version;
   uint32_t endian_mark;
   uint32_t required_abi_version;
   uint32_t flags;

   uintptr_t load_addr;
   uintptr_t file_end_addr;
   uintptr_t memory_end_addr;
   uintptr_t bss_addr;
   uintptr_t bss_end_addr;
   uintptr_t entry_addr;
   uintptr_t gp_addr;

   char core_id[UNIFROG_CORE_MODULE_ID_MAX];
   char extensions[UNIFROG_CORE_MODULE_EXTENSIONS_MAX];
   uint32_t reserved[8];
};

struct unifrog_core_module_exports {
   uint32_t magic;
   uint32_t size;
   uint32_t required_abi_version;
   uint32_t flags;
   const char *core_id;

   void (*retro_set_environment)(retro_environment_t cb);
   void (*retro_set_video_refresh)(retro_video_refresh_t cb);
   void (*retro_set_audio_sample)(retro_audio_sample_t cb);
   void (*retro_set_audio_sample_batch)(retro_audio_sample_batch_t cb);
   void (*retro_set_input_poll)(retro_input_poll_t cb);
   void (*retro_set_input_state)(retro_input_state_t cb);
   void (*retro_init)(void);
   void (*retro_deinit)(void);
   unsigned (*retro_api_version)(void);
   void (*retro_get_system_info)(struct retro_system_info *info);
   void (*retro_get_system_av_info)(struct retro_system_av_info *info);
   void (*retro_set_controller_port_device)(unsigned port, unsigned device);
   void (*retro_run)(void);
   void (*retro_unload_game)(void);
   bool (*retro_load_game)(const struct retro_game_info *game);
   unsigned (*retro_get_region)(void);
   size_t (*retro_serialize_size)(void);
   bool (*retro_serialize)(void *data, size_t size);
   bool (*retro_unserialize)(const void *data, size_t size);
   void *(*retro_get_memory_data)(unsigned id);
   size_t (*retro_get_memory_size)(unsigned id);
   void (*retro_cheat_reset)(void);
   void (*retro_cheat_set)(unsigned index, bool enabled, const char *code);
};

typedef const struct unifrog_core_module_exports *
   (*unifrog_core_module_entry_t)(const struct unifrog_abi *abi);

#ifdef __cplusplus
}
#endif

#endif
