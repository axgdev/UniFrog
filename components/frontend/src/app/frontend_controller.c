#include <unifrog/frontend_controller.h>

#include <stdio.h>
#include <string.h>

static void controller_build(struct unifrog_frontend_controller *controller,
   enum unifrog_frontend_model_screen screen)
{
   unifrog_frontend_model_build(&controller->model, screen,
      &controller->settings);
}

static void controller_notify(struct unifrog_frontend_controller *controller,
   enum unifrog_frontend_action action, const char *payload)
{
   if (controller->services.action)
      (void)controller->services.action(controller->services.userdata,
         action, payload ? payload : "");
}

static int controller_toggle(struct unifrog_frontend_controller *controller,
   enum unifrog_frontend_action action)
{
   int *value = NULL;

   switch (action) {
   case UNIFROG_FRONTEND_ACTION_SORT:
      value = &controller->settings.sort_desc;
      break;
   case UNIFROG_FRONTEND_ACTION_CLOCK:
      value = &controller->settings.clock_enabled;
      break;
   case UNIFROG_FRONTEND_ACTION_TITLE_ROOT:
      value = &controller->settings.title_include_root;
      break;
   case UNIFROG_FRONTEND_ACTION_COUNTER_FOLDER:
      value = &controller->settings.menu_counter_folder;
      break;
   case UNIFROG_FRONTEND_ACTION_COUNTER_FILE:
      value = &controller->settings.menu_counter_file;
      break;
   case UNIFROG_FRONTEND_ACTION_HIDDEN:
      value = &controller->settings.show_hidden;
      break;
   case UNIFROG_FRONTEND_ACTION_CONTENT_COLLECT:
      value = &controller->settings.content_collect;
      break;
   case UNIFROG_FRONTEND_ACTION_CONTENT_HISTORY:
      value = &controller->settings.content_history;
      break;
   case UNIFROG_FRONTEND_ACTION_MIXED_CONTENT:
      value = &controller->settings.mixed_content;
      break;
   default:
      return 0;
   }
   *value = !*value;
   controller_notify(controller, action, *value ? "1" : "0");
   controller_build(controller, controller->model.screen);
   return 1;
}

void unifrog_frontend_controller_init(
   struct unifrog_frontend_controller *controller,
   const struct unifrog_frontend_model_settings *settings,
   const struct unifrog_frontend_controller_services *services)
{
   if (!controller)
      return;
   memset(controller, 0, sizeof(*controller));
   if (settings)
      controller->settings = *settings;
   if (services)
      controller->services = *services;
   snprintf(controller->configured_storage_profile,
      sizeof(controller->configured_storage_profile), "%s",
      controller->settings.configured_storage_profile ?
      controller->settings.configured_storage_profile : "boot");
   controller->settings.configured_storage_profile =
      controller->configured_storage_profile;
   controller_build(controller, UNIFROG_FRONTEND_MODEL_LAUNCH);
}

void unifrog_frontend_controller_move(
   struct unifrog_frontend_controller *controller, int direction)
{
   if (controller)
      unifrog_frontend_model_move(&controller->model, direction);
}

enum unifrog_frontend_action unifrog_frontend_controller_activate(
   struct unifrog_frontend_controller *controller)
{
   const struct unifrog_frontend_model_item *item;
   enum unifrog_frontend_action action;

   if (!controller || controller->model.selected >= controller->model.count)
      return UNIFROG_FRONTEND_ACTION_NONE;
   item = &controller->model.items[controller->model.selected];
   action = item->action;
   switch (action) {
   case UNIFROG_FRONTEND_ACTION_CONFIG:
      controller_build(controller, UNIFROG_FRONTEND_MODEL_CONFIG);
      return action;
   case UNIFROG_FRONTEND_ACTION_INTERFACE:
      controller_build(controller, UNIFROG_FRONTEND_MODEL_VISUAL);
      return action;
   case UNIFROG_FRONTEND_ACTION_STORAGE:
      controller_build(controller, UNIFROG_FRONTEND_MODEL_STORAGE);
      return action;
   case UNIFROG_FRONTEND_ACTION_STORAGE_MODE:
      controller_build(controller, UNIFROG_FRONTEND_MODEL_STORAGE_MODE);
      return action;
   case UNIFROG_FRONTEND_ACTION_BACK_STORAGE:
      controller_build(controller, UNIFROG_FRONTEND_MODEL_STORAGE);
      return action;
   case UNIFROG_FRONTEND_ACTION_STORAGE_PROFILE:
      snprintf(controller->configured_storage_profile,
         sizeof(controller->configured_storage_profile), "%s",
         item->payload[0] ? item->payload : "boot");
      controller_notify(controller, action,
         controller->configured_storage_profile);
      controller_build(controller, UNIFROG_FRONTEND_MODEL_STORAGE_MODE);
      return action;
   default:
      break;
   }
   if (controller_toggle(controller, action))
      return action;
   if (action != UNIFROG_FRONTEND_ACTION_NONE)
      controller_notify(controller, action, item->payload);
   return action;
}

void unifrog_frontend_controller_back(
   struct unifrog_frontend_controller *controller)
{
   if (!controller)
      return;
   switch (controller->model.screen) {
   case UNIFROG_FRONTEND_MODEL_STORAGE_MODE:
      controller_build(controller, UNIFROG_FRONTEND_MODEL_STORAGE);
      break;
   case UNIFROG_FRONTEND_MODEL_VISUAL:
   case UNIFROG_FRONTEND_MODEL_STORAGE:
      controller_build(controller, UNIFROG_FRONTEND_MODEL_CONFIG);
      break;
   case UNIFROG_FRONTEND_MODEL_CONFIG:
      controller_build(controller, UNIFROG_FRONTEND_MODEL_LAUNCH);
      break;
   default:
      break;
   }
}
