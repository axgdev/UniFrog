#ifndef UNIFROG_JS2300_FRONTEND_H
#define UNIFROG_JS2300_FRONTEND_H

enum js2300_script_mode {
   JS2300_SCRIPT_MODE_STANDALONE = 0,
   JS2300_SCRIPT_MODE_EXTENSION = 1,
};

int js2300_run_script_file(const char *path);
int js2300_run_script_file_ex(const char *path, enum js2300_script_mode mode);

#endif
