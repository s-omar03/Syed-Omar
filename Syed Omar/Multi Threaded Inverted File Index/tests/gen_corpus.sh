#!/bin/sh
#
# Generates synthetic text documents for the tests and the benchmark.
# Word frequencies are skewed (a few very common words, many rare ones),
# roughly like real text.
#
# Usage: sh tests/gen_corpus.sh <output_dir> <file_count> <bytes_per_file> [seed]

if [ $# -lt 3 ]; then
    echo "Usage: $0 <output_dir> <file_count> <bytes_per_file> [seed]" >&2
    exit 1
fi

OUT_DIR=$1
COUNT=$2
SIZE=$3
SEED=${4:-1}

mkdir -p "$OUT_DIR" || exit 1

awk -v dir="$OUT_DIR" -v count="$COUNT" -v size="$SIZE" -v seed="$SEED" '
BEGIN {
    srand(seed)
    nsyl = split("ka lo mi ne ru sa ti vo be da fe gi hu jo ma pe qu ri so tu wa xe yo ze", syl, " ")
    nstop = split("the of and to in is it that for on with as was at by a an be this from", stop, " ")

    vocab_size = 20000
    for (i = 1; i <= vocab_size; i++) {
        word = ""
        parts = 2 + int(rand() * 3)
        for (j = 0; j < parts; j++)
            word = word syl[1 + int(rand() * nsyl)]
        vocab[i] = word
    }

    for (f = 1; f <= count; f++) {
        file = sprintf("%s/doc%04d.txt", dir, f)
        written = 0
        line = ""
        while (written < size) {
            if (rand() < 0.35)
                word = stop[1 + int(rand() * nstop)]
            else
                word = vocab[1 + int(vocab_size * rand() * rand() * rand())]
            if (rand() < 0.05)
                word = toupper(substr(word, 1, 1)) substr(word, 2)
            if (rand() < 0.07)
                word = word ","
            line = (line == "") ? word : line " " word
            if (length(line) >= 72) {
                print line > file
                written += length(line) + 1
                line = ""
            }
        }
        if (line != "")
            print line > file
        close(file)
    }
}'
