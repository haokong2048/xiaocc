#!/bin/sh
tmp=`mktemp -d /tmp/xiaocc-test-XXXXXX`
trap 'rm -rf $tmp' INT TERM HUP EXIT
echo > $tmp/empty.c

check() {
    if [ $? -eq 0 ]; then
        echo "testing $1 ... passed"
    else
        echo "testing $1 ... failed"
        exit 1
    fi
}

QEMU=qemu-aarch64

# -o
rm -f $tmp/out
"$QEMU" ./xiaocc -o $tmp/out $tmp/empty.c
[ -f $tmp/out ]
check -o

# --help
"$QEMU" ./xiaocc --help 2>&1 | grep -q xiaocc
check --help

echo OK
