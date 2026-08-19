#ifndef UNIFROG_FRONTEND_APP_H
#define UNIFROG_FRONTEND_APP_H

#include <unifrog/gfx.h>

#ifdef __cplusplus
extern "C" {
#endif

struct unifrog_frontend_launch_services;

int unifrog_frontend_app_init(void);
int unifrog_frontend_app_step(void);
int unifrog_frontend_app_running(void);
void unifrog_frontend_app_request_stop(void);
void unifrog_frontend_app_set_launch_services(
   const struct unifrog_frontend_launch_services *services);
struct unifrog_surface unifrog_frontend_app_surface(void);
void unifrog_frontend_app_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif
