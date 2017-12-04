/*
 * Copyright (c) 2015 Sunil Nimmagadda <sunil@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <err.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>

#include "http.h"

struct imsgbuf;

static FILE	*src_fp;

struct url *
file_request(struct imsgbuf *ibuf, struct url *url)
{
	int	src_fd;

	if ((src_fd = fd_request(ibuf, url->path, O_RDONLY, NULL)) == -1)
		exit(1);

	if ((src_fp = fdopen(src_fd, "r")) == NULL)
		err(1, "%s: fdopen", __func__);

	return url;
}

void
file_save(struct url *url, FILE *dst_fp)
{
	copy_file(url, src_fp, dst_fp);
	fclose(src_fp);
}
