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

#include <stdio.h>

#define	S_HTTP	0
#define S_HTTPS	1
#define S_FTP	2
#define S_FILE	3

#define TMPBUF_LEN	131072
#define	IMSG_OPEN	1

#define P_PRE	100
#define P_OK	200
#define P_INTER	300
#define N_TRANS	400
#define	N_PERM	500

struct imsg;
struct imsgbuf;

struct url {
	int	 scheme;
	int	 ipliteral;
	char	*host;
	char	*port;
	char	*path;

	char	*fname;
};

/* cmd.c */
void	cmd(struct url *);

/* extern.c */
extern struct imsgbuf	 child_ibuf;
extern const char	*scheme_str[4], *port_str[4], *ua;
extern int		 activemode, family, http_debug, verbose;

/* file.c */
struct url	*file_request(struct imsgbuf *, struct url *, off_t *, off_t *);
void		 file_save(struct url *, FILE *, off_t *);

/* ftp.c */
void		 ftp_connect(struct url *, struct url *, int);
struct url	*ftp_get(struct url *, struct url *, off_t *, off_t *);
void		 ftp_quit(struct url *);
void		 ftp_save(struct url *, FILE *, off_t *);

/* http.c */
void		 http_connect(struct url *, struct url *, int);
struct url	*http_get(struct url *, struct url *, off_t *, off_t *);
void		 http_save(struct url *, FILE *, off_t *);
void		 https_init(char *);

/* progressmeter.c */
void	start_progress_meter(const char *, const char *, off_t, off_t *);
void	stop_progress_meter(void);

/* url.c */
void		 url_connect(struct url *, struct url *, int);
char		*url_encode(const char *);
void		 url_free(struct url *);
struct url	*url_parse(const char *);
struct url	*url_request(struct url *, struct url *, off_t *, off_t *);
void		 url_save(struct url *, FILE *, off_t *);
char		*url_str(struct url *);

/* util.c */
void	 copy_file(struct url *, FILE *, FILE *, off_t *);
int	 ftp_auth(FILE *, const char *, const char *);
int	 ftp_command(FILE *, const char *, ...)
		    __attribute__((__format__ (printf, 2, 3)))
		    __attribute__((__nonnull__ (2)));
int	 ftp_getline(char **, size_t *, int, FILE *);
int	 ftp_size(FILE *, const char *, off_t *, char **);
int	 tcp_connect(const char *, const char *, int, struct url *);
int	 fd_request(char *, int, off_t *);
int	 read_message(struct imsgbuf *, struct imsg *);
void	 send_message(struct imsgbuf *, int, uint32_t, void *, size_t, int);
void	 log_info(const char *, ...)
	    __attribute__((__format__ (printf, 1, 2)))
	    __attribute__((__nonnull__ (1)));
int	 xasprintf(char **, const char *, ...)
	    __attribute__((__format__ (printf, 2, 3)))
	    __attribute__((__nonnull__ (2)));
char	*xstrdup(const char *, const char *);
char	*xstrndup(const char *, size_t, const char *);
