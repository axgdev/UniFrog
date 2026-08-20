#ifndef UNIFROG_MEDIA_POLICY_H
#define UNIFROG_MEDIA_POLICY_H

#include <stddef.h>
#include <stdint.h>

struct unifrog_media_readahead_slot {
   size_t size;
   int64_t start;
   uint32_t last_used;
};

int unifrog_media_policy_find_slot(
   const struct unifrog_media_readahead_slot *slots, unsigned count,
   int64_t position, int allow_end);
unsigned unifrog_media_policy_choose_slot(
   const struct unifrog_media_readahead_slot *slots, unsigned count,
   int *evicted);
uint32_t unifrog_media_policy_touch(
   struct unifrog_media_readahead_slot *slots, unsigned count,
   uint32_t clock, unsigned slot);

#endif
