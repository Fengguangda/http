/*
 * Copyright (c) 2017 Sunil Nimmagadda <sunil@openbsd.org>
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

/*-
 * Copyright (c) 1997 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Jason Thorpe and Luke Mewburn.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <ctype.h>
#include <err.h>
#include <libgen.h>
#include <limits.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "http.h"

#ifndef nitems
#define nitems(_a)	(sizeof((_a)) / sizeof((_a)[0]))
#endif

static int	unsafe_char(const char *);
static int	scheme_lookup(const char *);

static int
scheme_lookup(const char *str)
{
	const char	*s;
	size_t		 i;
	int		 scheme;

	scheme = -1;
	for (i = 0; i < nitems(scheme_str); i++) {
		s = scheme_str[i];
		if (strncasecmp(str, s, strlen(s)) == 0) {
			scheme = i;
			break;
		}
	}

	return scheme;
}

struct url *
url_parse(char *str)
{
	struct url	*url;
	char		*host, *port, *path, *p, *q, *r;
	size_t		 len;
	int		 scheme;

	host = port = path = NULL;
	p = str;
	while (isblank((unsigned char)*p))
		p++;

	/* Scheme */
	if ((q = strchr(p, ':')) == NULL)
		errx(1, "%s: scheme missing: %s", __func__, str);

	if ((scheme = scheme_lookup(p)) == -1)
		errx(1, "%s: invalid scheme: %s", __func__, p);

	/* Authority */
	p = ++q;
	if (strncmp(p, "//", 2) != 0)
		goto done;

	p += 2;
 	/* userinfo */
 	if ((q = strchr(p, '@')) != NULL) {
		warnx("%s: Ignoring deprecated userinfo", __func__);
		p = ++q;
 	}

	/* terminated by a '/' if present */
	if ((q = strchr(p, '/')) != NULL)
		p = xstrndup(p, q - p, __func__);

	/* Port */
	if ((r = strchr(p, ':')) != NULL) {
		*r++ = '\0';
		len = strlen(r);
		if (len > NI_MAXSERV)
			errx(1, "%s: port too long", __func__);
		if (len > 0)
			port = xstrdup(r, __func__);
	}
	/* assign default port */
	if (port == NULL && scheme != S_FILE)
		port = xstrdup(port_str[scheme], __func__);

	/* Host */
	len = strlen(p);
	if (len > HOST_NAME_MAX + 1)
		errx(1, "%s: hostname too long", __func__);
	if (len > 0)
		host = xstrdup(p, __func__);

	if (q != NULL)
		free(p);

 done:
	/* Path */
	if (q != NULL)
		path = xstrdup(q, __func__);

	if (http_debug) {
		fprintf(stderr,
		    "scheme: %s\nhost: %s\nport: %s\npath: %s\n",
		    scheme_str[scheme], host, port, path);
	}

	if ((url = calloc(1, sizeof *url)) == NULL)
		err(1, "%s: malloc", __func__);

	url->scheme = scheme;
	url->host = host;
	url->port = port;
	url->path = path;
	return url;
}

void
url_free(struct url *url)
{
	if (url == NULL)
		return;

	free(url->host);
	free(url->port);
	free(url->path);
	free(url->fname);
	free(url);
}

void
url_connect(struct url *url, struct url *proxy, int timeout)
{
	switch (url->scheme) {
	case S_HTTP:
	case S_HTTPS:
		http_connect(url, proxy, timeout);
		break;
	case S_FTP:
		if (proxy)
			http_connect(url, proxy, timeout);
		else
			ftp_connect(url, NULL, timeout);
		break;
	case S_FILE:
		file_connect(&child_ibuf, &child_imsg, url);
		break;
	}
}

struct url *
url_request(struct url *url, struct url *proxy)
{
	switch (url->scheme) {
	case S_HTTP:
	case S_HTTPS:
		log_request("Requesting", url, proxy);
		return http_get(url, proxy);
	case S_FTP:
		return proxy ? http_get(url, proxy) : ftp_get(url);
	case S_FILE:
		return file_request(&child_ibuf, &child_imsg, url);
	}

	errx(1, "%s: Invalid scheme", __func__);
}

void
url_save(struct url *url, struct url *proxy, const char *title, int pm, int fd)
{
	FILE		*dst_fp;
	const char	*fname;

	fname = strcmp(url->fname, "-") == 0 ?
	    basename(url->path) : basename(url->fname);

	if (pm)
		start_progress_meter(fname, title, url->file_sz, &url->offset);

	if ((dst_fp = fdopen(fd, "w")) == NULL)
		err(1, "%s: fdopen", __func__);

	switch (url->scheme) {
	case S_HTTP:
	case S_HTTPS:
		http_save(url, dst_fp);
		break;
	case S_FTP:
		proxy ? http_save(url, dst_fp) : ftp_save(url, dst_fp);
		break;
	case S_FILE:
		file_save(url, dst_fp);
		break;
	}

 	fclose(dst_fp);
	if (pm)
		stop_progress_meter();

	if (url->scheme == S_FTP)
		ftp_quit(url);
}

/*
 * Encode given URL, per RFC1738.
 * Allocate and return string to the caller.
 */
char *
url_encode(const char *path)
{
	size_t i, length, new_length;
	char *epath, *epathp;

	length = new_length = strlen(path);

	/*
	 * First pass:
	 * Count unsafe characters, and determine length of the
	 * final URL.
	 */
	for (i = 0; i < length; i++)
		if (unsafe_char(path + i))
			new_length += 2;

	epath = epathp = malloc(new_length + 1);	/* One more for '\0'. */
	if (epath == NULL)
		err(1, "Can't allocate memory for URL encoding");

	/*
	 * Second pass:
	 * Encode, and copy final URL.
	 */
	for (i = 0; i < length; i++)
		if (unsafe_char(path + i)) {
			snprintf(epathp, 4, "%%" "%02x",
			    (unsigned char)path[i]);
			epathp += 3;
		} else
			*(epathp++) = path[i];

	*epathp = '\0';
	return epath;
}

/*
 * Determine whether the character needs encoding, per RFC1738:
 * 	- No corresponding graphic US-ASCII.
 * 	- Unsafe characters.
 */
static int
unsafe_char(const char *c0)
{
	const char *unsafe_chars = " <>\"#{}|\\^~[]`";
	const unsigned char *c = (const unsigned char *)c0;

	/*
	 * No corresponding graphic US-ASCII.
	 * Control characters and octets not used in US-ASCII.
	 */
	return (iscntrl(*c) || !isascii(*c) ||

	    /*
	     * Unsafe characters.
	     * '%' is also unsafe, if is not followed by two
	     * hexadecimal digits.
	     */
	    strchr(unsafe_chars, *c) != NULL ||
	    (*c == '%' && (!isxdigit(*++c) || !isxdigit(*++c))));
}
