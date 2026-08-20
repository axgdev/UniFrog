#include <unifrog/surface_alloc.h>

#include <malloc.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <unifrog/log.h>
#include <unifrog/perf.h>

#define SURFACE_ALLOC_TRACKED 64u
#define SURFACE_ALLOC_MMZ_ID 0

extern void *mmz_memalign(int id, size_t alignment, size_t size)
   __attribute__((weak));
extern void mmz_free(int id, void *ptr) __attribute__((weak));

struct tracked_surface {
   void *ptr;
   size_t size;
};

static struct tracked_surface mmz_surfaces[SURFACE_ALLOC_TRACKED];
static unsigned mmz_alloc_count;
static unsigned heap_alloc_count;
static unsigned mmz_fail_logged;

static int track_mmz(void *ptr, size_t size)
{
   if (!ptr)
      return -1;
   for (unsigned i = 0; i < SURFACE_ALLOC_TRACKED; i++) {
      if (!mmz_surfaces[i].ptr) {
         mmz_surfaces[i].ptr = ptr;
         mmz_surfaces[i].size = size;
         return 0;
      }
   }
   return -1;
}

static int untrack_mmz(const void *ptr)
{
   if (!ptr)
      return 0;
   for (unsigned i = 0; i < SURFACE_ALLOC_TRACKED; i++) {
      if (mmz_surfaces[i].ptr == ptr) {
         mmz_surfaces[i].ptr = NULL;
         mmz_surfaces[i].size = 0;
         return 1;
      }
   }
   return 0;
}

int unifrog_surface_is_mmz(const void *ptr)
{
   if (!ptr)
      return 0;
   for (unsigned i = 0; i < SURFACE_ALLOC_TRACKED; i++) {
      if (mmz_surfaces[i].ptr == ptr)
         return 1;
   }
   return 0;
}

void *unifrog_surface_memalign(size_t alignment, size_t size)
{
   void *ptr = NULL;

   if (alignment < 32u)
      alignment = 32u;
   if (size == 0)
      return NULL;

   if (mmz_memalign) {
      ptr = mmz_memalign(SURFACE_ALLOC_MMZ_ID, alignment, size);
      if (ptr && track_mmz(ptr, size) == 0) {
         mmz_alloc_count++;
         if (mmz_alloc_count <= 4u) {
            unifrog_log("unifrog surface_alloc backend=mmz ptr=%p phys=0x%08lx align=%u size=%u count=%u\n",
               ptr, (unsigned long)unifrog_perf_phys_addr(ptr),
               (unsigned)alignment, (unsigned)size, mmz_alloc_count);
         }
         return ptr;
      }
      if (ptr && mmz_free)
         mmz_free(SURFACE_ALLOC_MMZ_ID, ptr);
      if (!mmz_fail_logged) {
         mmz_fail_logged = 1;
         unifrog_log("unifrog surface_alloc mmz_unavailable align=%u size=%u\n",
            (unsigned)alignment, (unsigned)size);
      }
   }

   ptr = memalign(alignment, size);
   if (ptr) {
      heap_alloc_count++;
      if (heap_alloc_count <= 4u) {
         unifrog_log("unifrog surface_alloc backend=heap ptr=%p phys=0x%08lx align=%u size=%u count=%u\n",
            ptr, (unsigned long)unifrog_perf_phys_addr(ptr),
            (unsigned)alignment, (unsigned)size, heap_alloc_count);
      }
   }
   return ptr;
}

void unifrog_surface_free(void *ptr)
{
   if (!ptr)
      return;
   if (untrack_mmz(ptr)) {
      if (mmz_free)
         mmz_free(SURFACE_ALLOC_MMZ_ID, ptr);
      return;
   }
   free(ptr);
}
