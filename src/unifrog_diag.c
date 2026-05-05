#include <unifrog/diag.h>

#include <stddef.h>
#include <string.h>

#include <unifrog/abi.h>
#include <unifrog/log.h>

struct unifrog_diag_heap_stats {
   size_t available;
   size_t largest;
   size_t smallest;
   size_t blocks;
   size_t min_ever;
   size_t allocs;
   size_t frees;
};

extern void vPortGetHeapStats(struct unifrog_diag_heap_stats *stats)
   __attribute__((weak));
extern size_t xPortGetFreeHeapSize(void) __attribute__((weak));
extern size_t xPortGetMinimumEverFreeHeapSize(void) __attribute__((weak));

static void log_region(const char *tag, const char *name,
   const struct unifrog_abi_memory_region *region)
{
   if (!region || region->bytes == 0)
      return;

   unifrog_log("unifrog diag memory tag=%s region=%s cached=0x%08lx phys=0x%08lx bytes=%u flags=0x%08lx\n",
      tag ? tag : "?", name ? name : "?",
      (unsigned long)region->cached_base,
      (unsigned long)region->physical_base,
      (unsigned)region->bytes,
      (unsigned long)region->flags);
}

void unifrog_diag_memory_snapshot(const char *tag)
{
   struct unifrog_diag_heap_stats heap;
   struct unifrog_abi_memory_layout layout;
   struct unifrog_abi_memory_slot slot;
   int layout_ret;
   int slot_ret;

   memset(&heap, 0, sizeof(heap));
   if (vPortGetHeapStats) {
      vPortGetHeapStats(&heap);
   } else {
      if (xPortGetFreeHeapSize)
         heap.available = xPortGetFreeHeapSize();
      if (xPortGetMinimumEverFreeHeapSize)
         heap.min_ever = xPortGetMinimumEverFreeHeapSize();
   }
   unifrog_log("unifrog diag heap tag=%s free=%u largest=%u smallest=%u blocks=%u min_ever=%u allocs=%u frees=%u\n",
      tag ? tag : "?",
      (unsigned)heap.available,
      (unsigned)heap.largest,
      (unsigned)heap.smallest,
      (unsigned)heap.blocks,
      (unsigned)heap.min_ever,
      (unsigned)heap.allocs,
      (unsigned)heap.frees);

   memset(&layout, 0, sizeof(layout));
   layout_ret = unifrog_abi_memory_layout(&layout);
   unifrog_log("unifrog diag memory tag=%s layout_ret=%d regions=%u version=0x%06lx\n",
      tag ? tag : "?", layout_ret, (unsigned)layout.region_count,
      (unsigned long)layout.version);
   if (layout_ret == 0) {
      log_region(tag, "runtime", &layout.runtime);
      log_region(tag, "external", &layout.external);
      log_region(tag, "media", &layout.media);
   }

   memset(&slot, 0, sizeof(slot));
   slot_ret = unifrog_abi_application_memory_slot(&slot);
   unifrog_log("unifrog diag appmem tag=%s ret=%d base=0x%08lx bytes=%u flags=0x%08lx\n",
      tag ? tag : "?", slot_ret, (unsigned long)slot.base,
      (unsigned)slot.bytes, (unsigned long)slot.flags);
}
