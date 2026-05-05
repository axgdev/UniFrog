/*
 * The SDK kernel keeps initcall and filesystem-map references to optional
 * media/NTFS plugins. In the module media build those plugins live outside the
 * boot image, so the base firmware only needs inert placeholders.
 *
 * Keep libviddrv linked in the base firmware so /dev/dis and normal display
 * controls still exist before the media module is loaded. Only the heavier
 * image sink and NTFS hooks remain stubbed here.
 */

struct mountpt_operations {
   void *reserved[19];
};

const struct mountpt_operations ntfs_operations = { { 0 } };

int vidsink_init(void)
{
   return 0;
}
