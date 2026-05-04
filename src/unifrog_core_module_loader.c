#include "unifrog_core_module_loader.h"

#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include <unifrog/abi.h>
#include <unifrog/log.h>

#include "unifrog_mips_call.h"

#define printf unifrog_log

#define CORE_MODULE_READ_CHUNK (256u * 1024u)
#define CORE_MODULE_TRAILING_PAD_MAX 64u

static int range_valid(uintptr_t start, uintptr_t end, uintptr_t base,
   size_t bytes)
{
   uintptr_t slot_end = base + bytes;

   if (end < start)
      return 0;
   if (start < base)
      return 0;
   if (end > slot_end)
      return 0;
   return 1;
}

static int read_available(FILE *file, void *dst, size_t bytes, size_t *out_done)
{
   uint8_t *out = (uint8_t *)dst;
   size_t done = 0;

   while (done < bytes) {
      size_t chunk = bytes - done;

      if (chunk > CORE_MODULE_READ_CHUNK)
         chunk = CORE_MODULE_READ_CHUNK;
      size_t got = fread(out + done, 1, chunk, file);
      done += got;
      if (got != chunk) {
         if (ferror(file))
            return -1;
         break;
      }
   }
   if (out_done)
      *out_done = done;
   return 0;
}

static int load_image_payload(FILE *file, uintptr_t load_addr,
   size_t image_size)
{
   size_t payload_size = image_size - sizeof(struct unifrog_core_module_header);
   size_t payload_read = 0;

   if (read_available(file,
       (void *)(load_addr + sizeof(struct unifrog_core_module_header)),
       payload_size, &payload_read) != 0)
      return -1;

   if (payload_read < payload_size) {
      size_t missing = payload_size - payload_read;

      if (missing > CORE_MODULE_TRAILING_PAD_MAX)
         return -1;
      memset((void *)(load_addr + sizeof(struct unifrog_core_module_header) +
         payload_read), 0, missing);
      printf("unifrog core_module padded_tail read=%u image=%u missing=%u\n",
         (unsigned)(sizeof(struct unifrog_core_module_header) + payload_read),
         (unsigned)image_size, (unsigned)missing);
   }
   return 0;
}

int unifrog_core_module_load_file(const char *path, const char *expected_id,
   struct unifrog_core_module_loaded *loaded)
{
   struct unifrog_abi_memory_slot slot;
   struct unifrog_core_module_header header;
   const struct unifrog_core_module_exports *exports;
   FILE *file;
   uintptr_t load_addr;
   size_t image_size;
   size_t memory_size;
   size_t bss_size;
   int ret = -1;

   if (!path || !path[0] || !loaded)
      return -1;

   memset(loaded, 0, sizeof(*loaded));
   if (unifrog_abi_application_memory_slot(&slot) != 0) {
      printf("unifrog core_module no_appmem path=%s\n", path);
      return -1;
   }

   file = fopen(path, "rb");
   if (!file) {
      printf("unifrog core_module open_failed path=%s errno=%d\n",
         path, errno);
      return -1;
   }

   if (fread(&header, 1, sizeof(header), file) != sizeof(header)) {
      printf("unifrog core_module read_header_failed path=%s\n", path);
      goto out_close;
   }

   if (header.magic != UNIFROG_CORE_MODULE_MAGIC ||
       header.header_size < sizeof(header) ||
       header.format_version != UNIFROG_CORE_MODULE_FORMAT_VERSION ||
       header.endian_mark != UNIFROG_CORE_MODULE_ENDIAN_MARK) {
      printf("unifrog core_module bad_header path=%s magic=0x%08x header=%u fmt=%u endian=0x%08x\n",
         path, header.magic, header.header_size, header.format_version,
         header.endian_mark);
      goto out_close;
   }
   if (!unifrog_abi_compatible(header.required_abi_version)) {
      printf("unifrog core_module bad_abi path=%s required=0x%06x runtime=0x%06x\n",
         path, header.required_abi_version, UNIFROG_ABI_VERSION);
      goto out_close;
   }
   header.core_id[sizeof(header.core_id) - 1] = '\0';
   if (expected_id && expected_id[0] &&
       strcmp(header.core_id, expected_id) != 0) {
      printf("unifrog core_module id_mismatch path=%s expected=%s actual=%s\n",
         path, expected_id, header.core_id);
      goto out_close;
   }

   load_addr = header.load_addr;
   if (load_addr != slot.base ||
       !(header.flags & UNIFROG_CORE_MODULE_FLAG_FIXED_ADDRESS)) {
      printf("unifrog core_module address_mismatch path=%s module=0x%08lx slot=0x%08lx flags=0x%08x\n",
         path, (unsigned long)load_addr, (unsigned long)slot.base,
         header.flags);
      goto out_close;
   }
   if (!range_valid(header.load_addr, header.file_end_addr,
       slot.base, slot.bytes) ||
       !range_valid(header.load_addr, header.memory_end_addr,
       slot.base, slot.bytes) ||
       !range_valid(header.bss_addr, header.bss_end_addr,
       slot.base, slot.bytes) ||
       !range_valid(header.entry_addr, header.entry_addr + 4u,
       slot.base, slot.bytes) ||
       !range_valid(header.gp_addr, header.gp_addr + 4u,
       slot.base, slot.bytes)) {
      printf("unifrog core_module range_bad path=%s slot=0x%08lx+%u load=0x%08lx file_end=0x%08lx mem_end=0x%08lx bss=0x%08lx..0x%08lx entry=0x%08lx gp=0x%08lx\n",
         path, (unsigned long)slot.base, (unsigned)slot.bytes,
         (unsigned long)header.load_addr,
         (unsigned long)header.file_end_addr,
         (unsigned long)header.memory_end_addr,
         (unsigned long)header.bss_addr,
         (unsigned long)header.bss_end_addr,
         (unsigned long)header.entry_addr,
         (unsigned long)header.gp_addr);
      goto out_close;
   }

   image_size = (size_t)(header.file_end_addr - header.load_addr);
   memory_size = (size_t)(header.memory_end_addr - header.load_addr);
   bss_size = (size_t)(header.bss_end_addr - header.bss_addr);
   if (image_size == 0 || memory_size < image_size ||
       image_size < sizeof(header) ||
       header.bss_addr < header.file_end_addr) {
      printf("unifrog core_module size_bad path=%s header=%u image=%u memory=%u bss=%u\n",
         path, (unsigned)sizeof(header), (unsigned)image_size,
         (unsigned)memory_size, (unsigned)bss_size);
      goto out_close;
   }

   printf("unifrog core_module loading id=%s path=%s image=%u memory=%u bss=%u gp=0x%08lx\n",
      header.core_id, path, (unsigned)image_size, (unsigned)memory_size,
      (unsigned)bss_size, (unsigned long)header.gp_addr);
   (void)unifrog_log_flush();

   memcpy((void *)header.load_addr, &header, sizeof(header));
   if (load_image_payload(file, header.load_addr, image_size) != 0) {
      printf("unifrog core_module read_image_failed path=%s image=%u\n",
         path, (unsigned)image_size);
      goto out_close;
   }
   if (bss_size)
      memset((void *)header.bss_addr, 0, bss_size);

   unifrog_abi_get()->cache_flush_invalidate((const void *)header.load_addr,
      memory_size);
   exports = (const struct unifrog_core_module_exports *)
      unifrog_mips_call1(header.gp_addr, header.entry_addr,
         (uintptr_t)unifrog_abi_get());
   if (!exports ||
       exports->magic != UNIFROG_CORE_MODULE_EXPORTS_MAGIC ||
       exports->size < offsetof(struct unifrog_core_module_exports,
          retro_serialize_size) ||
       !unifrog_abi_compatible(exports->required_abi_version) ||
       !exports->core_id) {
      printf("unifrog core_module bad_exports path=%s exports=0x%08lx\n",
         path, (unsigned long)exports);
      goto out_close;
   }
   if (strcmp(exports->core_id, header.core_id) != 0) {
      printf("unifrog core_module export_id_mismatch header=%s export=%s\n",
         header.core_id, exports->core_id);
      goto out_close;
   }

   loaded->header = header;
   loaded->exports = exports;
   loaded->load_addr = header.load_addr;
   loaded->image_size = image_size;
   loaded->memory_size = memory_size;
   loaded->gp_addr = header.gp_addr;
   printf("unifrog core_module loaded id=%s exports=0x%08lx\n",
      header.core_id, (unsigned long)exports);
   ret = 0;

out_close:
   fclose(file);
   return ret;
}

void unifrog_core_module_unload(struct unifrog_core_module_loaded *loaded)
{
   if (!loaded || !loaded->load_addr)
      return;

   printf("unifrog core_module unload id=%s memory=%u\n",
      loaded->header.core_id, (unsigned)loaded->memory_size);
   memset(loaded, 0, sizeof(*loaded));
}
