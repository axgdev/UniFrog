#ifndef UNIFROG_ZIP_H
#define UNIFROG_ZIP_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Read-only ZIP support for CBZ/EPUB.
 * Only the classic EOCD / central-directory layout is accepted.
 * ZIP64, encryption, unsafe paths, and malformed metadata are rejected.
 */
struct unifrog_zip_entry {
   const char *name;
   uint16_t flags;
   uint16_t method;
   uint32_t crc32;
   uint32_t compressed_size;
   uint32_t uncompressed_size;
   uint32_t local_offset;
};

struct unifrog_zip_archive {
   void *impl;
};

/* Streaming, store-only ZIP writer used for diagnostic bundles. It keeps only
 * central-directory metadata in memory and never buffers a complete file. */
struct unifrog_zip_writer {
   void *impl;
};

int unifrog_zip_open_path(const char *path, struct unifrog_zip_archive *zip);
int unifrog_zip_open_file(FILE *file, struct unifrog_zip_archive *zip);
void unifrog_zip_close(struct unifrog_zip_archive *zip);

size_t unifrog_zip_entry_count(const struct unifrog_zip_archive *zip);
const struct unifrog_zip_entry *unifrog_zip_entries(
   const struct unifrog_zip_archive *zip);
const struct unifrog_zip_entry *unifrog_zip_find(
   const struct unifrog_zip_archive *zip, const char *name);

int unifrog_zip_extract_entry_to_file(const struct unifrog_zip_archive *zip,
   const struct unifrog_zip_entry *entry, FILE *out);
int unifrog_zip_extract_name_to_file(const struct unifrog_zip_archive *zip,
   const char *name, FILE *out);
int unifrog_zip_extract_name_to_path(const struct unifrog_zip_archive *zip,
   const char *name, const char *out_path);

int unifrog_zip_writer_open_path(const char *path,
   struct unifrog_zip_writer *writer);
int unifrog_zip_writer_add_data(struct unifrog_zip_writer *writer,
   const char *name, const void *data, size_t size);
int unifrog_zip_writer_add_path(struct unifrog_zip_writer *writer,
   const char *name, const char *path, size_t maximum_size);
int unifrog_zip_writer_close(struct unifrog_zip_writer *writer);
void unifrog_zip_writer_abort(struct unifrog_zip_writer *writer);

#ifdef __cplusplus
}
#endif

#endif
