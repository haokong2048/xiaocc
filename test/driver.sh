#!/bin/bash
chibicc=$1

# --help
$chibicc --help 2>&1 | grep -q xiaocc
echo "testing --help ... passed"

echo OK
