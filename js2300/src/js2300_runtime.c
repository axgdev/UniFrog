#include <js2300/js2300.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "mquickjs.h"

#define JS2300_DEFAULT_HEAP_BYTES (8u * 1024u * 1024u)
#define JS2300_DEFAULT_STACK_BYTES (256u * 1024u)
#define JS2300_DEFAULT_BYTECODE_CACHE_BYTES (2u * 1024u * 1024u)
#define JS2300_BYTECODE_MANIFEST "bytecode-manifest.txt"
#define JS2300_MAX_BYTECODE_ENTRIES 128u
#define JS2300_MAX_BYTECODE_PATH 192u
#define JS2300_MAX_BYTECODE_MANIFEST_BYTES (64u * 1024u)
#define JS2300_MAX_PRELOAD_DEPTH 12u
#define JS2300_BYTECODE_FILE_CACHE_ENTRIES 32u
#define JS2300_BYTECODE_FILE_CACHE_MAX_BYTES (512u * 1024u)
#define JS2300_BYTECODE_FILE_CACHE_MAX_FILE_BYTES (128u * 1024u)
#define JS2300_MAX_CACHE_PATH 256u
#define JS2300_MAX_SCRIPT_BYTES (512u * 1024u)
#define JS2300_MAX_TEXT_FILE_BYTES (2u * 1024u * 1024u)
#define JS2300_MAX_RECTS 128u
#define JS2300_MAX_FS_ENTRIES 1024u
#define JS2300_HEARTBEAT_INPUT_POLL_INTERVAL 4096u

extern const JSSTDLibraryDef js2300_stdlib;

enum js2300_cfunc_id {
   JS2300_CFUNC_LOG = JS_CFUNCTION_USER,
   JS2300_CFUNC_FLUSH_LOG,
   JS2300_CFUNC_NOW,
   JS2300_CFUNC_SLEEP,
   JS2300_CFUNC_GC,
   JS2300_CFUNC_EXIT,
   JS2300_CFUNC_VIDEO_SIZE,
   JS2300_CFUNC_VIDEO_CLEAR,
   JS2300_CFUNC_VIDEO_RECTS,
   JS2300_CFUNC_VIDEO_TEXT,
   JS2300_CFUNC_VIDEO_IMAGE,
   JS2300_CFUNC_VIDEO_PRESENT,
   JS2300_CFUNC_VIDEO_FONT,
   JS2300_CFUNC_INPUT_POLL,
   JS2300_CFUNC_INPUT_MASK,
   JS2300_CFUNC_INPUT_DOWN,
   JS2300_CFUNC_INPUT_PRESSED,
   JS2300_CFUNC_INPUT_REPEATED,
   JS2300_CFUNC_SYSTEM_BATTERY,
   JS2300_CFUNC_SYSTEM_BACKLIGHT,
   JS2300_CFUNC_SYSTEM_AV_OUTPUT,
   JS2300_CFUNC_SYSTEM_ACTION,
   JS2300_CFUNC_FS_LIST,
   JS2300_CFUNC_FS_INDEX,
   JS2300_CFUNC_FS_READ_TEXT,
   JS2300_CFUNC_FS_WRITE_TEXT,
};

struct js2300_binding {
   const char *name;
   int idx;
};

struct js2300_button_state {
   uint32_t mask;
   uint32_t next_repeat_ms;
};

struct js2300_bytecode_entry {
   char path[JS2300_MAX_BYTECODE_PATH];
   size_t source_size;
   size_t bytecode_size;
   uint64_t source_hash;
   uint64_t bytecode_hash;
};

struct js2300_preloaded_bytecode {
   char path[JS2300_MAX_BYTECODE_PATH];
   JSGCRef ref;
   size_t bytecode_size;
   int loaded;
   int used;
};

struct js2300_bytecode_file_cache_entry {
   char path[JS2300_MAX_CACHE_PATH];
   char *data;
   size_t size;
   uint64_t hash;
   uint32_t last_used;
};

struct js2300_runtime {
   struct js2300_config config;
   struct js2300_host host;
   JSContext *ctx;
   void *heap;
   void **bytecode_buffers;
   unsigned bytecode_buffer_count;
   unsigned bytecode_buffer_capacity;
   struct js2300_bytecode_entry bytecode_entries[JS2300_MAX_BYTECODE_ENTRIES];
   unsigned bytecode_entry_count;
   int bytecode_manifest_loaded;
   struct js2300_preloaded_bytecode preloaded_bytecode[JS2300_MAX_BYTECODE_ENTRIES];
   unsigned preloaded_bytecode_count;
   int api_attached;
   uint32_t input_now;
   uint32_t input_prev;
   uint32_t native_call_count;
   uint32_t gc_count;
   uint32_t input_poll_count;
   int exit_requested;
   struct js2300_button_state repeat[16];
};

static struct js2300_runtime *active_runtime;
static struct js2300_bytecode_file_cache_entry
   bytecode_file_cache[JS2300_BYTECODE_FILE_CACHE_ENTRIES];
static uint32_t bytecode_file_cache_counter;
static size_t bytecode_file_cache_bytes;

const char *js2300_version_string(void)
{
   return "0.7.0";
}

int js2300_config_init(struct js2300_config *config)
{
   if (!config)
      return -1;

   memset(config, 0, sizeof(*config));
   config->size = sizeof(*config);
   config->api_version = JS2300_API_VERSION;
   config->app_root = "/media/mmcblk0/unifrog";
   config->entry_script = "main.js";
   config->heap_bytes = JS2300_DEFAULT_HEAP_BYTES;
   config->stack_bytes = JS2300_DEFAULT_STACK_BYTES;
   config->bytecode_cache_bytes = JS2300_DEFAULT_BYTECODE_CACHE_BYTES;
   return 0;
}

static void host_log(struct js2300_runtime *runtime, const char *message)
{
   if (runtime && runtime->host.log)
      runtime->host.log(runtime->host.opaque, message ? message : "");
}

static uint32_t host_millis(struct js2300_runtime *runtime)
{
   if (runtime && runtime->host.millis)
      return runtime->host.millis(runtime->host.opaque);
   return 0;
}

static void js2300_runtime_gc(struct js2300_runtime *runtime, const char *reason)
{
   char line[128];

   if (!runtime || !runtime->ctx)
      return;

   JS_GC(runtime->ctx);
   runtime->gc_count++;
   if (reason) {
      snprintf(line, sizeof(line), "js2300 gc reason=%s count=%lu calls=%lu",
               reason, (unsigned long)runtime->gc_count,
               (unsigned long)runtime->native_call_count);
      host_log(runtime, line);
   }
}

static void js2300_note_native_call(struct js2300_runtime *runtime)
{
   if (!runtime)
      return;

   runtime->native_call_count++;
}

static char *build_script_path(const struct js2300_config *config,
                               const char *script_path);
static int js_attach_api(JSContext *ctx);
static int js2300_ensure_api(struct js2300_runtime *runtime);
static void log_exception_message(struct js2300_runtime *runtime,
                                  const char *prefix);
static void clear_preloaded_bytecode(struct js2300_runtime *runtime);

static char *build_entry_path(const struct js2300_config *config)
{
   if (!config)
      return NULL;
   return build_script_path(config, config->entry_script);
}

static char *build_script_path(const struct js2300_config *config,
                               const char *script_path)
{
   size_t root_len;
   size_t script_len;
   size_t slash_len;
   char *path;

   if (!config || !script_path || !script_path[0])
      return NULL;
   if (script_path[0] == '/') {
      script_len = strlen(script_path);
      path = (char *)malloc(script_len + 1);
      if (!path)
         return NULL;
      memcpy(path, script_path, script_len + 1);
      return path;
   }
   if (!config->app_root)
      return NULL;

   root_len = strlen(config->app_root);
   script_len = strlen(script_path);
   slash_len = root_len > 0 && config->app_root[root_len - 1] != '/' ? 1u : 0u;
   path = (char *)malloc(root_len + slash_len + script_len + 1);
   if (!path)
      return NULL;

   memcpy(path, config->app_root, root_len);
   if (slash_len)
      path[root_len++] = '/';
   memcpy(path + root_len, script_path, script_len + 1);
   return path;
}

static char *read_file(const char *path, size_t max_bytes, size_t *out_len)
{
   FILE *fp;
   long size;
   char *buf;

   if (out_len)
      *out_len = 0;
   fp = fopen(path, "rb");
   if (!fp)
      return NULL;
   if (fseek(fp, 0, SEEK_END) != 0) {
      fclose(fp);
      return NULL;
   }
   size = ftell(fp);
   if (size < 0 || (size_t)size > max_bytes) {
      fclose(fp);
      return NULL;
   }
   rewind(fp);
   buf = (char *)malloc((size_t)size + 1);
   if (!buf) {
      fclose(fp);
      return NULL;
   }
   if (size > 0 && fread(buf, 1, (size_t)size, fp) != (size_t)size) {
      free(buf);
      fclose(fp);
      return NULL;
   }
   fclose(fp);
   buf[size] = 0;
   if (out_len)
      *out_len = (size_t)size;
   return buf;
}

static uint64_t fnv1a64(const void *data, size_t len)
{
   const uint8_t *bytes = (const uint8_t *)data;
   uint64_t hash = 1469598103934665603ull;

   for (size_t i = 0; i < len; i++) {
      hash ^= (uint64_t)bytes[i];
      hash *= 1099511628211ull;
   }
   return hash;
}

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

static char *build_bytecode_path(const char *script_path)
{
   size_t len;
   char *path;

   if (!script_path || !script_path[0])
      return NULL;

   len = strlen(script_path);
   path = (char *)malloc(len + 6u);
   if (!path)
      return NULL;
   memcpy(path, script_path, len);
   memcpy(path + len, ".mqbc", 6u);
   return path;
}

static const char *relative_script_path(const struct js2300_config *config,
                                        const char *path)
{
   size_t root_len;
   const char *rel;

   if (!config || !path)
      return NULL;
   if (!config->app_root || path[0] != '/')
      return path;

   root_len = strlen(config->app_root);
   if (root_len == 0 || strncmp(path, config->app_root, root_len) != 0)
      return path;
   if (path[root_len] && path[root_len] != '/')
      return path;

   rel = path + root_len;
   if (*rel == '/')
      rel++;
   return rel;
}

static int parse_u64_field(const char *text, unsigned base, uint64_t *out)
{
   char *end;
   unsigned long long value;

   if (!text || !text[0] || !out)
      return -1;
   errno = 0;
   value = strtoull(text, &end, (int)base);
   if (errno || end == text || *end)
      return -1;
   *out = (uint64_t)value;
   return 0;
}

static int parse_size_field(const char *text, size_t *out)
{
   uint64_t value;

   if (parse_u64_field(text, 10, &value) != 0)
      return -1;
   if ((uint64_t)(size_t)value != value)
      return -1;
   *out = (size_t)value;
   return 0;
}

static int parse_manifest_line(char *line, struct js2300_bytecode_entry *entry)
{
   char *fields[6];
   char *cursor;
   size_t path_len;

   if (!line || !entry)
      return -1;

   for (unsigned i = 0; i < 6; i++) {
      fields[i] = NULL;
   }

   cursor = line;
   for (unsigned i = 0; i < 6; i++) {
      char *sep;

      fields[i] = cursor;
      sep = strchr(cursor, '|');
      if (!sep)
         break;
      *sep = '\0';
      cursor = sep + 1;
   }

   if (!fields[5] || strcmp(fields[0], "file") != 0)
      return -1;

   path_len = strlen(fields[1]);
   if (path_len == 0 || path_len >= sizeof(entry->path))
      return -1;

   memset(entry, 0, sizeof(*entry));
   memcpy(entry->path, fields[1], path_len + 1u);
   if (parse_size_field(fields[2], &entry->source_size) != 0 ||
       parse_u64_field(fields[3], 16, &entry->source_hash) != 0 ||
       parse_size_field(fields[4], &entry->bytecode_size) != 0 ||
       parse_u64_field(fields[5], 16, &entry->bytecode_hash) != 0)
      return -1;
   return 0;
}

static void load_bytecode_manifest(struct js2300_runtime *runtime)
{
   char *path;
   char *manifest;
   char *cursor;
   size_t manifest_len;
   char line_msg[160];

   if (!runtime || runtime->bytecode_manifest_loaded)
      return;
   runtime->bytecode_manifest_loaded = 1;

   path = build_script_path(&runtime->config, JS2300_BYTECODE_MANIFEST);
   if (!path)
      return;

   manifest = read_file(path, JS2300_MAX_BYTECODE_MANIFEST_BYTES,
      &manifest_len);
   if (!manifest) {
      snprintf(line_msg, sizeof(line_msg),
         "js2300 bytecode manifest missing path=%s", path);
      host_log(runtime, line_msg);
      free(path);
      return;
   }

   cursor = manifest;
   while (cursor && *cursor &&
          runtime->bytecode_entry_count < JS2300_MAX_BYTECODE_ENTRIES) {
      char *next = strchr(cursor, '\n');
      char *line = cursor;
      size_t len;
      struct js2300_bytecode_entry entry;

      if (next) {
         *next = '\0';
         cursor = next + 1;
      } else {
         cursor = NULL;
      }

      len = strlen(line);
      if (len && line[len - 1] == '\r')
         line[len - 1] = '\0';
      if (parse_manifest_line(line, &entry) == 0)
         runtime->bytecode_entries[runtime->bytecode_entry_count++] = entry;
   }

   snprintf(line_msg, sizeof(line_msg),
      "js2300 bytecode manifest path=%s entries=%lu bytes=%lu", path,
      (unsigned long)runtime->bytecode_entry_count,
      (unsigned long)manifest_len);
   host_log(runtime, line_msg);
   free(manifest);
   free(path);
}

static const struct js2300_bytecode_entry *find_bytecode_entry(
   struct js2300_runtime *runtime, const char *script_path)
{
   const char *rel;

   if (!runtime || !script_path)
      return NULL;

   load_bytecode_manifest(runtime);
   rel = relative_script_path(&runtime->config, script_path);
   if (!rel || !rel[0] || rel[0] == '/')
      return NULL;

   for (unsigned i = 0; i < runtime->bytecode_entry_count; i++) {
      if (strcmp(runtime->bytecode_entries[i].path, rel) == 0)
         return &runtime->bytecode_entries[i];
   }
   return NULL;
}

static int remember_bytecode_buffer(struct js2300_runtime *runtime, void *buf)
{
   void **new_buffers;
   unsigned new_capacity;

   if (!runtime || !buf)
      return -1;

   if (runtime->bytecode_buffer_count >= runtime->bytecode_buffer_capacity) {
      new_capacity = runtime->bytecode_buffer_capacity ?
         runtime->bytecode_buffer_capacity * 2u : 8u;
      new_buffers = (void **)realloc(runtime->bytecode_buffers,
         (size_t)new_capacity * sizeof(runtime->bytecode_buffers[0]));
      if (!new_buffers)
         return -1;
      runtime->bytecode_buffers = new_buffers;
      runtime->bytecode_buffer_capacity = new_capacity;
   }

   runtime->bytecode_buffers[runtime->bytecode_buffer_count++] = buf;
   return 0;
}

static void free_bytecode_buffers(struct js2300_runtime *runtime)
{
   if (!runtime)
      return;

   for (unsigned i = 0; i < runtime->bytecode_buffer_count; i++)
      free(runtime->bytecode_buffers[i]);
   free(runtime->bytecode_buffers);
   runtime->bytecode_buffers = NULL;
   runtime->bytecode_buffer_count = 0;
   runtime->bytecode_buffer_capacity = 0;
}

static void js2300_close_context(struct js2300_runtime *runtime)
{
   if (!runtime)
      return;

   if (active_runtime == runtime)
      active_runtime = NULL;
   if (runtime->ctx) {
      clear_preloaded_bytecode(runtime);
      JS_FreeContext(runtime->ctx);
      runtime->ctx = NULL;
   }
   free_bytecode_buffers(runtime);
   free(runtime->heap);
   runtime->heap = NULL;
}

static void log_eval_result(struct js2300_runtime *runtime, const char *phase,
                            const char *kind, const char *path,
                            size_t bytes, uint32_t start_ms)
{
   char line[224];
   uint32_t end_ms = host_millis(runtime);

   snprintf(line, sizeof(line),
      "js2300 eval phase=%s kind=%s ms=%lu bytes=%lu path=%s",
      phase ? phase : "script", kind ? kind : "?",
      (unsigned long)(end_ms - start_ms), (unsigned long)bytes,
      path ? path : "?");
   host_log(runtime, line);
}

static void log_bytecode_skip(struct js2300_runtime *runtime,
                              const char *path, const char *reason)
{
   char line[224];

   snprintf(line, sizeof(line), "js2300 bytecode skip reason=%s path=%s",
      reason ? reason : "unknown", path ? path : "?");
   host_log(runtime, line);
}

static int is_script_space(char ch)
{
   return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
}

static struct js2300_preloaded_bytecode *find_preloaded_bytecode(
   struct js2300_runtime *runtime, const char *script_path)
{
   const char *rel;

   if (!runtime || !script_path)
      return NULL;

   rel = relative_script_path(&runtime->config, script_path);
   if (!rel || !rel[0] || rel[0] == '/')
      return NULL;

   for (unsigned i = 0; i < runtime->preloaded_bytecode_count; i++) {
      if (strcmp(runtime->preloaded_bytecode[i].path, rel) == 0)
         return &runtime->preloaded_bytecode[i];
   }
   return NULL;
}

static int preload_bytecode_path(struct js2300_runtime *runtime,
                                 const char *path,
                                 unsigned depth);

static void preload_load_literals(struct js2300_runtime *runtime,
                                  const char *source,
                                  size_t source_len,
                                  unsigned depth)
{
   size_t i = 0;

   if (!runtime || !source || depth >= JS2300_MAX_PRELOAD_DEPTH)
      return;

   while (i + 5u < source_len) {
      char quote;
      size_t start;
      size_t end;
      char script[JS2300_MAX_BYTECODE_PATH];
      char *path;

      if (source[i] != 'l' || source[i + 1u] != 'o' ||
          source[i + 2u] != 'a' || source[i + 3u] != 'd' ||
          source[i + 4u] != '(') {
         i++;
         continue;
      }

      i += 5u;
      while (i < source_len && is_script_space(source[i]))
         i++;
      if (i >= source_len || (source[i] != '"' && source[i] != '\''))
         continue;

      quote = source[i++];
      start = i;
      while (i < source_len && source[i] != quote)
         i++;
      end = i;
      if (i >= source_len || end <= start ||
          end - start >= sizeof(script))
         continue;

      memcpy(script, source + start, end - start);
      script[end - start] = '\0';
      path = build_script_path(&runtime->config, script);
      if (path) {
         (void)preload_bytecode_path(runtime, path, depth + 1u);
         free(path);
      }
   }
}

static int preload_bytecode_path(struct js2300_runtime *runtime,
                                 const char *path,
                                 unsigned depth)
{
   const struct js2300_bytecode_entry *entry;
   struct js2300_preloaded_bytecode *slot;
   const char *rel;
   char *bytecode_path;
   char *source = NULL;
   char *bytecode = NULL;
   size_t source_len = 0;
   size_t bytecode_len = 0;
   uint64_t source_hash;
   uint64_t bytecode_hash;
   int bytecode_cache_hit = 0;
   JSValue ret;
   JSValue *root;
   char line[224];
   uint32_t start_ms = host_millis(runtime);

   if (!runtime || !runtime->ctx || !path ||
       depth > JS2300_MAX_PRELOAD_DEPTH)
      return -1;
   if (runtime->api_attached)
      return -1;
   if (find_preloaded_bytecode(runtime, path))
      return 0;

   entry = find_bytecode_entry(runtime, path);
   if (!entry)
      return -1;

   rel = relative_script_path(&runtime->config, path);
   if (!rel || !rel[0] || rel[0] == '/' ||
       strlen(rel) >= JS2300_MAX_BYTECODE_PATH ||
       runtime->preloaded_bytecode_count >= JS2300_MAX_BYTECODE_ENTRIES)
      return -1;

   bytecode_path = build_bytecode_path(path);
   if (!bytecode_path)
      return -1;

   bytecode = read_bytecode_file_cached(bytecode_path,
      runtime->config.bytecode_cache_bytes ?
      runtime->config.bytecode_cache_bytes : JS2300_DEFAULT_BYTECODE_CACHE_BYTES,
      entry->bytecode_size, entry->bytecode_hash, &bytecode_len,
      &bytecode_hash, &bytecode_cache_hit);
   if (!bytecode) {
      log_bytecode_skip(runtime, path, "read_bytecode");
      free(bytecode_path);
      return -1;
   }

   if (bytecode_len != entry->bytecode_size ||
       bytecode_hash != entry->bytecode_hash) {
      log_bytecode_skip(runtime, path, "bytecode_changed");
      free(bytecode);
      free(bytecode_path);
      return -1;
   }

   source = read_file(path, JS2300_MAX_SCRIPT_BYTES, &source_len);
   if (!source) {
      log_bytecode_skip(runtime, path, "read_source");
      free(bytecode);
      free(bytecode_path);
      return -1;
   }

   source_hash = fnv1a64(source, source_len);
   if (source_len != entry->source_size || source_hash != entry->source_hash) {
      log_bytecode_skip(runtime, path, "source_changed");
      preload_load_literals(runtime, source, source_len, depth);
      free(source);
      free(bytecode);
      free(bytecode_path);
      return -1;
   }

   if (!JS_IsBytecode((const uint8_t *)bytecode, bytecode_len)) {
      log_bytecode_skip(runtime, path, "invalid_bytecode");
      preload_load_literals(runtime, source, source_len, depth);
      free(source);
      free(bytecode);
      free(bytecode_path);
      return -1;
   }

   if (JS_RelocateBytecode(runtime->ctx, (uint8_t *)bytecode,
       (uint32_t)bytecode_len) != 0) {
      log_bytecode_skip(runtime, path, "relocate");
      preload_load_literals(runtime, source, source_len, depth);
      free(source);
      free(bytecode);
      free(bytecode_path);
      return -1;
   }

   ret = JS_LoadBytecode(runtime->ctx, (const uint8_t *)bytecode);
   if (JS_IsException(ret)) {
      log_exception_message(runtime, "js2300 bytecode load_exception");
      preload_load_literals(runtime, source, source_len, depth);
      free(source);
      free(bytecode);
      free(bytecode_path);
      return -1;
   }

   if (remember_bytecode_buffer(runtime, bytecode) != 0) {
      free(source);
      free(bytecode);
      free(bytecode_path);
      return -1;
   }

   slot = &runtime->preloaded_bytecode[runtime->preloaded_bytecode_count++];
   memset(slot, 0, sizeof(*slot));
   memcpy(slot->path, rel, strlen(rel) + 1u);
   slot->bytecode_size = bytecode_len;
   slot->loaded = 1;
   root = JS_AddGCRef(runtime->ctx, &slot->ref);
   *root = ret;

   snprintf(line, sizeof(line),
      "js2300 bytecode preload ms=%lu bytes=%lu cache=%d path=%s",
      (unsigned long)(host_millis(runtime) - start_ms),
      (unsigned long)bytecode_len, bytecode_cache_hit, path);
   host_log(runtime, line);

   preload_load_literals(runtime, source, source_len, depth);
   free(source);
   free(bytecode_path);
   return 0;
}

static void clear_preloaded_bytecode(struct js2300_runtime *runtime)
{
   if (!runtime || !runtime->ctx)
      return;

   for (unsigned i = 0; i < runtime->preloaded_bytecode_count; i++) {
      if (runtime->preloaded_bytecode[i].loaded) {
         JS_DeleteGCRef(runtime->ctx, &runtime->preloaded_bytecode[i].ref);
         runtime->preloaded_bytecode[i].loaded = 0;
      }
   }
   runtime->preloaded_bytecode_count = 0;
}

static void log_exception_message(struct js2300_runtime *runtime,
                                  const char *prefix)
{
   JSCStringBuf buf;
   JSValue exc;
   const char *message;
   char line[224];

   if (!runtime || !runtime->ctx)
      return;

   exc = JS_GetException(runtime->ctx);
   message = JS_ToCString(runtime->ctx, exc, &buf);
   snprintf(line, sizeof(line), "%s %s", prefix ? prefix : "js2300 exception",
            message ? message : "unknown");
   host_log(runtime, line);
}

static JSValue eval_script_source(struct js2300_runtime *runtime,
                                  const char *path,
                                  char *source,
                                  size_t source_len,
                                  const char *phase,
                                  uint32_t start_ms)
{
   JSValue ret;

   if (!source) {
      source = read_file(path, JS2300_MAX_SCRIPT_BYTES, &source_len);
      if (!source) {
         char msg[192];
         snprintf(msg, sizeof(msg), "js2300 read failed path=%s errno=%d",
                  path ? path : "?", errno);
         host_log(runtime, msg);
         return JS_ThrowInternalError(runtime->ctx, "script read failed");
      }
   }

   if (js2300_ensure_api(runtime) != 0) {
      free(source);
      return JS_ThrowInternalError(runtime->ctx, "api attach failed");
   }

   ret = JS_Eval(runtime->ctx, source, source_len, path, 0);
   log_eval_result(runtime, phase, "source", path, source_len, start_ms);
   free(source);
   return ret;
}

static JSValue eval_script_bytecode(struct js2300_runtime *runtime,
                                    const char *path,
                                    const char *phase,
                                    char **fallback_source,
                                    size_t *fallback_source_len)
{
   struct js2300_preloaded_bytecode *slot;
   JSValue ret;
   uint32_t start_ms;

   if (fallback_source)
      *fallback_source = NULL;
   if (fallback_source_len)
      *fallback_source_len = 0;

   slot = find_preloaded_bytecode(runtime, path);
   if (!slot) {
      log_bytecode_skip(runtime, path, "not_preloaded");
      return JS_UNINITIALIZED;
   }
   if (!slot->loaded || slot->used) {
      log_bytecode_skip(runtime, path, "already_used");
      return JS_UNINITIALIZED;
   }

   if (js2300_ensure_api(runtime) != 0) {
      return JS_ThrowInternalError(runtime->ctx, "api attach failed");
   }

   ret = slot->ref.val;
   JS_DeleteGCRef(runtime->ctx, &slot->ref);
   slot->ref.val = JS_UNDEFINED;
   slot->loaded = 0;
   slot->used = 1;
   start_ms = host_millis(runtime);
   ret = JS_Run(runtime->ctx, ret);
   log_eval_result(runtime, phase, "bytecode", path, slot->bytecode_size,
      start_ms);
   return ret;
}

static JSValue eval_script_file(struct js2300_runtime *runtime,
                                const char *path,
                                const char *phase)
{
   char *source = NULL;
   size_t source_len = 0;
   uint32_t source_start_ms;
   JSValue ret;

   ret = eval_script_bytecode(runtime, path, phase, &source, &source_len);
   if (!JS_IsUninitialized(ret))
      return ret;

   source_start_ms = host_millis(runtime);
   return eval_script_source(runtime, path, source, source_len, phase,
      source_start_ms);
}

static uint32_t button_mask_for_name(const char *name)
{
   static const struct {
      const char *name;
      uint32_t mask;
   } buttons[] = {
      { "UP", 1u << 0 },
      { "DOWN", 1u << 1 },
      { "LEFT", 1u << 2 },
      { "RIGHT", 1u << 3 },
      { "A", 1u << 4 },
      { "B", 1u << 5 },
      { "X", 1u << 6 },
      { "Y", 1u << 7 },
      { "L", 1u << 8 },
      { "R", 1u << 9 },
      { "SELECT", 1u << 10 },
      { "START", 1u << 11 },
   };

   if (!name)
      return 0;
   for (unsigned i = 0; i < sizeof(buttons) / sizeof(buttons[0]); i++) {
      if (strcmp(name, buttons[i].name) == 0)
         return buttons[i].mask;
   }
   return 0;
}

static int button_index(uint32_t mask)
{
   for (int i = 0; i < 16; i++) {
      if (mask == (1u << i))
         return i;
   }
   return -1;
}

static int js_set_cfunc(JSContext *ctx, JSValue obj, const char *name, int idx)
{
   JSValue fn = JS_NewCFunctionParams(ctx, idx, JS_NULL);
   if (JS_IsException(fn))
      return 0;
   return !JS_IsException(JS_SetPropertyStr(ctx, obj, name, fn));
}

static int js_attach_object(JSContext *ctx, JSValue parent, const char *name,
                            const struct js2300_binding *bindings,
                            unsigned binding_count)
{
   JSGCRef obj_ref;
   JSValue *obj = JS_PushGCRef(ctx, &obj_ref);

   *obj = JS_NewObject(ctx);
   if (JS_IsException(*obj)) {
      JS_PopGCRef(ctx, &obj_ref);
      return 0;
   }

   for (unsigned i = 0; i < binding_count; i++) {
      if (!js_set_cfunc(ctx, *obj, bindings[i].name, bindings[i].idx)) {
         JS_PopGCRef(ctx, &obj_ref);
         return 0;
      }
   }

   if (JS_IsException(JS_SetPropertyStr(ctx, parent, name, *obj))) {
      JS_PopGCRef(ctx, &obj_ref);
      return 0;
   }

   JS_PopGCRef(ctx, &obj_ref);
   return 1;
}

static int js_attach_api(JSContext *ctx)
{
   static const struct js2300_binding root_bindings[] = {
      { "log", JS2300_CFUNC_LOG },
      { "flushLog", JS2300_CFUNC_FLUSH_LOG },
      { "now", JS2300_CFUNC_NOW },
      { "sleep", JS2300_CFUNC_SLEEP },
      { "gc", JS2300_CFUNC_GC },
      { "exit", JS2300_CFUNC_EXIT },
   };
   static const struct js2300_binding video_bindings[] = {
      { "size", JS2300_CFUNC_VIDEO_SIZE },
      { "clear", JS2300_CFUNC_VIDEO_CLEAR },
      { "rects", JS2300_CFUNC_VIDEO_RECTS },
      { "text", JS2300_CFUNC_VIDEO_TEXT },
      { "image", JS2300_CFUNC_VIDEO_IMAGE },
      { "present", JS2300_CFUNC_VIDEO_PRESENT },
      { "font", JS2300_CFUNC_VIDEO_FONT },
   };
   static const struct js2300_binding input_bindings[] = {
      { "poll", JS2300_CFUNC_INPUT_POLL },
      { "mask", JS2300_CFUNC_INPUT_MASK },
      { "down", JS2300_CFUNC_INPUT_DOWN },
      { "pressed", JS2300_CFUNC_INPUT_PRESSED },
      { "repeated", JS2300_CFUNC_INPUT_REPEATED },
   };
   static const struct js2300_binding system_bindings[] = {
      { "battery", JS2300_CFUNC_SYSTEM_BATTERY },
      { "backlight", JS2300_CFUNC_SYSTEM_BACKLIGHT },
      { "avOutput", JS2300_CFUNC_SYSTEM_AV_OUTPUT },
      { "action", JS2300_CFUNC_SYSTEM_ACTION },
   };
   static const struct js2300_binding fs_bindings[] = {
      { "list", JS2300_CFUNC_FS_LIST },
      { "index", JS2300_CFUNC_FS_INDEX },
      { "readText", JS2300_CFUNC_FS_READ_TEXT },
      { "writeText", JS2300_CFUNC_FS_WRITE_TEXT },
   };
   JSGCRef global_ref;
   JSGCRef root_ref;
   JSValue *global = JS_PushGCRef(ctx, &global_ref);
   JSValue *root = JS_PushGCRef(ctx, &root_ref);

   *global = JS_GetGlobalObject(ctx);
   *root = JS_NewObject(ctx);
   if (JS_IsException(*root))
      goto fail;

   for (unsigned i = 0; i < sizeof(root_bindings) / sizeof(root_bindings[0]); i++) {
      if (!js_set_cfunc(ctx, *root, root_bindings[i].name, root_bindings[i].idx))
         goto fail;
   }
   if (!js_attach_object(ctx, *root, "video", video_bindings,
                         sizeof(video_bindings) / sizeof(video_bindings[0])) ||
       !js_attach_object(ctx, *root, "input", input_bindings,
                         sizeof(input_bindings) / sizeof(input_bindings[0])) ||
       !js_attach_object(ctx, *root, "system", system_bindings,
                         sizeof(system_bindings) / sizeof(system_bindings[0])) ||
       !js_attach_object(ctx, *root, "fs", fs_bindings,
                         sizeof(fs_bindings) / sizeof(fs_bindings[0])))
      goto fail;

   if (JS_IsException(JS_SetPropertyStr(ctx, *global, "JS2300", *root)))
      goto fail;

   JS_PopGCRef(ctx, &root_ref);
   JS_PopGCRef(ctx, &global_ref);
   return 0;

fail:
   JS_PopGCRef(ctx, &root_ref);
   JS_PopGCRef(ctx, &global_ref);
   return -1;
}

static int js2300_ensure_api(struct js2300_runtime *runtime)
{
   if (!runtime || !runtime->ctx)
      return -1;
   if (runtime->api_attached)
      return 0;
   if (js_attach_api(runtime->ctx) != 0) {
      host_log(runtime, "js2300 attach api failed");
      return -1;
   }
   runtime->api_attached = 1;
   return 0;
}

int js2300_runtime_create(const struct js2300_config *config,
                          const struct js2300_host *host,
                          struct js2300_runtime **out_runtime)
{
   struct js2300_runtime *runtime;

   if (!config || !out_runtime || config->size < sizeof(*config) ||
       !host || host->size < sizeof(*host))
      return -1;

   runtime = (struct js2300_runtime *)calloc(1, sizeof(*runtime));
   if (!runtime)
      return -1;

   runtime->config = *config;
   runtime->host = *host;
   *out_runtime = runtime;
   return 0;
}

int js2300_runtime_run(struct js2300_runtime *runtime)
{
   char *path;
   JSValue ret;
   uint32_t run_start_ms;
   uint32_t phase_start_ms;
   uint32_t phase_done_ms;
   char line[192];

   if (!runtime)
      return -1;

   run_start_ms = host_millis(runtime);
   snprintf(line, sizeof(line),
      "js2300 runtime phase=run_start heap=%lu stack=%lu bytecode_cache=%lu root=%s entry=%s",
      (unsigned long)runtime->config.heap_bytes,
      (unsigned long)runtime->config.stack_bytes,
      (unsigned long)runtime->config.bytecode_cache_bytes,
      runtime->config.app_root ? runtime->config.app_root : "",
      runtime->config.entry_script ? runtime->config.entry_script : "");
   host_log(runtime, line);

   phase_start_ms = host_millis(runtime);
   path = build_entry_path(&runtime->config);
   if (!path)
      return -1;
   phase_done_ms = host_millis(runtime);
   snprintf(line, sizeof(line), "js2300 runtime phase=entry_path ms=%lu path=%s",
      (unsigned long)(phase_done_ms - phase_start_ms), path);
   host_log(runtime, line);

   phase_start_ms = host_millis(runtime);
   runtime->heap = calloc(1, runtime->config.heap_bytes);
   if (!runtime->heap) {
      free(path);
      return -1;
   }
   phase_done_ms = host_millis(runtime);
   snprintf(line, sizeof(line),
      "js2300 runtime phase=heap_alloc ms=%lu ptr=0x%08lx bytes=%lu",
      (unsigned long)(phase_done_ms - phase_start_ms),
      (unsigned long)(uintptr_t)runtime->heap,
      (unsigned long)runtime->config.heap_bytes);
   host_log(runtime, line);

   phase_start_ms = host_millis(runtime);
   runtime->ctx = JS_NewContext(runtime->heap, runtime->config.heap_bytes,
                                &js2300_stdlib);
   if (!runtime->ctx) {
      js2300_close_context(runtime);
      free(path);
      return -1;
   }
   phase_done_ms = host_millis(runtime);
   snprintf(line, sizeof(line),
      "js2300 runtime phase=context_create ms=%lu ctx=0x%08lx",
      (unsigned long)(phase_done_ms - phase_start_ms),
      (unsigned long)(uintptr_t)runtime->ctx);
   host_log(runtime, line);

   active_runtime = runtime;
   host_log(runtime, "js2300 eval start");
   phase_start_ms = host_millis(runtime);
   (void)preload_bytecode_path(runtime, path, 0);
   phase_done_ms = host_millis(runtime);
   snprintf(line, sizeof(line),
      "js2300 runtime phase=bytecode_preload ms=%lu entries=%lu buffers=%lu bytes=%lu",
      (unsigned long)(phase_done_ms - phase_start_ms),
      (unsigned long)runtime->preloaded_bytecode_count,
      (unsigned long)runtime->bytecode_buffer_count,
      (unsigned long)runtime->config.bytecode_cache_bytes);
   host_log(runtime, line);
   phase_start_ms = host_millis(runtime);
   ret = eval_script_file(runtime, path, "entry");
   phase_done_ms = host_millis(runtime);
   snprintf(line, sizeof(line),
      "js2300 runtime phase=entry_eval ms=%lu exception=%d",
      (unsigned long)(phase_done_ms - phase_start_ms),
      JS_IsException(ret) ? 1 : 0);
   host_log(runtime, line);
   if (JS_IsException(ret)) {
      JSCStringBuf buf;
      JSValue exc = JS_GetException(runtime->ctx);
      const char *message = JS_ToCString(runtime->ctx, exc, &buf);
      char line[224];
      snprintf(line, sizeof(line), "js2300 exception %s", message ? message : "unknown");
      host_log(runtime, line);
      js2300_close_context(runtime);
      free(path);
      return -1;
   }

   js2300_runtime_gc(runtime, "eval_done");
   snprintf(line, sizeof(line),
      "js2300 eval done total_ms=%lu native_calls=%lu gc=%lu input_polls=%lu",
      (unsigned long)(host_millis(runtime) - run_start_ms),
      (unsigned long)runtime->native_call_count,
      (unsigned long)runtime->gc_count,
      (unsigned long)runtime->input_poll_count);
   host_log(runtime, line);
   js2300_close_context(runtime);
   free(path);
   return 0;
}

void js2300_runtime_destroy(struct js2300_runtime *runtime)
{
   js2300_close_context(runtime);
   free(runtime);
}

static JSValue js2300_log(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv,
                          JSValue params)
{
   char line[256];
   size_t used = 0;
   (void)ctx;
   (void)this_val;
   (void)params;

   js2300_note_native_call(active_runtime);
   line[0] = 0;
   for (int i = 0; i < argc; i++) {
      JSCStringBuf buf;
      const char *part = JS_ToCString(ctx, argv[i], &buf);
      if (!part)
         part = "";
      if (i && used < sizeof(line) - 1)
         line[used++] = ' ';
      while (*part && used < sizeof(line) - 1)
         line[used++] = *part++;
      line[used] = 0;
   }
   host_log(active_runtime, line);
   return JS_UNDEFINED;
}

static JSValue js2300_flush_log(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv,
                                JSValue params)
{
   int ret = 0;
   (void)ctx;
   (void)this_val;
   (void)argc;
   (void)argv;
   (void)params;
   if (active_runtime && active_runtime->host.flush_log)
      ret = active_runtime->host.flush_log(active_runtime->host.opaque);
   js2300_note_native_call(active_runtime);
   return JS_NewInt32(ctx, ret);
}

static JSValue js2300_now(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv,
                          JSValue params)
{
   (void)this_val;
   (void)argc;
   (void)argv;
   (void)params;
   js2300_note_native_call(active_runtime);
   return JS_NewUint32(ctx, host_millis(active_runtime));
}

static JSValue js2300_sleep(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv,
                            JSValue params)
{
   int ms = 0;
   (void)this_val;
   (void)params;

   if (argc > 0 && JS_ToInt32(ctx, &ms, argv[0]))
      return JS_EXCEPTION;
   if (ms < 0)
      ms = 0;
   if (ms > 1000)
      ms = 1000;

   if (active_runtime && active_runtime->host.sleep_ms)
      active_runtime->host.sleep_ms(active_runtime->host.opaque, (uint32_t)ms);
   else if (ms > 0)
      usleep((useconds_t)ms * 1000u);

   js2300_note_native_call(active_runtime);
   return JS_UNDEFINED;
}

static JSValue js2300_gc(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv,
                         JSValue params)
{
   (void)ctx;
   (void)this_val;
   (void)argc;
   (void)argv;
   (void)params;
   js2300_runtime_gc(active_runtime, "script");
   return JS_UNDEFINED;
}

static JSValue js2300_exit(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv,
                           JSValue params)
{
   const char *reason = "exit";
   JSCStringBuf buf;
   (void)this_val;
   (void)params;

   if (argc > 0) {
      const char *arg = JS_ToCString(ctx, argv[0], &buf);
      if (arg)
         reason = arg;
   }
   if (active_runtime) {
      js2300_note_native_call(active_runtime);
      active_runtime->exit_requested = 1;
      if (active_runtime->host.exit)
         active_runtime->host.exit(active_runtime->host.opaque, reason);
   }
   return JS_UNDEFINED;
}

static JSValue js2300_video_size(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv,
                                 JSValue params)
{
   JSGCRef obj_ref;
   JSValue *obj = JS_PushGCRef(ctx, &obj_ref);
   (void)this_val;
   (void)argc;
   (void)argv;
   (void)params;

   *obj = JS_NewObject(ctx);
   JS_SetPropertyStr(ctx, *obj, "width", JS_NewInt32(ctx, 320));
   JS_SetPropertyStr(ctx, *obj, "height", JS_NewInt32(ctx, 240));
   js2300_note_native_call(active_runtime);
   return JS_PopGCRef(ctx, &obj_ref);
}

static JSValue js2300_video_clear(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv,
                                  JSValue params)
{
   uint32_t color = 0;
   (void)this_val;
   (void)params;
   if (argc > 0 && JS_ToUint32(ctx, &color, argv[0]))
      return JS_EXCEPTION;
   if (active_runtime && active_runtime->host.video_clear)
      active_runtime->host.video_clear(active_runtime->host.opaque, (uint16_t)color);
   js2300_note_native_call(active_runtime);
   return JS_UNDEFINED;
}

static JSValue js2300_video_rects(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv,
                                  JSValue params)
{
   struct js2300_rect rects[JS2300_MAX_RECTS];
   uint32_t outer = 0;
   size_t count = 0;
   (void)this_val;
   (void)params;

   if (argc < 1)
      return JS_UNDEFINED;

   for (;;) {
      JSGCRef row_ref;
      JSValue *row = JS_PushGCRef(ctx, &row_ref);
      int values[5];

      *row = JS_GetPropertyUint32(ctx, argv[0], outer++);
      if (JS_IsUndefined(*row) || JS_IsException(*row)) {
         JS_PopGCRef(ctx, &row_ref);
         break;
      }
      for (uint32_t j = 0; j < 5; j++) {
         JSValue v = JS_GetPropertyUint32(ctx, *row, j);
         if (JS_ToInt32(ctx, &values[j], v)) {
            JS_PopGCRef(ctx, &row_ref);
            return JS_EXCEPTION;
         }
      }
      JS_PopGCRef(ctx, &row_ref);

      if (count < JS2300_MAX_RECTS) {
         rects[count].x = values[0];
         rects[count].y = values[1];
         rects[count].w = values[2];
         rects[count].h = values[3];
         rects[count].color = (uint16_t)values[4];
         count++;
      }
   }

   if (count && active_runtime && active_runtime->host.video_rects)
      active_runtime->host.video_rects(active_runtime->host.opaque, rects, count);
   js2300_note_native_call(active_runtime);
   return JS_UNDEFINED;
}

static JSValue js2300_video_text(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv,
                                 JSValue params)
{
   int x = 0;
   int y = 0;
   uint32_t color = 0xffff;
   JSCStringBuf buf;
   const char *text;
   (void)this_val;
   (void)params;

   if (argc < 3 || JS_ToInt32(ctx, &x, argv[0]) || JS_ToInt32(ctx, &y, argv[1]))
      return JS_EXCEPTION;
   text = JS_ToCString(ctx, argv[2], &buf);
   if (!text)
      return JS_EXCEPTION;
   if (argc > 3 && JS_ToUint32(ctx, &color, argv[3]))
      return JS_EXCEPTION;
   if (active_runtime && active_runtime->host.video_text)
      active_runtime->host.video_text(active_runtime->host.opaque, x, y, text, (uint16_t)color);
   js2300_note_native_call(active_runtime);
   return JS_UNDEFINED;
}

static JSValue js2300_video_image(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv,
                                  JSValue params)
{
   JSCStringBuf path_buf;
   const char *path;
   int x = 0;
   int y = 0;
   int w = 0;
   int h = 0;
   int ret = -1;
   (void)this_val;
   (void)params;

   if (argc < 3)
      return JS_NewInt32(ctx, ret);
   path = JS_ToCString(ctx, argv[0], &path_buf);
   if (!path)
      return JS_EXCEPTION;
   if (JS_ToInt32(ctx, &x, argv[1]) || JS_ToInt32(ctx, &y, argv[2]))
      return JS_EXCEPTION;
   if (argc > 3 && JS_ToInt32(ctx, &w, argv[3]))
      return JS_EXCEPTION;
   if (argc > 4 && JS_ToInt32(ctx, &h, argv[4]))
      return JS_EXCEPTION;
   if (active_runtime && active_runtime->host.video_image)
      ret = active_runtime->host.video_image(active_runtime->host.opaque,
                                             path, x, y, w, h);
   js2300_note_native_call(active_runtime);
   return JS_NewInt32(ctx, ret);
}

static JSValue js2300_video_present(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv,
                                    JSValue params)
{
   (void)ctx;
   (void)this_val;
   (void)argc;
   (void)argv;
   (void)params;
   if (active_runtime && active_runtime->host.video_present)
      active_runtime->host.video_present(active_runtime->host.opaque);
   js2300_note_native_call(active_runtime);
   return JS_UNDEFINED;
}

static JSValue js2300_video_font(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv,
                                 JSValue params)
{
   JSCStringBuf path_buf;
   const char *path;
   int ret = -1;
   (void)this_val;
   (void)params;

   if (argc < 1)
      return JS_NewInt32(ctx, ret);

   path = JS_ToCString(ctx, argv[0], &path_buf);
   if (!path)
      return JS_EXCEPTION;
   if (active_runtime && active_runtime->host.font_load)
      ret = active_runtime->host.font_load(active_runtime->host.opaque, path);
   js2300_note_native_call(active_runtime);
   return JS_NewInt32(ctx, ret);
}

static JSValue js2300_input_poll(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv,
                                 JSValue params)
{
   (void)this_val;
   (void)argc;
   (void)argv;
   (void)params;
   if (active_runtime && active_runtime->host.input_poll) {
      active_runtime->input_prev = active_runtime->input_now;
      active_runtime->input_now = active_runtime->host.input_poll(active_runtime->host.opaque);
      active_runtime->input_poll_count++;
      if ((active_runtime->input_poll_count % JS2300_HEARTBEAT_INPUT_POLL_INTERVAL) == 0) {
         char line[128];
         snprintf(line, sizeof(line), "js2300 alive polls=%lu gc=%lu buttons=0x%08lx",
                  (unsigned long)active_runtime->input_poll_count,
                  (unsigned long)active_runtime->gc_count,
                  (unsigned long)active_runtime->input_now);
         host_log(active_runtime, line);
      }
   }
   js2300_note_native_call(active_runtime);
   return JS_NewUint32(ctx, active_runtime ? active_runtime->input_now : 0);
}

static JSValue js2300_input_pressed(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv,
                                    JSValue params)
{
   JSCStringBuf buf;
   const char *name;
   uint32_t mask;
   (void)this_val;
   (void)params;

   if (argc < 1)
      return JS_FALSE;
   name = JS_ToCString(ctx, argv[0], &buf);
   mask = button_mask_for_name(name);
   if (!active_runtime || !mask)
      return JS_FALSE;
   js2300_note_native_call(active_runtime);
   return JS_NewBool((active_runtime->input_now & mask) &&
                     !(active_runtime->input_prev & mask));
}

static JSValue js2300_input_mask(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv,
                                 JSValue params)
{
   (void)this_val;
   (void)argc;
   (void)argv;
   (void)params;
   js2300_note_native_call(active_runtime);
   return JS_NewUint32(ctx, active_runtime ? active_runtime->input_now : 0);
}

static JSValue js2300_input_down(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv,
                                 JSValue params)
{
   JSCStringBuf buf;
   const char *name;
   uint32_t mask;
   (void)this_val;
   (void)params;

   if (argc < 1)
      return JS_FALSE;
   name = JS_ToCString(ctx, argv[0], &buf);
   mask = button_mask_for_name(name);
   if (!active_runtime || !mask)
      return JS_FALSE;
   js2300_note_native_call(active_runtime);
   return JS_NewBool((active_runtime->input_now & mask) != 0);
}

static JSValue js2300_input_repeated(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv,
                                     JSValue params)
{
   JSCStringBuf buf;
   const char *name;
   uint32_t mask;
   int idx;
   int delay = 250;
   int interval = 100;
   uint32_t now;
   (void)this_val;
   (void)params;

   if (argc < 1 || !active_runtime)
      return JS_FALSE;
   name = JS_ToCString(ctx, argv[0], &buf);
   mask = button_mask_for_name(name);
   idx = button_index(mask);
   if (!mask || idx < 0)
      return JS_FALSE;
   if (argc > 1 && JS_ToInt32(ctx, &delay, argv[1]))
      return JS_EXCEPTION;
   if (argc > 2 && JS_ToInt32(ctx, &interval, argv[2]))
      return JS_EXCEPTION;

   if (!(active_runtime->input_now & mask)) {
      active_runtime->repeat[idx].mask = 0;
      active_runtime->repeat[idx].next_repeat_ms = 0;
      js2300_note_native_call(active_runtime);
      return JS_FALSE;
   }

   now = host_millis(active_runtime);
   if (!(active_runtime->input_prev & mask)) {
      active_runtime->repeat[idx].mask = mask;
      active_runtime->repeat[idx].next_repeat_ms = now + (uint32_t)delay;
      js2300_note_native_call(active_runtime);
      return JS_TRUE;
   }
   if (active_runtime->repeat[idx].mask == mask &&
       now >= active_runtime->repeat[idx].next_repeat_ms) {
      active_runtime->repeat[idx].next_repeat_ms = now + (uint32_t)interval;
      js2300_note_native_call(active_runtime);
      return JS_TRUE;
   }
   js2300_note_native_call(active_runtime);
   return JS_FALSE;
}

static JSValue js2300_system_battery(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv,
                                     JSValue params)
{
   struct js2300_battery_status status = { -1, 0, 0 };
   JSGCRef obj_ref;
   JSValue *obj = JS_PushGCRef(ctx, &obj_ref);
   (void)this_val;
   (void)argc;
   (void)argv;
   (void)params;

   if (active_runtime && active_runtime->host.battery)
      active_runtime->host.battery(active_runtime->host.opaque, &status);

   *obj = JS_NewObject(ctx);
   if (status.percent >= 0)
      JS_SetPropertyStr(ctx, *obj, "percent", JS_NewInt32(ctx, status.percent));
   else
      JS_SetPropertyStr(ctx, *obj, "percent", JS_NULL);
   JS_SetPropertyStr(ctx, *obj, "charging", JS_NewBool(status.charging));
   JS_SetPropertyStr(ctx, *obj, "low", JS_NewBool(status.low));
   js2300_note_native_call(active_runtime);
   return JS_PopGCRef(ctx, &obj_ref);
}

static JSValue js2300_system_backlight(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv,
                                       JSValue params)
{
   int level = -1;
   int out_level = -1;
   int ret = -1;
   (void)this_val;
   (void)params;

   if (argc > 0 && JS_ToInt32(ctx, &level, argv[0]))
      return JS_EXCEPTION;

   if (active_runtime && active_runtime->host.backlight)
      ret = active_runtime->host.backlight(active_runtime->host.opaque,
                                           argc > 0 ? level : -1,
                                           &out_level);
   js2300_note_native_call(active_runtime);
   return JS_NewInt32(ctx, ret == 0 ? out_level : -1);
}

static JSValue js2300_system_av_output(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv,
                                       JSValue params)
{
   int mode = -1;
   int out_mode = -1;
   int ret = -1;
   (void)this_val;
   (void)params;

   if (argc > 0 && JS_ToInt32(ctx, &mode, argv[0]))
      return JS_EXCEPTION;

   if (active_runtime && active_runtime->host.av_output)
      ret = active_runtime->host.av_output(active_runtime->host.opaque,
                                           argc > 0 ? mode : -1,
                                           &out_mode);
   js2300_note_native_call(active_runtime);
   return JS_NewInt32(ctx, ret == 0 ? out_mode : -1);
}

static JSValue js2300_system_action(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv,
                                    JSValue params)
{
   struct js2300_runtime *runtime = active_runtime;
   JSCStringBuf buf;
   const char *id;
   int ret = -1;
   (void)this_val;
   (void)params;

   if (argc < 1)
      return JS_NewInt32(ctx, ret);

   id = JS_ToCString(ctx, argv[0], &buf);
   if (runtime && runtime->host.action && id)
      ret = runtime->host.action(runtime->host.opaque, id);
   if (active_runtime != runtime)
      active_runtime = runtime;
   if (ret == 0 && runtime)
      runtime->exit_requested = 1;
   js2300_note_native_call(runtime);
   return JS_NewInt32(ctx, ret);
}

static JSValue js2300_fs_list(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv,
                              JSValue params)
{
   struct js2300_fs_entry *entries;
   JSCStringBuf path_buf;
   const char *path;
   int count = -1;
   JSGCRef array_ref;
   JSValue *array = JS_PushGCRef(ctx, &array_ref);
   (void)this_val;
   (void)params;

   if (argc < 1) {
      *array = JS_NewArray(ctx, 0);
      js2300_note_native_call(active_runtime);
      return JS_PopGCRef(ctx, &array_ref);
   }

   path = JS_ToCString(ctx, argv[0], &path_buf);
   if (!path) {
      JS_PopGCRef(ctx, &array_ref);
      return JS_EXCEPTION;
   }

   entries = (struct js2300_fs_entry *)calloc(JS2300_MAX_FS_ENTRIES,
                                              sizeof(*entries));
   if (!entries) {
      JS_PopGCRef(ctx, &array_ref);
      return JS_EXCEPTION;
   }

   if (active_runtime && active_runtime->host.fs_list)
      count = active_runtime->host.fs_list(active_runtime->host.opaque, path,
                                           entries, JS2300_MAX_FS_ENTRIES);

   if (count < 0)
      count = 0;
   if (count > (int)JS2300_MAX_FS_ENTRIES)
      count = JS2300_MAX_FS_ENTRIES;

   *array = JS_NewArray(ctx, count);
   if (JS_IsException(*array)) {
      free(entries);
      JS_PopGCRef(ctx, &array_ref);
      return JS_EXCEPTION;
   }

   for (int i = 0; i < count; i++) {
      JSGCRef entry_ref;
      JSValue *entry = JS_PushGCRef(ctx, &entry_ref);

      *entry = JS_NewObject(ctx);
      if (JS_IsException(*entry)) {
         free(entries);
         JS_PopGCRef(ctx, &entry_ref);
         JS_PopGCRef(ctx, &array_ref);
         return JS_EXCEPTION;
      }
      JS_SetPropertyStr(ctx, *entry, "name", JS_NewString(ctx, entries[i].name));
      JS_SetPropertyStr(ctx, *entry, "dir", JS_NewBool(entries[i].is_dir != 0));
      if (JS_IsException(JS_SetPropertyUint32(ctx, *array, (uint32_t)i, *entry))) {
         free(entries);
         JS_PopGCRef(ctx, &entry_ref);
         JS_PopGCRef(ctx, &array_ref);
         return JS_EXCEPTION;
      }
      JS_PopGCRef(ctx, &entry_ref);
   }

   free(entries);
   js2300_note_native_call(active_runtime);
   return JS_PopGCRef(ctx, &array_ref);
}

static JSValue js2300_fs_index(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv,
                               JSValue params)
{
   struct js2300_fs_index_result result;
   JSCStringBuf root_buf;
   JSCStringBuf game_buf;
   JSCStringBuf media_buf;
   const char *root;
   const char *game_path;
   const char *media_path;
   int ret = -1;
   JSGCRef obj_ref;
   JSValue *obj = JS_PushGCRef(ctx, &obj_ref);
   (void)this_val;
   (void)params;

   memset(&result, 0, sizeof(result));
   if (argc < 3) {
      JS_PopGCRef(ctx, &obj_ref);
      return JS_NULL;
   }

   root = JS_ToCString(ctx, argv[0], &root_buf);
   game_path = JS_ToCString(ctx, argv[1], &game_buf);
   media_path = JS_ToCString(ctx, argv[2], &media_buf);
   if (!root || !game_path || !media_path) {
      JS_PopGCRef(ctx, &obj_ref);
      return JS_EXCEPTION;
   }

   if (active_runtime && active_runtime->host.fs_index)
      ret = active_runtime->host.fs_index(active_runtime->host.opaque, root,
                                          game_path, media_path, &result);

   *obj = JS_NewObject(ctx);
   if (JS_IsException(*obj)) {
      JS_PopGCRef(ctx, &obj_ref);
      return JS_EXCEPTION;
   }
   JS_SetPropertyStr(ctx, *obj, "ok", JS_NewBool(ret == 0));
   JS_SetPropertyStr(ctx, *obj, "ret", JS_NewInt32(ctx, ret));
   JS_SetPropertyStr(ctx, *obj, "games", JS_NewUint32(ctx, result.games));
   JS_SetPropertyStr(ctx, *obj, "media", JS_NewUint32(ctx, result.media));
   JS_SetPropertyStr(ctx, *obj, "files", JS_NewUint32(ctx, result.files));
   JS_SetPropertyStr(ctx, *obj, "dirs", JS_NewUint32(ctx, result.dirs));
   JS_SetPropertyStr(ctx, *obj, "truncated", JS_NewBool(result.truncated != 0));
   JS_SetPropertyStr(ctx, *obj, "ms", JS_NewUint32(ctx, result.ms));
   js2300_note_native_call(active_runtime);
   return JS_PopGCRef(ctx, &obj_ref);
}

static JSValue js2300_fs_read_text(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv,
                                   JSValue params)
{
   char *text;
   JSCStringBuf path_buf;
   const char *path;
   int ret = -1;
   (void)this_val;
   (void)params;

   if (argc < 1)
      return JS_NULL;

   path = JS_ToCString(ctx, argv[0], &path_buf);
   if (!path)
      return JS_EXCEPTION;

   text = (char *)malloc(JS2300_MAX_TEXT_FILE_BYTES + 1u);
   if (!text)
      return JS_EXCEPTION;

   if (active_runtime && active_runtime->host.fs_read_text)
      ret = active_runtime->host.fs_read_text(active_runtime->host.opaque,
                                              path, text,
                                              JS2300_MAX_TEXT_FILE_BYTES + 1u);
   js2300_note_native_call(active_runtime);
   if (ret < 0) {
      free(text);
      return JS_NULL;
   }
   text[JS2300_MAX_TEXT_FILE_BYTES] = '\0';
   {
      JSValue out = JS_NewString(ctx, text);
      free(text);
      return out;
   }
}

static JSValue js2300_fs_write_text(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv,
                                    JSValue params)
{
   JSCStringBuf path_buf;
   JSCStringBuf text_buf;
   const char *path;
   const char *text;
   size_t size;
   int ret = -1;
   (void)this_val;
   (void)params;

   if (argc < 2)
      return JS_NewInt32(ctx, ret);

   path = JS_ToCString(ctx, argv[0], &path_buf);
   if (!path)
      return JS_EXCEPTION;
   text = JS_ToCStringLen(ctx, &size, argv[1], &text_buf);
   if (!text)
      return JS_EXCEPTION;
   if (size > JS2300_MAX_TEXT_FILE_BYTES)
      return JS_NewInt32(ctx, ret);

   if (active_runtime && active_runtime->host.fs_write_text)
      ret = active_runtime->host.fs_write_text(active_runtime->host.opaque,
                                               path, text, size);
   js2300_note_native_call(active_runtime);
   return JS_NewInt32(ctx, ret);
}

static JSValue js_date_now(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
   (void)this_val;
   (void)argc;
   (void)argv;
   return JS_NewUint32(ctx, host_millis(active_runtime));
}

static JSValue js_performance_now(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
   return js_date_now(ctx, this_val, argc, argv);
}

static JSValue js_print(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
   return js2300_log(ctx, this_val, argc, argv, JS_NULL);
}

static JSValue js_gc(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
   (void)this_val;
   (void)argc;
   (void)argv;
   JS_GC(ctx);
   return JS_UNDEFINED;
}

static JSValue js_load(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
   JSCStringBuf script_buf;
   const char *script_name;
   char *path;
   JSValue ret;

   (void)this_val;

   if (!active_runtime)
      return JS_ThrowInternalError(ctx, "load() has no active JS2300 runtime");
   if (argc < 1)
      return JS_ThrowInternalError(ctx, "load() requires a script path");

   script_name = JS_ToCString(ctx, argv[0], &script_buf);
   if (!script_name)
      return JS_EXCEPTION;

   path = build_script_path(&active_runtime->config, script_name);
   if (!path)
      return JS_ThrowInternalError(ctx, "load() could not resolve script path");

   ret = eval_script_file(active_runtime, path, "load");
   free(path);
   return ret;
}

static JSValue js_setTimeout(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
   (void)ctx;
   (void)this_val;
   (void)argc;
   (void)argv;
   return JS_ThrowInternalError(ctx, "setTimeout() is not enabled in this JS2300 build");
}

static JSValue js_clearTimeout(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
   (void)ctx;
   (void)this_val;
   (void)argc;
   (void)argv;
   return JS_UNDEFINED;
}

#include "js2300_stdlib.h"
