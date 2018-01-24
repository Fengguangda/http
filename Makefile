PROG=		ftp

CFLAGS=		-D 'pledge(pr, pa)=0' -D'getdtablecount()=0' -D'setproctitle(p, t)=0'
CFLAGS+=	-std=c99 -D_BSD_SOURCE -D_XOPEN_SOURCE=500 -D_GNU_SOURCE

CFLAGS=		-D 'pledge(pr, pa)=0' -D'getdtablecount()=0' -D'setproctitle(p, t)=0'
CFLAGS+=	-std=c99 -D_BSD_SOURCE -D_XOPEN_SOURCE=500 -D_GNU_SOURCE

CFLAGS+=	-W -Wall -Wstrict-prototypes -Wno-unused -Wunused-variable \
		-Wno-unused-parameter
CFLAGS+=	-I${.CURDIR}

SRCS=		main.c extern.c http.c ftp.c file.c progressmeter.c url.c util.c
SRCS+=		imsg.c imsg-buffer.c openbsd_compat.c

LDADD+=	-lutil -ltls -lssl -lcrypto
DPADD+=	${LIBUTIL} ${LIBTLS} ${LIBSSL} ${LIBCRYPTO}

# define CMD to build FTP interactive command interpreter
CMD=

.ifdef CMD
CFLAGS+=	-DCMD
SRCS+=		cmd.c
LDADD+=		-lcurses -ledit
DPADD+=		${CURSES} ${LIBEDIT}
.endif

.include <bsd.prog.mk>
