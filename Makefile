PROG=   http

SRCS=   main.c http.c ftp.c file.c progressmeter.c util.c

CFLAGS+=	-W -Wall -Wstrict-prototypes -Wno-unused -Wunused-variable \
		-Wno-unused-parameter

LDADD+=	-lutil -ltls -lssl -lcrypto
DPADD+=	${LIBUTIL} ${LIBTLS} ${LIBSSL} ${LIBCRYPTO}

.include <bsd.prog.mk>
