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

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <imsg.h>
#include <libgen.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "http.h"

static void		 child(int, int, char **);
static int		 parent(int, pid_t, int, char **);
static struct url	*proxy_parse(const char *);
static void		 re_exec(int, int, char **);
static void		 validate_output_fname(struct url *, const char *);
static __dead void	 usage(void);

static const char	*title;
static char		*tls_options, *oarg;
static int		 connect_timeout, resume, progressmeter;

int
main(int argc, char **argv)
{
	const char	 *e;
	char		**save_argv, *term;
	int		  ch, csock, dumb_terminal, rexec = 0, save_argc, sp[2];
	pid_t		  pid;

	term = getenv("TERM");
	dumb_terminal = (term == NULL || *term == '\0' ||
	    !strcmp(term, "dumb") || !strcmp(term, "emacs") ||
	    !strcmp(term, "su"));
	if (isatty(STDOUT_FILENO) && isatty(STDERR_FILENO) && !dumb_terminal)
		progressmeter = 1;

	save_argc = argc;
	save_argv = argv;
	while ((ch = getopt(argc, argv, "4AaCD:o:mMS:s:U:vVw:x")) != -1) {
		switch (ch) {
		case 'A':
			activemode = 1;
			break;
		case 'C':
			resume = 1;
			break;
		case 'D':
			title = optarg;
			break;
		case 'o':
			oarg = optarg;
			if (!strlen(oarg))
				oarg = NULL;
			break;
		case 'M':
			progressmeter = 0;
			break;
		case 'm':
			progressmeter = 1;
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
		case 'w':
			connect_timeout = strtonum(optarg, 0, 200, &e);
			if (e)
				errx(1, "-w: %s", e);
			break;
		/* options for compatibility, on by default */
		case '4':
			break;
		case 'a':
			break;
		case 'v':
			break;
		/* options for internal use only */
		case 'x':
			rexec = 1;
			break;
		case 's':
			csock = strtonum(optarg, 3, getdtablesize() - 1, &e);
			if (e)
				errx(1, "-s: %s", e);
			break;
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;
	if (argc == 0)
		usage();

	if (rexec)
		child(csock, argc, argv);

	if (socketpair(AF_UNIX, SOCK_STREAM, PF_UNSPEC, sp) != 0)
		err(1, "socketpair");

	switch (pid = fork()) {
	case -1:
		err(1, "fork");
	case 0:
		close(sp[0]);
		re_exec(sp[1], save_argc, save_argv);
	}

	close(sp[1]);
	return parent(sp[0], pid, argc, argv);
}

static void
re_exec(int sock, int argc, char **argv)
{
	char	**nargv, *sock_str;
	int	  i, j, nargc;

	nargc = argc + 4;
	if ((nargv = calloc(nargc, sizeof(*nargv))) == NULL)
		err(1, "%s: calloc", __func__);

	xasprintf(&sock_str, "%d", sock);
	i = 0;
	nargv[i++] = argv[0];
	nargv[i++] = "-s";
	nargv[i++] = sock_str;
	nargv[i++] = "-x";
	for (j = 1; j < argc; j++)
		nargv[i++] = argv[j];

	execvp(nargv[0], nargv);
	err(1, "execvp");
}

static int
parent(int sock, pid_t child_pid, int argc, char **argv)
{
	struct imsgbuf	 ibuf;
	struct imsg	 imsg;
	struct stat	 sb;
	off_t		 offset;
	int		 fd, sig, status;

	setproctitle("%s", "parent");
	if (pledge("stdio cpath rpath wpath sendfd", NULL) == -1)
		err(1, "pledge");

	imsg_init(&ibuf, sock);
	for (;;) {
		if (read_message(&ibuf, &imsg) == 0)
			break;

		if (imsg.hdr.type != IMSG_OPEN)
			errx(1, "%s: IMSG_OPEN expected", __func__);

		if ((fd = open(imsg.data, imsg.hdr.peerid, 0666)) == -1)
			err(1, "Can't open file %s", imsg.data);

		offset = 0;
		if (fstat(fd, &sb) == 0)
			offset = sb.st_size;

		send_message(&ibuf, IMSG_OPEN, -1, &offset, sizeof offset, fd);
		imsg_free(&imsg);
	}

	close(sock);
	if (waitpid(child_pid, &status, 0) == -1 && errno != ECHILD)
		err(1, "wait");

	sig = WTERMSIG(status);
	if (WIFSIGNALED(status) && sig != SIGPIPE)
		errx(1, "child terminated: signal %d", sig);

	return WEXITSTATUS(status);
}

static void
child(int sock, int argc, char **argv)
{
	struct url	*ftp_proxy, *http_proxy, *proxy, *url;
	int		 fd, i;
	off_t		 offset;

	setproctitle("%s", "child");
	https_init(tls_options);
	if (progressmeter) {
		if (pledge("stdio inet dns recvfd tty", NULL) == -1)
			err(1, "pledge");
	} else {
		if (pledge("stdio inet dns recvfd", NULL) == -1)
			err(1, "pledge");
	}

	http_debug = getenv("HTTP_DEBUG") != NULL;
	ftp_proxy = proxy_parse("ftp_proxy");
	http_proxy = proxy_parse("http_proxy");

	imsg_init(&child_ibuf, sock);
	for (i = 0; i < argc; i++) {
		if ((url = url_parse(argv[i])) == NULL)
			exit(1);

		validate_output_fname(url, argv[i]);
		proxy = NULL;
		switch (url->scheme) {
		case S_HTTP:
			proxy = http_proxy;
			break;
		case S_FTP:
			proxy = ftp_proxy;
			break;
		}

		url_connect(url, proxy, connect_timeout);
		offset = 0;
		if (oarg && strcmp(oarg, "-") == 0) {
			if ((fd = dup(STDOUT_FILENO)) == -1)
				err(1, "%s: dup", __func__);
		} else if ((fd = fd_request(&child_ibuf,
			    url->fname, O_CREAT|O_WRONLY, &offset)) == -1)
				break;

		if (resume) {
			url->offset = offset;
			if (fcntl(fd, F_SETFL, O_APPEND) == -1)
				warn("%s: fcntl", __func__);
		}

		url = url_request(url, proxy);
		/* url->offset gets set to 0 if range request fails */
		if (resume && url->offset == 0)
			if (ftruncate(fd, 0) == -1)
				err(1, "%s: ftruncate", __func__);

		url_save(url, proxy, title, progressmeter, fd);
		url_free(url);
	}

	exit(0);
}

static void
validate_output_fname(struct url *url, const char *name)
{
	url->fname = xstrdup(oarg ? oarg : basename(url->path), __func__);
	if (strcmp(url->fname, "/") == 0)
		errx(1, "No filename after host (use -o): %s", name);

	if (strcmp(url->fname, ".") == 0)
		errx(1, "No '/' after host (use -o): %s", name);
}

static struct url *
proxy_parse(const char *name)
{
	struct url	*proxy;
	char		*str;

	if ((str = getenv(name)) == NULL)
		return NULL;

	if (strlen(str) == 0)
		return NULL;

	if ((proxy = url_parse(str)) == NULL)
		exit(1);

	if (proxy->scheme != S_HTTP)
		errx(1, "Malformed proxy URL: %s", str);

	return proxy;
}

__dead void
usage(void)
{
	fprintf(stderr, "usage: %s [-ACVM] [-D title] [-o output] "
	    "[-S tls_options] [-U useragent] "
	    "[-w seconds] url ...\n", getprogname());

	exit(1);
}
