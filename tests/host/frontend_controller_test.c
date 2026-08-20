#include "test.h"

#include <unifrog/frontend_controller.h>

struct action_log {
   enum unifrog_frontend_action action;
   unsigned calls;
   char payload[32];
};

static int record_action(void *userdata, enum unifrog_frontend_action action,
   const char *payload)
{
   struct action_log *log = userdata;

   log->action = action;
   log->calls++;
   snprintf(log->payload, sizeof(log->payload), "%s", payload ? payload : "");
   return 0;
}

static int select_action(struct unifrog_frontend_controller *controller,
   enum unifrog_frontend_action action)
{
   for (unsigned i = 0; i < controller->model.count; i++) {
      if (controller->model.items[i].action == action) {
         controller->model.selected = i;
         return 0;
      }
   }
   return -1;
}

static void test_navigation(void)
{
   struct unifrog_frontend_model_settings settings;
   struct unifrog_frontend_controller controller;

   memset(&settings, 0, sizeof(settings));
   settings.theme = "muos";
   settings.language = "english";
   settings.rom_root_label = "ROMs";
   settings.rom_root = "/ROMS";
   settings.active_storage_profile = "wide25";
   settings.configured_storage_profile = "wide25";
   settings.boot_storage_profile = "wide25";
   unifrog_frontend_controller_init(&controller, &settings, NULL);

   TEST_EQ_INT(UNIFROG_FRONTEND_MODEL_LAUNCH, controller.model.screen);
   TEST_EQ_INT(0, select_action(&controller, UNIFROG_FRONTEND_ACTION_CONFIG));
   TEST_EQ_INT(UNIFROG_FRONTEND_ACTION_CONFIG,
      unifrog_frontend_controller_activate(&controller));
   TEST_EQ_INT(UNIFROG_FRONTEND_MODEL_CONFIG, controller.model.screen);
   TEST_EQ_INT(0, select_action(&controller, UNIFROG_FRONTEND_ACTION_STORAGE));
   unifrog_frontend_controller_activate(&controller);
   TEST_EQ_INT(UNIFROG_FRONTEND_MODEL_STORAGE, controller.model.screen);
   TEST_EQ_INT(0,
      select_action(&controller, UNIFROG_FRONTEND_ACTION_STORAGE_MODE));
   unifrog_frontend_controller_activate(&controller);
   TEST_EQ_INT(UNIFROG_FRONTEND_MODEL_STORAGE_MODE, controller.model.screen);
   unifrog_frontend_controller_back(&controller);
   TEST_EQ_INT(UNIFROG_FRONTEND_MODEL_STORAGE, controller.model.screen);
}

static void test_mutation_and_services(void)
{
   struct unifrog_frontend_model_settings settings;
   struct unifrog_frontend_controller_services services;
   struct unifrog_frontend_controller controller;
   struct action_log log;

   memset(&settings, 0, sizeof(settings));
   memset(&log, 0, sizeof(log));
   settings.theme = "muos";
   settings.language = "english";
   settings.rom_root_label = "ROMs";
   settings.rom_root = "/ROMS";
   settings.active_storage_profile = "wide25";
   settings.configured_storage_profile = "wide25";
   settings.boot_storage_profile = "wide25";
   services.action = record_action;
   services.userdata = &log;
   unifrog_frontend_controller_init(&controller, &settings, &services);

   select_action(&controller, UNIFROG_FRONTEND_ACTION_CONFIG);
   unifrog_frontend_controller_activate(&controller);
   select_action(&controller, UNIFROG_FRONTEND_ACTION_INTERFACE);
   unifrog_frontend_controller_activate(&controller);
   TEST_EQ_INT(0, controller.settings.clock_enabled);
   select_action(&controller, UNIFROG_FRONTEND_ACTION_CLOCK);
   unifrog_frontend_controller_activate(&controller);
   TEST_EQ_INT(1, controller.settings.clock_enabled);
   TEST_EQ_INT(1, log.calls);
   TEST_EQ_INT(UNIFROG_FRONTEND_ACTION_CLOCK, log.action);
   TEST_EQ_STR("1", log.payload);

   unifrog_frontend_controller_back(&controller);
   select_action(&controller, UNIFROG_FRONTEND_ACTION_STORAGE);
   unifrog_frontend_controller_activate(&controller);
   select_action(&controller, UNIFROG_FRONTEND_ACTION_STORAGE_MODE);
   unifrog_frontend_controller_activate(&controller);
   TEST_EQ_INT(0,
      select_action(&controller, UNIFROG_FRONTEND_ACTION_STORAGE_PROFILE));
   unifrog_frontend_controller_activate(&controller);
   TEST_EQ_INT(2, log.calls);
   TEST_EQ_INT(UNIFROG_FRONTEND_ACTION_STORAGE_PROFILE, log.action);
   TEST_CHECK(log.payload[0] != '\0');

   unifrog_frontend_controller_back(&controller);
   unifrog_frontend_controller_back(&controller);
   unifrog_frontend_controller_back(&controller);
   select_action(&controller, UNIFROG_FRONTEND_ACTION_EXPLORE_SD);
   unifrog_frontend_controller_activate(&controller);
   TEST_EQ_INT(3, log.calls);
   TEST_EQ_INT(UNIFROG_FRONTEND_ACTION_EXPLORE_SD, log.action);
}

int main(void)
{
   test_navigation();
   test_mutation_and_services();
   return test_finish("frontend controller");
}
