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
#include <fcntl.h>
#include <histedit.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "http.h"

#define ARGVMAX	64

static int	 cmd_lookup(const char *);
static char	*prompt(void);
static FILE	*data_fopen(void);

static void	do_open(int, char **);
static void	do_help(int, char **);
static void	do_quit(int, char **);
static void	do_ls(int, char **);
static void	do_pwd(int, char **);
static void	do_cd(int, char **);

static FILE	*ctrl_fp;
static struct {
	const char	 *name;
	const char	 *info;
	int		  conn_required;
	void		(*cmd)(int, char **);
} cmd_tbl[] = {
	{ "open", "connect to remote ftp server", 0, do_open },
	{ "close", "terminate ftp session", 1, do_quit },
	{ "help", "print local help information", 0, do_help },
	{ "quit", "terminate ftp session and exit", 0, do_quit },
	{ "exit", "terminate ftp session and exit", 0, do_quit },
	{ "ls", "list contents of remote directory", 1, do_ls },
	{ "pwd", "print working directory on remote machine", 1, do_pwd },
	{ "cd", "change remote working directory", 1, do_cd },
};

void
cmd(const char *host, const char *port)
{
	HistEvent	  hev;
	EditLine	 *el;
	History		 *hist;
	const char	 *line;
	char		**ap, *argv[ARGVMAX], *cp;
	int		  count, i;

	if ((el = el_init(getprogname(), stdin, stdout, stderr)) == NULL)
		err(1, "couldn't initialise editline");

	if ((hist = history_init()) == NULL)
		err(1, "couldn't initialise editline history");

	history(hist, &hev, H_SETSIZE, 100);
	el_set(el, EL_HIST, history, hist);
	el_set(el, EL_PROMPT, prompt);
	el_set(el, EL_EDITOR, "emacs");
	el_set(el, EL_TERMINAL, NULL);
	el_set(el, EL_SIGNAL, 1);
	el_source(el, NULL);

	for (;;) {
		if ((line = el_gets(el, &count)) == NULL || count <= 0) {
			fprintf(stderr, "\n");
			argv[0] = "quit";
			do_quit(1, argv);
			break;
		}

		if (count <= 1)
			continue;

		if ((cp = strrchr(line, '\n')) != NULL)
			*cp = '\0';

		history(hist, &hev, H_ENTER, line);
		for (ap = argv; ap < &argv[ARGVMAX - 1] &&
		    (*ap = strsep((char **)&line, " \t")) != NULL;) {
			if (**ap != '\0')
				ap++;
		}
		*ap = NULL;

		if ((i = cmd_lookup(argv[0])) == -1) {
			fprintf(stderr, "Invalid command.\n");
			continue;
		}

		if (cmd_tbl[i].conn_required && ctrl_fp == NULL) {
			fprintf(stderr, "Not connected.\n");
			continue;
		}

		cmd_tbl[i].cmd(ap - argv, argv);

		if (strcmp(cmd_tbl[i].name, "quit") == 0 ||
		    strcmp(cmd_tbl[i].name, "exit") == 0)
			break;
	}

	el_end(el);
}

static int
cmd_lookup(const char *cmd)
{
	size_t	i;

	for (i = 0; i < nitems(cmd_tbl); i++)
		if (strcmp(cmd, cmd_tbl[i].name) == 0)
			return i;

	return -1;
}

static char *
prompt(void)
{
	return "ftp> ";
}

static FILE *
data_fopen(void)
{
	int	 fd;

	fd = activemode ? ftp_eprt(ctrl_fp) : ftp_epsv(ctrl_fp);
	if (fd == -1) {
		if (http_debug)
			fprintf(stderr, "Failed to open data connection");

		return NULL;
	}

	return fdopen(fd, "r");
}

static void
do_open(int argc, char **argv)
{
	const char	*host = NULL, *port = "21";
	char		*buf = NULL;
	size_t		 n = 0;
	int		 sock;

	if (ctrl_fp != NULL) {
		fprintf(stderr, "already connected, use close first.\n");
		return;
	}

	switch (argc) {
	case 3:
		port = argv[2];
		/* FALLTHROUGH */
	case 2:
		host = argv[1];
		break;
	default:
		fprintf(stderr, "usage: open host [port]\n");
		return;
	}

	if ((sock = tcp_connect(host, port, 0, NULL)) == -1)
		return;

	if ((ctrl_fp = fdopen(sock, "r+")) == NULL)
		err(1, "%s: fdopen", __func__);

	/* greeting */
	ftp_getline(&buf, &n, 0, ctrl_fp);
	free(buf);
	ftp_auth(ctrl_fp, NULL, NULL);
}

static void
do_help(int argc, char **argv)
{
	size_t	i;
	int	j;

	if (argc == 1) {
		for (i = 0; i < nitems(cmd_tbl); i++)
			fprintf(stderr, "%s\n", cmd_tbl[i].name);

		return;
	}

	for (i = 1; i < (size_t)argc; i++) {
		if ((j = cmd_lookup(argv[i])) == -1)
			fprintf(stderr, "invalid help command %s\n", argv[i]);
		else
			fprintf(stderr, "%s\t%s\n", argv[i], cmd_tbl[j].info);
	}
}

static void
do_quit(int argc, char **argv)
{
	if (ctrl_fp == NULL)
		return;

	ftp_command(ctrl_fp, "QUIT");
	fclose(ctrl_fp);
	ctrl_fp = NULL;
}

static void
do_ls(int argc, char **argv)
{
	FILE		*data_fp, *dst_fp = stdout;
	const char	*remote_dir = NULL;
	char		*buf = NULL;
	size_t		 n = 0;
	int		 r;

	switch (argc) {
	case 3:
		if (strcmp(argv[2], "-") != 0 &&
		    (dst_fp = fopen(argv[2], "w")) == NULL)
			err(1, "fopen %s", argv[2]);
		/* FALLTHROUGH */
	case 2:
		remote_dir = argv[1];
		/* FALLTHROUGH */
	case 1:
		break;
	default:
		fprintf(stderr, "usage: ls [remote-directory [local-file]]\n");
		return;
	}

	if ((data_fp = data_fopen()) == NULL) {
		warn("%s: data_fopen", __func__);
		goto done;
	}

	if (remote_dir != NULL)
		r = ftp_command(ctrl_fp, "LIST %s", remote_dir);
	else
		r = ftp_command(ctrl_fp, "LIST");

	if (r != P_PRE)
		goto done;

	while (getline(&buf, &n, data_fp) != -1)
		fprintf(dst_fp, "%s", buf);

	ftp_getline(&buf, &n, 0, ctrl_fp);
	free(buf);

 done:
	fclose(data_fp);
	if (dst_fp != stdout)
		fclose(dst_fp);
}

static void
do_pwd(int argc, char **argv)
{
	ftp_command(ctrl_fp, "PWD");
}

static void
do_cd(int argc, char **argv)
{
	if (argc != 2) {
		fprintf(stderr, "usage: cd remote-directory\n");
		return;
	}

	ftp_command(ctrl_fp, "CWD %s", argv[1]);
}
