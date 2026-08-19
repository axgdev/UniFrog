#ifndef UNIFROG_READER_H
#define UNIFROG_READER_H

#ifdef __cplusplus
extern "C" {
#endif

int unifrog_reader_path_supported(const char *path);
int unifrog_reader_path_is_image(const char *path);
int unifrog_reader_path_is_text(const char *path);
int unifrog_reader_path_is_archive(const char *path);
int unifrog_reader_run(const char *path);

#ifdef __cplusplus
}
#endif

#endif
