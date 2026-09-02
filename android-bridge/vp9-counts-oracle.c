/* SPDX-License-Identifier: MIT */
/*
 * Run the pure VP9GetCounts() routine exported by Allwinner's Android HAL.
 *
 * This is a reverse-engineering aid only: it turns the H618's 0x3398-byte
 * hardware counter image into the HAL's software counter structure so that
 * the native driver can be checked against the vendor implementation.
 */
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define AUX_SIZE       0x88000
#define COUNTS_OFFSET  0x4b00
#define COUNTS_SIZE    0x3398
#define HAL_INPUT_OFF  0x710
#define HAL_BUFFER_SIZE 0x4000

typedef void (*get_counts_fn)(void *output, const void *input);

static int read_file(const char *path, unsigned char **data, size_t *size)
{
	FILE *file;
	long length;

	file = fopen(path, "rb");
	if (!file)
		return -1;
	if (fseek(file, 0, SEEK_END) || (length = ftell(file)) < 0 ||
	    fseek(file, 0, SEEK_SET)) {
		fclose(file);
		return -1;
	}
	*data = malloc((size_t)length);
	if (!*data || fread(*data, 1, (size_t)length, file) != (size_t)length) {
		free(*data);
		fclose(file);
		return -1;
	}
	fclose(file);
	*size = (size_t)length;
	return 0;
}

int main(int argc, char **argv)
{
	unsigned char *file_data = NULL;
	unsigned char *input = NULL;
	unsigned char *output = NULL;
	const unsigned char *counts;
	get_counts_fn get_counts = NULL;
	size_t file_size;
	void *library;
	FILE *file;

	if (argc != 4) {
		fprintf(stderr, "usage: %s libawvp9HwAL.so aux-or-counts.bin output.bin\n",
			argv[0]);
		return 64;
	}
	if (read_file(argv[2], &file_data, &file_size)) {
		perror(argv[2]);
		return 66;
	}
	if (file_size == AUX_SIZE)
		counts = file_data + COUNTS_OFFSET;
	else if (file_size == COUNTS_SIZE)
		counts = file_data;
	else {
		fprintf(stderr, "unexpected input size %zu (need %#x or %#x)\n",
			file_size, AUX_SIZE, COUNTS_SIZE);
		return 65;
	}

	library = dlopen(argv[1], RTLD_LAZY | RTLD_LOCAL);
	if (!library) {
		fprintf(stderr, "dlopen: %s\n", dlerror());
		return 1;
	}
	*(void **)(&get_counts) = dlsym(library, "VP9GetCounts");
	if (!get_counts) {
		fprintf(stderr, "dlsym: %s\n", dlerror());
		return 2;
	}
	input = calloc(1, HAL_BUFFER_SIZE);
	output = calloc(1, HAL_BUFFER_SIZE);
	if (!input || !output)
		return 70;
	memcpy(input + HAL_INPUT_OFF, counts, COUNTS_SIZE);
	get_counts(output, input);

	file = fopen(argv[3], "wb");
	if (!file || fwrite(output, 1, HAL_BUFFER_SIZE, file) != HAL_BUFFER_SIZE ||
	    fclose(file)) {
		perror(argv[3]);
		return 73;
	}

	free(output);
	free(input);
	free(file_data);
	dlclose(library);
	return 0;
}
