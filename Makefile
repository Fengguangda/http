#define CMD to build FTP interactive command interpreter
CMD=

#define TLS to build HTTPS support
TLS=

PROG=		ftp

CFLAGS+=	-W -Wall -Wstrict-prototypes -Wno-unused -Wunused-variable \
		-Wno-unused-parameter

SRCS=		main.c extern.c http.c ftp.c file.c progressmeter.c url.c util.c

LDADD+=		-lutil
DPADD+=		${LIBUTIL}

.ifdef TLS
CFLAGS+=	-DTLS
LDADD+=		-ltls -lssl -lcrypto
DPADD+=		${LIBTLS} ${LIBSSL} ${LIBCRYPTO}
.endif

.ifdef CMD
CFLAGS+=	-DCMD
SRCS+=		cmd.c
LDADD+=		-lcurses -ledit
DPADD+=		${CURSES} ${LIBEDIT}
.endif

.include <bsd.prog.mk>
