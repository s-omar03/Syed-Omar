#!/bin/bash
set -e

cd "$(dirname "$0")/.."

./source2html tests/test.c

echo "--- Checking output.html was generated ---"
test -f output.html && echo "OK: output.html exists"

echo "--- Checking escaped-quote string wasn't corrupted ---"
if grep -q "quoted" output.html; then
    echo "OK: escaped-quote content survived parsing"
else
    echo "FAIL: escaped-quote content missing from output"
    exit 1
fi

echo "--- Checking float/hex numbers were captured whole ---"
if grep -q "3.14159f" output.html && grep -q "0xFF" output.html; then
    echo "OK: float and hex literals captured as single tokens"
else
    echo "FAIL: numeric literal was split or mishandled"
    exit 1
fi

echo "All tests passed."
