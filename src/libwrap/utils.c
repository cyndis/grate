/*
 * Copyright (c) 2012, 2013 Erik Faye-Lund
 * Copyright (c) 2013 Avionic Design GmbH
 * Copyright (c) 2013 Thierry Reding
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "utils.h"

struct file_table_entry {
	const char *path;

	struct file *(*open)(const char *path, int fd);

	struct list_head list;
};

static LIST_HEAD(file_table);
static LIST_HEAD(files);

void print_hexdump(FILE *fp, int prefix_type, const char *prefix,
		   const void *buffer, size_t size, size_t columns,
		   bool ascii)
{
	const unsigned char *ptr = buffer;
	unsigned int i, j;

	if (!libwrap_verbose)
		return;

	for (j = 0; j < size; j += columns) {
		const char *space = "";

		if (prefix)
			fputs(prefix, fp);

		switch (prefix_type) {
		case DUMP_PREFIX_NONE:
		default:
			break;

		case DUMP_PREFIX_OFFSET:
			fprintf(fp, "%08x: ", j);
			break;

		case DUMP_PREFIX_ADDRESS:
			fprintf(fp, "%p: ", buffer + j);
			break;
		}

		for (i = 0; (i < columns) && (j + i < size); i++) {
			fprintf(fp, "%s%02x", space, ptr[j + i]);
			space = " ";
		}

		for (i = i; i < columns; i++)
			fprintf(fp, "   ");

		fputs(" | ", fp);

		if (ascii) {
			for (i = 0; (i < columns) && (j + i < size); i++) {
				if (isprint(ptr[j + i]))
					fprintf(fp, "%c", ptr[j + i]);
				else
					fprintf(fp, ".");
			}
		}

		fputs("\n", fp);
	}
}

static void file_put(struct file *file)
{
	if (file && file->ops && file->ops->release)
		file->ops->release(file);
}

struct file *file_open(const char *path, int fd)
{
	struct file_table_entry *entry;
	struct file *file;
	unsigned int i;

	list_for_each_entry(entry, &file_table, list) {
		if (strcmp(entry->path, path) == 0) {
			file = entry->open(path, fd);
			if (!file) {
				fprintf(stderr, "failed to wrap `%s'\n", path);
				return NULL;
			}

			for (i = 0; i < ARRAY_SIZE(file->dup_fds); i++)
				file->dup_fds[i] = -1;

			list_add_tail(&file->list, &files);
			return file;
		}
	}

	if (libwrap_verbose)
		fprintf(stderr, "no wrapper for file `%s'\n", path);
	return NULL;
}

struct file *file_lookup(int fd)
{
	struct file *file;
	unsigned int i;

	list_for_each_entry(file, &files, list) {
		if (file->fd == fd)
			return file;

		for (i = 0; i < ARRAY_SIZE(file->dup_fds); i++)
			if (file->dup_fds[i] == fd)
				return file;
	}

	return NULL;
}

struct file *file_find(const char *path)
{
	struct file *file;

	list_for_each_entry(file, &files, list)
		if (strcmp(file->path, path) == 0)
			return file;

	return NULL;
}

void file_close(int fd)
{
	struct file *file;
	unsigned int i;

	list_for_each_entry(file, &files, list) {
		if (file->fd == fd) {
			file->fd = -1;

			for (i = 0; i < ARRAY_SIZE(file->dup_fds); i++)
				if (file->dup_fds[i] >= 0)
					return;

			PRINTF("closing %s\n", file->path);
			list_del(&file->list);
			file_put(file);
			return;
		}

		for (i = 0; i < ARRAY_SIZE(file->dup_fds); i++) {
			if (file->dup_fds[i] != fd)
				continue;

			file->dup_fds[i] = -1;

			if (file->fd != -1)
				return;

			for (i = 0; i < ARRAY_SIZE(file->dup_fds); i++)
				if (file->dup_fds[i] != -1)
					return;

			PRINTF("closing %s\n", file->path);
			list_del(&file->list);
			file_put(file);
			return;
		}
	}
}

void file_table_register(const struct file_table *table, unsigned int count)
{
	unsigned int i;

	for (i = 0; i < count; i++) {
		struct file_table_entry *entry;

		entry = calloc(1, sizeof(*entry));
		if (!entry) {
			fprintf(stderr, "out of memory\n");
			return;
		}

		INIT_LIST_HEAD(&entry->list);
		entry->path = table[i].path;
		entry->open = table[i].open;

		list_add_tail(&entry->list, &file_table);
	}
}

void file_dup(struct file *file, int fd)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(file->dup_fds); i++) {
		if (file->dup_fds[i] < 0) {
			PRINTF("duplicating %s\n", file->path);
			file->dup_fds[i] = fd;
			return;
		}
	}

	fprintf(stderr, "out of FD slots\n");
}

static enum chip_id read_chip_id(const char *path)
{
	FILE *file = fopen(path, "r");
	if (file) {
		unsigned int id = 0;

		if (fscanf(file, "%d", &id) != 1)
			fprintf(stderr, "fscanf failed for %s\n", path);
		fclose(file);

		switch (id) {
		case 0x20:
			return TEGRA20;
		case 0x30:
			return TEGRA30;
		case 0x35:
			return TEGRA114;
		}

		return TEGRA_UNKNOWN;
	}

	return TEGRA_INVALID;
}

enum chip_id tegra_chip_id(void)
{
	const char *path[] =  {
		"/sys/module/tegra_fuse/parameters/tegra_chip_id",
		"/sys/module/fuse/parameters/tegra_chip_id",
		"/sys/devices/soc0/soc_id",
	};
	static enum chip_id id = TEGRA_INVALID;
	unsigned int i;

	if (id != TEGRA_INVALID)
		return id;

	for (i = 0; i < ARRAY_SIZE(path); i++) {
		id = read_chip_id(path[i]);
		if (id != TEGRA_INVALID)
			return id;
	}

	fprintf(stderr, "failed to identify SoC version\n");
	id = TEGRA_UNKNOWN;

	return id;
}
