#include <unifrog/exception_record.h>

#include <fastboot/handoff.h>
#include <unifrog/log.h>
#include <unifrog/perf.h>

static uint32_t exception_checksum(
   const volatile struct fastboot_exception_record *record)
{
   return record->magic ^ record->version ^ record->cause ^ record->epc ^
      record->badvaddr ^ record->ra ^ record->count ^ 0x9e3779b9u;
}

void unifrog_exception_record_store(uint32_t cause, uint32_t epc,
   uint32_t badvaddr, uint32_t ra)
{
   volatile struct fastboot_exception_record *record =
      FASTBOOT_EXCEPTION_ADDR;
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
   record->checksum = exception_checksum(record);
   unifrog_perf_cache_flush((const void *)record, sizeof(*record));
}

void unifrog_exception_record_log_and_clear(const char *tag)
{
   volatile struct fastboot_exception_record *record =
      FASTBOOT_EXCEPTION_ADDR;
   uint32_t checksum;

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

   unifrog_log("unifrog exception retained tag=%s count=%lu cause=0x%08lx epc=0x%08lx badv=0x%08lx ra=0x%08lx\n",
      tag ? tag : "",
      (unsigned long)record->count,
      (unsigned long)record->cause,
      (unsigned long)record->epc,
      (unsigned long)record->badvaddr,
      (unsigned long)record->ra);
   record->magic = 0;
   record->checksum = 0;
   unifrog_perf_cache_flush((const void *)record, sizeof(*record));
}
