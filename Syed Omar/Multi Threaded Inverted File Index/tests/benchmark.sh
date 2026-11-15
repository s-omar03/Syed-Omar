#!/bin/sh
#
# Benchmark: indexes a synthetic document collection with 1, 2, 4 and 8
# threads, then reports index size, peak memory and search times.
#
# Usage: sh tests/benchmark.sh [file_count] [bytes_per_file]
#        (default: 100 files of ~1MB, like the project specification)

PROJECT_DIR=$(cd "$(dirname "$0")/.." && pwd)
BIN="$PROJECT_DIR/search"
FILES=${1:-100}
SIZE=${2:-1000000}

if [ ! -x "$BIN" ]; then
    echo "search binary not found - run 'make' first" >&2
    exit 1
fi

WORK=$(mktemp -d) || exit 1
trap 'rm -rf "$WORK"' EXIT
cd "$WORK" || exit 1

echo "Generating $FILES files of ~$SIZE bytes..."
sh "$PROJECT_DIR/tests/gen_corpus.sh" docs "$FILES" "$SIZE" 2024 || exit 1
echo "Collection size: $(cat docs/*.txt | wc -c | tr -d ' ') bytes"
echo "CPU cores:       $(getconf _NPROCESSORS_ONLN 2>/dev/null || echo unknown)"
echo

echo "Indexing:"
for threads in 1 2 4 8; do
    "$BIN" index -t "$threads" docs/*.txt | grep '^Elapsed'
done
echo "Index file size: $(wc -c < index.dat | tr -d ' ') bytes"

# Peak memory, when GNU time is installed
if /usr/bin/time -v true > /dev/null 2>&1; then
    /usr/bin/time -v "$BIN" index -t 8 docs/*.txt > /dev/null 2> time.log
    echo "Peak memory (index, 8 threads): $(grep 'Maximum resident' time.log | awk '{print $NF}') KB"
    /usr/bin/time -v "$BIN" search the > /dev/null 2> time.log
    echo "Peak memory (search):           $(grep 'Maximum resident' time.log | awk '{print $NF}') KB"
fi
echo

echo "Searching (index load + hash table lookup):"
for word in the and kaka zzzz; do
    echo "-- $word"
    "$BIN" search -v "$word" | tail -3
done
