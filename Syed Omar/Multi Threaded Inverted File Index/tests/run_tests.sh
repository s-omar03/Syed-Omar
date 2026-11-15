#!/bin/sh
#
# Automated tests for the inverted index search tool.
# Run from the project directory:  make test   (or: sh tests/run_tests.sh)
#
# Everything runs in a temporary directory, so an index.dat in the project
# directory is never touched.

PROJECT_DIR=$(cd "$(dirname "$0")/.." && pwd)
BIN="$PROJECT_DIR/search"
PASS=0
FAIL=0

if [ ! -x "$BIN" ]; then
    echo "search binary not found - run 'make' first" >&2
    exit 1
fi

WORK=$(mktemp -d) || exit 1
trap 'rm -rf "$WORK"' EXIT
cp "$PROJECT_DIR/sample1.txt" "$PROJECT_DIR/sample2.txt" "$WORK/"
cd "$WORK" || exit 1

pass() {
    PASS=$((PASS + 1))
    echo "  PASS  $1"
}

fail() {
    FAIL=$((FAIL + 1))
    echo "  FAIL  $1"
    if [ -n "$2" ]; then
        printf '%s\n' "$2" | sed 's/^/        /'
    fi
}

# run <command...>: stores combined stdout+stderr in OUT and exit code in STATUS
run() {
    OUT=$("$@" 2>&1)
    STATUS=$?
}

check_eq() {        # <name> <expected> <actual>
    if [ "$2" = "$3" ]; then
        pass "$1"
    else
        fail "$1" "expected: [$2]
got:      [$3]"
    fi
}

check_contains() {  # <name> <text> <output>
    case "$3" in
        *"$2"*) pass "$1" ;;
        *)      fail "$1" "missing: [$2]
in:      [$3]" ;;
    esac
}

check_status() {    # <name> <expected> <actual>
    if [ "$2" -eq "$3" ]; then
        pass "$1"
    else
        fail "$1" "exit status $3, expected $2"
    fi
}

# Byte at offset $2 of file $1 (empty past the end of the file)
byte_at() {
    dd if="$1" bs=1 skip="$2" count=1 2>/dev/null
}

# check_positions <word> <file...>: each reported position must start a
# whole-word match (the first CHECK_LIMIT results are read back from the
# files) and the number of results must equal grep's count.
CHECK_LIMIT=300
check_positions() {
    word=$1
    shift
    run "$BIN" search "$word"
    bad=""
    found=0
    while IFS= read -r line; do
        [ -z "$line" ] && continue
        found=$((found + 1))
        [ "$found" -gt "$CHECK_LIMIT" ] && continue
        file=${line% at position *}
        pos=${line##* }
        len=${#word}
        text=$(dd if="$file" bs=1 skip="$pos" count="$len" 2>/dev/null | tr 'A-Z' 'a-z')
        after=$(byte_at "$file" $((pos + len)))
        before=""
        [ "$pos" -gt 0 ] && before=$(byte_at "$file" $((pos - 1)))
        if [ "$text" != "$word" ]; then
            bad="$line: found '$text'"
        fi
        case "$before$after" in
            *[A-Za-z0-9]*) bad="$line: not a whole word" ;;
        esac
    done <<EOF
$OUT
EOF
    expected=$(cat "$@" | grep -o -i -w "$word" | wc -l)
    if [ -z "$bad" ] && [ "$found" -eq "$expected" ]; then
        pass "'$word': $found results, positions verified against the files"
    else
        fail "'$word' positions" "$bad (results: $found, grep count: $expected)"
    fi
}

echo "== Spec examples (sample1.txt, sample2.txt)"
run "$BIN" index sample1.txt sample2.txt
check_status "index exits with 0" 0 "$STATUS"
check_contains "header line" "Indexing 2 files using 2 threads..." "$OUT"
check_contains "completion line" "Indexing complete. Index saved to index.dat" "$OUT"
check_contains "timing line" "seconds (threads=2, files=2)" "$OUT"
lines=$(printf '%s\n' "$OUT" | grep -c '^Thread [0-9][0-9]* indexing sample[12]\.txt$')
check_eq "one 'Thread ... indexing' line per file" 2 "$lines"
if [ -f index.dat ]; then pass "index.dat created"; else fail "index.dat created"; fi

EXPECTED="sample1.txt at position 79
sample2.txt at position 44"
run "$BIN" search "success"
check_eq "search success" "$EXPECTED" "$OUT"
run "$BIN" search "SUCCESS"
check_eq "search is case-insensitive" "$EXPECTED" "$OUT"
run "$BIN" search "Success."
check_eq "punctuation around the query is ignored" "$EXPECTED" "$OUT"

run "$BIN" search "the"
sorted=$(printf '%s\n' "$OUT" | sort -t ' ' -k1,1 -k4,4n)
check_eq "results ordered by file, then position" "$sorted" "$OUT"

echo "== Every reported position really holds the word"
for word in the and hard reading time is in to a of success one; do
    check_positions "$word" sample1.txt sample2.txt
done

echo "== Search command"
run "$BIN" search zzzz
check_eq "word not in index" "No matches found for 'zzzz'." "$OUT"
check_status "word not in index exits with 0" 0 "$STATUS"
run "$BIN" search -v success
check_contains "-v prints a summary" "Found 2 occurrence(s) in 2 file(s)." "$OUT"
run "$BIN" search "two words"
check_status "multi-word query rejected" 1 "$STATUS"
run "$BIN" search "!!!"
check_status "query without letters rejected" 1 "$STATUS"
run "$BIN" search
check_status "missing search word rejected" 1 "$STATUS"

echo "== Delete command and missing index"
run "$BIN" delete
check_eq "delete" "Index file 'index.dat' deleted successfully." "$OUT"
if [ ! -f index.dat ]; then pass "index.dat removed"; else fail "index.dat removed"; fi
run "$BIN" delete
check_status "delete without index fails" 1 "$STATUS"
run "$BIN" search "test"
check_eq "search without index" "Error: No index exists. Please index files first." "$OUT"
check_status "search without index exits with 1" 1 "$STATUS"

echo "== Command line errors"
run "$BIN" invalid_command
check_status "invalid command exits with 1" 1 "$STATUS"
check_contains "invalid command message" "Invalid command." "$OUT"
check_contains "usage is printed" "index <file1> <file2> ...  # Create index for given files" "$OUT"
run "$BIN" index
check_status "index without files" 1 "$STATUS"
run "$BIN" index -t 0 sample1.txt
check_status "-t 0 rejected" 1 "$STATUS"
run "$BIN" index -t abc sample1.txt
check_status "-t abc rejected" 1 "$STATUS"
run "$BIN" index -t
check_status "-t without value rejected" 1 "$STATUS"
run "$BIN" index -x sample1.txt
check_status "unknown option rejected" 1 "$STATUS"

echo "== Unreadable input files"
run "$BIN" index nonexistent.txt
check_contains "thread line for missing file" "indexing nonexistent.txt" "$OUT"
check_contains "missing file warning" "Warning: Could not open file nonexistent.txt" "$OUT"
check_status "nothing indexed exits with 1" 1 "$STATUS"
if [ ! -f index.dat ]; then pass "no index written when nothing was indexed"; else fail "no index written when nothing was indexed"; fi

run "$BIN" index sample1.txt nonexistent.txt
check_status "partial failure still saves the index" 0 "$STATUS"
check_contains "partial failure note" "1 of 2 files could not be indexed" "$OUT"
run "$BIN" search success
check_eq "only the readable file is indexed" "sample1.txt at position 79" "$OUT"

mkdir somedir
run "$BIN" index somedir
check_contains "directory is reported" "Warning: Could not read file somedir" "$OUT"

echo "== Thread handling"
run "$BIN" index -t 4 sample1.txt
check_contains "threads capped at file count" "Indexing 1 files using 1 threads..." "$OUT"
run "$BIN" index sample1.txt sample2.txt sample1.txt
check_contains "duplicate file names ignored" "1 duplicate file name(s) ignored" "$OUT"
check_contains "duplicates not counted" "Indexing 2 files using 2 threads..." "$OUT"

echo "== Tokenizer edge cases"
printf 'Hello HELLO hello' > case.txt
printf 'alpha beta' > nonewline.txt
: > empty.txt
printf 'year 2024, caf\303\251 time' > mixed.txt
long=$(printf '%0300d' 0 | tr 0 a)
printf '%s end\n' "$long" > long.txt
printf 'spaced out\n' > "with space.txt"
run "$BIN" index case.txt nonewline.txt empty.txt mixed.txt long.txt "with space.txt"
check_status "edge case files indexed" 0 "$STATUS"
run "$BIN" search hello
check_eq "mixed case occurrences" "case.txt at position 0
case.txt at position 6
case.txt at position 12" "$OUT"
run "$BIN" search beta
check_eq "word at end of file without newline" "nonewline.txt at position 6" "$OUT"
run "$BIN" search 2024
check_eq "numbers are words" "mixed.txt at position 5" "$OUT"
run "$BIN" search "$(printf 'caf\303\251')"
check_eq "UTF-8 word kept whole" "mixed.txt at position 11" "$OUT"
run "$BIN" search time
check_eq "position counted in bytes after UTF-8" "mixed.txt at position 17" "$OUT"
run "$BIN" search "$long"
check_eq "300-character word" "long.txt at position 0" "$OUT"
run "$BIN" search end
check_eq "word after a long word" "long.txt at position 301" "$OUT"
run "$BIN" search spaced
check_eq "file name with a space" "with space.txt at position 0" "$OUT"

echo "== Thread safety: index identical for 1, 2, 3 and 8 threads"
sh "$PROJECT_DIR/tests/gen_corpus.sh" corpus 24 150000 7 || exit 1
"$BIN" index -t 1 corpus/*.txt > /dev/null 2>&1
cp index.dat reference.dat
for threads in 2 3 8 8 8; do
    run "$BIN" index -t "$threads" corpus/*.txt
    if cmp -s index.dat reference.dat; then
        pass "-t $threads gives a byte-identical index"
    else
        fail "-t $threads gives a byte-identical index"
    fi
done
check_positions the corpus/*.txt
check_positions of corpus/*.txt

echo "== Corrupted index files"
"$BIN" index sample1.txt sample2.txt > /dev/null 2>&1
cp index.dat good.dat
size=$(wc -c < good.dat)

head -c $((size / 2)) good.dat > index.dat
run "$BIN" search success
check_contains "truncated index detected" "corrupted" "$OUT"
check_status "truncated index exits with 1" 1 "$STATUS"

printf 'not an index file' > index.dat
run "$BIN" search success
check_contains "foreign file detected" "corrupted" "$OUT"

: > index.dat
run "$BIN" search success
check_contains "empty index file detected" "corrupted" "$OUT"

crashed=""
offset=0
while [ "$offset" -lt "$size" ]; do
    cp good.dat index.dat
    printf '\377' | dd of=index.dat bs=1 seek="$offset" conv=notrunc 2>/dev/null
    run "$BIN" search the
    if [ "$STATUS" -gt 1 ]; then
        crashed="$crashed $offset"
    fi
    offset=$((offset + 3))
done
if [ -z "$crashed" ]; then
    pass "damaged bytes never crash the search"
else
    fail "damaged bytes never crash the search" "crashed at offsets:$crashed"
fi

echo
echo "Passed: $PASS  Failed: $FAIL"
[ "$FAIL" -eq 0 ]
