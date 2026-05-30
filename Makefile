CROSS_COMPILE=aarch64-linux-gnu-
CC=$(CROSS_COMPILE)gcc

CFLAGS=-std=c11 -g -fno-common
LDFLAGS=-static

xiaocc: main.o
	$(CC) -o xiaocc main.o $(LDFLAGS)

test: xiaocc
	./test.sh

clean:
	rm -f xiaocc *.o *~ tmp*

.PHONY: test clean
