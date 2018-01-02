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

static int		 auto_fetch(int, int, int, char **, int, char **);
static void		 child(int, int, char **);
static int		 parent(int, pid_t, int, char **);
static struct url	*proxy_parse(const char *);
static struct url	*get_proxy(int);
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
	int		  ch, csock, dumb_terminal, rexec, save_argc;

	term = getenv("TERM");
	dumb_terminal = (term == NULL || *term == '\0' ||
	    !strcmp(term, "dumb") || !strcmp(term, "emacs") ||
	    !strcmp(term, "su"));
	if (isatty(STDOUT_FILENO) && isatty(STDERR_FILENO) && !dumb_terminal)
		progressmeter = 1;

	csock = rexec = 0;
	save_argc = argc;
	save_argv = argv;
	while ((ch = getopt(argc, argv, "46AaCD:o:mMS:s:U:vVw:x")) != -1) {
		switch (ch) {
		case '4':
			family = AF_INET;
			break;
		case '6':
			family = AF_INET6;
			break;
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

	return auto_fetch(rexec, csock, argc, argv, save_argc, save_argv);
}

static int
auto_fetch(int rexec, int csock, int argc, char **argv, int sargc, char **sargv)
{
	pid_t	  pid;
	int	  sp[2];

	if (rexec)
		child(csock, argc, argv);

	if (socketpair(AF_UNIX, SOCK_STREAM, PF_UNSPEC, sp) != 0)
		err(1, "socketpair");

	switch (pid = fork()) {
	case -1:
		err(1, "fork");
	case 0:
		close(sp[0]);
		re_exec(sp[1], sargc, sargv);
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
	int		 fd, save_errno, sig, status;

	setproctitle("%s", "parent");
	if (pledge("stdio cpath rpath wpath sendfd", NULL) == -1)
		err(1, "pledge");

	imsg_init(&ibuf, sock);
	for (;;) {
		if (read_message(&ibuf, &imsg) == 0)
			break;

		if (imsg.hdr.type != IMSG_OPEN)
			errx(1, "%s: IMSG_OPEN expected", __func__);

		offset = 0;
		fd = open(imsg.data, imsg.hdr.peerid, 0666);
		save_errno = errno;
		if (fd != -1)
			if (fstat(fd, &sb) == 0)
				offset = sb.st_size;

		send_message(&ibuf, IMSG_OPEN, save_errno,
		    &offset, sizeof offset, fd);
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
	struct url	*url;
	FILE		*dst_fp;
	int		 fd, i, tostdout;

	setproctitle("%s", "child");
	https_init(tls_options);
	if (pledge("stdio inet dns recvfd tty", NULL) == -1)
		err(1, "pledge");
	if (!progressmeter && pledge("stdio inet dns recvfd", NULL) == -1)
		err(1, "pledge");

	http_debug = getenv("HTTP_DEBUG") != NULL;
	imsg_init(&child_ibuf, sock);
	tostdout = oarg && (strcmp(oarg, "-") == 0);
	for (i = 0; i < argc; i++) {
		if ((url = url_parse(argv[i])) == NULL)
			exit(1);

		validate_output_fname(url, argv[i]);
		url_connect(url, get_proxy(url->scheme), connect_timeout);
		fd = -1;
		if (resume && !tostdout)
			fd = fd_request(url->fname, O_WRONLY|O_APPEND,
			    &url->offset);

		url = url_request(url, get_proxy(url->scheme));
		/* If range request fails, url->offset will be set to zero */
		if (resume && fd != -1 && url->offset == 0)
			if (ftruncate(fd, 0) == -1)
				err(1, "%s: ftruncate", __func__);

		if (fd == -1 && !tostdout) {
			fd = fd_request(url->fname, O_CREAT|O_WRONLY, NULL);
			if (fd == -1)
				err(1, "Can't open file %s", url->fname);
		}

		if (tostdout)
			dst_fp = stdout;
		else if ((dst_fp = fdopen(fd, "w")) == NULL)
			err(1, "%s: fdopen", __func__);

		if (progressmeter)
			start_progress_meter(basename(url->path), title,
			    url->file_sz, &url->offset);

		url_save(url, dst_fp);
		if (progressmeter)
			stop_progress_meter();

		if (dst_fp != stdout)
			fclose(dst_fp);

		if (url->scheme == S_FTP)
			ftp_quit(url);

		url_free(url);
	}

	exit(0);
}

static struct url *
get_proxy(int scheme)
{
	static struct url	*ftp_proxy, *http_proxy;

	switch (scheme) {
	case S_HTTP:
		if (http_proxy)
			return http_proxy;
		else
			return (http_proxy = proxy_parse("http_proxy"));
	case S_FTP:
		if (ftp_proxy)
			return ftp_proxy;
		else
			return (ftp_proxy = proxy_parse("ftp_proxy"));
	default:
		return NULL;
	}
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

static __dead void
usage(void)
{
	fprintf(stderr, "usage: %s [-46ACVM] [-D title] [-o output] "
	    "[-S tls_options] [-U useragent] "
	    "[-w seconds] url ...\n", getprogname());

	exit(1);
}
