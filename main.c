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

#include <sys/cdefs.h>
#include <sys/types.h>
#include <sys/queue.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/wait.h>

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <imsg.h>
#include <libgen.h>
#include <limits.h>
#include <netdb.h>
#include <resolv.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "http.h"

#define	HTTP	0
#define HTTPS	1
#define FTP	2
const char	*scheme_str[] = { "http", "https", "ftp" };

static void	child(int, int, char **);
static void	env_parse(void);
static int	parent(int, pid_t, int, char **);
static int	read_message(struct imsgbuf *, struct imsg *, pid_t);
static void	send_message(struct imsgbuf *, int, void *, size_t, int);
static void	url_connect(struct url *);
static void	url_request(struct url *);
static void	url_save(struct url *, int);
__dead void	usage(void);

enum {
	IMSG_STAT,
	IMSG_OPEN
};

struct open_req {
	char	fname[FILENAME_MAX];
	int	append;
};

const char	*ua = "OpenBSD http";
struct url	*proxy;
int		 http_debug;

static char	*oarg;
static char	*tls_options;
static int	 resume;
static int	 progressmeter = 1;
static int	 verbose = 1;

int
main(int argc, char **argv)
{
	char	*Darg = NULL;
	int	 ch, sp[2];
	pid_t	 pid;

	while ((ch = getopt(argc, argv, "aCD:o:mMS:U:V")) != -1) {
		switch (ch) {
		case 'C':
			resume = 1;
			break;
		case 'D':
			Darg = optarg;
			break;
		case 'o':
			oarg = optarg;
			break;
		case 'M':
			progressmeter = 0;
			break;
		case 'S':
			tls_options = optarg;
			break;
		case 'U':
			ua = optarg;
			break;
		case 'V':
			verbose = 0;
			break;
		/* options for compatibility, on by default */
		case 'a':
		case 'm':
			break;
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	if (progressmeter) {
		init_progress_meter(Darg, verbose);
		if (pledge("stdio cpath wpath rpath inet dns recvfd sendfd proc tty",
		    NULL) == -1)
			err(1, "pledge");
	} else {
		if (pledge("stdio cpath wpath rpath inet dns recvfd sendfd proc",
		    NULL) == -1)
			err(1, "pledge");
	}

	if (argc == 0)
		usage();

	if (oarg && argc > 1)
		errx(1, "Can't use -o with multiple urls");

	env_parse();
	if (socketpair(AF_UNIX, SOCK_STREAM, PF_UNSPEC, sp) != 0)
		err(1, "socketpair");

	switch (pid = fork()) {
	case -1:
		err(1, "fork");
	case 0:
		close(sp[0]);
		child(sp[1], argc, argv);
	}

	close(sp[1]);
	return parent(sp[0], pid, argc, argv);
}

static int
parent(int sock, pid_t child_pid, int argc, char **argv)
{
	struct stat	 sb;
	struct imsgbuf	 ibuf;
	struct imsg	 imsg;
	struct open_req	*req;
	const char	*fn;
	size_t		 datalen;
	off_t		 offset;
	int		 fd, flags, status;

	if (pledge("stdio cpath rpath wpath sendfd", NULL) == -1)
		err(1, "pledge");

	imsg_init(&ibuf, sock);
	for (;;) {
		if (read_message(&ibuf, &imsg, child_pid) == 0)
			break;

		switch (imsg.hdr.type) {
		case IMSG_STAT:
			fn = imsg.data;
			offset = 0;
			if (stat(fn, &sb) == 0)
				offset = sb.st_size;
			send_message(&ibuf, IMSG_STAT, &offset,
			    sizeof offset, -1);
			break;
		case IMSG_OPEN:
			datalen = imsg.hdr.len - IMSG_HEADER_SIZE;
			if (datalen != sizeof *req)
				errx(1, "%s: imsg size mismatch", __func__);

			req = imsg.data;
			if (strcmp(req->fname, "-") == 0)
				fd = STDOUT_FILENO;
			else {
				flags = O_CREAT | O_WRONLY;
				if (req->append)
					flags  |= O_APPEND;
				if ((fd = open(req->fname, flags, 0666)) == -1)
					err(1, "open %s", req->fname);
			}
			send_message(&ibuf, IMSG_OPEN, NULL, 0, fd);
			break;
		}

		imsg_free(&imsg);
	}

	close(sock);
	if (waitpid(child_pid, &status, 0) == -1 && errno != ECHILD)
		err(1, "wait");

	if (WIFSIGNALED(status))
		errx(1, "child terminated: signal %d", WTERMSIG(status));

	return WEXITSTATUS(status);
}

static void
child(int sock, int argc, char **argv)
{
	struct imsgbuf	 ibuf;
	struct imsg	 imsg;
	struct url	 url;
	struct open_req	 req;
	char		*str;
	size_t		 flen;
	off_t		*poff;
	int		 i;

	/* XXX libtls will provide hook to preload cert.pem, then drop rpath */
	if (progressmeter) {
		if (pledge("stdio rpath inet dns recvfd tty", NULL) == -1)
			err(1, "pledge");
	} else {
		if (pledge("stdio rpath inet dns recvfd", NULL) == -1)
			err(1, "pledge");
	}

	imsg_init(&ibuf, sock);
	for (i = 0; i < argc; i++) {
		str = url_encode(argv[i]);
		url_parse(&url, str);
		free(str);
		if ((url.fname = oarg ? oarg : basename(url.path)) == NULL)
			err(1, "basename(%s)", url.path);

		if (strcmp(url.fname, "/") == 0)
			errx(1, "No filename after host (use -o): %s", argv[i]);

		if (strcmp(url.fname, ".") == 0)
			errx(1, "No '/' after host (use -o): %s", argv[i]);

		url_connect(&url);

		flen = strlen(url.fname) + 1;
		url.offset = 0;
		if (resume) {
			send_message(&ibuf, IMSG_STAT, (char *)url.fname,
			    flen, -1);
			if (read_message(&ibuf, &imsg, getppid()) == 0)
				break;

			if (imsg.hdr.type != IMSG_STAT)
				errx(1, "%s: IMSG_STAT expected", __func__);

			if (imsg.hdr.len - IMSG_HEADER_SIZE != sizeof(off_t))
				errx(1, "%s: imsg size mismatch", __func__);

			poff = imsg.data;
			url.offset = *poff;
		}

		url_request(&url);
		if (strlcpy(req.fname, url.fname,
		    sizeof req.fname) >= sizeof req.fname)
			errx(1, "%s: filename overflow", __func__);

		req.append = url.offset ? 1 : 0;
		send_message(&ibuf, IMSG_OPEN, &req, sizeof req, -1);
		if (read_message(&ibuf, &imsg, getppid()) == 0)
			break;

		if (imsg.hdr.type != IMSG_OPEN)
			errx(1, "%s: IMSG_OPEN expected", __func__);

		if (imsg.fd == -1)
			errx(1, "%s: expected a file descriptor", __func__);

		url_save(&url, imsg.fd);
		free((void *)url.path);
		imsg_free(&imsg);
	}

	exit(0);
}

static void
url_connect(struct url *url)
{
	switch (url->scheme) {
	case HTTP:
	case HTTPS:
		http_connect(url);
		break;
	case FTP:
		ftp_connect(url);
		break;
	}
}

static void
url_request(struct url *url)
{
	log_request(url);
	switch (url->scheme) {
	case HTTP:
	case HTTPS:
		http_get(url);
		break;
	case FTP:
		break;
	}
}

static void
url_save(struct url *url, int fd)
{
	if (oarg) {
		if (progressmeter) {
			if (pledge("stdio tty", NULL) == -1)
				err(1, "pledge");
		} else {
			if (pledge("stdio", NULL) == -1)
				err(1, "pledge");
		}
	}

	if (progressmeter)
		start_progress_meter(url->fname, url->file_sz, &url->offset);

	switch (url->scheme) {
	case HTTP:
	case HTTPS:
		http_save(url, fd);
		break;
	case FTP:
		ftp_save(url, fd);
		break;
	}

	if (progressmeter)
		stop_progress_meter();
}

static void
send_message(struct imsgbuf *ibuf, int type, void *msg, size_t msglen, int fd)
{
	if (imsg_compose(ibuf, type, -1, 0, fd, msg, msglen) != 1)
		err(1, "imsg_compose");

	if (imsg_flush(ibuf) != 0)
		err(1, "imsg_flush");
}

static int
read_message(struct imsgbuf *ibuf, struct imsg *imsg, pid_t from)
{
	int	n;

	if ((n = imsg_read(ibuf)) == -1)
		err(1, "imsg_read");
	if (n == 0)
		return 0;

	if ((n = imsg_get(ibuf, imsg)) == -1)
		err(1, "imsg_get");
	if (n == 0)
		return 0;

	if ((pid_t)imsg->hdr.pid != from)
		errx(1, "PIDs don't match");

	return n;
}

static void
env_parse(void)
{
	char	*proxy_str;

	http_debug = getenv("HTTP_DEBUG") != NULL;
	if ((proxy_str = getenv("HTTP_PROXY")) == NULL)
		return;

	if (strlen(proxy_str) == 0)
		return;

	if ((proxy = malloc(sizeof *proxy)) == NULL)
		err(1, "%s: malloc", __func__);

	url_parse(proxy, proxy_str);
	if (proxy->scheme != HTTP)
		errx(1, "Invalid proxy scheme: %s", proxy_str);

	if (proxy->port[0] == '\0')
		(void)strlcpy(proxy->port, "80", sizeof proxy->port);
}

void
url_parse(struct url *url, const char *url_str)
{
	char	*t;

	memset(url, 0, sizeof *url);
	while (isblank((unsigned char)*url_str))
		url_str++;

	/* Determine the scheme */
	if ((t = strstr(url_str, "://")) != NULL) {
		if (strncasecmp(url_str, "http://", 7) == 0)
			url->scheme = HTTP;
		else if (strncasecmp(url_str, "https://", 8) == 0)
			url->scheme = HTTPS;
		else if (strncasecmp(url_str, "ftp://", 6) == 0)
			url->scheme = FTP;
		else
			errx(1, "%s: Invalid scheme %s", __func__, url_str);

		url_str = t + strlen("://");
	} else
		url->scheme = HTTP;	/* default to HTTP */

	/* Prepare Basic Auth of credentials if present */
	if ((t = strchr(url_str, '@')) != NULL) {
		if (b64_ntop((unsigned char *)url_str, t - url_str,
		    url->basic_auth, sizeof url->basic_auth) == -1)
			errx(1, "error in base64 encoding");

		url_str = ++t;
	}

	/* Extract path component */
	if ((t = strchr(url_str, '/')) != NULL) {
		url->path = xstrdup(t, __func__);
		*t = '\0';
	}

	/* hostname and port */
	if ((t = strchr(url_str, ':')) != NULL)	{
		*t++ = '\0';
		if (strlcpy(url->port, t, sizeof url->port) >= sizeof url->port)
			errx(1, "%s: port too long", __func__);
	}

	if (strlcpy(url->host, url_str, sizeof url->host) >= sizeof url->host)
		errx(1, "%s: hostname too long", __func__);
}

void
log_info(const char *fmt, ...)
{
	va_list	ap;

	if (verbose == 0)
		return;

	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
}

void
log_request(struct url *url)
{
	int	custom_port = 0;

	switch (url->scheme) {
	case HTTP:
		custom_port = strcmp(url->port, "80") ? 1 : 0;
		break;
	case HTTPS:
		custom_port = strcmp(url->port, "443") ? 1 : 0;
		break;
	case FTP:
		custom_port = strcmp(url->port, "21") ? 1 : 0;
		break;
	}

	if (proxy)
		log_info("Requesting %s://%s%s%s%s\n"
		    " (via %s://%s%s%s)",
		    scheme_str[url->scheme],
		    url->host,
		    custom_port ? ":" : "",
		    custom_port ? url->port : "",
		    url->path ? url->path : "",

		    /* via proxy part */
		    (proxy->scheme == HTTP) ? "http" : "https",
		    proxy->host,
		    proxy->port[0] ? ":" : "",
		    proxy->port[0] ? proxy->port : "");
	else
		log_info("Requesting %s://%s%s%s%s\n",
		    scheme_str[url->scheme],
		    url->host,
		    custom_port ? ":" : "",
		    custom_port ? url->port : "",
		    url->path ? url->path : "");
}

__dead void
usage(void)
{
	extern char	*__progname;

	fprintf(stderr, "usage: %s [-CVM] [-D title] [-o output] "
	    "[-S tls_options] [-U useragent] url ...\n", __progname);

	exit(1);
}
