.PHONY: clean

CFLAGS  := -Wall -Werror -g
LD      := gcc
LDLIBS  := ${LDLIBS} -lrdmacm -libverbs -lpthread

APPS    := rdma-server 

all: ${APPS}

rdma-server: rdma-common.o rdma-server.o main-server.o
	${LD} -o $@ $^ ${LDLIBS}

clean:
	rm -f *.o 
