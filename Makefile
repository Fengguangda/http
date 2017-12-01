PROG=   http

CFLAGS=	-D'pledge(pr, pa)=0' -D'getdtablecount()=0'
CFLAGS+= -I${.CURDIR} -I/usr/pkg/libressl/include
LDFLAGS= -L /usr/pkg/libressl/lib

SRCS=   main.c extern.c http.c ftp.c file.c progressmeter.c url.c util.c
SRCS+=	openbsd_compat.c imsg.c imsg-buffer.c

CFLAGS+=	-W -Wall -Wstrict-prototypes -Wno-unused -Wunused-variable \
		-Wno-unused-parameter

LDADD+=	-lutil -ltls -lssl -lcrypto
DPADD+=	${LIBUTIL} ${LIBTLS} ${LIBSSL} ${LIBCRYPTO}

.include <bsd.prog.mk>
