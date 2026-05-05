static char *duplicate_bytes(const char *data, size_t size)
{
   char *copy;

   if (!data)
      return NULL;
   copy = (char *)malloc(size + 1u);
   if (!copy)
      return NULL;
   if (size)
      memcpy(copy, data, size);
   copy[size] = '\0';
   return copy;
}

static void bytecode_file_cache_clear_slot(
   struct js2300_bytecode_file_cache_entry *slot)
{
   if (!slot || !slot->data)
      return;
   if (bytecode_file_cache_bytes >= slot->size)
      bytecode_file_cache_bytes -= slot->size;
   else
      bytecode_file_cache_bytes = 0;
   free(slot->data);
   memset(slot, 0, sizeof(*slot));
}

static struct js2300_bytecode_file_cache_entry *bytecode_file_cache_find(
   const char *path, size_t size, uint64_t hash)
{
   if (!path)
      return NULL;
   for (unsigned i = 0; i < JS2300_BYTECODE_FILE_CACHE_ENTRIES; i++) {
      struct js2300_bytecode_file_cache_entry *slot =
         &bytecode_file_cache[i];

      if (slot->data && slot->size == size && slot->hash == hash &&
          strcmp(slot->path, path) == 0) {
         slot->last_used = ++bytecode_file_cache_counter;
         return slot;
      }
   }
   return NULL;
}

static struct js2300_bytecode_file_cache_entry *bytecode_file_cache_pick_slot(
   const char *path)
{
   struct js2300_bytecode_file_cache_entry *oldest = NULL;

   for (unsigned i = 0; i < JS2300_BYTECODE_FILE_CACHE_ENTRIES; i++) {
      struct js2300_bytecode_file_cache_entry *slot =
         &bytecode_file_cache[i];

      if (slot->data && strcmp(slot->path, path) == 0)
         return slot;
      if (!slot->data)
         return slot;
      if (!oldest || slot->last_used < oldest->last_used)
         oldest = slot;
   }
   return oldest;
}

static void bytecode_file_cache_store(const char *path, const char *data,
                                      size_t size, uint64_t hash)
{
   struct js2300_bytecode_file_cache_entry *slot;
   char *copy;
   size_t path_len;

   if (!path || !data || size == 0 ||
       size > JS2300_BYTECODE_FILE_CACHE_MAX_FILE_BYTES ||
       size > JS2300_BYTECODE_FILE_CACHE_MAX_BYTES)
      return;
   path_len = strlen(path);
   if (path_len == 0 || path_len >= JS2300_MAX_CACHE_PATH)
      return;

   while (bytecode_file_cache_bytes + size >
          JS2300_BYTECODE_FILE_CACHE_MAX_BYTES) {
      struct js2300_bytecode_file_cache_entry *oldest = NULL;

      for (unsigned i = 0; i < JS2300_BYTECODE_FILE_CACHE_ENTRIES; i++) {
         struct js2300_bytecode_file_cache_entry *candidate =
            &bytecode_file_cache[i];

         if (candidate->data &&
             (!oldest || candidate->last_used < oldest->last_used))
            oldest = candidate;
      }
      if (!oldest)
         break;
      bytecode_file_cache_clear_slot(oldest);
   }

   slot = bytecode_file_cache_pick_slot(path);
   if (!slot)
      return;
   bytecode_file_cache_clear_slot(slot);

   copy = duplicate_bytes(data, size);
   if (!copy)
      return;
   memcpy(slot->path, path, path_len + 1u);
   slot->data = copy;
   slot->size = size;
   slot->hash = hash;
   slot->last_used = ++bytecode_file_cache_counter;
   bytecode_file_cache_bytes += size;
}

static char *read_bytecode_file_cached(const char *path, size_t max_bytes,
                                       size_t expected_size,
                                       uint64_t expected_hash,
                                       size_t *out_len,
                                       uint64_t *out_hash,
                                       int *out_cache_hit)
{
   struct js2300_bytecode_file_cache_entry *cached;
   char *buf;
   uint64_t hash;
   size_t len = 0;

   if (out_len)
      *out_len = 0;
   if (out_hash)
      *out_hash = 0;
   if (out_cache_hit)
      *out_cache_hit = 0;

   cached = bytecode_file_cache_find(path, expected_size, expected_hash);
   if (cached) {
      buf = duplicate_bytes(cached->data, cached->size);
      if (buf) {
         if (out_len)
            *out_len = cached->size;
         if (out_hash)
            *out_hash = cached->hash;
         if (out_cache_hit)
            *out_cache_hit = 1;
         return buf;
      }
   }

   buf = read_file(path, max_bytes, &len);
   if (!buf)
      return NULL;
   hash = fnv1a64(buf, len);
   if (len == expected_size && hash == expected_hash)
      bytecode_file_cache_store(path, buf, len, hash);
   if (out_len)
      *out_len = len;
   if (out_hash)
      *out_hash = hash;
   return buf;
}
