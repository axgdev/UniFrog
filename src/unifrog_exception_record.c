#include <unifrog/exception_record.h>

#include <fastboot/handoff.h>
#include <unifrog/log.h>
#include <unifrog/paths.h>
#include <unifrog/perf.h>

#include <stdio.h>

static uint32_t checksum_mix(uint32_t checksum, uint32_t value);

static uint32_t exception_checksum(
   const volatile struct fastboot_exception_record *record)
{
   uint32_t checksum = record->magic ^ record->version ^ record->cause ^ record->epc ^
      record->badvaddr ^ record->ra ^ record->count ^ 0x9e3779b9u;

   checksum = checksum_mix(checksum, record->status);
   checksum = checksum_mix(checksum, record->sp);
   checksum = checksum_mix(checksum, record->task);
   checksum = checksum_mix(checksum, record->irq_nesting);
   checksum = checksum_mix(checksum, record->phase);
   checksum = checksum_mix(checksum, record->marker);
   checksum = checksum_mix(checksum, record->heartbeat);
   checksum = checksum_mix(checksum, record->detail0);
   checksum = checksum_mix(checksum, record->detail1);
   checksum = checksum_mix(checksum, record->epc_section);
   checksum = checksum_mix(checksum, record->ra_section);
   checksum = checksum_mix(checksum, record->gp);
   for (unsigned i = 0; i < FASTBOOT_EXCEPTION_EPC_WORDS; i++)
      checksum = checksum_mix(checksum, record->epc_words[i]);
   for (unsigned i = 0; i < FASTBOOT_EXCEPTION_STACK_WORDS; i++)
      checksum = checksum_mix(checksum, record->stack_words[i]);
   return checksum;
}

static uint32_t checksum_mix(uint32_t checksum, uint32_t value)
{
   return checksum ^ (value + 0x9e3779b9u +
      (checksum << 6) + (checksum >> 2));
}

static uint32_t activity_checksum(
   const volatile struct fastboot_activity_record *record)
{
   uint32_t checksum = 0x41435456u;

   checksum = checksum_mix(checksum, record->magic);
   checksum = checksum_mix(checksum, record->version);
   checksum = checksum_mix(checksum, record->flags);
   checksum = checksum_mix(checksum, record->phase);
   checksum = checksum_mix(checksum, record->marker);
   checksum = checksum_mix(checksum, record->heartbeat);
   checksum = checksum_mix(checksum, record->detail0);
   checksum = checksum_mix(checksum, record->detail1);
   checksum = checksum_mix(checksum, record->count);
   return checksum;
}

static int activity_valid(const volatile struct fastboot_activity_record *record)
{
   return record->magic == FASTBOOT_ACTIVITY_MAGIC &&
      record->version == FASTBOOT_ACTIVITY_VERSION &&
      record->checksum == activity_checksum(record);
}

static uint32_t read_status_register(void)
{
   uint32_t status;

   __asm__ volatile("mfc0 %0, $12" : "=r"(status));
   return status;
}

static uint32_t read_sp_register(void)
{
   uint32_t sp;

   __asm__ volatile("move %0, $29" : "=r"(sp));
   return sp;
}

static uint32_t read_gp_register(void)
{
   uint32_t gp;

   __asm__ volatile("move %0, $28" : "=r"(gp));
   return gp;
}

static int readable_code_addr(uint32_t addr)
{
   return addr >= 0x80000000u && addr < 0x80800000u && (addr & 3u) == 0;
}

static int readable_stack_addr(uint32_t addr)
{
   return addr >= 0x80000000u && addr < 0x88000000u && (addr & 3u) == 0;
}

static void copy_words(volatile uint32_t *out, unsigned count, uint32_t addr,
   int (*valid)(uint32_t))
{
   for (unsigned i = 0; i < count; i++) {
      uint32_t cur = addr + i * 4u;

      out[i] = valid(cur) ? *(volatile const uint32_t *)(uintptr_t)cur : 0;
   }
}

static const char *phase_label(uint32_t phase)
{
   switch (phase) {
   case UNIFROG_ACTIVITY_PHASE_BOOT:
      return "boot";
   case UNIFROG_ACTIVITY_PHASE_DMA:
      return "dma";
   case UNIFROG_ACTIVITY_PHASE_FRONTEND_START:
      return "frontend_start";
   case UNIFROG_ACTIVITY_PHASE_FRONTEND_STORAGE:
      return "frontend_storage";
   case UNIFROG_ACTIVITY_PHASE_EXTERNAL_CORE_BEGIN:
      return "external_core_begin";
   case UNIFROG_ACTIVITY_PHASE_EXTERNAL_CORE_LOAD:
      return "external_core_load";
   case UNIFROG_ACTIVITY_PHASE_EXTERNAL_CORE_API:
      return "external_core_api";
   case UNIFROG_ACTIVITY_PHASE_LIBRETRO_BEGIN:
      return "libretro_begin";
   case UNIFROG_ACTIVITY_PHASE_LIBRETRO_API:
      return "libretro_api";
   case UNIFROG_ACTIVITY_PHASE_LIBRETRO_CALLBACKS:
      return "libretro_callbacks";
   case UNIFROG_ACTIVITY_PHASE_CONTENT_PREPARE:
      return "content_prepare";
   case UNIFROG_ACTIVITY_PHASE_CORE_INIT:
      return "core_init";
   case UNIFROG_ACTIVITY_PHASE_RETRO_LOAD_GAME:
      return "retro_load_game";
   case UNIFROG_ACTIVITY_PHASE_SAVE_LOAD:
      return "save_load";
   case UNIFROG_ACTIVITY_PHASE_PRESENTER_OPEN:
      return "presenter_open";
   case UNIFROG_ACTIVITY_PHASE_AUDIO_OPEN:
      return "audio_open";
   case UNIFROG_ACTIVITY_PHASE_RUN_FRAME:
      return "run_frame";
   case UNIFROG_ACTIVITY_PHASE_SAVE_MEMORY:
      return "save_memory";
   case UNIFROG_ACTIVITY_PHASE_UNLOAD_GAME:
      return "unload_game";
   case UNIFROG_ACTIVITY_PHASE_DEINIT:
      return "deinit";
   case UNIFROG_ACTIVITY_PHASE_FINISH:
      return "finish";
   case UNIFROG_ACTIVITY_PHASE_CORE_MODULE_BEGIN:
      return "core_module_begin";
   case UNIFROG_ACTIVITY_PHASE_CORE_MODULE_OPEN:
      return "core_module_open";
   case UNIFROG_ACTIVITY_PHASE_CORE_MODULE_HEADER:
      return "core_module_header";
   case UNIFROG_ACTIVITY_PHASE_CORE_MODULE_VALIDATE:
      return "core_module_validate";
   case UNIFROG_ACTIVITY_PHASE_CORE_MODULE_READ:
      return "core_module_read";
   case UNIFROG_ACTIVITY_PHASE_CORE_MODULE_BSS:
      return "core_module_bss";
   case UNIFROG_ACTIVITY_PHASE_CORE_MODULE_CACHE:
      return "core_module_cache";
   case UNIFROG_ACTIVITY_PHASE_CORE_MODULE_ENTRY:
      return "core_module_entry";
   case UNIFROG_ACTIVITY_PHASE_CORE_MODULE_DONE:
      return "core_module_done";
   default:
      return "none";
   }
}

void unifrog_exception_record_store(uint32_t cause, uint32_t epc,
   uint32_t badvaddr, uint32_t ra)
{
   volatile struct fastboot_exception_record *record =
      FASTBOOT_EXCEPTION_ADDR;
   volatile struct fastboot_activity_record *activity =
      FASTBOOT_ACTIVITY_ADDR;
   uint32_t count = 1;

   if (record->magic == FASTBOOT_EXCEPTION_MAGIC &&
       record->version == FASTBOOT_EXCEPTION_VERSION)
      count = record->count + 1u;

   record->magic = FASTBOOT_EXCEPTION_MAGIC;
   record->version = FASTBOOT_EXCEPTION_VERSION;
   record->cause = cause;
   record->epc = epc;
   record->badvaddr = badvaddr;
   record->ra = ra;
   record->count = count;
   record->status = read_status_register();
   record->sp = read_sp_register();
   record->task = 0;
   record->irq_nesting = 0;
   record->phase = 0;
   record->marker = 0;
   record->heartbeat = 0;
   record->detail0 = 0;
   record->detail1 = 0;
   if (activity_valid(activity) &&
       (activity->flags & FASTBOOT_ACTIVITY_FLAG_ARMED)) {
      record->phase = activity->phase;
      record->marker = activity->marker;
      record->heartbeat = activity->heartbeat;
      record->detail0 = activity->detail0;
      record->detail1 = activity->detail1;
   }
   record->epc_section = epc & 0xfff00000u;
   record->ra_section = ra & 0xfff00000u;
   record->gp = read_gp_register();
   copy_words(record->epc_words, FASTBOOT_EXCEPTION_EPC_WORDS,
      epc & ~3u, readable_code_addr);
   copy_words(record->stack_words, FASTBOOT_EXCEPTION_STACK_WORDS,
      record->sp, readable_stack_addr);
   record->checksum = exception_checksum(record);
   unifrog_perf_cache_flush((const void *)record, sizeof(*record));
}

uint32_t unifrog_exception_activity_hash(const char *text)
{
   uint32_t hash = 2166136261u;

   if (text) {
      while (*text) {
         hash ^= (uint32_t)(uint8_t)*text++;
         hash *= 16777619u;
      }
   }
   return hash;
}

void unifrog_exception_activity_set(uint32_t phase, uint32_t marker,
   uint32_t detail0, uint32_t detail1)
{
   volatile struct fastboot_activity_record *record =
      FASTBOOT_ACTIVITY_ADDR;
   uint32_t count = 1;
   uint32_t heartbeat = 1;

   if (record->magic == FASTBOOT_ACTIVITY_MAGIC &&
       record->version == FASTBOOT_ACTIVITY_VERSION) {
      count = record->count + 1u;
      heartbeat = record->heartbeat + 1u;
   }

   record->magic = FASTBOOT_ACTIVITY_MAGIC;
   record->version = FASTBOOT_ACTIVITY_VERSION;
   record->flags = FASTBOOT_ACTIVITY_FLAG_ARMED;
   record->phase = phase;
   record->marker = marker;
   record->heartbeat = heartbeat;
   record->detail0 = detail0;
   record->detail1 = detail1;
   record->count = count;
   record->checksum = activity_checksum(record);
   unifrog_perf_cache_flush((const void *)record, sizeof(*record));
}

void unifrog_exception_activity_clear(void)
{
   volatile struct fastboot_activity_record *record =
      FASTBOOT_ACTIVITY_ADDR;

   record->magic = 0;
   record->flags = 0;
   record->checksum = 0;
   unifrog_perf_cache_flush((const void *)record, sizeof(*record));
}

int unifrog_exception_record_peek(uint32_t *cause, uint32_t *epc,
   uint32_t *badvaddr, uint32_t *ra, uint32_t *count)
{
   volatile struct fastboot_exception_record *record =
      FASTBOOT_EXCEPTION_ADDR;
   uint32_t checksum;

   if (record->magic != FASTBOOT_EXCEPTION_MAGIC ||
       record->version != FASTBOOT_EXCEPTION_VERSION)
      return 0;
   checksum = exception_checksum(record);
   if (checksum != record->checksum)
      return -1;
   if (cause)
      *cause = record->cause;
   if (epc)
      *epc = record->epc;
   if (badvaddr)
      *badvaddr = record->badvaddr;
   if (ra)
      *ra = record->ra;
   if (count)
      *count = record->count;
   return 1;
}

void unifrog_exception_record_log_and_clear(const char *tag)
{
   volatile struct fastboot_exception_record *record =
      FASTBOOT_EXCEPTION_ADDR;
   uint32_t checksum;
   uint32_t count;
   uint32_t cause;
   uint32_t epc;
   uint32_t badvaddr;
   uint32_t ra;
   uint32_t status;
   uint32_t sp;
   uint32_t gp;
   FILE *file;

   if (record->magic != FASTBOOT_EXCEPTION_MAGIC ||
       record->version != FASTBOOT_EXCEPTION_VERSION)
      return;

   checksum = exception_checksum(record);
   if (checksum != record->checksum) {
      unifrog_log("unifrog exception retained tag=%s invalid checksum stored=0x%08lx expected=0x%08lx\n",
         tag ? tag : "",
         (unsigned long)record->checksum,
         (unsigned long)checksum);
      record->magic = 0;
      record->checksum = 0;
      unifrog_perf_cache_flush((const void *)record, sizeof(*record));
      return;
   }

   count = record->count;
   cause = record->cause;
   epc = record->epc;
   badvaddr = record->badvaddr;
   ra = record->ra;
   status = record->status;
   sp = record->sp;
   gp = record->gp;
   unifrog_log("unifrog exception retained tag=%s count=%lu cause=0x%08lx epc=0x%08lx badv=0x%08lx ra=0x%08lx status=0x%08lx sp=0x%08lx gp=0x%08lx phase=%lu(%s) marker=%lu beat=%lu detail0=0x%08lx detail1=0x%08lx epc_section=0x%08lx ra_section=0x%08lx\n",
      tag ? tag : "",
      (unsigned long)count,
      (unsigned long)cause,
      (unsigned long)epc,
      (unsigned long)badvaddr,
      (unsigned long)ra,
      (unsigned long)status,
      (unsigned long)sp,
      (unsigned long)gp,
      (unsigned long)record->phase,
      phase_label(record->phase),
      (unsigned long)record->marker,
      (unsigned long)record->heartbeat,
      (unsigned long)record->detail0,
      (unsigned long)record->detail1,
      (unsigned long)record->epc_section,
      (unsigned long)record->ra_section);
   file = fopen(UNIFROG_CRASH_MARKER_PATH, "wb");
   if (file) {
      fprintf(file,
         "type=exception\n"
         "tag=%s\n"
         "count=%lu\n"
         "cause=0x%08lx\n"
         "epc=0x%08lx\n"
         "badvaddr=0x%08lx\n"
         "ra=0x%08lx\n"
         "status=0x%08lx\n"
         "sp=0x%08lx\n"
         "gp=0x%08lx\n"
         "phase=%lu\n"
         "phase_label=%s\n"
         "marker=%lu\n"
         "heartbeat=%lu\n"
         "detail0=0x%08lx\n"
         "detail1=0x%08lx\n"
         "epc_section=0x%08lx\n"
         "ra_section=0x%08lx\n",
         tag ? tag : "",
         (unsigned long)count,
         (unsigned long)cause,
         (unsigned long)epc,
         (unsigned long)badvaddr,
         (unsigned long)ra,
         (unsigned long)status,
         (unsigned long)sp,
         (unsigned long)gp,
         (unsigned long)record->phase,
         phase_label(record->phase),
         (unsigned long)record->marker,
         (unsigned long)record->heartbeat,
         (unsigned long)record->detail0,
         (unsigned long)record->detail1,
         (unsigned long)record->epc_section,
         (unsigned long)record->ra_section);
      for (unsigned i = 0; i < FASTBOOT_EXCEPTION_EPC_WORDS; i++)
         fprintf(file, "epc_word%u=0x%08lx\n", i,
            (unsigned long)record->epc_words[i]);
      for (unsigned i = 0; i < FASTBOOT_EXCEPTION_STACK_WORDS; i++)
         fprintf(file, "stack_word%u=0x%08lx\n", i,
            (unsigned long)record->stack_words[i]);
      fclose(file);
   } else {
      unifrog_log("unifrog exception marker write failed path=%s\n",
         UNIFROG_CRASH_MARKER_PATH);
   }
   record->magic = 0;
   record->checksum = 0;
   unifrog_perf_cache_flush((const void *)record, sizeof(*record));
}

void unifrog_exception_activity_log_and_clear(const char *tag)
{
   volatile struct fastboot_activity_record *record =
      FASTBOOT_ACTIVITY_ADDR;
   uint32_t checksum;
   FILE *file;

   if (record->magic != FASTBOOT_ACTIVITY_MAGIC ||
       record->version != FASTBOOT_ACTIVITY_VERSION)
      return;

   checksum = activity_checksum(record);
   if (checksum != record->checksum) {
      unifrog_log("unifrog activity retained tag=%s invalid checksum stored=0x%08lx expected=0x%08lx\n",
         tag ? tag : "",
         (unsigned long)record->checksum,
         (unsigned long)checksum);
      unifrog_exception_activity_clear();
      return;
   }
   if (!(record->flags & FASTBOOT_ACTIVITY_FLAG_ARMED)) {
      unifrog_exception_activity_clear();
      return;
   }

   unifrog_log("unifrog activity retained tag=%s count=%lu phase=%lu(%s) marker=%lu beat=%lu detail0=0x%08lx detail1=0x%08lx\n",
      tag ? tag : "",
      (unsigned long)record->count,
      (unsigned long)record->phase,
      phase_label(record->phase),
      (unsigned long)record->marker,
      (unsigned long)record->heartbeat,
      (unsigned long)record->detail0,
      (unsigned long)record->detail1);
   file = fopen(UNIFROG_CRASH_MARKER_PATH, "wb");
   if (file) {
      fprintf(file,
         "type=activity\n"
         "tag=%s\n"
         "count=%lu\n"
         "phase=%lu\n"
         "phase_label=%s\n"
         "marker=%lu\n"
         "heartbeat=%lu\n"
         "detail0=0x%08lx\n"
         "detail1=0x%08lx\n",
         tag ? tag : "",
         (unsigned long)record->count,
         (unsigned long)record->phase,
         phase_label(record->phase),
         (unsigned long)record->marker,
         (unsigned long)record->heartbeat,
         (unsigned long)record->detail0,
         (unsigned long)record->detail1);
      fclose(file);
   } else {
      unifrog_log("unifrog activity marker write failed path=%s\n",
         UNIFROG_CRASH_MARKER_PATH);
   }
   unifrog_exception_activity_clear();
}
