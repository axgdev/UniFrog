#ifndef FRONTEND_INTERNAL_H
#define FRONTEND_INTERNAL_H

#include <unifrog/frontend.h>

#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#include <unifrog_default_options.h>

#include <unifrog/backlight.h>
#include <unifrog/audio.h>
#include <unifrog/artwork.h>
#include <unifrog/battery.h>
#include <unifrog/clock.h>
#include <unifrog/boot.h>
#include <unifrog/boot_logo.h>
#include <unifrog/build_info.h>
#include <unifrog/config.h>
#include <unifrog/core_module.h>
#include <unifrog/core_registry.h>
#include <unifrog/device.h>
#include <unifrog/diag.h>
#include <unifrog/exception_record.h>
#include <unifrog/frontend_config.h>
#include <unifrog/frontend_services.h>
#include <unifrog/gfx.h>
#include <unifrog/input.h>
#include <unifrog/libretro_host.h>
#include <unifrog/libretro_policy.h>
#include <unifrog/log.h>
#include <unifrog/media.h>
#include <unifrog/media_content.h>
#include <unifrog/frontend_lvgl.h>
#include <unifrog/frontend_model.h>
#include <unifrog/paths.h>
#include <unifrog/platform.h>
#include <unifrog/perf.h>
#include <unifrog/reader.h>
#include <unifrog/scpu.h>
#include <unifrog/storage_io.h>
#include <unifrog/storage_profile.h>
#include <unifrog/storage_probe.h>
#include <unifrog/text.h>
#include <unifrog/ui.h>
#include <unifrog/zlib_port.h>

#define FRONTEND_ROOT UNIFROG_SD_ROOT
#define FRONTEND_ROMS_ROOT UNIFROG_ROMS_ROOT
#define FRONTEND_DIST_ROOT UNIFROG_DIST_ROOT
#define FRONTEND_DATA_ROOT UNIFROG_DATA_ROOT
#define FRONTEND_ARCHIVE_ROOT UNIFROG_ARCHIVE_ROOT
#define FRONTEND_STOCK_ARCHIVE_ROOT FRONTEND_ROOT "/ARCHIVE"
#define FRONTEND_SCRIPT_ROOT UNIFROG_SCRIPT_ROOT
#define FRONTEND_THEME_ROOT UNIFROG_THEME_ROOT
#define FRONTEND_LANGUAGE_ROOT UNIFROG_LANGUAGE_ROOT
#define FRONTEND_FIRMWARE_ROOT UNIFROG_USER_FIRMWARE_ROOT
#define FRONTEND_UPDATE_ROOT UNIFROG_UPDATE_ROOT
#define FRONTEND_VERSION_ROOT UNIFROG_VERSION_ROOT
#define FRONTEND_ACTIVE_VERSION_PATH UNIFROG_ACTIVE_VERSION_PATH
#define FRONTEND_HISTORY_PATH UNIFROG_HISTORY_PATH
#define FRONTEND_FAVORITES_PATH UNIFROG_FAVORITES_PATH
#define FRONTEND_MAX_ITEMS 1024u
#define FRONTEND_MAX_PATH 256u
#define FRONTEND_MAX_LINE 384u
#define FRONTEND_ROWS 8u
#define FRONTEND_HISTORY_MAX 64u
#define FRONTEND_FAVORITES_MAX 256u
#define FRONTEND_NAV_MAX 16u
#define FRONTEND_NAV_LOG_STEP 16u
#define FRONTEND_JUMP_FALLBACK_STEP 50u
#define FRONTEND_ROM_ROOT_LABEL_MAX 48u
#define FRONTEND_ROM_ROOT_MAX 8u
#define FRONTEND_ROM_SYSTEM_MAP_MAX 48u
#define FRONTEND_I18N_MAX 192u
#define FRONTEND_SCHEME_MAX 96u
#define FRONTEND_ASSOCIATION_MAX 112u
#define FRONTEND_ASSOCIATION_HANDLER_MAX 8u
#define FRONTEND_LVGL_LIST_ROWS 8u
#define FRONTEND_CORE_CANDIDATE_MAX UNIFROG_CORE_REGISTRY_MAX
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

enum frontend_view {
   FRONTEND_VIEW_LAUNCH = 0,
   FRONTEND_VIEW_EXPLORE,
   FRONTEND_VIEW_HISTORY,
   FRONTEND_VIEW_FAVORITES,
   FRONTEND_VIEW_APPS,
   FRONTEND_VIEW_CONFIG,
   FRONTEND_VIEW_CONNECT,
   FRONTEND_VIEW_CUSTOM,
   FRONTEND_VIEW_VISUAL,
   FRONTEND_VIEW_LAUNCH_SETTINGS,
   FRONTEND_VIEW_POWER,
   FRONTEND_VIEW_STORAGE,
   FRONTEND_VIEW_STORAGE_MODE,
   FRONTEND_VIEW_STORAGE_CONFIRM,
   FRONTEND_VIEW_ROM_SYSTEMS,
   FRONTEND_VIEW_ROM_SYSTEM_MAPPINGS,
   FRONTEND_VIEW_ROM_ROOTS,
   FRONTEND_VIEW_ROM_ROOT_PICKER,
   FRONTEND_VIEW_OPEN_WITH,
   FRONTEND_VIEW_OPEN_WITH_OTHER,
   FRONTEND_VIEW_MEDIA_PLAYER,
   FRONTEND_VIEW_READER,
   FRONTEND_VIEW_THEME,
   FRONTEND_VIEW_LANGUAGE,
   FRONTEND_VIEW_FIRMWARE,
   FRONTEND_VIEW_SCRIPTS,
   FRONTEND_VIEW_UPDATES,
   FRONTEND_VIEW_CORES,
   FRONTEND_VIEW_CORE_INFO,
   FRONTEND_VIEW_PACKAGE_CHECK,
   FRONTEND_VIEW_CLOCK,
   FRONTEND_VIEW_ARTWORK,
   FRONTEND_VIEW_FONT,
   FRONTEND_VIEW_INFO,
   FRONTEND_VIEW_SYSINFO,
};

enum frontend_item_kind {
   FRONTEND_ITEM_ACTION = 0,
   FRONTEND_ITEM_INFO,
   FRONTEND_ITEM_DIR,
   FRONTEND_ITEM_GAME,
   FRONTEND_ITEM_MEDIA,
   FRONTEND_ITEM_READER,
   FRONTEND_ITEM_FIRMWARE,
   FRONTEND_ITEM_SCRIPT,
   FRONTEND_ITEM_THEME_ARCHIVE,
   FRONTEND_ITEM_UPDATE_ARCHIVE,
   FRONTEND_ITEM_VERSION,
   FRONTEND_ITEM_CORE_MODULE,
};

struct frontend_association {
   char extension[16];
   char handlers[FRONTEND_ASSOCIATION_HANDLER_MAX]
      [UNIFROG_CORE_MODULE_ID_MAX];
   unsigned handler_count;
   char default_handler[UNIFROG_CORE_MODULE_ID_MAX];
};

struct frontend_item {
   char name[96];
   char meta[64];
   char path[FRONTEND_MAX_PATH];
   char core[96];
   enum frontend_item_kind kind;
   enum unifrog_frontend_action action;
};

struct frontend_zip_entry {
   char name[FRONTEND_MAX_PATH];
   uint16_t flags;
   uint16_t method;
   uint32_t compressed_size;
   uint32_t uncompressed_size;
   uint32_t local_offset;
};

struct frontend_navigation_state {
   char path[FRONTEND_NAV_MAX][FRONTEND_MAX_PATH];
   unsigned selected[FRONTEND_NAV_MAX];
   unsigned count;
   enum frontend_view view_stack[FRONTEND_NAV_MAX];
   unsigned view_selected[FRONTEND_NAV_MAX];
   unsigned view_scroll[FRONTEND_NAV_MAX];
   unsigned view_count;
   enum frontend_view parent_view;
   int has_parent_view;
};

struct frontend_state {
   struct unifrog_ui ui;
   const struct unifrog_ui_theme *theme;
   struct unifrog_ui_theme active_theme;
   struct unifrog_ui_theme base_theme;
   struct unifrog_frontend_lvgl_style active_style;
   struct unifrog_frontend_lvgl_style base_style;
   struct unifrog_frontend_lvgl_style screen_style[UNIFROG_FRONTEND_LVGL_VISUAL + 1];
   struct unifrog_frontend_lvgl_style list_style;
   struct unifrog_frontend_lvgl_style view_style[FRONTEND_VIEW_SYSINFO + 1];
   uint8_t screen_style_valid[UNIFROG_FRONTEND_LVGL_VISUAL + 1];
   uint8_t view_style_valid[FRONTEND_VIEW_SYSINFO + 1];
   int dir_theme_loaded;
   enum frontend_view view;
   struct frontend_navigation_state nav;
   struct frontend_item items[FRONTEND_MAX_ITEMS];
   char item_glyph_path[FRONTEND_MAX_ITEMS][FRONTEND_MAX_PATH];
   const char *item_glyph[FRONTEND_MAX_ITEMS];
   uint8_t item_glyph_resolved[FRONTEND_MAX_ITEMS];
   unsigned item_generation;
   unsigned glyph_cache_generation;
   char glyph_cache_module[32];
   unsigned item_count;
   unsigned selected;
   unsigned scroll;
   char title[64];
   char current_dir[FRONTEND_MAX_PATH];
   char last_path[FRONTEND_MAX_PATH];
   char last_core[24];
   struct frontend_item pending_open_item;
   char pending_open_dir[FRONTEND_MAX_PATH];
   int pending_open_valid;
   char rom_root[FRONTEND_MAX_PATH];
   char rom_root_label[FRONTEND_ROM_ROOT_LABEL_MAX];
   char rom_roots[FRONTEND_ROM_ROOT_MAX][FRONTEND_MAX_PATH];
   unsigned rom_root_count;
   char rom_system_name[FRONTEND_ROM_SYSTEM_MAP_MAX][32];
   char rom_system_core[FRONTEND_ROM_SYSTEM_MAP_MAX][UNIFROG_CORE_MODULE_ID_MAX];
   unsigned rom_system_count;
   struct frontend_association associations[FRONTEND_ASSOCIATION_MAX];
   unsigned association_count;
   struct unifrog_frontend_config scoped_config;
   const struct unifrog_frontend_launch_services *launch_services;
   struct unifrog_core_registry core_registry;
   char status[96];
   struct unifrog_battery_status battery;
   struct unifrog_battery_calibration battery_calibration;
   uint32_t battery_ms;
   uint32_t battery_notice_input_ms;
   uint32_t battery_notice_until_ms;
   struct unifrog_libretro_run_options run_options;
   struct unifrog_media_tuning media_tuning;
   int sort_desc;
   int show_hidden;
   int folder_counts;
   int mixed_content;
   int display_empty_folder;
   int menu_counter_folder;
   int menu_counter_file;
   int content_collect;
   int content_history;
   int clock_enabled;
   int title_include_root;
   int theme_alternate;
   int boxart_hidden;
   int launch_splash;
   int sound_enabled;
   char log_level[8];
   uint32_t log_flush_ms;
   char theme_name[48];
   char theme_font[FRONTEND_MAX_PATH];
   char loaded_theme_name[48];
   char loaded_theme_language[48];
   char resource_cache_key[64];
   char language_name[48];
   char artwork_layout[16];
   char artwork_box_templates[UNIFROG_ARTWORK_TEMPLATE_MAX];
   char artwork_preview_templates[UNIFROG_ARTWORK_TEMPLATE_MAX];
   char artwork_text_templates[UNIFROG_ARTWORK_TEMPLATE_MAX];
   char artwork_cache_item[FRONTEND_MAX_PATH];
   struct unifrog_artwork_paths artwork_cache_paths;
   char artwork_cache_text[192];
   char scheme_name[FRONTEND_SCHEME_MAX][32];
   unsigned scheme_count;
   char i18n_key[FRONTEND_I18N_MAX][40];
   char i18n_value[FRONTEND_I18N_MAX][128];
   unsigned i18n_count;
   char storage_profile[16];
   char storage_normal_profile[16];
   char storage_fallback_profile[16];
   int battery_low_warning;
   char storage_pending_profile[16];
   char default_boot[UNIFROG_BOOT_PATH_MAX];
   char device_board[16];
   int running;
   int shutdown_safe;
   int needs_draw;
   int last_draw_valid;
   int theme_loaded;
   int loaded_theme_alternate;
   uint32_t last_draw_signature;
   uint32_t animation_draw_ms;
   unsigned nav_log_last_selected;
   enum frontend_view nav_log_last_view;
   int applied_style_id;
};

typedef void (*frontend_progress_cb)(void *userdata, const char *stage,
   unsigned done, unsigned total);

struct frontend_install_progress {
   struct frontend_state *fe;
   uint32_t start_ms;
   uint32_t last_draw_ms;
   unsigned last_percent;
   char title[32];
   char name[64];
};


extern const struct unifrog_ui_theme frontend_theme;

const char *frontend_basename(const char *path);
void frontend_strip_eol(char *text);
const char *frontend_read_key_value(const char *line, const char *key);
int frontend_read_file_key(char *dst, size_t dst_size, const char *path,
   const char *key);
int frontend_parse_int(const char *text, int fallback);
unsigned frontend_parse_unsigned_setting(const char *text, unsigned fallback);
size_t frontend_parse_size_setting(const char *text, size_t fallback);
char *frontend_trim_ascii(char *text);
void frontend_cycle_string_choice(char *value, size_t value_size,
   const char *const *choices, unsigned count);
uint8_t frontend_parse_alpha(const char *text, uint8_t fallback);
int frontend_parse_rgb565_hex(const char *text, uint16_t *out);
uint16_t frontend_rgb888_to_rgb565(uint32_t color);
int frontend_parse_theme_hex(const char *text, uint16_t *out);
int frontend_path_join(char *dst, size_t dst_size, const char *base,
   const char *name);
int frontend_path_join_ini(char *dst, size_t dst_size, const char *base,
   const char *name);
int frontend_sd_relative_path(char *dst, size_t dst_size, const char *path);
int frontend_path_is_valid(const char *path);
int frontend_normalize_path(char *dst, size_t dst_size, const char *src);
int frontend_file_exists(const char *path);
int frontend_write_text_file(const char *path, const char *text);
int frontend_mkdir_p(const char *path);
int frontend_ensure_parent_dir(const char *path);
int frontend_remove_tree(const char *path);
int frontend_remove_tree_under(const char *path, const char *root);
int frontend_copy_file_path(const char *src, const char *dst);
int frontend_copy_tree_merge(const char *src, const char *dst,
   int (*skip_name)(const char *name));
void frontend_strip_ini_suffix(char *name);
void frontend_strip_known_suffix(char *name, const char *suffix);
void frontend_sanitize_slot_name(char *name);
void frontend_ensure_data_dirs(void);
int frontend_path_has_dir_prefix(const char *path, const char *root);
const char *frontend_rom_root(const struct frontend_state *fe);
const char *frontend_rom_root_label(const struct frontend_state *fe);
const char *frontend_rom_root_at(const struct frontend_state *fe,
   unsigned index);
unsigned frontend_rom_root_count(const struct frontend_state *fe);
int frontend_rom_root_index(const struct frontend_state *fe,
   const char *path);
unsigned frontend_rom_root_preset_count(void);
const char *frontend_rom_root_preset_at(unsigned index);
void frontend_rom_root_sync_primary(struct frontend_state *fe);
int frontend_rom_root_set_primary(struct frontend_state *fe,
   const char *path);
int frontend_rom_root_add(struct frontend_state *fe, const char *path);
int frontend_rom_root_remove(struct frontend_state *fe, const char *path);
int frontend_path_is_rom_root(const struct frontend_state *fe,
   const char *path);
int frontend_path_is_inside_rom_root(const struct frontend_state *fe,
   const char *path);
const char *frontend_rom_title(const struct frontend_state *fe,
   const char *path);
unsigned clamp_state_slot(unsigned slot);
const char *frameskip_label(int frameskip);
const char *display_label(int display_mode);
const char *framebuffer_label(int format);
const char *input_profile_label(int profile);
const char *state_slot_label(unsigned slot);
const char *ge_clock_label(int ge_clock);
const char *on_off_label(int value);
int is_back_item(const struct frontend_item *item);

void reset_items(struct frontend_state *fe, const char *title);
struct frontend_item *add_item(struct frontend_state *fe, const char *name,
   const char *meta, enum frontend_item_kind kind, const char *path,
   const char *core);
struct frontend_item *add_info(struct frontend_state *fe, const char *name,
   const char *meta);
void add_model_items(struct frontend_state *fe,
   const struct unifrog_frontend_model *model);
void frontend_model_settings(const struct frontend_state *fe,
   struct unifrog_frontend_model_settings *settings);
void sort_items(struct frontend_state *fe);
void clamp_selection(struct frontend_state *fe);
void log_selection(struct frontend_state *fe, const char *reason);
void log_item_sample(struct frontend_state *fe, const char *tag);
void frontend_invalidate_draw(struct frontend_state *fe);
void frontend_request_return_redraw(struct frontend_state *fe,
   const char *source);
void frontend_nav_reset(struct frontend_state *fe);
void frontend_nav_push(struct frontend_state *fe);
int frontend_nav_pop(struct frontend_state *fe, char *path, size_t path_size,
   unsigned *selected);
void frontend_parent_view_push(struct frontend_state *fe);
void frontend_parent_view_clear(struct frontend_state *fe);
void frontend_history_record(struct frontend_state *fe, const char *path,
   const char *core);
int frontend_favorite_contains(const char *path);
void frontend_favorite_toggle(struct frontend_state *fe,
   const struct frontend_item *item);
void frontend_draw(struct frontend_state *fe);
const char *frontend_rom_system_mapped_core(const struct frontend_state *fe,
   const char *name);
void frontend_cycle_rom_root(struct frontend_state *fe, int dir);
void frontend_cycle_rom_system_core(struct frontend_state *fe,
   const char *name, int dir);
void frontend_show_rom_systems(struct frontend_state *fe);
void frontend_show_rom_system_mappings(struct frontend_state *fe);
void frontend_show_rom_roots(struct frontend_state *fe);
void frontend_show_rom_root_picker(struct frontend_state *fe,
   const char *path);
void frontend_show_explore(struct frontend_state *fe, const char *path);
void frontend_show_firmware_browser(struct frontend_state *fe,
   const char *path);
void frontend_show_script_browser(struct frontend_state *fe,
   const char *path);
void frontend_show_file_list(struct frontend_state *fe, const char *title,
   const char *path, enum frontend_view view);
void frontend_show_power(struct frontend_state *fe);
void frontend_show_clock(struct frontend_state *fe);
void frontend_show_storage(struct frontend_state *fe);
void frontend_show_storage_mode(struct frontend_state *fe);
void frontend_show_storage_confirm(struct frontend_state *fe,
   const char *profile);
void frontend_sound_shutdown(void);
void frontend_prepare_safe_shutdown(struct frontend_state *fe);
int frontend_adjust_clock_item(struct frontend_state *fe, const char *path,
   int amount);
int frontend_system_activate(struct frontend_state *fe,
   const struct frontend_item *item, unsigned selected);
void frontend_show_launch_settings(struct frontend_state *fe);
void frontend_change_config(struct frontend_state *fe, int dir);
int frontend_preferences_activate(struct frontend_state *fe,
   const struct frontend_item *item, unsigned selected);
void frontend_show_custom(struct frontend_state *fe);
void frontend_show_visual(struct frontend_state *fe);
void frontend_show_artwork(struct frontend_state *fe);
void frontend_show_font_list(struct frontend_state *fe);
void frontend_show_theme_list(struct frontend_state *fe);
void frontend_show_language_list(struct frontend_state *fe);
void frontend_set_artwork_layout(struct frontend_state *fe,
   const char *layout);
int frontend_appearance_activate(struct frontend_state *fe,
   const struct frontend_item *item, unsigned selected);
void frontend_show_apps(struct frontend_state *fe);
void frontend_show_media_player(struct frontend_state *fe, const char *path);
void frontend_show_reader_browser(struct frontend_state *fe,
   const char *path);
int frontend_apps_activate(struct frontend_state *fe,
   const struct frontend_item *item);
void frontend_show_firmware(struct frontend_state *fe);
void frontend_show_info(struct frontend_state *fe);
void frontend_show_updates(struct frontend_state *fe);
void frontend_show_core_info(struct frontend_state *fe, const char *path);
void frontend_show_core_manager(struct frontend_state *fe);
void frontend_show_package_check(struct frontend_state *fe);
void frontend_show_sysinfo(struct frontend_state *fe);
int frontend_maintenance_activate(struct frontend_state *fe,
   const struct frontend_item *item);
void frontend_input_jump_selection_group(struct frontend_state *fe, int dir);
int frontend_input_handle_menu_navigation(struct frontend_state *fe,
   uint32_t now, int combo_handled);
void frontend_launch_game(struct frontend_state *fe,
   struct frontend_item *item);
void frontend_launch_media(struct frontend_state *fe,
   struct frontend_item *item);
void frontend_launch_reader(struct frontend_state *fe,
   struct frontend_item *item);
void frontend_launch_script(struct frontend_state *fe,
   struct frontend_item *item);
void frontend_launch_with_handler(struct frontend_state *fe,
   struct frontend_item *item, const char *handler);
void frontend_launch_last_game(struct frontend_state *fe);
void frontend_launch_audio_diagnostics(struct frontend_state *fe);
void frontend_show_open_with(struct frontend_state *fe,
   const struct frontend_item *item);
void frontend_show_open_with_other(struct frontend_state *fe);
void frontend_return_from_open_with(struct frontend_state *fe);
void frontend_set_status(struct frontend_state *fe, const char *fmt, ...);
void restore_view_selection(struct frontend_state *fe, unsigned selected,
   unsigned scroll);
int restore_parent_view(struct frontend_state *fe, enum frontend_view fallback);

int is_media_file(const char *path);
int is_reader_file(const char *path);
int media_path_is_audio(const char *path);
int media_handler_route(const char *handler, enum unifrog_media_route *route);
int media_path_has_open_with_choices(const char *path);
int is_asd_file(const char *path);
int is_js_script_file(const char *path);
int is_js_libretro_core_script(const char *path);
int is_zip_file(const char *path);
int is_core_module_file(const char *path);
int core_module_header_compatible(const struct unifrog_core_module_header *h);
void core_module_meta(char *dst, size_t dst_size,
   const struct unifrog_core_module_header *h);
const char *safe_core_for_path(const struct frontend_state *fe,
   const char *path, const char *core);
unsigned collect_core_candidates(const struct frontend_state *fe,
   const char *path, char ids[][UNIFROG_CORE_MODULE_ID_MAX]);
unsigned collect_other_core_candidates(const struct frontend_state *fe,
   char ids[][UNIFROG_CORE_MODULE_ID_MAX]);
int is_content_file(const struct frontend_state *fe, const char *path);
const char *default_handler_for_path(const struct frontend_state *fe,
   const char *path);

int install_theme_archive(const char *archive_path, char *installed_name,
   size_t installed_name_size, frontend_progress_cb progress, void *userdata);
int install_update_archive(const char *archive_path, char *slot_name,
   size_t slot_name_size);
int validate_update_archive(const char *archive_path, char *summary,
   size_t summary_size);
void mark_boot_ok(void);
int activate_installed_version(const char *slot);

const char *active_language_label(struct frontend_state *fe);
const char *active_theme_label(struct frontend_state *fe);
const char *tr(struct frontend_state *fe, const char *key);
void load_language(struct frontend_state *fe);
const char *lvgl_screen_module(enum unifrog_frontend_lvgl_screen screen);
const char *list_view_glyph_module(enum frontend_view view);
void frontend_loading_show(struct frontend_state *fe, const char *title,
   const char *name, const char *stage, unsigned percent);
void frontend_loading_handoff_black(struct frontend_state *fe,
   const char *reason);
void frontend_install_progress_update(void *userdata, const char *stage,
   unsigned done, unsigned total);
void visible_item_range(unsigned count, unsigned selected, unsigned rows,
   unsigned *start, unsigned *stop);
unsigned visible_rows_for_style(const struct unifrog_frontend_lvgl_style *style);
void fill_visible_item_glyphs(struct frontend_state *fe, const char *module,
   unsigned start, unsigned stop,
   char paths[FRONTEND_MAX_ITEMS][FRONTEND_MAX_PATH],
   const char *glyphs[FRONTEND_MAX_ITEMS]);
void alternate_style(struct unifrog_frontend_lvgl_style *style);
void apply_frontend_style(struct frontend_state *fe, int id,
   const struct unifrog_frontend_lvgl_style *style);
const struct unifrog_frontend_lvgl_style *frontend_screen_style(
   struct frontend_state *fe, enum unifrog_frontend_lvgl_screen screen);
const struct unifrog_frontend_lvgl_style *frontend_view_style(
   struct frontend_state *fe, enum frontend_view view);
void load_theme(struct frontend_state *fe);
void load_theme_font_preference(struct frontend_state *fe);

const char *storage_profile_label(const char *profile);
int apply_storage_profile(struct frontend_state *fe, const char *reason);
int sync_default_boot(struct frontend_state *fe);
const char *active_storage_label(void);
int save_settings(struct frontend_state *fe);
void load_settings(struct frontend_state *fe);
void frontend_associations_load(struct frontend_state *fe);
int frontend_associations_save(const struct frontend_state *fe);
const struct frontend_association *frontend_association_for_path(
   const struct frontend_state *fe, const char *path);
int frontend_association_set_default(struct frontend_state *fe,
   const char *path, const char *handler);

int frontend_services_run_game(const struct frontend_state *fe,
   const char *path, const struct unifrog_libretro_run_options *options);
int frontend_services_play_media(const struct frontend_state *fe,
   const char *path, const struct unifrog_media_video_options *options);
int frontend_services_run_reader(const struct frontend_state *fe,
   const char *path);
int frontend_services_run_script(const struct frontend_state *fe,
   const char *path);
int frontend_services_run_audio_diagnostics(const struct frontend_state *fe,
   char *summary, size_t summary_size, unifrog_media_progress_cb progress,
   void *userdata);
int frontend_services_create_bug_report(const struct frontend_state *fe,
   char *output_path, size_t output_path_size, char *summary,
   size_t summary_size);

#endif
