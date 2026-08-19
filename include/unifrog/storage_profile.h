#ifndef UNIFROG_STORAGE_PROFILE_H
#define UNIFROG_STORAGE_PROFILE_H

#ifdef __cplusplus
extern "C" {
#endif

struct unifrog_storage_profile_info {
   const char *name;
   const char *label;
   const char *bus_width;
   const char *timing;
   const char *signal;
};

const struct unifrog_storage_profile_info *unifrog_storage_profile_info(
   const char *profile);
unsigned unifrog_storage_profile_count(void);
const char *unifrog_storage_profile_name(unsigned index);

#ifdef __cplusplus
}
#endif

#endif
