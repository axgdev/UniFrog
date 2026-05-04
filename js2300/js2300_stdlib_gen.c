#include "mquickjs_build.h"

#define main js2300_unused_mqjs_stdlib_main
#include "mqjs_stdlib.c"
#undef main

static const JSPropDef js2300_c_function_decl[] = {
   JS_CFUNC_SPECIAL_DEF("bound", 0, generic_params, js_function_bound),
   JS_CFUNC_SPECIAL_DEF("js2300_log", 1, generic_params, js2300_log),
   JS_CFUNC_SPECIAL_DEF("js2300_flush_log", 0, generic_params, js2300_flush_log),
   JS_CFUNC_SPECIAL_DEF("js2300_now", 0, generic_params, js2300_now),
   JS_CFUNC_SPECIAL_DEF("js2300_sleep", 1, generic_params, js2300_sleep),
   JS_CFUNC_SPECIAL_DEF("js2300_gc", 0, generic_params, js2300_gc),
   JS_CFUNC_SPECIAL_DEF("js2300_exit", 1, generic_params, js2300_exit),
   JS_CFUNC_SPECIAL_DEF("js2300_video_size", 0, generic_params, js2300_video_size),
   JS_CFUNC_SPECIAL_DEF("js2300_video_clear", 1, generic_params, js2300_video_clear),
   JS_CFUNC_SPECIAL_DEF("js2300_video_rects", 1, generic_params, js2300_video_rects),
   JS_CFUNC_SPECIAL_DEF("js2300_video_text", 4, generic_params, js2300_video_text),
   JS_CFUNC_SPECIAL_DEF("js2300_video_image", 5, generic_params, js2300_video_image),
   JS_CFUNC_SPECIAL_DEF("js2300_video_present", 0, generic_params, js2300_video_present),
   JS_CFUNC_SPECIAL_DEF("js2300_video_font", 1, generic_params, js2300_video_font),
   JS_CFUNC_SPECIAL_DEF("js2300_input_poll", 0, generic_params, js2300_input_poll),
   JS_CFUNC_SPECIAL_DEF("js2300_input_mask", 0, generic_params, js2300_input_mask),
   JS_CFUNC_SPECIAL_DEF("js2300_input_down", 1, generic_params, js2300_input_down),
   JS_CFUNC_SPECIAL_DEF("js2300_input_pressed", 1, generic_params, js2300_input_pressed),
   JS_CFUNC_SPECIAL_DEF("js2300_input_repeated", 3, generic_params, js2300_input_repeated),
   JS_CFUNC_SPECIAL_DEF("js2300_system_battery", 0, generic_params, js2300_system_battery),
   JS_CFUNC_SPECIAL_DEF("js2300_system_backlight", 1, generic_params, js2300_system_backlight),
   JS_CFUNC_SPECIAL_DEF("js2300_system_av_output", 1, generic_params, js2300_system_av_output),
   JS_CFUNC_SPECIAL_DEF("js2300_system_action", 1, generic_params, js2300_system_action),
   JS_CFUNC_SPECIAL_DEF("js2300_fs_list", 1, generic_params, js2300_fs_list),
   JS_CFUNC_SPECIAL_DEF("js2300_fs_read_text", 1, generic_params, js2300_fs_read_text),
   JS_CFUNC_SPECIAL_DEF("js2300_fs_write_text", 2, generic_params, js2300_fs_write_text),
   JS_PROP_END,
};

int main(int argc, char **argv)
{
   return build_atoms("js2300_stdlib", js_global_object,
                      js2300_c_function_decl, argc, argv);
}
