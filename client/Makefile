.PHONY: clean

CFLAGS  := -Wall -Werror -g
LD      := gcc
LDLIBS  := ${LDLIBS} -lrdmacm -libverbs -lpthread

APPS    := rdma-client 

all: ${APPS}

rdma-client: rdma-common.o rdma-client.o main-client.o
	${LD} -o $@ $^ ${LDLIBS}

clean:
	rm -f *.o 

