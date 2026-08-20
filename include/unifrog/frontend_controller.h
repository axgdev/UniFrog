#ifndef UNIFROG_FRONTEND_CONTROLLER_H
#define UNIFROG_FRONTEND_CONTROLLER_H

#include <unifrog/frontend_model.h>

#ifdef __cplusplus
extern "C" {
#endif

#define UNIFROG_FRONTEND_CONTROLLER_PROFILE_MAX 16u

typedef int (*unifrog_frontend_controller_action_cb)(void *userdata,
   enum unifrog_frontend_action action, const char *payload);

struct unifrog_frontend_controller_services {
   unifrog_frontend_controller_action_cb action;
   void *userdata;
};

struct unifrog_frontend_controller {
   struct unifrog_frontend_model_settings settings;
   struct unifrog_frontend_model model;
   struct unifrog_frontend_controller_services services;
   char configured_storage_profile[UNIFROG_FRONTEND_CONTROLLER_PROFILE_MAX];
};

void unifrog_frontend_controller_init(
   struct unifrog_frontend_controller *controller,
   const struct unifrog_frontend_model_settings *settings,
   const struct unifrog_frontend_controller_services *services);
void unifrog_frontend_controller_move(
   struct unifrog_frontend_controller *controller, int direction);
enum unifrog_frontend_action unifrog_frontend_controller_activate(
   struct unifrog_frontend_controller *controller);
void unifrog_frontend_controller_back(
   struct unifrog_frontend_controller *controller);

#ifdef __cplusplus
}
#endif

#endif
