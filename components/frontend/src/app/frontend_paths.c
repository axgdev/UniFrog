#include "frontend_internal.h"

/* Shared frontend path and filesystem helpers. */
const char *frontend_basename(const char *path)
{
   const char *slash = path ? strrchr(path, '/') : NULL;

   return slash ? slash + 1 : path ? path : "";
}

int frontend_path_join(char *dst, size_t dst_size, const char *base,
   const char *name)
{
   size_t base_len;
   int need_slash;
   int wrote;

   if (!dst || dst_size == 0 || !base || !name)
      return -1;
   base_len = strlen(base);
   need_slash = base_len > 0 && base[base_len - 1u] != '/';
   wrote = snprintf(dst, dst_size, "%s%s%s", base, need_slash ? "/" : "",
      name);
   return wrote > 0 && (size_t)wrote < dst_size ? 0 : -1;
}

int frontend_path_join_ini(char *dst, size_t dst_size, const char *base,
   const char *name)
{
   int wrote;

   if (!dst || dst_size == 0 || !base || !name || !name[0])
      return -1;
   wrote = snprintf(dst, dst_size, "%s/%s.ini", base, name);
   return wrote > 0 && (size_t)wrote < dst_size ? 0 : -1;
}

int frontend_sd_relative_path(char *dst, size_t dst_size, const char *path)
{
   size_t root_len = strlen(FRONTEND_ROOT);

   if (!dst || dst_size == 0 || !path)
      return -1;
   dst[0] = '\0';
   if (strncmp(path, FRONTEND_ROOT, root_len) != 0)
      return -1;
   if (path[root_len] == '\0')
      return -1;
   if (path[root_len] != '/')
      return -1;
   unifrog_text_copy(dst, dst_size, path + root_len + 1u);
   return dst[0] ? 0 : -1;
}

int frontend_path_is_valid(const char *path)
{
   const char *p;

   if (!path || !path[0])
      return 0;
   if (strlen(path) >= FRONTEND_MAX_PATH)
      return 0;
   for (p = path; *p; p++) {
      unsigned char c = (unsigned char)*p;

      if (c < 32 || c == 127)
         return 0;
   }
   if (strstr(path, "/../") || strcmp(path, "..") == 0 ||
       unifrog_text_ends_with_ci(path, "/.."))
      return 0;
   return 1;
}

int frontend_path_has_dir_prefix(const char *path, const char *root)
{
   size_t root_len;

   if (!path || !root)
      return 0;
   root_len = strlen(root);
   return strncmp(path, root, root_len) == 0 &&
      (path[root_len] == '\0' || path[root_len] == '/');
}

int frontend_normalize_path(char *dst, size_t dst_size,
   const char *path)
{
   char tmp[FRONTEND_MAX_PATH];
   const char *src;
   size_t len;
   int wrote;

   if (!dst || dst_size == 0 || !path)
      return -1;
   unifrog_text_copy(tmp, sizeof(tmp), path);
   src = frontend_trim_ascii(tmp);
   if (!frontend_path_is_valid(src))
      return -1;
   len = strlen(src);
   while (len > 1u && src[len - 1u] == '/')
      tmp[--len] = '\0';
   if (strcmp(src, "/") == 0) {
      wrote = snprintf(dst, dst_size, "%s", FRONTEND_ROOT);
      return wrote > 0 && (size_t)wrote < dst_size ? 0 : -1;
   }
   if (frontend_path_has_dir_prefix(src, FRONTEND_ROOT)) {
      wrote = snprintf(dst, dst_size, "%s", src);
      return wrote > 0 && (size_t)wrote < dst_size ? 0 : -1;
   }
   if (src[0] == '/')
      return frontend_path_join(dst, dst_size, FRONTEND_ROOT, src + 1u);
   return frontend_path_join(dst, dst_size, FRONTEND_ROOT, src);
}

void frontend_ensure_data_dirs(void)
{
   (void)mkdir(FRONTEND_DATA_ROOT, 0777);
   (void)mkdir(FRONTEND_DATA_ROOT "/saves", 0777);
   (void)mkdir(FRONTEND_DATA_ROOT "/cache", 0777);
   (void)mkdir(UNIFROG_CONFIG_ROOT, 0777);
   (void)mkdir(UNIFROG_LOG_ROOT, 0777);
   (void)mkdir(UNIFROG_REPORT_ROOT, 0777);
   (void)mkdir(UNIFROG_BUG_REPORT_ROOT, 0777);
   (void)mkdir(UNIFROG_LOG_ROOT "/frontend-driver", 0777);
   (void)mkdir(UNIFROG_SCREENSHOT_ROOT, 0777);
   (void)mkdir(FRONTEND_SCRIPT_ROOT, 0777);
   (void)mkdir(FRONTEND_ARCHIVE_ROOT, 0777);
   (void)mkdir(FRONTEND_THEME_ROOT, 0777);
   (void)mkdir(FRONTEND_LANGUAGE_ROOT, 0777);
   (void)mkdir(UNIFROG_FONT_ROOT, 0777);
   (void)mkdir(UNIFROG_ARTWORK_ROOT, 0777);
   (void)mkdir(FRONTEND_FIRMWARE_ROOT, 0777);
   (void)mkdir(FRONTEND_UPDATE_ROOT, 0777);
   (void)mkdir(FRONTEND_VERSION_ROOT, 0777);
}

int frontend_file_exists(const char *path)
{
   return path && access(path, F_OK) == 0;
}

int frontend_write_text_file(const char *path, const char *text)
{
   FILE *file;
   size_t len;

   if (!path || !text)
      return -1;
   file = fopen(path, "wb");
   if (!file)
      return -1;
   len = strlen(text);
   if (fwrite(text, 1, len, file) != len) {
      fclose(file);
      return -1;
   }
   return fclose(file) == 0 ? 0 : -1;
}

int frontend_mkdir_p(const char *path)
{
   char tmp[FRONTEND_MAX_PATH];
   size_t len;

   if (!path || !path[0])
      return -1;
   unifrog_text_copy(tmp, sizeof(tmp), path);
   len = strlen(tmp);
   while (len > 1u && tmp[len - 1u] == '/')
      tmp[--len] = '\0';
   for (char *p = tmp + 1; *p; p++) {
      if (*p != '/')
         continue;
      *p = '\0';
      if (access(tmp, F_OK) != 0 && mkdir(tmp, 0777) != 0)
         return -1;
      *p = '/';
   }
   if (access(tmp, F_OK) != 0 && mkdir(tmp, 0777) != 0)
      return -1;
   return 0;
}

int frontend_ensure_parent_dir(const char *path)
{
   char tmp[FRONTEND_MAX_PATH];
   char *slash;

   if (!path)
      return -1;
   unifrog_text_copy(tmp, sizeof(tmp), path);
   slash = strrchr(tmp, '/');
   if (!slash)
      return 0;
   *slash = '\0';
   return frontend_mkdir_p(tmp);
}

int frontend_remove_tree(const char *path)
{
   DIR *dir;
   struct dirent *entry;
   int ret = 0;

   if (!path || strlen(path) < strlen(FRONTEND_THEME_ROOT) + 2u)
      return -1;
   dir = opendir(path);
   if (!dir)
      return errno == ENOENT ? 0 : -1;
   while ((entry = readdir(dir)) != NULL) {
      char child[FRONTEND_MAX_PATH];
      struct stat st;

      if (strcmp(entry->d_name, ".") == 0 ||
          strcmp(entry->d_name, "..") == 0)
         continue;
      if (frontend_path_join(child, sizeof(child), path, entry->d_name) != 0) {
         ret = -1;
         continue;
      }
      if (stat(child, &st) == 0 && S_ISDIR(st.st_mode)) {
         if (frontend_remove_tree(child) != 0)
            ret = -1;
      } else if (unlink(child) != 0 && errno != ENOENT) {
         ret = -1;
      }
   }
   if (closedir(dir) != 0)
      ret = -1;
   if (rmdir(path) != 0 && errno != ENOENT)
      ret = -1;
   return ret;
}

int frontend_remove_tree_under(const char *path, const char *root)
{
   DIR *dir;
   struct dirent *entry;
   size_t root_len = root ? strlen(root) : 0;
   int ret = 0;

   if (!path || !root || root_len == 0 ||
       strncmp(path, root, root_len) != 0 ||
       path[root_len] != '/' || path[root_len + 1u] == '\0')
      return -1;
   dir = opendir(path);
   if (!dir)
      return errno == ENOENT ? 0 : -1;
   while ((entry = readdir(dir)) != NULL) {
      char child[FRONTEND_MAX_PATH];
      struct stat st;

      if (strcmp(entry->d_name, ".") == 0 ||
          strcmp(entry->d_name, "..") == 0)
         continue;
      if (frontend_path_join(child, sizeof(child), path, entry->d_name) != 0) {
         ret = -1;
         continue;
      }
      if (stat(child, &st) == 0 && S_ISDIR(st.st_mode)) {
         if (frontend_remove_tree_under(child, root) != 0)
            ret = -1;
      } else if (unlink(child) != 0 && errno != ENOENT) {
         ret = -1;
      }
   }
   if (closedir(dir) != 0)
      ret = -1;
   if (rmdir(path) != 0 && errno != ENOENT)
      ret = -1;
   return ret;
}

int frontend_copy_file_path(const char *src, const char *dst)
{
   FILE *in;
   FILE *out;
   uint8_t buf[8192];
   char temporary[FRONTEND_MAX_PATH];
   int written;
   int ret = -1;

   if (!src || !dst || frontend_ensure_parent_dir(dst) != 0)
      return -1;
   written = snprintf(temporary, sizeof(temporary), "%s.tmp", dst);
   if (written < 0 || (size_t)written >= sizeof(temporary))
      return -1;
   in = fopen(src, "rb");
   if (!in)
      return -1;
   out = fopen(temporary, "wb");
   if (!out) {
      fclose(in);
      return -1;
   }
   for (;;) {
      size_t got = fread(buf, 1, sizeof(buf), in);

      if (got > 0 && fwrite(buf, 1, got, out) != got)
         goto out;
      if (got < sizeof(buf)) {
         if (ferror(in))
            goto out;
         break;
      }
   }
   ret = 0;

out:
   if (fclose(out) != 0)
      ret = -1;
   if (fclose(in) != 0)
      ret = -1;
   if (ret == 0)
      ret = unifrog_config_commit(temporary, dst);
   if (ret != 0)
      (void)unlink(temporary);
   return ret;
}

int frontend_copy_tree_merge(const char *src, const char *dst,
   int (*skip_name)(const char *name))
{
   DIR *dir;
   struct dirent *entry;
   int ret = 0;

   if (!src || !dst || frontend_mkdir_p(dst) != 0)
      return -1;
   dir = opendir(src);
   if (!dir)
      return -1;
   while ((entry = readdir(dir)) != NULL) {
      char src_child[FRONTEND_MAX_PATH];
      char dst_child[FRONTEND_MAX_PATH];
      struct stat st;

      if (strcmp(entry->d_name, ".") == 0 ||
          strcmp(entry->d_name, "..") == 0)
         continue;
      if (skip_name && skip_name(entry->d_name))
         continue;
      if (frontend_path_join(src_child, sizeof(src_child), src, entry->d_name) != 0 ||
          frontend_path_join(dst_child, sizeof(dst_child), dst, entry->d_name) != 0) {
         ret = -1;
         continue;
      }
      if (stat(src_child, &st) != 0) {
         ret = -1;
         continue;
      }
      if (S_ISDIR(st.st_mode)) {
         if (frontend_copy_tree_merge(src_child, dst_child, skip_name) != 0)
            ret = -1;
      } else if (S_ISREG(st.st_mode)) {
         if (frontend_copy_file_path(src_child, dst_child) != 0)
            ret = -1;
      }
   }
   if (closedir(dir) != 0)
      ret = -1;
   return ret;
}
