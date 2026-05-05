#include <stddef.h>
#include <stdint.h>

#include <unifrog/core_module.h>
#include <unifrog/media.h>

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

extern int vidsink_init(void);
extern int llav_dis_init(void);
extern int viddec_driver_init(void);
extern int llav_vdec_init(void);

const struct unifrog_core_module_exports *unifrog_core_module_entry(
   const struct unifrog_abi *abi);

const struct unifrog_core_module_header unifrog_core_module_header
   __attribute__((section(".unifrog_module_header"), used, aligned(16))) = {
   .magic = UNIFROG_CORE_MODULE_MAGIC,
   .header_size = sizeof(struct unifrog_core_module_header),
   .format_version = UNIFROG_CORE_MODULE_FORMAT_VERSION,
   .endian_mark = UNIFROG_CORE_MODULE_ENDIAN_MARK,
   .required_abi_version = UNIFROG_ABI_VERSION,
   .flags = UNIFROG_CORE_MODULE_FLAG_FIXED_ADDRESS,
   .load_addr = (uintptr_t)__unifrog_module_start,
   .file_end_addr = (uintptr_t)__unifrog_module_file_end,
   .memory_end_addr = (uintptr_t)__unifrog_module_end,
   .bss_addr = (uintptr_t)__unifrog_module_bss_start,
   .bss_end_addr = (uintptr_t)__unifrog_module_bss_end,
   .entry_addr = (uintptr_t)unifrog_core_module_entry,
   .gp_addr = (uintptr_t)_gp,
   .core_id = "hcrtos-media",
   .extensions = "avi|mp4|mov|mkv|ts|m2ts|mpg|mpeg|h264|264|mp3|wav|flac|ogg|opus|aac|m4a|jpg|jpeg|png|gif|bmp",
};

static struct unifrog_core_module_exports exports = {
   .magic = UNIFROG_CORE_MODULE_EXPORTS_MAGIC,
   .size = sizeof(struct unifrog_core_module_exports),
   .required_abi_version = UNIFROG_ABI_VERSION,
   .flags = 0,
   .core_id = "hcrtos-media",
   .native_media_play_video_ex = unifrog_media_play_video_ex,
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

static void media_module_init_drivers(void)
{
   (void)vidsink_init();
   (void)llav_dis_init();
   (void)viddec_driver_init();
   (void)llav_vdec_init();
}

const struct unifrog_core_module_exports *unifrog_core_module_entry(
   const struct unifrog_abi *abi)
{
   static int initialized;

   if (!abi || abi->magic != UNIFROG_ABI_MAGIC)
      return NULL;
   unifrog_core_module_abi = abi;
   if (!initialized) {
      module_run_ctors();
      media_module_init_drivers();
      initialized = 1;
   }
   return &exports;
}
