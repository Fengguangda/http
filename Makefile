#define CMD to build FTP interactive command interpreter
CMD=

#define NOTLS to build without HTTPS support
#NOTLS=

PROG=		ftp

CFLAGS+=	-W -Wall -Wstrict-prototypes -Wno-unused -Wunused-variable \
		-Wno-unused-parameter

SRCS=		main.c extern.c http.c ftp.c file.c progressmeter.c url.c util.c

LDADD+=		-lutil
DPADD+=		${LIBUTIL}

.ifndef NOTLS
LDADD+=		-ltls -lssl -lcrypto
DPADD+=		${LIBTLS} ${LIBSSL} ${LIBCRYPTO}
.endif

.ifdef NOTLS
CFLAGS+=	-DNOTLS
.endif

.ifdef CMD
CFLAGS+=	-DCMD
SRCS+=		cmd.c
LDADD+=		-lcurses -ledit
DPADD+=		${CURSES} ${LIBEDIT}
.endif

.include <bsd.prog.mk>
