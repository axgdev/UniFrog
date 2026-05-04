/* Copyright (C) 2023 Nikita Burnashev

Permission to use, copy, modify, and/or distribute this software
for any purpose with or without fee is hereby granted.

THIS SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND! */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define IMAGE_START 0x1000
#define LCFG_HEADER_SIZE 0x200
#define LCFG_SIZE_OFF 0x184
#define LCFG_CRC_OFF 0x18c

static uint32_t crc32_mpeg2(const uint8_t *buf, size_t sz)
{
	uint32_t tab[256], c;
	size_t i;
	int j;

	for (i = 0; i < 256; i++) {
		c = (uint32_t)i << 24;
		for (j = 0; j < 8; j++)
			c = c & (1u << 31) ? c << 1 ^ 0x04c11db7 : c << 1;
		tab[i] = c;
	}

	c = 0xffffffff;
	for (i = 0; i < sz; i++)
		c = c << 8 ^ tab[(c >> 24) ^ buf[i]];

	return c;
}

static uint32_t read_le32(const uint8_t *buf)
{
	return (uint32_t)buf[0] |
		((uint32_t)buf[1] << 8) |
		((uint32_t)buf[2] << 16) |
		((uint32_t)buf[3] << 24);
}

static void write_le32(uint8_t *buf, uint32_t value)
{
	buf[0] = value & 255;
	buf[1] = (value >> 8) & 255;
	buf[2] = (value >> 16) & 255;
	buf[3] = (value >> 24) & 255;
}

static int read_file(const char *path, uint8_t **buf_out, size_t *sz_out)
{
	FILE *pf;
	long end;
	uint8_t *buf;

	pf = fopen(path, "rb");
	if (pf == NULL) {
		fprintf(stderr, "Cannot open %s for reading\n", path);
		return -1;
	}
	if (fseek(pf, 0, SEEK_END) != 0) {
		fprintf(stderr, "Cannot seek %s\n", path);
		fclose(pf);
		return -1;
	}
	end = ftell(pf);
	if (end < 0) {
		fprintf(stderr, "Cannot get size of %s\n", path);
		fclose(pf);
		return -1;
	}
	if (fseek(pf, 0, SEEK_SET) != 0) {
		fprintf(stderr, "Cannot rewind %s\n", path);
		fclose(pf);
		return -1;
	}

	buf = malloc((size_t)end);
	if (buf == NULL && end != 0) {
		fprintf(stderr, "Out of memory reading %s\n", path);
		fclose(pf);
		return -1;
	}
	if (fread(buf, 1, (size_t)end, pf) != (size_t)end) {
		fprintf(stderr, "Cannot read %s\n", path);
		free(buf);
		fclose(pf);
		return -1;
	}
	fclose(pf);

	*buf_out = buf;
	*sz_out = (size_t)end;
	return 0;
}

static int pack_file(const char *in_path, const char *out_path)
{
	uint8_t *in_buf = NULL;
	uint8_t *out_buf;
	size_t in_sz, out_sz, payload_sz;
	uint32_t crc;
	FILE *pf;
	int rc = EXIT_FAILURE;

	if (read_file(in_path, &in_buf, &in_sz) != 0)
		return EXIT_FAILURE;

	out_sz = IMAGE_START + in_sz;
	payload_sz = out_sz - LCFG_HEADER_SIZE;
	out_buf = calloc(1, out_sz);
	if (out_buf == NULL) {
		fprintf(stderr, "Out of memory building %s\n", out_path);
		goto out;
	}

	memcpy(out_buf + IMAGE_START, in_buf, in_sz);
	out_buf[0] = 'L';
	out_buf[1] = 'C';
	out_buf[2] = 'F';
	out_buf[3] = 'G';
	write_le32(out_buf + LCFG_SIZE_OFF, (uint32_t)payload_sz);

	crc = crc32_mpeg2(out_buf + LCFG_HEADER_SIZE, payload_sz);
	write_le32(out_buf + LCFG_CRC_OFF, crc);

	pf = fopen(out_path, "wb");
	if (pf == NULL) {
		fprintf(stderr, "Cannot open %s for writing\n", out_path);
		free(out_buf);
		goto out;
	}
	if (fwrite(out_buf, 1, out_sz, pf) != out_sz) {
		fprintf(stderr, "Cannot write %s\n", out_path);
		fclose(pf);
		free(out_buf);
		goto out;
	}
	if (fclose(pf) != 0) {
		fprintf(stderr, "Cannot close %s\n", out_path);
		free(out_buf);
		goto out;
	}

	free(out_buf);
	rc = EXIT_SUCCESS;

out:
	free(in_buf);
	return rc;
}

static int check_file(const char *path)
{
	uint8_t *buf = NULL;
	size_t sz, payload_sz;
	uint32_t stored_payload, stored_crc, crc;
	int rc = EXIT_FAILURE;

	if (read_file(path, &buf, &sz) != 0)
		return EXIT_FAILURE;

	if (sz < IMAGE_START) {
		fprintf(stderr, "%s: too small for LCFG image\n", path);
		goto out;
	}
	if (memcmp(buf, "LCFG", 4) != 0) {
		fprintf(stderr, "%s: missing LCFG magic\n", path);
		goto out;
	}

	payload_sz = sz - LCFG_HEADER_SIZE;
	stored_payload = read_le32(buf + LCFG_SIZE_OFF);
	if (stored_payload != payload_sz) {
		fprintf(stderr, "%s: payload size mismatch: header=%u actual=%zu\n",
			path, stored_payload, payload_sz);
		goto out;
	}

	stored_crc = read_le32(buf + LCFG_CRC_OFF);
	crc = crc32_mpeg2(buf + LCFG_HEADER_SIZE, payload_sz);
	if (stored_crc != crc) {
		fprintf(stderr, "%s: CRC mismatch: header=0x%08x actual=0x%08x\n",
			path, stored_crc, crc);
		goto out;
	}

	printf("%s: OK size=%zu payload=%zu crc=0x%08x\n",
		path, sz, payload_sz, crc);
	rc = EXIT_SUCCESS;

out:
	free(buf);
	return rc;
}

static void usage(const char *argv0)
{
	fprintf(stderr, "Usage: %s <input.bin> <output.asd>\n", argv0);
	fprintf(stderr, "       %s --check <input.asd>\n", argv0);
}

int main(int argc, char *argv[])
{
	if (argc == 3 && strcmp(argv[1], "--check") == 0)
		return check_file(argv[2]);
	if (argc == 3)
		return pack_file(argv[1], argv[2]);

	usage(argv[0]);
	return EXIT_FAILURE;
}
