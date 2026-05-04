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
#define JS2300_MAX_SCRIPT_BYTES (512u * 1024u)
#define JS2300_MAX_TEXT_FILE_BYTES (1024u * 1024u)
#define JS2300_MAX_RECTS 128u
#define JS2300_MAX_FS_ENTRIES 256u
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

struct js2300_runtime {
   struct js2300_config config;
   struct js2300_host host;
   JSContext *ctx;
   void *heap;
   uint32_t input_now;
   uint32_t input_prev;
   uint32_t native_call_count;
   uint32_t gc_count;
   uint32_t input_poll_count;
   int exit_requested;
   struct js2300_button_state repeat[16];
};

static struct js2300_runtime *active_runtime;

const char *js2300_version_string(void)
{
   return "0.5.0";
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

static char *build_entry_path(const struct js2300_config *config)
{
   size_t root_len;
   size_t entry_len;
   char *path;

   if (!config || !config->app_root || !config->entry_script)
      return NULL;

   root_len = strlen(config->app_root);
   entry_len = strlen(config->entry_script);
   path = (char *)malloc(root_len + entry_len + 2);
   if (!path)
      return NULL;

   memcpy(path, config->app_root, root_len);
   path[root_len] = '/';
   memcpy(path + root_len + 1, config->entry_script, entry_len + 1);
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
   char *script;
   size_t script_len;
   JSValue ret;

   if (!runtime)
      return -1;

   path = build_entry_path(&runtime->config);
   if (!path)
      return -1;
   script = read_file(path, JS2300_MAX_SCRIPT_BYTES, &script_len);
   if (!script) {
      char msg[192];
      snprintf(msg, sizeof(msg), "js2300 read failed path=%s errno=%d", path, errno);
      host_log(runtime, msg);
      free(path);
      return -1;
   }

   runtime->heap = calloc(1, runtime->config.heap_bytes);
   if (!runtime->heap) {
      free(script);
      free(path);
      return -1;
   }

   runtime->ctx = JS_NewContext(runtime->heap, runtime->config.heap_bytes,
                                &js2300_stdlib);
   if (!runtime->ctx) {
      free(runtime->heap);
      runtime->heap = NULL;
      free(script);
      free(path);
      return -1;
   }

   active_runtime = runtime;
   if (js_attach_api(runtime->ctx) != 0) {
      host_log(runtime, "js2300 attach api failed");
      active_runtime = NULL;
      JS_FreeContext(runtime->ctx);
      runtime->ctx = NULL;
      free(runtime->heap);
      runtime->heap = NULL;
      free(script);
      free(path);
      return -1;
   }

   host_log(runtime, "js2300 eval start");
   ret = JS_Eval(runtime->ctx, script, script_len, path, 0);
   if (JS_IsException(ret)) {
      JSCStringBuf buf;
      JSValue exc = JS_GetException(runtime->ctx);
      const char *message = JS_ToCString(runtime->ctx, exc, &buf);
      char line[224];
      snprintf(line, sizeof(line), "js2300 exception %s", message ? message : "unknown");
      host_log(runtime, line);
      active_runtime = NULL;
      JS_FreeContext(runtime->ctx);
      runtime->ctx = NULL;
      free(runtime->heap);
      runtime->heap = NULL;
      free(script);
      free(path);
      return -1;
   }

   js2300_runtime_gc(runtime, "eval_done");
   host_log(runtime, "js2300 eval done");
   active_runtime = NULL;
   JS_FreeContext(runtime->ctx);
   runtime->ctx = NULL;
   free(runtime->heap);
   runtime->heap = NULL;
   free(script);
   free(path);
   return 0;
}

void js2300_runtime_destroy(struct js2300_runtime *runtime)
{
   if (runtime && runtime->ctx)
      JS_FreeContext(runtime->ctx);
   if (runtime)
      free(runtime->heap);
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
   JSCStringBuf buf;
   const char *id;
   int ret = -1;
   (void)this_val;
   (void)params;

   if (argc < 1)
      return JS_NewInt32(ctx, ret);

   id = JS_ToCString(ctx, argv[0], &buf);
   if (active_runtime && active_runtime->host.action && id)
      ret = active_runtime->host.action(active_runtime->host.opaque, id);
   if (ret == 0 && active_runtime)
      active_runtime->exit_requested = 1;
   js2300_note_native_call(active_runtime);
   return JS_NewInt32(ctx, ret);
}

static JSValue js2300_fs_list(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv,
                              JSValue params)
{
   struct js2300_fs_entry entries[JS2300_MAX_FS_ENTRIES];
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

   if (active_runtime && active_runtime->host.fs_list)
      count = active_runtime->host.fs_list(active_runtime->host.opaque, path,
                                           entries, JS2300_MAX_FS_ENTRIES);

   if (count < 0)
      count = 0;
   if (count > (int)JS2300_MAX_FS_ENTRIES)
      count = JS2300_MAX_FS_ENTRIES;

   *array = JS_NewArray(ctx, count);
   if (JS_IsException(*array)) {
      JS_PopGCRef(ctx, &array_ref);
      return JS_EXCEPTION;
   }

   for (int i = 0; i < count; i++) {
      JSGCRef entry_ref;
      JSValue *entry = JS_PushGCRef(ctx, &entry_ref);

      *entry = JS_NewObject(ctx);
      if (JS_IsException(*entry)) {
         JS_PopGCRef(ctx, &entry_ref);
         JS_PopGCRef(ctx, &array_ref);
         return JS_EXCEPTION;
      }
      JS_SetPropertyStr(ctx, *entry, "name", JS_NewString(ctx, entries[i].name));
      JS_SetPropertyStr(ctx, *entry, "dir", JS_NewBool(entries[i].is_dir != 0));
      if (JS_IsException(JS_SetPropertyUint32(ctx, *array, (uint32_t)i, *entry))) {
         JS_PopGCRef(ctx, &entry_ref);
         JS_PopGCRef(ctx, &array_ref);
         return JS_EXCEPTION;
      }
      JS_PopGCRef(ctx, &entry_ref);
   }

   js2300_note_native_call(active_runtime);
   return JS_PopGCRef(ctx, &array_ref);
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
   (void)ctx;
   (void)this_val;
   (void)argc;
   (void)argv;
   return JS_ThrowInternalError(ctx, "load() is not enabled in this JS2300 build");
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
