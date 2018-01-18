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

#include <sys/types.h>
#include <sys/queue.h>
#include <sys/socket.h>

#include <arpa/inet.h>
#include <netinet/in.h>

#include <err.h>
#include <errno.h>
#include <imsg.h>
#include <limits.h>
#include <netdb.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include "http.h"

static int	connect_wait(int);
static void	tooslow(int);

/*
 * Wait for an asynchronous connect(2) attempt to finish.
 */
int
connect_wait(int s)
{
	struct pollfd pfd[1];
	int error = 0;
	socklen_t len = sizeof(error);

	pfd[0].fd = s;
	pfd[0].events = POLLOUT;

	if (poll(pfd, 1, -1) == -1)
		return -1;
	if (getsockopt(s, SOL_SOCKET, SO_ERROR, &error, &len) < 0)
		return -1;
	if (error != 0) {
		errno = error;
		return -1;
	}
	return 0;
}

static void
tooslow(int signo)
{
	dprintf(STDERR_FILENO, "%s: connect taking too long\n", getprogname());
	_exit(2);
}

int
tcp_connect(const char *host, const char *port, int timeout, struct url *proxy)
{
	struct addrinfo	 hints, *res, *res0;
	char		 hbuf[NI_MAXHOST];
	const char	*cause = NULL;
	int		 error, s = -1, save_errno;

	if (proxy) {
		host = proxy->host;
		port = proxy->port;
	}

	if (host == NULL)
		errx(1, "hostname missing");

	memset(&hints, 0, sizeof hints);
	hints.ai_family = family;
	hints.ai_socktype = SOCK_STREAM;
	if ((error = getaddrinfo(host, port, &hints, &res0)))
		errx(1, "%s: %s: %s", __func__, gai_strerror(error), host);

	if (timeout) {
		(void)signal(SIGALRM, tooslow);
		alarm(timeout);
	}

	for (res = res0; res; res = res->ai_next) {
		if (getnameinfo(res->ai_addr, res->ai_addrlen, hbuf,
		    sizeof hbuf, NULL, 0, NI_NUMERICHOST) != 0)
			(void)strlcpy(hbuf, "(unknown)", sizeof hbuf);

		log_info("Trying %s...\n", hbuf);
		s = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
		if (s == -1) {
			cause = "socket";
			continue;
		}

		for (error = connect(s, res->ai_addr, res->ai_addrlen);
		    error != 0 && errno == EINTR; error = connect_wait(s))
			continue;

		if (error != 0) {
			cause = "connect";
			save_errno = errno;
			close(s);
			errno = save_errno;
			s = -1;
			continue;
		}

		break;
	}

	freeaddrinfo(res0);
	if (s == -1)
		err(1, "%s: %s", __func__, cause);

	if (timeout) {
		signal(SIGALRM, SIG_DFL);
		alarm(0);
	}

	return s;
}

char *
xstrdup(const char *str, const char *where)
{
	char	*r;

	if ((r = strdup(str)) == NULL)
		err(1, "%s: strdup", where);

	return r;
}

int
xasprintf(char **str, const char *fmt, ...)
{
	va_list	ap;
	int	ret;

	va_start(ap, fmt);
	ret = vasprintf(str, fmt, ap);
	va_end(ap);
	if (ret == -1)
		err(1, NULL);

	return ret;
}

char *
xstrndup(const char *str, size_t maxlen, const char *where)
{
	char	*r;

	if ((r = strndup(str, maxlen)) == NULL)
		err(1, "%s: strndup", where);

	return r;
}

int
fd_request(char *path, int flags, off_t *offset)
{
	struct imsg	 imsg;
	off_t		*poffset;
	int		 fd, save_errno;

	send_message(&child_ibuf, IMSG_OPEN, flags, path, strlen(path) + 1, -1);
	if (read_message(&child_ibuf, &imsg) == 0)
		return -1;

	if (imsg.hdr.type != IMSG_OPEN)
		errx(1, "%s: IMSG_OPEN expected", __func__);

	fd = imsg.fd;
	if (offset) {
		poffset = imsg.data;
		*offset = *poffset;
	}

	save_errno = imsg.hdr.peerid;
	imsg_free(&imsg);
	errno = save_errno;
	return fd;
}

void
send_message(struct imsgbuf *ibuf, int type, uint32_t peerid,
    void *msg, size_t msglen, int fd)
{
	if (imsg_compose(ibuf, type, peerid, 0, fd, msg, msglen) != 1)
		err(1, "imsg_compose");

	if (imsg_flush(ibuf) != 0)
		err(1, "imsg_flush");
}

int
read_message(struct imsgbuf *ibuf, struct imsg *imsg)
{
	int	n;

	if ((n = imsg_read(ibuf)) == -1)
		err(1, "%s: imsg_read", __func__);
	if (n == 0)
		return 0;

	if ((n = imsg_get(ibuf, imsg)) == -1)
		err(1, "%s: imsg_get", __func__);
	if (n == 0)
		return 0;

	return n;
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
copy_file(struct url *url, FILE *src_fp, FILE *dst_fp, off_t *offset)
{
	char	*tmp_buf;
	size_t	 r;

	if ((tmp_buf = malloc(TMPBUF_LEN)) == NULL)
		err(1, "%s: malloc", __func__);

	while ((r = fread(tmp_buf, 1, TMPBUF_LEN, src_fp)) != 0) {
		*offset += r;
		if (fwrite(tmp_buf, 1, r, dst_fp) != r)
			err(1, "%s: fwrite", __func__);
	}

	if (!feof(src_fp))
		errx(1, "%s: fread", __func__);

	free(tmp_buf);
}

int
ftp_getline(char **lineptr, size_t *n, int suppress_output, FILE *fp)
{
	ssize_t		 len;
	char		*bufp, code[4];
	const char	*errstr;
	int		 lookup[] = { P_PRE, P_OK, P_INTER, N_TRANS, N_PERM };


	if ((len = getline(lineptr, n, fp)) == -1)
		err(1, "%s: getline", __func__);

	bufp = *lineptr;
	if (!suppress_output)
		log_info("%s", bufp);

	if (len < 4)
		errx(1, "%s: line too short", __func__);

	(void)strlcpy(code, bufp, sizeof code);
	if (bufp[3] == ' ')
		goto done;

	/* multi-line reply */
	while (!(strncmp(code, bufp, 3) == 0 && bufp[3] == ' ')) {
		if ((len = getline(lineptr, n, fp)) == -1)
			err(1, "%s: getline", __func__);

		bufp = *lineptr;
		if (!suppress_output)
			log_info("%s", bufp);

		if (len < 4)
			continue;
	}

 done:
	(void)strtonum(code, 100, 553, &errstr);
	if (errstr)
		errx(1, "%s: Response code is %s: %s", __func__, errstr, code);

	return lookup[code[0] - '1'];
}

int
ftp_command(FILE *fp, const char *fmt, ...)
{
	va_list	 ap;
	char	*buf = NULL, *cmd;
	size_t	 n = 0;
	int	 r;

	va_start(ap, fmt);
	r = vasprintf(&cmd, fmt, ap);
	va_end(ap);
	if (r < 0)
		errx(1, "%s: vasprintf", __func__);

	if (http_debug)
		fprintf(stderr, ">>> %s\n", cmd);

	if (fprintf(fp, "%s\r\n", cmd) < 0)
		errx(1, "%s: fprintf", __func__);

	(void)fflush(fp);
	free(cmd);
	r = ftp_getline(&buf, &n, 0, fp);
	free(buf);
	return r;

}

int
ftp_auth(FILE *fp, const char *user, const char *pass)
{
	char	*addr = NULL, hn[HOST_NAME_MAX+1], *un;
	int	 code;

	code = ftp_command(fp, "USER %s", user ? user : "anonymous");
	if (code != P_OK && code != P_INTER)
		return code;

	if (pass == NULL) {
		if (gethostname(hn, sizeof hn) == -1)
			err(1, "%s: gethostname", __func__);

		un = getlogin();
		xasprintf(&addr, "%s@%s", un ? un : "anonymous", hn);
	}

	code = ftp_command(fp, "PASS %s", pass ? pass : addr);
	free(addr);
	return code;
}

int
ftp_size(FILE *fp, const char *fn, off_t *sizep, char **buf)
{
	size_t	 n = 0;
	off_t	 file_sz;
	int	 code;

	if (http_debug)
		fprintf(stderr, ">>> SIZE %s\n", fn);

	if (fprintf(fp, "SIZE %s\r\n", fn) < 0)
		errx(1, "%s: fprintf", __func__);

	(void)fflush(fp);
	if ((code = ftp_getline(buf, &n, 1, fp)) != P_OK)
		return code;

	if (sscanf(*buf, "%*u %lld", &file_sz) != 1)
		errx(1, "%s: sscanf size", __func__);

	if (sizep)
		*sizep = file_sz;

	return code;
}

int
ftp_eprt(FILE *fp)
{
	struct sockaddr_storage	 ss;
	struct sockaddr_in	*s_in;
	struct sockaddr_in6	*s_in6;
	char			 addr[NI_MAXHOST], port[NI_MAXSERV], *eprt;
	socklen_t		 len;
	int			 e, on, ret, sock;

	len = sizeof(ss);
	memset(&ss, 0, len);
	if (getsockname(fileno(fp), (struct sockaddr *)&ss, &len) == -1)
		err(1, "%s: getsockname", __func__);

	if (ss.ss_family != AF_INET && ss.ss_family != AF_INET6)
		errx(1, "Control connection not on IPv4 or IPv6");

	/* pick a free port */
	switch (ss.ss_family) {
	case AF_INET:
		s_in = (struct sockaddr_in *)&ss;
		s_in->sin_port = 0;
		break;
	case AF_INET6:
		s_in6 = (struct sockaddr_in6 *)&ss;
		s_in6->sin6_port = 0;
		break;
	}

	if ((sock = socket(ss.ss_family, SOCK_STREAM, 0)) == -1)
		err(1, "%s: socket", __func__);

	switch (ss.ss_family) {
	case AF_INET:
		on = IP_PORTRANGE_HIGH;
		if (setsockopt(sock, IPPROTO_IP, IP_PORTRANGE,
		    (char *)&on, sizeof(on)) < 0)
			warn("setsockopt IP_PORTRANGE (ignored)");
		break;
	case AF_INET6:
		on = IPV6_PORTRANGE_HIGH;
		if (setsockopt(sock, IPPROTO_IPV6, IPV6_PORTRANGE,
		    (char *)&on, sizeof(on)) < 0)
			warn("setsockopt IPV6_PORTRANGE (ignored)");
		break;
	}

	if (bind(sock, (struct sockaddr *)&ss, len) == -1)
		err(1, "%s: bind", __func__);

	if (listen(sock, 1) == -1)
		err(1, "%s: listen", __func__);

	/* Find out the ephermal port chosen */
	len = sizeof(ss);
	memset(&ss, 0, len);
	if (getsockname(sock, (struct sockaddr *)&ss, &len) == -1)
		err(1, "%s: getsockname", __func__);

	if ((e = getnameinfo((struct sockaddr *)&ss, len,
	    addr, sizeof(addr), port, sizeof(port),
	    NI_NUMERICHOST | NI_NUMERICSERV)) != 0)
		err(1, "%s: getnameinfo: %s", __func__, gai_strerror(e));

	xasprintf(&eprt, "EPRT |%d|%s|%s|",
	    ss.ss_family == AF_INET ? 1 : 2, addr, port);

	ret = ftp_command(fp, "%s", eprt);
	free(eprt);
	if (ret != P_OK) {
		close(sock);
		activemode = 0;
		return -1;
	}

	activemode = 1;
	return sock;
}

int
ftp_epsv(FILE *fp)
{
	struct sockaddr_storage	 ss;
	struct sockaddr_in	*s_in;
	struct sockaddr_in6	*s_in6;
	char			*buf = NULL, delim[4], *s, *e;
	size_t			 n = 0;
	socklen_t		 len;
	int			 port, sock;

	if (http_debug)
		fprintf(stderr, ">>> EPSV\n");

	if (fprintf(fp, "EPSV\r\n") < 0)
		errx(1, "%s: fprintf", __func__);

	(void)fflush(fp);
	if (ftp_getline(&buf, &n, 1, fp) != P_OK) {
		free(buf);
		return -1;
	}

	if ((s = strchr(buf, '(')) == NULL || (e = strchr(s, ')')) == NULL) {
		warnx("Malformed EPSV reply");
		free(buf);
		return -1;
	}

	s++;
	*e = '\0';
	if (sscanf(s, "%c%c%c%d%c", &delim[0], &delim[1], &delim[2],
	    &port, &delim[3]) != 5) {
		warnx("EPSV parse error");
		free(buf);
		return -1;
	}
	free(buf);

	if (delim[0] != delim[1] || delim[0] != delim[2]
	    || delim[0] != delim[3]) {
		warnx("EPSV parse error");
		return -1;
	}

	len = sizeof(ss);
	memset(&ss, 0, len);
	if (getpeername(fileno(fp), (struct sockaddr *)&ss, &len) == -1)
		err(1, "%s: getpeername", __func__);

	switch (ss.ss_family) {
	case AF_INET:
		s_in = (struct sockaddr_in *)&ss;
		s_in->sin_port = htons(port);
		break;
	case AF_INET6:
		s_in6 = (struct sockaddr_in6 *)&ss;
		s_in6->sin6_port = htons(port);
		break;
	default:
		errx(1, "%s: Invalid socket family", __func__);
	}

	if ((sock = socket(ss.ss_family, SOCK_STREAM, 0)) == -1)
		err(1, "%s: socket", __func__);

	if (connect(sock, (struct sockaddr *)&ss, len) == -1)
		err(1, "%s: connect", __func__);

	return sock;
}
