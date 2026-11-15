#!/bin/bash
#
# Automated tests for the TFTP server and client.
# Run from the project directory:  make test   (or: bash tests/run_tests.sh)
#
# Needs only bash and coreutils. Everything runs in a temporary directory;
# the servers listen on random high UDP ports and are stopped at the end.
#
#   TEST_TIMEOUT=<seconds>  limit for every wait and client run (default 30)
#   TEST_LARGE=0            skip the 33 MB block-number wraparound test

PROJECT_DIR=$(cd "$(dirname "$0")/.." && pwd)
SERVER="$PROJECT_DIR/tftp_server"
CLIENT="$PROJECT_DIR/tftp_client"
UNIT="$PROJECT_DIR/tests/unit_tests"
PROBE="$PROJECT_DIR/tests/tftp_probe"
TEST_TIMEOUT=${TEST_TIMEOUT:-30}
TEST_LARGE=${TEST_LARGE:-1}
LARGE_TIMEOUT=$((TEST_TIMEOUT * 4))
PASS=0
FAIL=0
SERVER_PIDS=""
CLIENT_ENV=""
CLIENT_ARGS=""
SERVER_FSIZE=""

for bin in "$SERVER" "$CLIENT" "$UNIT" "$PROBE"; do
    if [ ! -x "$bin" ]; then
        echo "$bin not found - run 'make test' to build everything" >&2
        exit 1
    fi
done

WORK=$(mktemp -d) || exit 1
SRV="$WORK/srv"
CLI="$WORK/cli"
mkdir -p "$SRV" "$CLI" || exit 1

# ---------------------------------------------------------------- helpers

pass() {
    PASS=$((PASS + 1))
    echo "  PASS  $1"
}

fail() {
    FAIL=$((FAIL + 1))
    echo "  FAIL  $1"
    if [ -n "$2" ]; then
        while IFS= read -r line; do
            echo "        $line"
        done <<< "$2"
    fi
}

check() {           # <name> <command...>: passes if the command succeeds
    local name=$1
    shift
    if "$@"; then pass "$name"; else fail "$name"; fi
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

check_not_contains() {  # <name> <text> <output>
    case "$3" in
        *"$2"*) fail "$1" "unexpected: [$2]
in:         [$3]" ;;
        *)      pass "$1" ;;
    esac
}

check_status() {    # <name> <expected> <actual>
    if [ "$2" -eq "$3" ]; then
        pass "$1"
    else
        fail "$1" "exit status $3, expected $2"
    fi
}

check_same() {      # <name> <expected file> <actual file>
    if [ ! -f "$3" ]; then
        fail "$1" "$3 does not exist"
    elif cmp -s "$2" "$3"; then
        pass "$1"
    else
        fail "$1" "$3 differs from $2 ($(wc -c < "$2") vs $(wc -c < "$3") bytes)"
    fi
}

count_matches() {   # <text> <output>: number of lines containing <text>
    local n=0 line
    while IFS= read -r line; do
        case "$line" in *"$1"*) n=$((n + 1)) ;; esac
    done <<< "$2"
    echo "$n"
}

reply_port() {      # <probe output>: the "from=" port of its first line
    if [[ $1 =~ from=([0-9]+) ]]; then echo "${BASH_REMATCH[1]}"; else echo 0; fi
}

no_part_files() {   # <dir>: no download temporary files left behind
    local f
    for f in "$1"/*.part; do
        [ -e "$f" ] && return 1
    done
    return 0
}

# client_input <dir> <text>: runs tftp_client in <dir> with <text> on stdin.
# Output in OUT, exit status in STATUS. CLIENT_ENV and CLIENT_ARGS add
# environment variables and command line arguments.
client_input() {
    OUT=$(cd "$1" && printf '%s' "$2" | timeout "$TEST_TIMEOUT" env $CLIENT_ENV "$CLIENT" $CLIENT_ARGS 2>&1)
    STATUS=$?
    printf '%s\n' "$OUT" >> "$WORK/client.log"
}

run_client() {      # <dir> <command...>: one command per argument, then "bye"
    local dir=$1
    shift
    client_input "$dir" "$(printf '%s\n' "$@" bye)
"
}

session_on() {      # <port> <dir> <command...>: like run_client, connected first
    local port=$1 dir=$2
    shift 2
    run_client "$dir" "connect 127.0.0.1 $port" "$@"
}

session() {         # <dir> <command...>: session with the main server
    session_on "$PORT" "$@"
}

proc_fields() {     # <stat file>: sets FIELDS to "state ppid ..." of a process
    local line
    FIELDS=()
    read -r line 2>/dev/null < "$1" || return 1
    read -r -a FIELDS <<< "${line##*) }"
}

child_pids() {      # <ppid>: pids of the children of a process (from /proc)
    local f pid
    for f in /proc/[0-9]*/stat; do
        proc_fields "$f" || continue
        if [ "${FIELDS[1]}" = "$1" ]; then
            pid=${f#/proc/}
            echo "${pid%/stat}"
        fi
    done
}

pid_of() {          # <command name>: pid of a running process with that name
    local f pid line
    for f in /proc/[0-9]*/stat; do
        read -r line 2>/dev/null < "$f" || continue
        case "$line" in
            *"($1)"*) pid=${f#/proc/}; echo "${pid%/stat}"; return 0 ;;
        esac
    done
    return 1
}

part_has_data() {   # <file prefix>: a non-empty "<prefix>.<pid>.part" exists
    local f
    for f in "$1".*.part; do
        [ -s "$f" ] && return 0
    done
    return 1
}

no_zombies() {      # <ppid>: true if the process has no zombie children
    local f
    for f in /proc/[0-9]*/stat; do
        proc_fields "$f" || continue
        [ "${FIELDS[1]}" = "$1" ] && [ "${FIELDS[0]}" = "Z" ] && return 1
    done
    return 0
}

wait_until() {      # <command...>: polls until it succeeds or TEST_TIMEOUT passes
    local deadline=$((SECONDS + TEST_TIMEOUT))
    until "$@"; do
        [ "$SECONDS" -ge "$deadline" ] && return 1
        sleep 0.1
    done
    return 0
}

server_ready() {    # <port> <pid>: the server answers an RRQ with "File not found"
    kill -0 "$2" 2>/dev/null || return 2
    case $(PROBE_TIMEOUT_MS=200 "$PROBE" rrq "$1" __ready_check__ octet 2>/dev/null) in
        *"code=1"*) return 0 ;;
    esac
    return 1
}

# start_server <dir> <log> [VAR=value...]: starts a server in the background
# on a random free port and waits until it answers. Sets STARTED_PORT and
# STARTED_PID. SERVER_FSIZE, if set, limits the size of files it may write.
start_server() {
    local dir=$1 log=$2 attempt port pid rc deadline
    shift 2
    for attempt in 1 2 3; do
        port=$((40000 + RANDOM % 20000))
        (
            if [ -n "$SERVER_FSIZE" ]; then ulimit -f "$SERVER_FSIZE" || exit 1; fi
            exec env "$@" "$SERVER" -p "$port" -d "$dir" -v >> "$log" 2>&1
        ) &
        pid=$!
        deadline=$((SECONDS + TEST_TIMEOUT))
        while :; do
            server_ready "$port" "$pid"     # each try waits up to 200 ms
            rc=$?
            [ "$rc" -ne 1 ] && break
            [ "$SECONDS" -ge "$deadline" ] && break
        done
        if [ "$rc" -eq 0 ]; then
            SERVER_PIDS="$SERVER_PIDS $pid"
            STARTED_PORT=$port
            STARTED_PID=$pid
            return 0
        fi
        kill "$pid" 2>/dev/null
        wait "$pid" 2>/dev/null
    done
    return 1
}

stop_server() {     # <pid>: stops a server and any transfer it is still serving
    local kids
    kids=$(child_pids "$1")
    kill "$1" $kids 2>/dev/null
    wait "$1" 2>/dev/null
}

cleanup() {
    local pid
    for pid in $SERVER_PIDS; do
        stop_server "$pid"
    done
    rm -rf "$WORK"
}
trap cleanup EXIT

finish() {
    echo
    echo "Passed: $PASS  Failed: $FAIL"
    [ "$FAIL" -eq 0 ]
    exit $?
}

make_crlf_file() {  # <path>: CR, LF and NUL in many combinations and offsets
    local i
    for ((i = 1; i <= 400; i++)); do
        printf 'line %d\r\n\n\r\0\r\r\n\n\r%.*s' "$i" $((i % 7)) xxxxxxx
    done > "$1"
}

# ------------------------------------------------------------------ tests

echo "== Unit tests (tests/unit_tests)"
UNIT_OUT=$("$UNIT" 2>&1)
UNIT_STATUS=$?
printf '%s\n' "$UNIT_OUT" >> "$WORK/client.log"
while IFS= read -r line; do
    case "$line" in
        "  PASS  "*) pass "${line#  PASS  }" ;;
        "  FAIL  "*) fail "${line#  FAIL  }" ;;
    esac
done <<< "$UNIT_OUT"
check_status "unit test program exits with 0" 0 "$UNIT_STATUS"

echo "== Client commands without a server"
run_client "$CLI" help
check_status "bye exits with 0" 0 "$STATUS"
check_contains "banner" "Type help to get supported features" "$OUT"
check_contains "prompt" "tftp] " "$OUT"
for word in "connect <server-ip>" "get <file-name>" "put <file-name>" "mode [octet|netascii]" \
            "bye / quit"; do
    check_contains "help lists '$word'" "$word" "$OUT"
done
run_client "$CLI" "get syslog"
check_contains "get before connect" "ERROR: Not connected to server" "$OUT"
run_client "$CLI" "put README"
check_contains "put before connect" "ERROR: Not connected to server" "$OUT"
run_client "$CLI" tata
check_contains "unknown command" "Invalid command" "$OUT"
run_client "$CLI" "mode mail"
check_contains "mode mail is refused" "ERROR: Mode 'mail' is obsolete" "$OUT"
run_client "$CLI" "mode binary"
check_contains "unknown mode is refused" "ERROR: Unknown mode 'binary'" "$OUT"
run_client "$CLI" mode
check_contains "default mode is netascii" "INFO: Transfer Mode: netascii" "$OUT"
run_client "$CLI" "mode OCTET"
check_contains "mode names are case-insensitive" "INFO: Transfer Mode: octet" "$OUT"
run_client "$CLI" "connect 999.1.1.1"
check_contains "invalid server address" "ERROR: Invalid server address" "$OUT"
run_client "$CLI" "connect 127.0.0.1 70000"
check_contains "invalid port" "ERROR: Invalid port" "$OUT"
run_client "$CLI" get
check_contains "get without a file name" "ERROR: Usage: get <file-name>" "$OUT"
run_client "$CLI" "get a b c d"
check_contains "too many arguments" "ERROR: Too many arguments" "$OUT"
client_input "$CLI" "quit
help
"
check_not_contains "quit leaves the client" "Connect to server" "$OUT"
check_status "quit exits with 0" 0 "$STATUS"
client_input "$CLI" "help
"
check_status "end of input exits with 0" 0 "$STATUS"
OUT=$("$CLIENT" -h 2>&1)
check_contains "client -h prints usage" "Usage:" "$OUT"
"$CLIENT" a b c > /dev/null 2>&1 < /dev/null
check_status "client with too many arguments exits with 1" 1 "$?"

echo "== Server command line"
OUT=$(timeout 5 "$SERVER" -h 2>&1)
check_status "server -h exits with 0" 0 "$?"
check_contains "server -h prints usage" "Usage:" "$OUT"
OUT=$(timeout 5 "$SERVER" -p 0 2>&1)
check_status "port 0 rejected" 1 "$?"
OUT=$(timeout 5 "$SERVER" -p 70000 2>&1)
check_status "port 70000 rejected" 1 "$?"
OUT=$(timeout 5 "$SERVER" -p 45000 -d "$WORK/no_such_dir" 2>&1)
check_status "missing directory rejected" 1 "$?"
check_contains "missing directory message" "cannot open directory" "$OUT"
OUT=$(timeout 5 "$SERVER" extra_argument 2>&1)
check_status "unexpected argument rejected" 1 "$?"

echo "== No server listening"
NOBODY_PORT=$((40000 + RANDOM % 20000))
CLIENT_ENV="TFTP_TIMEOUT_MS=100"
start=$SECONDS
session_on "$NOBODY_PORT" "$CLI" "get anything.txt"
CLIENT_ENV=""
check_contains "timeout reported" "ERROR: Server not responding" "$OUT"
check "gives up after the retry limit" [ $((SECONDS - start)) -lt 10 ]
check "no temporary file left after a timeout" no_part_files "$CLI"

echo "== Server start"
if start_server "$SRV" "$WORK/server.log"; then
    PORT=$STARTED_PORT
    SERVER_PID=$STARTED_PID
    pass "server runs in the background on UDP port $PORT"
else
    fail "server runs in the background" "$(cat "$WORK/server.log" 2>/dev/null)"
    finish
fi

CLIENT_ARGS="127.0.0.1 $PORT"
client_input "$CLI" "bye
"
CLIENT_ARGS=""
check_contains "connect from the command line" "Type help to get supported features
INFO: Connected to server" "$OUT"
run_client "$CLI" "connect 127.0.0.1 $PORT"
check_contains "connect command" "INFO: Connected to server" "$OUT"

echo "== Transfers in octet and netascii mode"
SIZES=(0 1 511 512 513 1024 1048576 1000001)
for mode in octet netascii; do
    put_cmds=("mode $mode")
    get_cmds=("mode $mode")
    for size in "${SIZES[@]}"; do
        head -c "$size" /dev/urandom > "$CLI/put_${mode}_$size.bin"
        head -c "$size" /dev/urandom > "$SRV/get_${mode}_$size.bin"
        put_cmds+=("put put_${mode}_$size.bin")
        get_cmds+=("get get_${mode}_$size.bin")
    done

    session "$CLI" "${put_cmds[@]}"
    check_eq "$mode: every put reports 'File transfer successful'" "${#SIZES[@]}" \
             "$(count_matches "INFO: File transfer successful" "$OUT")"
    check_contains "$mode: put prints the file name" "INFO: File: put_${mode}_0.bin" "$OUT"
    check_contains "$mode: put prints the mode" "INFO: Transfer Mode: $mode" "$OUT"
    check_contains "$mode: progress bar" "#" "$OUT"
    for size in "${SIZES[@]}"; do
        check_same "$mode put, $size bytes" "$CLI/put_${mode}_$size.bin" "$SRV/put_${mode}_$size.bin"
    done

    session "$CLI" "${get_cmds[@]}"
    check_eq "$mode: every get reports 'File receive successful'" "${#SIZES[@]}" \
             "$(count_matches "INFO: File receive successful" "$OUT")"
    for size in "${SIZES[@]}"; do
        check_same "$mode get, $size bytes" "$SRV/get_${mode}_$size.bin" "$CLI/get_${mode}_$size.bin"
    done
done

echo "== Netascii conversion"
printf 'line one\nline two\n\nbare CR\r here, CR LF\r\n here\nno newline at the end' > "$CLI/text.txt"
make_crlf_file "$CLI/crlf.bin"
head -c 256 /dev/zero | tr '\0' '\n' > "$CLI/lf256.txt"
{ head -c 511 /dev/zero | tr '\0' 'x'; printf '\r'; head -c 510 /dev/zero | tr '\0' 'y'; printf '\r\n\n'; } \
    > "$CLI/edges.txt"
session "$CLI" "mode netascii" "put text.txt" "put crlf.bin" "put lf256.txt" "put edges.txt"
check_same "netascii put of Unix text stores the same text" "$CLI/text.txt" "$SRV/text.txt"
check_same "netascii put of CR/LF/NUL data" "$CLI/crlf.bin" "$SRV/crlf.bin"
check_same "netascii put that encodes to exactly 512 bytes" "$CLI/lf256.txt" "$SRV/lf256.txt"
check_same "netascii put with pairs on block boundaries" "$CLI/edges.txt" "$SRV/edges.txt"
for f in text.txt crlf.bin lf256.txt edges.txt; do
    cp "$CLI/$f" "$SRV/back_$f"
done
session "$CLI" "mode netascii" "get back_text.txt" "get back_crlf.bin" "get back_lf256.txt" \
        "get back_edges.txt"
for f in text.txt crlf.bin lf256.txt edges.txt; do
    check_same "netascii get round trip of $f" "$CLI/$f" "$CLI/back_$f"
done

printf 'a\nb\rc\0d' > "$SRV/wire.txt"
OUT=$("$PROBE" rrq "$PORT" wire.txt netascii)
check_contains "netascii on the wire: LF -> CR LF, CR -> CR NUL" "len=9 data=610d0a620d00630064" "$OUT"
OUT=$("$PROBE" rrq "$PORT" wire.txt octet)
check_contains "octet on the wire: bytes unchanged" "len=7 data=610a620d630064" "$OUT"
{ head -c 511 /dev/zero | tr '\0' 'x'; printf '\n'; } > "$SRV/edge511.txt"
OUT=$("$PROBE" rrq "$PORT" edge511.txt netascii)
check_contains "netascii: block 1 is filled to 512 bytes" "block=1 len=512" "$OUT"
check "netascii: CR of a CR LF pair ends block 1" [ "${OUT: -4}" = "780d" ]

echo "== Error handling"
session "$CLI" "get missing.txt"
check_contains "get of a missing file" "INFO: File: missing.txt
INFO: Transfer Mode: netascii
ERROR: File not found" "$OUT"
check "no local file created for a failed get" [ ! -e "$CLI/missing.txt" ]
check "no temporary file left after a failed get" no_part_files "$CLI"
printf 'precious\n' > "$CLI/keep.txt"
session "$CLI" "get keep.txt"
check_contains "get of a missing file over a local file" "ERROR: File not found" "$OUT"
check_eq "failed get leaves the local file untouched" "precious" "$(cat "$CLI/keep.txt")"
printf 'server copy\n' > "$SRV/exists.txt"
printf 'client copy\n' > "$CLI/exists.txt"
session "$CLI" "put exists.txt"
check_contains "put of an existing file" "INFO: File: exists.txt
INFO: Transfer Mode: netascii
ERROR: File already exist" "$OUT"
check_eq "existing server file untouched" "server copy" "$(cat "$SRV/exists.txt")"
session "$CLI" "put no_such_local_file.txt"
check_contains "put of a missing local file" "ERROR: Cannot open local file" "$OUT"
mkdir -p "$SRV/subdir" "$CLI/adir"
printf 'inner\n' > "$SRV/subdir/inner.txt"
session "$CLI" "get subdir"
check_contains "get of a directory" "ERROR: Access violation" "$OUT"
session "$CLI" "put adir"
check_contains "put of a local directory" "ERROR: Cannot open local file 'adir'" "$OUT"
session "$CLI" "get subdir/inner.txt"
check_same "get from a subdirectory (saved under its base name)" "$SRV/subdir/inner.txt" "$CLI/inner.txt"
printf 'upload\n' > "$CLI/up.txt"
session "$CLI" "put up.txt subdir/up.txt"
check_same "put into a subdirectory" "$CLI/up.txt" "$SRV/subdir/up.txt"
session "$CLI" "put up.txt no_dir/up.txt"
check_contains "put into a missing directory" "ERROR: File not found" "$OUT"
session "$CLI" "get subdir/inner.txt local_name.txt"
check_same "get with a local name" "$SRV/subdir/inner.txt" "$CLI/local_name.txt"
printf 'old\n' > "$CLI/replace.txt"
printf 'new\n' > "$SRV/replace.txt"
session "$CLI" "get replace.txt"
check_eq "successful get replaces the local file" "new" "$(cat "$CLI/replace.txt")"
run_client "$CLI" "connect localhost $PORT" "mode octet" "get wire.txt host_wire.txt"
check_same "connect with a host name" "$SRV/wire.txt" "$CLI/host_wire.txt"

echo "== Path traversal is refused"
printf 'secret\n' > "$WORK/secret.txt"
ln -s "$WORK/secret.txt" "$SRV/link_to_secret"
ln -s "$WORK" "$SRV/link_to_work"
for name in ../secret.txt /etc/passwd subdir/../../secret.txt link_to_secret link_to_work/secret.txt; do
    session "$CLI" "get $name"
    check_contains "get '$name' refused" "ERROR: Access violation" "$OUT"
done
check "nothing outside the directory was downloaded" [ ! -e "$CLI/secret.txt" -a ! -e "$CLI/passwd" ]
for name in ../evil.txt /tmp/evil.txt link_to_work/evil.txt subdir/../../evil.txt; do
    session "$CLI" "put up.txt $name"
    check_contains "put to '$name' refused" "ERROR: Access violation" "$OUT"
done
check "nothing written outside the directory" [ ! -e "$WORK/evil.txt" -a ! -e /tmp/evil.txt ]

echo "== Protocol checks with raw packets (tests/tftp_probe)"
OUT=$("$PROBE" rrq "$PORT" missing.txt octet)
check_contains "RRQ of a missing file -> error 1" "opcode=5 code=1 msg=File not found." "$OUT"
p1=$(reply_port "$OUT")
check "reply comes from a transfer port >= 20000, not the listening port" \
      [ "$p1" -ge 20000 -a "$p1" -ne "$PORT" ]
OUT=$("$PROBE" rrq "$PORT" missing.txt octet)
p2=$(reply_port "$OUT")
check "next request gets the next port ($p1 -> $p2)" [ "$p2" -eq $((p1 + 1)) ]
OUT=$("$PROBE" rrq "$PORT" ../secret.txt octet)
check_contains "RRQ '../secret.txt' -> error 2" "code=2" "$OUT"
OUT=$("$PROBE" wrq "$PORT" /tmp/x octet)
check_contains "WRQ '/tmp/x' -> error 2" "code=2" "$OUT"
OUT=$("$PROBE" wrq "$PORT" exists.txt octet)
check_contains "WRQ of an existing file -> error 6" "code=6 msg=File already exists." "$OUT"
OUT=$("$PROBE" rrq "$PORT" wire.txt mail)
check_contains "mode mail -> error 4" "code=4" "$OUT"
OUT=$("$PROBE" rrq "$PORT" wire.txt binary)
check_contains "unknown mode -> error 4" "code=4" "$OUT"
OUT=$("$PROBE" rrq "$PORT" wire.txt OcTeT)
check_contains "mode names are case-insensitive on the wire" "opcode=3 block=1 len=7" "$OUT"
OUT=$("$PROBE" wrq "$PORT" probe_upload.bin octet)
check_contains "WRQ is answered with ACK 0" "opcode=4 block=0" "$OUT"
wait_until [ ! -e "$SRV/probe_upload.bin" ]
check "an abandoned upload is removed" [ ! -e "$SRV/probe_upload.bin" ]
OUT=$("$PROBE" raw "$PORT" "0001776972652e74787400" )
check_contains "request without a mode -> error 4" "code=4" "$OUT"
OUT=$("$PROBE" raw "$PORT" "000177697265" )
check_contains "file name without NUL -> error 4 from the listening port" "from=$PORT opcode=5 code=4" "$OUT"
OUT=$("$PROBE" raw "$PORT" "0001776972652e747874006f63746574007473697a65003000")
check_contains "RFC 2347 options are ignored (DATA, no OACK)" "opcode=3 block=1" "$OUT"
OUT=$("$PROBE" raw "$PORT" "")
check_contains "empty datagram -> error 4" "code=4" "$OUT"
OUT=$("$PROBE" raw "$PORT" "00")
check_contains "1-byte datagram -> error 4" "code=4" "$OUT"
OUT=$("$PROBE" raw "$PORT" "00090000")
check_contains "unknown opcode -> error 4" "code=4" "$OUT"
OUT=$("$PROBE" raw "$PORT" "00040001")
check_contains "ACK on the listening port -> error 4" "code=4" "$OUT"
OUT=$(PROBE_TIMEOUT_MS=500 "$PROBE" raw "$PORT" "000500006f6f707300")
check_contains "ERROR on the listening port is not answered" "reply timeout" "$OUT"
head -c 1048576 /dev/urandom > "$SRV/big.bin"
OUT=$("$PROBE" tid "$PORT" big.bin)
tport=$(reply_port "$OUT")
check_contains "unknown transfer ID gets error 5" "stranger from=$tport opcode=5 code=5" "$OUT"
check_contains "the transfer goes on after a stranger's packet" "continue from=$tport opcode=3 block=2" "$OUT"
OUT=$("$PROBE" dupack "$PORT" big.bin)
check_contains "duplicate ACK does not resend DATA (Sorcerer's Apprentice)" "data2_count=1" "$OUT"

echo "== Malformed and random packets sent with bash /dev/udp"
for packet in '\x00' '\x00\x01' '\x00\x01abc' '\x00\x02name\x00' '\x00\x01\x00octet\x00' \
              '\x00\x03\x00\x01data' '\x00\x04\x00\x01' '\x00\x05\x00\x01oops' '\xff\xff\xff\xff' \
              '\x00\x01wire.txt\x00mail\x00' '\x00\x02../../evil\x00octet\x00' \
              '\x00\x01wire.txt\x00octet'; do
    printf "$packet" > "/dev/udp/127.0.0.1/$PORT"
done
for ((i = 0; i < 300; i++)); do
    head -c $((1 + RANDOM % 700)) /dev/urandom > "/dev/udp/127.0.0.1/$PORT"
done
check "server survives malformed and random packets" kill -0 "$SERVER_PID"
session "$CLI" "mode octet" "get big.bin big_after_garbage.bin"
check_same "server still serves after the garbage" "$SRV/big.bin" "$CLI/big_after_garbage.bin"

echo "== Concurrent transfers"
pids=""
for i in 1 2 3 4 5 6; do
    mode=octet
    [ $((i % 2)) -eq 0 ] && mode=netascii
    mkdir -p "$WORK/c$i"
    head -c $((200000 + i * 1000)) /dev/urandom > "$WORK/c$i/up_$i.bin"
    (
        cd "$WORK/c$i" && printf 'connect 127.0.0.1 %s\nmode %s\nget big.bin\nput up_%s.bin\nbye\n' \
            "$PORT" "$mode" "$i" | timeout "$TEST_TIMEOUT" "$CLIENT" > out.txt 2>&1
    ) &
    pids="$pids $!"
done
for pid in $pids; do
    wait "$pid"
done
for i in 1 2 3 4 5 6; do
    cat "$WORK/c$i/out.txt" >> "$WORK/client.log"
    check_same "concurrent client $i: get" "$SRV/big.bin" "$WORK/c$i/big.bin"
    check_same "concurrent client $i: put" "$WORK/c$i/up_$i.bin" "$SRV/up_$i.bin"
done

if [ -d /proc/self ]; then
    echo "== Ctrl-C cancels a running transfer"
    # The client drops half of its packets, so the 1 MB download is slow
    (
        cd "$CLI" && printf 'connect 127.0.0.1 %s\nmode octet\nget big.bin cancelled.bin\nbye\n' "$PORT" \
            | TFTP_DROP_PERCENT=50 TFTP_TIMEOUT_MS=200 timeout "$TEST_TIMEOUT" "$CLIENT" \
            > "$WORK/cancel.out" 2>&1
    ) &
    cancel_job=$!
    if wait_until part_has_data "$CLI/cancelled.bin" && cpid=$(pid_of tftp_client); then
        kill -INT "$cpid"
        wait "$cancel_job"
        check_status "client goes on after the cancel and exits with 0" 0 "$?"
        OUT=$(cat "$WORK/cancel.out")
        cat "$WORK/cancel.out" >> "$WORK/client.log"
        check_contains "transfer cancelled" "ERROR: Transfer cancelled by user" "$OUT"
        check_contains "back at the prompt after the cancel" "tftp] bye" "$OUT"
        check "no partial download left" [ ! -e "$CLI/cancelled.bin" ]
        check "no temporary file left after the cancel" no_part_files "$CLI"
    else
        fail "download to cancel did not start"
        wait "$cancel_job"
    fi
fi

echo "== Retransmission with 20% simulated packet loss"
mkdir -p "$WORK/srv_loss"
if start_server "$WORK/srv_loss" "$WORK/server_loss.log" TFTP_DROP_PERCENT=20 TFTP_TIMEOUT_MS=50; then
    LOSS_PORT=$STARTED_PORT
    head -c 150000 /dev/urandom > "$CLI/loss_up.bin"
    head -c 150000 /dev/urandom > "$WORK/srv_loss/loss_get.bin"
    cp "$CLI/crlf.bin" "$WORK/srv_loss/loss_crlf.bin"
    CLIENT_ENV="TFTP_DROP_PERCENT=20 TFTP_TIMEOUT_MS=50"
    session_on "$LOSS_PORT" "$CLI" "mode octet" "put loss_up.bin" "get loss_get.bin" \
               "mode netascii" "put crlf.bin loss_crlf_up.bin" "get loss_crlf.bin"
    CLIENT_ENV=""
    check_same "lossy octet put" "$CLI/loss_up.bin" "$WORK/srv_loss/loss_up.bin"
    check_same "lossy octet get" "$WORK/srv_loss/loss_get.bin" "$CLI/loss_get.bin"
    check_same "lossy netascii put" "$CLI/crlf.bin" "$WORK/srv_loss/loss_crlf_up.bin"
    check_same "lossy netascii get" "$WORK/srv_loss/loss_crlf.bin" "$CLI/loss_crlf.bin"
    retransmitted=0
    while IFS= read -r line; do
        if [[ $line =~ ([0-9]+)\ retransmissions ]]; then
            retransmitted=$((retransmitted + BASH_REMATCH[1]))
        fi
    done < "$WORK/server_loss.log"
    check "packets were really lost and sent again ($retransmitted by the server)" \
          [ "$retransmitted" -gt 0 ]
else
    fail "lossy server started"
fi

echo "== Disk full (file size limit on the server)"
mkdir -p "$WORK/srv_full"
SERVER_FSIZE=64
if start_server "$WORK/srv_full" "$WORK/server_full.log"; then
    FULL_PORT=$STARTED_PORT
    head -c 200000 /dev/urandom > "$CLI/too_big.bin"
    head -c 10000 /dev/urandom > "$CLI/small.bin"
    session_on "$FULL_PORT" "$CLI" "mode octet" "put too_big.bin" "put small.bin"
    check_contains "disk full reported" "ERROR: Disk full or allocation exceeded" "$OUT"
    check "incomplete upload removed" [ ! -e "$WORK/srv_full/too_big.bin" ]
    check_same "small upload still works" "$CLI/small.bin" "$WORK/srv_full/small.bin"
else
    fail "size-limited server started"
fi
SERVER_FSIZE=""

if [ "$TEST_LARGE" != 0 ]; then
    echo "== Block number wraparound (33 MB = more than 65535 blocks)"
    head -c $((65536 * 512 + 1000)) /dev/urandom > "$CLI/huge.bin"
    saved_timeout=$TEST_TIMEOUT
    TEST_TIMEOUT=$LARGE_TIMEOUT
    session "$CLI" "mode octet" "put huge.bin"
    check_same "put of 65538 blocks" "$CLI/huge.bin" "$SRV/huge.bin"
    mv "$SRV/huge.bin" "$SRV/huge_get.bin"
    session "$CLI" "mode octet" "get huge_get.bin"
    check_same "get of 65538 blocks" "$SRV/huge_get.bin" "$CLI/huge_get.bin"
    TEST_TIMEOUT=$saved_timeout
    rm -f "$CLI/huge.bin" "$CLI/huge_get.bin" "$SRV/huge_get.bin"
fi

echo "== Server health"
check "server is still running" kill -0 "$SERVER_PID"
if [ -d /proc/self ]; then
    check "finished children are reaped (no zombies)" wait_until no_zombies "$SERVER_PID"
fi
reports=""
for log in "$WORK"/*.log; do
    case "$(cat "$log")" in
        *Sanitizer*|*"runtime error"*) reports="$reports $(basename "$log")" ;;
    esac
done
check_eq "no sanitizer reports in the logs" "" "$reports"

finish
