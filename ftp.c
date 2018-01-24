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

#include <sys/socket.h>

#include <err.h>
#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "http.h"

static FILE	*ctrl_fp;
static int	 data_fd;

void
ftp_connect(struct url *url, struct url *proxy, int timeout)
{
	char		*buf = NULL;
	size_t		 n = 0;
	int		 sock;

	if (proxy) {
		http_connect(url, proxy, timeout);
		return;
	}

	if ((sock = tcp_connect(url->host, url->port, timeout)) == -1)
		exit(1);

	if ((ctrl_fp = fdopen(sock, "r+")) == NULL)
		err(1, "%s: fdopen", __func__);

	/* greeting */
	if (ftp_getline(&buf, &n, 0, ctrl_fp) != P_OK) {
		warnx("Can't connect to host `%s'", url->host);
		ftp_command(ctrl_fp, "QUIT");
		exit(1);
	}

	free(buf);
	log_info("Connected to %s\n", url->host);
	if (ftp_auth(ctrl_fp, NULL, NULL) != P_OK) {
		warnx("Can't login to host `%s'", url->host);
		ftp_command(ctrl_fp, "QUIT");
		exit(1);
	}
}

struct url *
ftp_get(struct url *url, struct url *proxy, off_t *offset, off_t *sz)
{
	char	*buf = NULL, *dir, *file;

	if (proxy) {
		url = http_get(url, proxy, offset, sz);
		/* this url should now be treated as HTTP */
		url->scheme = S_HTTP;
		return url;
	}

	log_info("Using binary mode to transfer files.\n");
	if (ftp_command(ctrl_fp, "TYPE I") != P_OK)
		errx(1, "Failed to set mode to binary");

	dir = dirname(url->path);
	if (ftp_command(ctrl_fp, "CWD %s", dir) != P_OK)
		errx(1, "CWD command failed");

	log_info("Retrieving %s\n", url->path);
	file = basename(url->path);
	if (strcmp(url->fname, "-"))
		log_info("local: %s remote: %s\n", url->fname, file);
	else
		log_info("remote: %s\n", file);

	if (ftp_size(ctrl_fp, file, sz, &buf) != P_OK) {
		fprintf(stderr, "%s", buf);
		ftp_command(ctrl_fp, "QUIT");
		exit(1);
	}
	free(buf);

	if (activemode)
		data_fd = ftp_eprt(ctrl_fp);
	else if ((data_fd = ftp_epsv(ctrl_fp)) == -1)
		data_fd = ftp_eprt(ctrl_fp);

	if (data_fd == -1)
		errx(1, "Failed to establish data connection");

	if (*offset && ftp_command(ctrl_fp, "REST %lld", *offset) != P_INTER)
		errx(1, "REST command failed");

	if (ftp_command(ctrl_fp, "RETR %s", file) != P_PRE) {
		ftp_command(ctrl_fp, "QUIT");
		exit(1);
	}

	return url;
}

void
ftp_save(struct url *url, FILE *dst_fp, off_t *offset)
{
	struct sockaddr_storage	 ss;
	FILE			*data_fp;
	socklen_t		 len;
	int			 s;

	if (activemode) {
		len = sizeof(ss);
		if ((s = accept(data_fd, (struct sockaddr *)&ss, &len)) == -1)
			err(1, "%s: accept", __func__);

		close(data_fd);
		data_fd = s;
	}

	if ((data_fp = fdopen(data_fd, "r")) == NULL)
		err(1, "%s: fdopen data_fd", __func__);

	copy_file(url, data_fp, dst_fp, offset);
	fclose(data_fp);
}

void
ftp_quit(struct url *url)
{
	char	*buf = NULL;
	size_t	 n = 0;

	if (ftp_getline(&buf, &n, 0, ctrl_fp) != P_OK)
		errx(1, "error retrieving file %s", url->fname);

	free(buf);
	ftp_command(ctrl_fp, "QUIT");
	fclose(ctrl_fp);
}
