#!/bin/bash
chibicc=$1

tmp=`mktemp -d /tmp/chibicc-test-XXXXXX`
trap 'rm -rf $tmp' INT TERM HUP EXIT

check() {
    if [ $? -eq 0 ]; then
        echo "testing $1 ... passed"
    else
        echo "testing $1 ... failed"
        exit 1
    fi
}

# -o
echo > $tmp/empty.c
$chibicc -o $tmp/out $tmp/empty.c 2>/dev/null
[ -f $tmp/out ]
check -o

# --help
$chibicc --help 2>&1 | grep -q xiaocc
check --help

echo OK
