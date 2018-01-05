PROG=		http

CFLAGS+=	-W -Wall -Wstrict-prototypes -Wno-unused -Wunused-variable \
		-Wno-unused-parameter

SRCS=		main.c extern.c http.c ftp.c file.c progressmeter.c url.c util.c

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
