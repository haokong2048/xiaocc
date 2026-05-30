#!/bin/bash

QEMU=qemu-aarch64
CC=aarch64-linux-gnu-gcc

assert() {
    expected="$1"
    input="$2"

    "$QEMU" ./xiaocc "$input" > tmp.s || exit
    "$CC" -static -o tmp tmp.s
    "$QEMU" ./tmp
    actual="$?"

    if [ "$actual" = "$expected" ]; then
        echo "$input => $actual"
    else
        echo -e "$input => $expected expected, but got $actual"
        exit 1
    fi
}

assert 0 0
assert 42 42

echo OK
