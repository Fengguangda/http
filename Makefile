# Define SMALL to disable command line editing
#CFLAGS+=-DSMALL

PROG=	ftp
SRCS=	cmd.c ftp.c file.c http.c main.c progressmeter.c url.c util.c

LDADD+=	-ledit -lcurses -lutil -ltls -lssl -lcrypto
DPADD+=	${LIBEDIT} ${LIBCURSES} ${LIBUTIL} ${LIBTLS} ${LIBSSL} ${LIBCRYPTO}

.include <bsd.prog.mk>
