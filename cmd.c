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
#include <histedit.h>
#include <string.h>
#include <stdlib.h>

#include "http.h"

static char	*prompt(void);

static char *
prompt(void)
{
	return "ftp> ";
}

void
cmd(const char *host, const char *port)
{
	HistEvent	 hev;
	EditLine	*el;
	History		*hist;
	const char	*line;
	int		 count;

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
			break;
		}

		if (strlen(line) > 1)
			history(hist, &hev, H_ENTER, line);
	}

	el_end(el);
}
