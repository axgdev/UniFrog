FRONTEND_APP_SOURCES := \
	components/frontend/src/app/frontend_app.c \
	components/frontend/src/app/frontend_appearance.c \
	components/frontend/src/app/frontend_apps.c \
	components/frontend/src/app/frontend_archives.c \
	components/frontend/src/app/frontend_associations.c \
	components/frontend/src/app/frontend_browser.c \
	components/frontend/src/app/frontend_config.c \
	components/frontend/src/app/frontend_content.c \
	components/frontend/src/app/frontend_controller.c \
	components/frontend/src/app/frontend_core_registry.c \
	components/frontend/src/app/frontend_draw.c \
	components/frontend/src/app/frontend_favorites.c \
	components/frontend/src/app/frontend_history.c \
	components/frontend/src/app/frontend_input.c \
	components/frontend/src/app/frontend_items.c \
	components/frontend/src/app/frontend_labels.c \
	components/frontend/src/app/frontend_launch.c \
	components/frontend/src/app/frontend_maintenance.c \
	components/frontend/src/app/frontend_model.c \
	components/frontend/src/app/frontend_navigation.c \
	components/frontend/src/app/frontend_paths.c \
	components/frontend/src/app/frontend_preferences.c \
	components/frontend/src/app/frontend_progress.c \
	components/frontend/src/app/frontend_rom_roots.c \
	components/frontend/src/app/frontend_services.c \
	components/frontend/src/app/frontend_settings.c \
	components/frontend/src/app/frontend_system.c \
	components/frontend/src/app/frontend_text.c \
	components/frontend/src/app/frontend_theme.c \
	components/frontend/src/app/frontend_lvgl.c

FRONTEND_APP_OBJECTS := \
	$(patsubst components/frontend/src/%.c,$(BUILD)/frontend/%.o,$(FRONTEND_APP_SOURCES))
FRONTEND_QUICK_MENU_OBJECT := $(BUILD)/frontend/libretro_frontend/libretro_frontend_quick_menu.o
FRONTEND_READER_OBJECT := $(BUILD)/frontend/reader/unifrog_reader_ui.o
FRONTEND_OWNED_COMPONENT_OBJECTS := \
	$(FRONTEND_APP_OBJECTS) \
	$(FRONTEND_QUICK_MENU_OBJECT) \
	$(FRONTEND_READER_OBJECT)
