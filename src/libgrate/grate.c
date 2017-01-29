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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <errno.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../libhost1x/host1x-private.h"
#include "libgrate-private.h"

#include "grate.h"
#include "host1x.h"

struct grate_bo *grate_bo_create(struct grate *grate, size_t size,
				 unsigned long flags)
{
	struct grate_bo *bo;

	bo = calloc(1, sizeof(*bo));
	if (!bo)
		return NULL;

	bo->bo = host1x_bo_create(grate->host1x, size, 2);
	if (!bo->bo) {
		free(bo);
		return NULL;
	}

	bo->size = size;

	return bo;
}

struct grate_bo *grate_bo_create_from_data(struct grate *grate, size_t size,
					   unsigned long flags,
					   const void *data)
{
	struct grate_bo *bo;
	void *map;

	bo = calloc(1, sizeof(*bo));
	if (!bo)
		return NULL;

	bo->bo = host1x_bo_create(grate->host1x, size, 2);
	if (!bo->bo) {
		free(bo);
		return NULL;
	}

	bo->size = size;

	map = grate_bo_map(bo);
	if (!map) {
		grate_bo_free(bo);
		return NULL;
	}

	memcpy(map, data, size);

	grate_bo_invalidate(bo, size);

	return bo;
}

struct grate_bo *grate_wrap_bo(struct grate_bo *bo, unsigned long offset)
{
	struct grate_bo *wrap;

	if (offset >= bo->size)
		return NULL;

	wrap = calloc(1, sizeof(*wrap));
	if (!wrap)
		return NULL;

	wrap->bo = bo->bo;
	wrap->size = bo->size - offset;
	wrap->offset = bo->offset + offset;

	return wrap;
}

void grate_bo_free(struct grate_bo *bo)
{
	host1x_bo_free(bo->bo);
	free(bo);
}

void *grate_bo_map(struct grate_bo *bo)
{
	void *ptr = NULL;
	int err;

	err = host1x_bo_mmap(bo->bo, &ptr);
	if (err < 0) {
		grate_error("Failed to mmap BO: %s\n", strerror(err));
		return NULL;
	}

	return ptr + bo->offset;
}

void grate_bo_unmap(struct grate_bo *bo, void *ptr)
{
}

void grate_bo_invalidate(struct grate_bo *bo, size_t size)
{
	host1x_bo_invalidate(bo->bo, bo->offset, size);
}

bool grate_parse_command_line(struct grate_options *options, int argc,
			      char *argv[])
{
	static const struct option long_opts[] = {
		{ "fullscreen", 0, NULL, 'f' },
		{ "width", 1, NULL, 'w' },
		{ "height", 1, NULL, 'h' },
		{ "vsync", 0, NULL, 'v' },
	};
	static const char opts[] = "fw:h:v";
	int opt;

	options->fullscreen = false;
	options->vsync = false;
	options->x = 0;
	options->y = 0;
	options->width = 256;
	options->height = 256;

	while ((opt = getopt_long(argc, argv, opts, long_opts, NULL)) != -1) {
		switch (opt) {
		case 'f':
			options->fullscreen = true;
			break;

		case 'w':
			options->width = strtoul(optarg, NULL, 10);
			break;

		case 'h':
			options->height = strtoul(optarg, NULL, 10);
			break;

		case 'v':
			options->vsync = true;
			break;

		default:
			return false;
		}
	}

	return true;
}

struct grate *grate_init(struct grate_options *options)
{
	struct grate *grate;

	grate = calloc(1, sizeof(*grate));
	if (!grate)
		return NULL;

	grate->host1x = host1x_open();
	if (!grate->host1x) {
		free(grate);
		return NULL;
	}

	grate->options = options;

	grate->display = grate_display_open(grate);
	if (grate->display) {
		if (!grate->options->fullscreen)
			grate->overlay = grate_overlay_create(grate->display);

		if (!grate->overlay)
			grate_display_get_resolution(grate->display,
						     &grate->options->width,
						     &grate->options->height);
	}

	return grate;
}

void grate_exit(struct grate *grate)
{
	if (grate)
		host1x_close(grate->host1x);

	free(grate);
}

void grate_bind_framebuffer(struct grate *grate, struct grate_framebuffer *fb)
{
	grate->fb = fb;
}

struct grate_bo * grate_get_front_framebuffer_bo(struct grate_framebuffer *fb)
{
	struct grate_bo *bo = calloc(1, sizeof(struct grate_bo));
	bo->bo = fb->front->pb->bo;
	return bo;
}

struct grate_bo * grate_get_back_framebuffer_bo(struct grate_framebuffer *fb)
{
	struct grate_bo *bo = calloc(1, sizeof(struct grate_bo));
	bo->bo = fb->back->pb->bo;
	return bo;
}

void grate_flush(struct grate *grate)
{
}

struct grate_framebuffer *grate_framebuffer_create(struct grate *grate,
						   unsigned int width,
						   unsigned int height,
						   enum pixel_format format,
						   unsigned long flags)
{
	struct grate_framebuffer *fb;

	fb = calloc(1, sizeof(*fb));
	if (!fb)
		return NULL;

	fb->front = host1x_framebuffer_create(grate->host1x, width, height,
					      format, 0);
	if (!fb->front) {
		free(fb);
		return NULL;
	}

	if (flags & GRATE_DOUBLE_BUFFERED) {
		fb->back = host1x_framebuffer_create(grate->host1x, width,
						     height, format, 0);
		if (!fb->back) {
			host1x_framebuffer_free(fb->front);
			free(fb);
			return NULL;
		}
	}

	return fb;
}

void grate_framebuffer_free(struct grate_framebuffer *fb)
{
	if (fb) {
		host1x_framebuffer_free(fb->front);
		host1x_framebuffer_free(fb->back);
	}

	free(fb);
}

void grate_framebuffer_swap(struct grate_framebuffer *fb)
{
	struct host1x_framebuffer *tmp = fb->front;

	if (fb->back) {
		fb->front = fb->back;
		fb->back = tmp;
	}
}

void grate_framebuffer_save(struct grate *grate,
			    struct grate_framebuffer *fb,
			    const char *path)
{
	if (fb->back)
		host1x_framebuffer_save(grate->host1x, fb->back, path);
	else
		host1x_framebuffer_save(grate->host1x, fb->front, path);
}

void grate_swap_buffers(struct grate *grate)
{
	if (grate->display || grate->overlay) {
		struct grate_options *options = grate->options;

		if (grate->overlay)
			grate_overlay_show(grate->overlay, grate->fb, 0, 0,
					   options->width, options->height,
					   options->vsync);
		else
			grate_display_show(grate->display, grate->fb,
					   options->vsync);
	} else {
		grate_framebuffer_save(grate, grate->fb, "test.png");
	}
}

void grate_wait_for_key(struct grate *grate)
{
	/*
	 * If on-screen display isn't supported, don't wait for key-press
	 * because the output is rendered into a PNG file.
	 */
	if (!grate->display && !grate->overlay)
		return;

	getchar();
}

bool grate_key_pressed(struct grate *grate)
{
	int err, max_fd = STDIN_FILENO;
	struct timeval timeout;
	fd_set fds;

	/*
	 * If on-screen display isn't supported, pretend that a key was
	 * pressed so that the main loop can be exited.
	 */
	if (!grate->display && !grate->overlay)
		return true;

	memset(&timeout, 0, sizeof(timeout));
	timeout.tv_sec = 0;
	timeout.tv_usec = 0;

	FD_ZERO(&fds);
	FD_SET(STDIN_FILENO, &fds);

	err = select(max_fd + 1, &fds, NULL, NULL, &timeout);
	if (err <= 0) {
		if (err < 0) {
			grate_error("select() failed: %m\n");
			return false;
		}

		return false;
	}

	if (FD_ISSET(STDIN_FILENO, &fds))
		return true;

	return false;
}

void *grate_framebuffer_data(struct grate_framebuffer *fb, bool front)
{
	struct host1x_framebuffer *host1x_fb = front ? fb->front : fb->back;
	struct host1x_bo *fb_bo = host1x_fb->pb->bo;
	void *ret;
	int err;

	if (fb_bo == NULL) {
		grate_error("failed to get framebuffer's bo\n");
		return NULL;
	}

	err = host1x_bo_mmap(fb_bo, &ret);
	if (err < 0) {
		grate_error("failed to mmap framebuffer's bo\n");
		return NULL;
	}

	return ret;
}
