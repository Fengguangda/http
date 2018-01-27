/*
 * Copyright (c) 2018 Sunil Nimmagadda <sunil@openbsd.org>
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
#include <limits.h>
#include <stdlib.h>
#include <tls.h>

#include "http.h"

#define	DEFAULT_CA_FILE	"/etc/ssl/cert.pem"

static size_t	https_read(const char *, size_t);
static void	https_write(const char *);

static struct tls_config	*tls_config;
static struct tls		*ctx;

static char * const		 tls_verify_opts[] = {
#define HTTP_TLS_CAFILE		0
	"cafile",
#define HTTP_TLS_CAPATH		1
	"capath",
#define HTTP_TLS_CIPHERS	2
	"ciphers",
#define HTTP_TLS_DONTVERIFY	3
	"dont",
#define HTTP_TLS_VERIFYDEPTH	4
	"depth",
#define HTTP_TLS_PROTOCOLS	5
	"protocols",
#define HTTP_TLS_MUSTSTAPLE	6
	"muststaple",
#define HTTP_TLS_NOVERIFYTIME	7
	"noverifytime",
	NULL
};

void
https_init(char *tls_options)
{
	char		*str;
	int		 depth;
	uint32_t	 http_tls_protocols;
	const char	*ca_file = DEFAULT_CA_FILE, *errstr;

	if (tls_init() != 0)
		errx(1, "tls_init failed");

	if ((tls_config = tls_config_new()) == NULL)
		errx(1, "tls_config_new failed");

	while (tls_options && *tls_options) {
		switch (getsubopt(&tls_options, tls_verify_opts, &str)) {
		case HTTP_TLS_CAFILE:
			if (str == NULL)
				errx(1, "missing CA file");
			ca_file = str;
			break;
		case HTTP_TLS_CAPATH:
			if (str == NULL)
				errx(1, "missing ca path");
			if (tls_config_set_ca_path(tls_config, str) != 0)
				errx(1, "tls ca path failed");
			break;
		case HTTP_TLS_CIPHERS:
			if (str == NULL)
				errx(1, "missing cipher list");
			if (tls_config_set_ciphers(tls_config, str) != 0)
				errx(1, "tls set ciphers failed");
			break;
		case HTTP_TLS_DONTVERIFY:
			tls_config_insecure_noverifycert(tls_config);
			tls_config_insecure_noverifyname(tls_config);
			break;
		case HTTP_TLS_PROTOCOLS:
			if (tls_config_parse_protocols(&http_tls_protocols,
			    str) != 0)
				errx(1, "tls parsing protocols failed");
			tls_config_set_protocols(tls_config,
			    http_tls_protocols);
			break;
		case HTTP_TLS_VERIFYDEPTH:
			if (str == NULL)
				errx(1, "missing depth");
			depth = strtonum(str, 0, INT_MAX, &errstr);
			if (errstr)
				errx(1, "Cert validation depth is %s", errstr);
			tls_config_set_verify_depth(tls_config, depth);
			break;
		case HTTP_TLS_MUSTSTAPLE:
			tls_config_ocsp_require_stapling(tls_config);
			break;
		case HTTP_TLS_NOVERIFYTIME:
			tls_config_insecure_noverifytime(tls_config);
			break;
		default:
			errx(1, "Unknown -S suboption `%s'",
			    suboptarg ? suboptarg : "");
		}
	}

	if (tls_config_set_ca_file(tls_config, ca_file) == -1)
		errx(1, "tls_config_set_ca_file failed");
}

void
https_connect(struct url *url, struct url *proxy, int timeout)
{
	const char	*host, *port;
	char		*req;
	int		 code, sock;

	host = proxy ? proxy->host : url->host;
	port = proxy ? proxy->port : url->port;
	if ((sock = tcp_connect(host, port, timeout)) == -1)
		exit(1);

	if (proxy) {
		xasprintf(&req,
		    "CONNECT %s:%s HTTP/1.0\r\n"
		    "User-Agent: %s\r\n"
		    "\r\n",
		    url->host, url->port, ua);

		if ((code = http_request(req, https_read, https_write)) != 200)
			errx(1, "%s: failed to CONNECT to %s:%s: %s",
			    __func__, url->host, url->port, http_error(code));

		free(req);
	}

	if ((ctx = tls_client()) == NULL)
		errx(1, "failed to create tls client");

	if (tls_configure(ctx, tls_config) != 0)
		errx(1, "%s: %s", __func__, tls_error(ctx));

	if (tls_connect_socket(ctx, sock, url->host) != 0)
		errx(1, "%s: %s", __func__, tls_error(ctx));
}

struct url *
https_get(struct url *url, struct url *proxy, off_t *offset, off_t *sz)
{
	return url;
}

void
https_save(struct url *url, FILE *dst_fp, off_t *offset)
{
}

static size_t
https_read(const char *buf, size_t buflen)
{
	return 0;
}

static void
https_write(const char *buf)
{
}
