#include <stddef.h>

/*
 * The SDK kernel keeps initcall and filesystem-map references to optional
 * media/NTFS plugins. In the module media build those plugins live outside the
 * boot image, so the base firmware only needs inert placeholders.
 */

struct mountpt_operations {
   void *reserved[16];
};

const struct mountpt_operations ntfs_operations = { { NULL } };

int vidsink_init(void)
{
   return 0;
}

int llav_dis_init(void)
{
   return 0;
}

int viddec_driver_init(void)
{
   return 0;
}

int viddec_driver_exit(void)
{
   return 0;
}

int llav_vdec_init(void)
{
   return 0;
}
