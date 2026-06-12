CROSS_COMPILE=aarch64-linux-gnu-
CC=$(CROSS_COMPILE)gcc
QEMU=qemu-aarch64

CFLAGS=-std=c11 -g -fno-common
LDFLAGS=-static
SRCS=$(wildcard *.c)
OBJS=$(SRCS:.c=.o)

TEST_SRCS=$(wildcard test/*.c)
TESTS=$(TEST_SRCS:.c=.exe)

xiaocc: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(OBJS): xiaocc.h

test/macro.exe: xiaocc test/macro.c
	$(QEMU) ./xiaocc -o test/macro.s test/macro.c
	$(CC) -static -o $@ test/macro.s -xc test/common

test/stdhdr.exe: xiaocc test/stdhdr.c
	$(QEMU) ./xiaocc -o test/stdhdr.s test/stdhdr.c
	$(CC) -static -o $@ test/stdhdr.s -xc test/common


test/%.exe: xiaocc test/%.c
	$(CC) -o- -E -P -C test/$*.c | $(QEMU) ./xiaocc -o test/$*.s -
	$(CC) -static -o $@ test/$*.s -xc test/common

test: $(TESTS)
	for i in $^; do echo $$i; $(QEMU) ./$$i || exit 1; echo; done
	test/driver.sh $(QEMU) ./xiaocc || exit 1

clean:
	rm -f xiaocc tmp* $(TESTS) test/*.s test/*.exe
	find * -type f '(' -name '*~' -o -name '*.o' ')' -exec rm {} ';'

.PHONY: test clean
