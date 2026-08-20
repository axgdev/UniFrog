#include "unifrog_core_module_loader.h"

#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/unistd.h>

#include <unifrog/abi.h>
#include <unifrog/log.h>
#include <unifrog/perf.h>
#include "abi/unifrog_mips_call.h"

#define printf unifrog_log

#define CORE_MODULE_READ_CHUNK (256u * 1024u)
#define CORE_MODULE_TRAILING_PAD_MAX 64u

static unsigned elapsed_ms(uint32_t start_ms, uint32_t end_ms)
{
   return end_ms - start_ms;
}

static int range_valid(uintptr_t start, uintptr_t end, uintptr_t base,
   size_t bytes)
{
   if (base > UINTPTR_MAX - bytes)
      return 0;

   uintptr_t slot_end = base + bytes;

   if (end < start)
      return 0;
   if (start < base)
      return 0;
   if (end > slot_end)
      return 0;
   return 1;
}

const char *unifrog_core_module_load_error_name(
   enum unifrog_core_module_load_error error)
{
   switch (error) {
   case UNIFROG_CORE_MODULE_LOAD_OK:
      return "ok";
   case UNIFROG_CORE_MODULE_LOAD_ARGUMENT:
      return "argument";
   case UNIFROG_CORE_MODULE_LOAD_NO_APP_MEMORY:
      return "no_app_memory";
   case UNIFROG_CORE_MODULE_LOAD_IO:
      return "io";
   case UNIFROG_CORE_MODULE_LOAD_FORMAT:
      return "format";
   case UNIFROG_CORE_MODULE_LOAD_ABI:
      return "abi";
   case UNIFROG_CORE_MODULE_LOAD_ID:
      return "id";
   case UNIFROG_CORE_MODULE_LOAD_RANGE:
      return "range";
   case UNIFROG_CORE_MODULE_LOAD_EXPORTS:
      return "exports";
   }
   return "unknown";
}

static int read_available_fd(int fd, void *dst, size_t bytes,
   size_t *out_done)
{
   uint8_t *out = (uint8_t *)dst;
   size_t done = 0;

   while (done < bytes) {
      size_t chunk = bytes - done;

      if (chunk > CORE_MODULE_READ_CHUNK)
         chunk = CORE_MODULE_READ_CHUNK;
      ssize_t got = read(fd, out + done, chunk);
      if (got < 0)
         return -1;
      if (got == 0)
         break;
      done += got;
   }
   if (out_done)
      *out_done = done;
   return 0;
}

static int load_image_payload_fd(int fd, uintptr_t load_addr,
   size_t image_size)
{
   size_t payload_size = image_size - sizeof(struct unifrog_core_module_header);
   size_t payload_read = 0;

   if (read_available_fd(fd,
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
   int fd;
   uintptr_t load_addr;
   size_t image_size;
   size_t memory_size;
   size_t bss_size;
   uint32_t total_start_ms;
   uint32_t open_done_ms;
   uint32_t header_done_ms;
   uint32_t validate_done_ms;
   uint32_t read_done_ms;
   uint32_t bss_done_ms;
   uint32_t cache_done_ms;
   uint32_t entry_done_ms;
   size_t header_read = 0;
   int ret = -1;

   if (!loaded)
      return -1;

   total_start_ms = unifrog_perf_time_ms();
   memset(loaded, 0, sizeof(*loaded));
   if (!path || !path[0]) {
      loaded->error = UNIFROG_CORE_MODULE_LOAD_ARGUMENT;
      return -1;
   }
   unifrog_log_sync("core_module load begin path=%s expected=%s",
      path, expected_id ? expected_id : "");
   if (unifrog_abi_application_memory_slot(&slot) != 0) {
      printf("unifrog core_module no_appmem path=%s\n", path);
      unifrog_log_sync("core_module load fail stage=no_appmem path=%s", path);
      loaded->error = UNIFROG_CORE_MODULE_LOAD_NO_APP_MEMORY;
      return -1;
   }

   fd = open(path, O_RDONLY);
   if (fd < 0) {
      printf("unifrog core_module open_failed path=%s errno=%d\n",
         path, errno);
      unifrog_log_sync("core_module load fail stage=open path=%s errno=%d",
         path, errno);
      loaded->error = UNIFROG_CORE_MODULE_LOAD_IO;
      return -1;
   }
   open_done_ms = unifrog_perf_time_ms();

   if (read_available_fd(fd, &header, sizeof(header), &header_read) != 0 ||
       header_read != sizeof(header)) {
      printf("unifrog core_module read_header_failed path=%s read=%u expected=%u errno=%d\n",
         path, (unsigned)header_read, (unsigned)sizeof(header), errno);
      loaded->error = UNIFROG_CORE_MODULE_LOAD_IO;
      goto out_close;
   }
   header_done_ms = unifrog_perf_time_ms();

   if (!unifrog_core_module_header_layout_valid(&header)) {
      printf("unifrog core_module bad_header path=%s magic=0x%08x header=%u fmt=%u endian=0x%08x flags=0x%08x load=0x%08lx file_end=0x%08lx memory_end=0x%08lx bss=0x%08lx..0x%08lx entry=0x%08lx gp=0x%08lx\n",
         path, header.magic, header.header_size, header.format_version,
         header.endian_mark, header.flags, (unsigned long)header.load_addr,
         (unsigned long)header.file_end_addr,
         (unsigned long)header.memory_end_addr,
         (unsigned long)header.bss_addr, (unsigned long)header.bss_end_addr,
         (unsigned long)header.entry_addr, (unsigned long)header.gp_addr);
      loaded->error = UNIFROG_CORE_MODULE_LOAD_FORMAT;
      goto out_close;
   }
   if (!unifrog_abi_table_compatible(unifrog_abi_get(),
       header.required_abi_version,
       header.required_abi_size ? header.required_abi_size :
       UNIFROG_ABI_CORE_MIN_SIZE)) {
      printf("unifrog core_module bad_abi path=%s required=0x%06x required_size=%u runtime=0x%06x runtime_size=%u built=0x%06x built_size=%u\n",
         path, header.required_abi_version,
         header.required_abi_size ? header.required_abi_size :
         (unsigned)UNIFROG_ABI_CORE_MIN_SIZE,
         unifrog_abi_get()->version, (unsigned)unifrog_abi_get()->size,
         header.built_abi_version, header.built_abi_size);
      loaded->error = UNIFROG_CORE_MODULE_LOAD_ABI;
      goto out_close;
   }
   header.core_id[sizeof(header.core_id) - 1] = '\0';
   if (expected_id && expected_id[0] &&
       strcmp(header.core_id, expected_id) != 0) {
      printf("unifrog core_module id_mismatch path=%s expected=%s actual=%s\n",
         path, expected_id, header.core_id);
      loaded->error = UNIFROG_CORE_MODULE_LOAD_ID;
      goto out_close;
   }

   load_addr = header.load_addr;
   if (load_addr != slot.base) {
      printf("unifrog core_module address_mismatch path=%s module=0x%08lx slot=0x%08lx flags=0x%08x\n",
         path, (unsigned long)load_addr, (unsigned long)slot.base,
         header.flags);
      loaded->error = UNIFROG_CORE_MODULE_LOAD_RANGE;
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
      loaded->error = UNIFROG_CORE_MODULE_LOAD_RANGE;
      goto out_close;
   }

   image_size = (size_t)(header.file_end_addr - header.load_addr);
   memory_size = (size_t)(header.memory_end_addr - header.load_addr);
   bss_size = (size_t)(header.bss_end_addr - header.bss_addr);
   if (image_size == 0 || memory_size < image_size ||
       image_size < sizeof(header)) {
      printf("unifrog core_module size_bad path=%s header=%u image=%u memory=%u bss=%u\n",
         path, (unsigned)sizeof(header), (unsigned)image_size,
         (unsigned)memory_size, (unsigned)bss_size);
      loaded->error = UNIFROG_CORE_MODULE_LOAD_RANGE;
      goto out_close;
   }

   printf("unifrog core_module loading id=%s path=%s image=%u memory=%u bss=%u gp=0x%08lx required_abi=0x%06x built_abi=0x%06x required_size=%u built_size=%u exports_size=%u runtime_abi=0x%06x runtime_size=%u\n",
      header.core_id, path, (unsigned)image_size, (unsigned)memory_size,
      (unsigned)bss_size, (unsigned long)header.gp_addr,
      header.required_abi_version, header.built_abi_version,
      header.required_abi_size, header.built_abi_size, header.exports_size,
      unifrog_abi_get()->version, (unsigned)unifrog_abi_get()->size);
   (void)unifrog_log_flush();
   validate_done_ms = unifrog_perf_time_ms();

   memcpy((void *)header.load_addr, &header, sizeof(header));
   if (load_image_payload_fd(fd, header.load_addr, image_size) != 0) {
      printf("unifrog core_module read_image_failed path=%s image=%u errno=%d\n",
         path, (unsigned)image_size, errno);
      loaded->error = UNIFROG_CORE_MODULE_LOAD_IO;
      goto out_close;
   }
   read_done_ms = unifrog_perf_time_ms();
   if (bss_size)
      memset((void *)header.bss_addr, 0, bss_size);
   bss_done_ms = unifrog_perf_time_ms();

   unifrog_abi_get()->cache_flush_invalidate((const void *)header.load_addr,
      image_size);
   cache_done_ms = unifrog_perf_time_ms();
   exports = (const struct unifrog_core_module_exports *)
      unifrog_mips_call1(header.gp_addr, header.entry_addr,
         (uintptr_t)unifrog_abi_get());
   entry_done_ms = unifrog_perf_time_ms();
   if (!exports ||
       exports->magic != UNIFROG_CORE_MODULE_EXPORTS_MAGIC ||
       exports->size < UNIFROG_CORE_MODULE_EXPORTS_LIBRETRO_SIZE ||
       !unifrog_abi_table_compatible(unifrog_abi_get(),
          exports->required_abi_version,
          header.required_abi_size ? header.required_abi_size :
          UNIFROG_ABI_CORE_MIN_SIZE) ||
       !exports->core_id) {
      printf("unifrog core_module bad_exports path=%s exports=0x%08lx magic=0x%08x size=%u min_size=%u required_abi=0x%06x\n",
         path, (unsigned long)exports, exports ? exports->magic : 0,
         exports ? exports->size : 0,
         (unsigned)UNIFROG_CORE_MODULE_EXPORTS_LIBRETRO_SIZE,
         exports ? exports->required_abi_version : 0);
      loaded->error = UNIFROG_CORE_MODULE_LOAD_EXPORTS;
      goto out_close;
   }
   if (strcmp(exports->core_id, header.core_id) != 0) {
      printf("unifrog core_module export_id_mismatch header=%s export=%s\n",
         header.core_id, exports->core_id);
      loaded->error = UNIFROG_CORE_MODULE_LOAD_EXPORTS;
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
   printf("unifrog core_module timing id=%s total=%u open=%u header=%u validate=%u read=%u bss=%u cache=%u entry=%u image=%u memory=%u\n",
      header.core_id, elapsed_ms(total_start_ms, entry_done_ms),
      elapsed_ms(total_start_ms, open_done_ms),
      elapsed_ms(open_done_ms, header_done_ms),
      elapsed_ms(header_done_ms, validate_done_ms),
      elapsed_ms(validate_done_ms, read_done_ms),
      elapsed_ms(read_done_ms, bss_done_ms),
      elapsed_ms(bss_done_ms, cache_done_ms),
      elapsed_ms(cache_done_ms, entry_done_ms), (unsigned)image_size,
      (unsigned)memory_size);
   unifrog_log_sync("core_module load done id=%s path=%s total=%u read=%u image=%u memory=%u",
      header.core_id, path, elapsed_ms(total_start_ms, entry_done_ms),
      elapsed_ms(validate_done_ms, read_done_ms), (unsigned)image_size,
      (unsigned)memory_size);
   ret = 0;

out_close:
   if (ret != 0) {
      if (loaded->error == UNIFROG_CORE_MODULE_LOAD_OK)
         loaded->error = UNIFROG_CORE_MODULE_LOAD_IO;
      unifrog_log_sync("core_module load fail path=%s expected=%s error=%s errno=%d",
         path, expected_id ? expected_id : "",
         unifrog_core_module_load_error_name(loaded->error), errno);
   }
   close(fd);
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
