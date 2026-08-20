#include <unifrog/media_policy.h>

#include <limits.h>

int unifrog_media_policy_find_slot(
   const struct unifrog_media_readahead_slot *slots, unsigned count,
   int64_t position, int allow_end)
{
   if (!slots || position < 0)
      return -1;
   for (unsigned i = 0; i < count; i++) {
      int64_t end;

      if (slots[i].size == 0 || slots[i].start < 0 ||
          slots[i].size > (size_t)(INT64_MAX - slots[i].start))
         continue;
      end = slots[i].start + (int64_t)slots[i].size;
      if (position >= slots[i].start &&
          (position < end || (allow_end && position == end)))
         return (int)i;
   }
   return -1;
}

unsigned unifrog_media_policy_choose_slot(
   const struct unifrog_media_readahead_slot *slots, unsigned count,
   int *evicted)
{
   unsigned best = 0;
   uint32_t best_used = UINT32_MAX;

   if (evicted)
      *evicted = 0;
   if (!slots || count == 0)
      return 0;
   for (unsigned i = 0; i < count; i++) {
      if (slots[i].size == 0)
         return i;
      if (slots[i].last_used < best_used) {
         best = i;
         best_used = slots[i].last_used;
      }
   }
   if (evicted)
      *evicted = 1;
   return best;
}

uint32_t unifrog_media_policy_touch(
   struct unifrog_media_readahead_slot *slots, unsigned count,
   uint32_t clock, unsigned slot)
{
   if (!slots || slot >= count)
      return clock;
   clock++;
   if (clock == 0) {
      for (unsigned i = 0; i < count; i++)
         slots[i].last_used = 0;
      clock = 1;
   }
   slots[slot].last_used = clock;
   return clock;
}
