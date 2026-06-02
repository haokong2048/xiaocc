CROSS_COMPILE=aarch64-linux-gnu-
CC=$(CROSS_COMPILE)gcc

CFLAGS=-std=c11 -g -fno-common
LDFLAGS=-static
SRCS=$(wildcard *.c)
OBJS=$(SRCS:.c=.o)

xiaocc: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(OBJS): xiaocc.h

test: xiaocc
	./test.sh

clean:
	rm -f xiaocc *.o *~ tmp*

.PHONY: test clean
