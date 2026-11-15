#!/bin/bash
#
# Automated tests for the remote diagnostics server and client.
# Run from the project directory:  make test   (or: bash tests/run_tests.sh)
#
# Everything runs in a temporary directory against a server on a random
# high port. The target processes are copies of sleep/bash with unique
# names (rdtest_*), so no other process on the machine is touched.
#
# Needs bash and coreutils (grep and sed too). The arrow-key tests use
# 'script' from util-linux and are skipped if it is not installed.
# TEST_TIMEOUT (seconds, default 20) limits every wait and every client run.

PROJECT_DIR=$(cd "$(dirname "$0")/.." && pwd)
SERVER_BIN="$PROJECT_DIR/server"
CLIENT_BIN="$PROJECT_DIR/client"
TIMEOUT=${TEST_TIMEOUT:-20}
PASS=0
FAIL=0
SKIP=0
SERVER_PID=""
HELPER_PIDS=()

for bin in "$SERVER_BIN" "$CLIENT_BIN"; do
    if [ ! -x "$bin" ]; then
        echo "$bin not found - run 'make' first" >&2
        exit 1
    fi
done

WORK=$(mktemp -d) || exit 1

cleanup() {
    local pid
    for pid in "${HELPER_PIDS[@]}"; do
        kill -KILL "$pid" 2>/dev/null
    done
    if [ -n "$SERVER_PID" ]; then
        kill -TERM "$SERVER_PID" 2>/dev/null
        wait "$SERVER_PID" 2>/dev/null
    fi
    rm -rf "$WORK"
}
trap cleanup EXIT
cd "$WORK" || exit 1
mkdir -p home

# ---------------------------------------------------------------- helpers

pass() {
    PASS=$((PASS + 1))
    echo "  PASS  $1"
}

fail() {
    FAIL=$((FAIL + 1))
    echo "  FAIL  $1"
    if [ -n "$2" ]; then
        printf '%s\n' "$2" | head -n 40 | sed 's/^/        /'
    fi
}

skip() {
    SKIP=$((SKIP + 1))
    echo "  SKIP  $1"
}

check_eq() {            # <name> <expected> <actual>
    if [ "$2" = "$3" ]; then
        pass "$1"
    else
        fail "$1" "expected: [$2]
got:      [$3]"
    fi
}

check_contains() {      # <name> <text> <output>
    case "$3" in
        *"$2"*) pass "$1" ;;
        *)      fail "$1" "missing: [$2]
in:
$3" ;;
    esac
}

check_not_contains() {  # <name> <text> <output>
    case "$3" in
        *"$2"*) fail "$1" "unexpected: [$2]
in:
$3" ;;
        *)      pass "$1" ;;
    esac
}

check_line() {          # <name> <extended regex> <output>: some line matches
    if printf '%s\n' "$3" | grep -Eq -- "$2"; then
        pass "$1"
    else
        fail "$1" "no line matches: $2
in:
$3"
    fi
}

check_status() {        # <name> <expected> <actual>
    if [ "$2" -eq "$3" ]; then
        pass "$1"
    else
        fail "$1" "exit status $3, expected $2"
    fi
}

check_true() {          # <name> <command...>
    local name=$1
    shift
    if "$@"; then
        pass "$name"
    else
        fail "$name"
    fi
}

count_lines() {         # <extended regex> <output>: number of matching lines
    printf '%s\n' "$2" | grep -Ec -- "$1"
}

# wait_for <command...>: polls until the command succeeds; 1 after TIMEOUT
wait_for() {
    local deadline=$((SECONDS + TIMEOUT))
    until "$@"; do
        if [ "$SECONDS" -ge "$deadline" ]; then
            return 1
        fi
        sleep 0.05
    done
    return 0
}

# client_run <home> <input>: runs the client with the input on stdin.
# OUT = output, STATUS = exit status. Every output is also kept in clients.log.
client_run() {
    OUT=$(printf '%s' "$2" | HOME="$1" timeout "$TIMEOUT" "$CLIENT_BIN" 127.0.0.1 "$PORT" 2>&1)
    STATUS=$?
    printf '%s\n' "$OUT" >> "$WORK/clients.log"
}

# start_helper <command...>: starts a target process in the background; LAST_PID
start_helper() {
    "$@" </dev/null >/dev/null 2>&1 &
    LAST_PID=$!
    HELPER_PIDS+=("$LAST_PID")
    disown "$LAST_PID" 2>/dev/null      # no "Terminated" job messages
}

comm_is() {             # <pid> <name>
    local c
    read -r c 2>/dev/null < "/proc/$1/comm" || return 1
    [ "$c" = "$2" ]
}

proc_state() {          # <pid>: state letter from /proc/<pid>/stat ("" if gone)
    local line rest
    read -r line 2>/dev/null < "/proc/$1/stat" || return 0
    rest=${line##*) }
    printf '%s' "${rest%% *}"
}

proc_gone() {           # <pid>: exited (zombies count as exited)
    local s
    s=$(proc_state "$1")
    [ -z "$s" ] || [ "$s" = Z ]
}

proc_alive() {
    ! proc_gone "$1"
}

proc_ppid() {           # <pid>: parent PID ("" if gone)
    local line rest
    read -r line 2>/dev/null < "/proc/$1/stat" || return 0
    rest=${line##*) }
    set -- $rest
    printf '%s' "$2"
}

children_of() {         # <pid>: PIDs of its children (zombies included)
    local parent=$1 f p
    for f in /proc/[0-9]*/stat; do
        p=${f#/proc/}
        p=${p%/stat}
        [ "$(proc_ppid "$p")" = "$parent" ] && echo "$p"
    done
}

children_are() {        # <n>: the server has exactly n child processes
    [ "$(children_of "$SERVER_PID" | wc -l)" -eq "$1" ]
}

# descendant_by_comm <ancestor> <name>: living processes with that name
# below the ancestor (so unrelated processes with the same name are ignored)
descendant_by_comm() {
    local f c p q depth
    for f in /proc/[0-9]*/comm; do
        read -r c 2>/dev/null < "$f" || continue
        [ "$c" = "$2" ] || continue
        p=${f#/proc/}
        p=${p%/comm}
        proc_alive "$p" || continue
        q=$p
        for depth in 1 2 3 4; do
            q=$(proc_ppid "$q")
            if [ "$q" = "$1" ]; then
                echo "$p"
                break
            fi
            [ -z "$q" ] && break
        done
    done
}

# End of a table row in a regex: the last row of a reply is followed by the
# closing quote of RESPONSE: "..."
ROW_END='"?$'

# row_of <pid> <output>: the table row of a PID
row_of() {
    printf '%s\n' "$2" | grep -E "^$1 +" | head -n 1
}

log_has() {             # <text>: the server log contains the text
    grep -qF -- "$1" "$WORK/server.log"
}

# raw_exchange <printf format>: sends raw bytes to the server and collects
# everything it sends back until it closes the connection (RAW_OUT, with
# non-printable bytes shown as '.')
raw_exchange() {
    RAW_OUT=""
    exec 9<>"/dev/tcp/127.0.0.1/$PORT" || return 1
    printf "$1" >&9
    RAW_OUT=$(timeout "$TIMEOUT" cat <&9 | tr -c '[:print:]\n' '.')
    exec 9<&-
}

server_ready_or_dead() {
    grep -q "Server Port Information" "$WORK/server.log" 2>/dev/null && return 0
    ! kill -0 "$SERVER_PID" 2>/dev/null
}

start_server() {
    local attempt
    for attempt in 1 2 3 4 5; do
        PORT=$((20000 + RANDOM % 40000))
        "$SERVER_BIN" "$PORT" > "$WORK/server.log" 2>&1 &
        SERVER_PID=$!
        wait_for server_ready_or_dead
        if grep -q "Server Port Information" "$WORK/server.log"; then
            return 0
        fi
        kill "$SERVER_PID" 2>/dev/null
        wait "$SERVER_PID" 2>/dev/null
        SERVER_PID=""
    done
    return 1
}

# Unique copies of system programs, so the tests have targets with known names
SLEEP_BIN=$(command -v sleep)
BASH_BIN=$(command -v bash)
for name in rdtest_sleeper rdtest_multi rdtest_fdproc rdtest_netproc rdtest_stubborn \
            rdtest_hup rdtest_nine rdtest_long_process_name; do
    cp "$SLEEP_BIN" "$WORK/$name" || exit 1
done
cp "$BASH_BIN" "$WORK/rdtest_busy" || exit 1

# ---------------------------------------------------------------- tests

echo "== Server start and client connection"
if ! start_server; then
    echo "Could not start the server" >&2
    cat "$WORK/server.log" >&2
    exit 1
fi
check_contains "server prints its address (Fig 5.1)" \
    "RMS Server: Server Port Information: 127.0.0.1:$PORT" "$(cat server.log)"
client_run "$WORK/home" ""
check_status "client exits with 0 at end of input" 0 "$STATUS"
check_contains "client: connected message (Fig 5.2)" "INFO: Connected to the Server
Type help to get supported commands
Client] " "$OUT"
check_true "server logs the incoming connection (Fig 5.5)" \
    wait_for log_has "RMS Server: Incoming Remote Request from 127.0.0.1:"

echo "== help (client-only command)"
client_run "$WORK/home" $'help\n'
check_contains "help lists the spec's commands (Fig 5.3)" \
"get-mem <process-name>          : Gets the memory usage of given process
get-cpu-usage <process-name>    : Gets the CPU usage of given process
get-ports-used <process-name>   : Fetch the socket ports used by given process
get-open-fd <process-name>      : Fetch the open file descriptors of a given process
kill <process-name>             : Kill the given process
history                         : Show commands history
help                            : Shows this output" "$OUT"
check_not_contains "help is not sent to the server" "Message sent" "$OUT"

echo "== get-mem"
start_helper "$WORK/rdtest_sleeper" 1000
SLEEPER=$LAST_PID
wait_for comm_is "$SLEEPER" rdtest_sleeper
client_run "$WORK/home" $'get-mem rdtest_sleeper\n'
check_contains "request/ACK/response sequence (Fig 5.4)" "Client] get-mem rdtest_sleeper
INFO: Message sent to the Server
INFO: Message accepted by the Server
RESPONSE: \"PID       %MEM    RSS(kB)    VSZ(kB)   PEAK(kB)  SWAP(kB)  NAME" "$OUT"
row=$(row_of "$SLEEPER" "$OUT")
read -r _ mem rss vsz peak swap name <<< "$row"
check_eq "row names the process" "rdtest_sleeper" "$name"
read -r _ expected_vsz _ <<< "$(grep '^VmSize:' "/proc/$SLEEPER/status")"
check_eq "VSZ equals VmSize of /proc/<pid>/status" "$expected_vsz" "$vsz"
check_true "RSS is a positive number" test "${rss:-0}" -gt 0
check_line "%MEM is a percentage" '^[0-9]+ +[0-9]+\.[0-9] ' "$row"
check_line "response ends with a closing quote" '"$' "$OUT"

echo "== get-cpu-usage"
start_helper "$WORK/rdtest_busy" -c 'while :; do :; done'
BUSY=$LAST_PID
wait_for comm_is "$BUSY" rdtest_busy
client_run "$WORK/home" $'get-cpu-usage rdtest_sleeper\nget-cpu-usage rdtest_busy\n'
check_contains "CPU table header" \
    "PID       %CPU  AVG%CPU    CPU-TIME THREADS STATE  NAME" "$OUT"
read -r _ cpu avg cputime threads state name <<< "$(row_of "$SLEEPER" "$OUT")"
check_eq "idle process: 0.0 %CPU" "0.0" "$cpu"
check_eq "idle process: sleeping state" "S" "$state"
check_eq "idle process: one thread" "1" "$threads"
check_line "CPU-TIME as min:sec.hundredths" '^[0-9]+:[0-9]{2}\.[0-9]{2}$' "$cputime"
read -r _ cpu avg _ _ state name <<< "$(row_of "$BUSY" "$OUT")"
check_true "busy loop: %CPU above 1 (got $cpu)" test "${cpu%.*}" -ge 1
check_true "busy loop: AVG%CPU above 1 (got $avg)" test "${avg%.*}" -ge 1
check_eq "busy loop: running state" "R" "$state"
kill -KILL "$BUSY" 2>/dev/null

echo "== get-open-fd"
echo "marker" > "$WORK/marker.txt"
start_helper bash -c 'exec 5<"$1" 6>>"$1"; exec "$2" 1000' _ "$WORK/marker.txt" "$WORK/rdtest_fdproc"
FDPROC=$LAST_PID
wait_for comm_is "$FDPROC" rdtest_fdproc
client_run "$WORK/home" $'get-open-fd rdtest_fdproc\n'
nfds=$(ls "/proc/$FDPROC/fd" | wc -l)
check_contains "descriptor count equals /proc/<pid>/fd" \
    "PID $FDPROC (rdtest_fdproc): $nfds open file descriptors" "$OUT"
check_line "fd 0 is /dev/null, read-only" "^0 +r +/dev/null$ROW_END" "$OUT"
check_line "fd 5 is the marker file, read-only" "^5 +r +$WORK/marker.txt$ROW_END" "$OUT"
check_line "fd 6 is the marker file, write-only" "^6 +w +$WORK/marker.txt$ROW_END" "$OUT"

echo "== get-ports-used"
client_run "$WORK/home" $'get-ports-used server\n'
check_line "server's listening socket" \
    "^tcp +[0-9]+ +[0-9]+ +127\\.0\\.0\\.1:$PORT +0\\.0\\.0\\.0:\\* +LISTEN +$SERVER_PID/server$ROW_END" "$OUT"
check_line "connection of the child serving this client" \
    "^tcp +[0-9]+ +[0-9]+ +127\\.0\\.0\\.1:$PORT +127\\.0\\.0\\.1:[0-9]+ +ESTABLISHED +[0-9]+/server$ROW_END" "$OUT"
check_contains "netstat-like header (Fig 5.6)" \
    "Proto Recv-Q Send-Q Local Address           Foreign Address         State       PID/Program name" "$OUT"

start_helper bash -c 'exec 3<>/dev/udp/127.0.0.1/5555 4<>"/dev/tcp/127.0.0.1/$2"; exec "$1" 1000' \
    _ "$WORK/rdtest_netproc" "$PORT"
NETPROC=$LAST_PID
wait_for comm_is "$NETPROC" rdtest_netproc
netproc_greeted() {
    client_run "$WORK/home" $'get-ports-used rdtest_netproc\n'
    printf '%s\n' "$OUT" | grep -Eq "^tcp +8 +0 +127\\.0\\.0\\.1:[0-9]+ +127\\.0\\.0\\.1:$PORT +ESTABLISHED"
}
wait_for netproc_greeted
check_line "connected UDP socket" \
    "^udp +0 +0 +127\\.0\\.0\\.1:[0-9]+ +127\\.0\\.0\\.1:5555 +ESTABLISHED +$NETPROC/rdtest_netproc$ROW_END" "$OUT"
check_line "TCP client socket; Recv-Q shows the unread 8-byte greeting" \
    "^tcp +8 +0 +127\\.0\\.0\\.1:[0-9]+ +127\\.0\\.0\\.1:$PORT +ESTABLISHED +$NETPROC/rdtest_netproc$ROW_END" "$OUT"
kill -KILL "$NETPROC" 2>/dev/null
client_run "$WORK/home" $'get-ports-used rdtest_sleeper\n'
check_contains "process without sockets" "PID $SLEEPER (rdtest_sleeper): no sockets open" "$OUT"

if [ -e /proc/net/tcp6 ]; then
    PORT6=$((PORT + 1))
    "$SERVER_BIN" "$PORT6" ::1 > "$WORK/server6.log" 2>&1 &
    SERVER6_PID=$!
    server6_ready() { grep -q "Server Port Information" "$WORK/server6.log" || ! kill -0 "$SERVER6_PID" 2>/dev/null; }
    wait_for server6_ready
    if grep -q "Server Port Information: \[::1\]:$PORT6" "$WORK/server6.log"; then
        OUT=$(printf 'get-ports-used server\n' | HOME="$WORK/home" timeout "$TIMEOUT" "$CLIENT_BIN" ::1 "$PORT6" 2>&1)
        printf '%s\n' "$OUT" >> "$WORK/clients.log"
        check_line "IPv6: client connects to ::1 and sees the tcp6 listener" \
            "^tcp6 +[0-9]+ +[0-9]+ +\\[::1\\]:$PORT6 +\\[::\\]:\\* +LISTEN +$SERVER6_PID/server$ROW_END" "$OUT"
    else
        skip "IPv6 loopback not available"
    fi
    kill -TERM "$SERVER6_PID" 2>/dev/null
    wait "$SERVER6_PID" 2>/dev/null
else
    skip "IPv6 not available"
fi

echo "== Long process names (more than 15 characters)"
start_helper "$WORK/rdtest_long_process_name" 1000
LONGPROC=$LAST_PID
wait_for comm_is "$LONGPROC" rdtest_long_pro
client_run "$WORK/home" $'get-mem rdtest_long_process_name\nget-mem rdtest_long_pro\nget-mem rdtest_long_process_nameX\n'
check_eq "full name and 15-character kernel name both match" 2 "$(count_lines "^$LONGPROC +" "$OUT")"
check_contains "a different long name does not match" \
    "No process named 'rdtest_long_process_nameX' is running" "$OUT"

echo "== Unknown process and invalid input"
long_name=$(printf '%065d' 0 | tr 0 a)
long_line=$(printf '%0300d' 0 | tr 0 b)
mkdir -p h0
client_run "$WORK/h0" "get-mem nosuchprocess
foo bar
get-mem
get-mem a b
get-mem a;b
get-mem -x
get-mem $long_name
get-cpu-usage
kill rdtest_sleeper BOGUS
kill rdtest_sleeper TERM extra
help me
history please
get-mem $(printf 'x\001y')
$long_line
history
"
check_contains "unknown process" "RESPONSE: \"No process named 'nosuchprocess' is running\"
INFO: Server reported: no matching process" "$OUT"
check_contains "unknown command" "ERROR: Unknown command 'foo'. Type help to get supported commands" "$OUT"
check_contains "missing process name" "ERROR: Missing process name. Usage: get-mem <process-name>" "$OUT"
check_contains "too many arguments" "ERROR: Too many arguments. Usage: get-mem <process-name>" "$OUT"
check_contains "characters outside the allowed set" "ERROR: Invalid process name 'a;b'" "$OUT"
check_contains "name starting with '-'" "ERROR: Invalid process name '-x'" "$OUT"
check_contains "name longer than 64 characters" "ERROR: Process name too long (at most 64 characters)" "$OUT"
check_contains "unknown signal" "ERROR: Unknown signal 'BOGUS'" "$OUT"
check_contains "kill with too many arguments" \
    "ERROR: Too many arguments. Usage: kill <process-name> [TERM|KILL|INT|HUP|QUIT]" "$OUT"
check_contains "help takes no arguments" "ERROR: help takes no arguments" "$OUT"
check_contains "history takes no arguments" "ERROR: history takes no arguments" "$OUT"
check_contains "control characters rejected" \
    "ERROR: Commands may only contain printable ASCII characters" "$OUT"
check_contains "over-long line rejected" "ERROR: Command too long (at most 255 characters)" "$OUT"
check_eq "only the valid command reached the server" 1 "$(count_lines 'Message sent' "$OUT")"
check_true "the target survived the rejected kill" proc_alive "$SLEEPER"
check_line "history holds only the executed command" '^1 get-mem nosuchprocess$' "$OUT"
check_eq "nothing else in the history" 1 "$(count_lines '^[0-9]+ ' "$OUT")"

echo "== Several processes with the same name"
MULTI=()
for i in 1 2 3; do
    start_helper "$WORK/rdtest_multi" 1000
    MULTI+=("$LAST_PID")
done
for pid in "${MULTI[@]}"; do
    wait_for comm_is "$pid" rdtest_multi
done
client_run "$WORK/home" $'get-mem rdtest_multi\nget-open-fd rdtest_multi\nget-cpu-usage rdtest_multi\n'
for pid in "${MULTI[@]}"; do
    check_eq "get-mem and get-cpu-usage report PID $pid" 2 "$(count_lines "^$pid +.*rdtest_multi\$" "$OUT")"
    check_contains "get-open-fd reports PID $pid" "PID $pid (rdtest_multi): " "$OUT"
done

echo "== kill"
client_run "$WORK/home" $'kill rdtest_multi\nget-mem rdtest_multi\n'
for pid in "${MULTI[@]}"; do
    check_contains "SIGTERM reported for PID $pid" \
        "PID $pid (rdtest_multi): SIGTERM sent - process terminated" "$OUT"
    check_true "PID $pid is gone" wait_for proc_gone "$pid"
done
check_contains "kill summary" "3 of 3 matching processes terminated" "$OUT"
check_contains "killed processes are no longer found" \
    "No process named 'rdtest_multi' is running" "$OUT"
check_true "server logs the kill request" log_has "Request: kill rdtest_multi (SIGTERM)"

start_helper bash -c 'trap "" TERM; exec "$1" 1000' _ "$WORK/rdtest_stubborn"
STUBBORN=$LAST_PID
wait_for comm_is "$STUBBORN" rdtest_stubborn
client_run "$WORK/home" $'kill rdtest_stubborn\n'
check_contains "process ignoring SIGTERM is reported as still running" \
    "PID $STUBBORN (rdtest_stubborn): SIGTERM sent - still running after 1000 ms" "$OUT"
check_contains "status: failed" "INFO: Server reported: the request failed" "$OUT"
check_true "stubborn process still alive" proc_alive "$STUBBORN"
client_run "$WORK/home" $'kill rdtest_stubborn KILL\n'
check_contains "kill <name> KILL" "PID $STUBBORN (rdtest_stubborn): SIGKILL sent - process terminated" "$OUT"
check_true "stubborn process gone" wait_for proc_gone "$STUBBORN"

# (Background jobs of a non-interactive shell ignore SIGINT and SIGQUIT,
# so HUP and a signal number are used here.)
start_helper "$WORK/rdtest_hup" 1000
SIGPROC=$LAST_PID
wait_for comm_is "$SIGPROC" rdtest_hup
start_helper "$WORK/rdtest_nine" 1000
NINEPROC=$LAST_PID
wait_for comm_is "$NINEPROC" rdtest_nine
client_run "$WORK/home" $'kill rdtest_hup -sighup\nkill rdtest_nine 9\n'
check_contains "signal given as -sighup" "PID $SIGPROC (rdtest_hup): SIGHUP sent - process terminated" "$OUT"
check_contains "signal given as a number" "PID $NINEPROC (rdtest_nine): SIGKILL sent - process terminated" "$OUT"
check_true "SIGHUP target gone" wait_for proc_gone "$SIGPROC"
check_true "SIGKILL target gone" wait_for proc_gone "$NINEPROC"

echo "== history and .remote_history"
mkdir -p h1 h2 h3 h4 cwd
client_run "$WORK/h1" $'  get-mem    rdtest_sleeper  \nhelp\nfoo\nget-cpu-usage rdtest_sleeper\nhistory\n'
check_contains "history output (Fig 5.7)" "Client] history
1 get-mem rdtest_sleeper
2 get-cpu-usage rdtest_sleeper
Client] " "$OUT"
check_eq "history file saved on exit" "get-mem rdtest_sleeper
get-cpu-usage rdtest_sleeper" "$(cat h1/.remote_history 2>/dev/null)"
check_eq "history file is private (mode 600)" "600" "$(stat -c %a h1/.remote_history 2>/dev/null)"
client_run "$WORK/h1" $'history\nget-open-fd rdtest_sleeper\nhistory\n'
check_contains "history loaded at start" "Client] history
1 get-mem rdtest_sleeper
2 get-cpu-usage rdtest_sleeper
Client] get-open-fd" "$OUT"
check_line "new command appended to the loaded history" '^3 get-open-fd rdtest_sleeper$' "$OUT"

input=""
for i in $(seq 1 60); do
    input="${input}get-mem p$i
"
done
client_run "$WORK/h2" "${input}history
"
hist=$(printf '%s\n' "$OUT" | grep -E '^[0-9]+ get-mem p[0-9]+$')
check_eq "history keeps 50 commands" 50 "$(printf '%s\n' "$hist" | wc -l)"
check_eq "oldest kept command" "1 get-mem p11" "$(printf '%s\n' "$hist" | head -n 1)"
check_eq "newest command" "50 get-mem p60" "$(printf '%s\n' "$hist" | tail -n 1)"
check_eq "history file has 50 lines" 50 "$(wc -l < h2/.remote_history)"
client_run "$WORK/h2" $'get-mem p61\nhistory\n'
hist=$(printf '%s\n' "$OUT" | grep -E '^[0-9]+ get-mem p[0-9]+$')
check_eq "still 50 after reload and one more command" 50 "$(printf '%s\n' "$hist" | wc -l)"
check_eq "oldest entry dropped" "1 get-mem p12" "$(printf '%s\n' "$hist" | head -n 1)"
check_eq "new entry last" "50 get-mem p61" "$(printf '%s\n' "$hist" | tail -n 1)"

{
    printf '%01000d\n' 0
    printf 'get-mem ok1\n'
    printf 'bad\001line\n'
    printf 'get-mem ok2\r\n'
    printf '\n'
} > h3/.remote_history
client_run "$WORK/h3" $'history\n'
check_contains "damaged history file: only valid lines loaded" "Client] history
1 get-mem ok1
2 get-mem ok2
Client] " "$OUT"

OUT=$(cd cwd && printf 'get-mem rdtest_sleeper\n' | env -u HOME timeout "$TIMEOUT" "$CLIENT_BIN" 127.0.0.1 "$PORT" 2>&1)
check_eq "without HOME the history goes to ./.remote_history" "get-mem rdtest_sleeper" \
    "$(cat cwd/.remote_history 2>/dev/null)"

mkfifo "$WORK/in.fifo"
HOME="$WORK/h4" "$CLIENT_BIN" 127.0.0.1 "$PORT" < "$WORK/in.fifo" > "$WORK/sigterm.out" 2>&1 &
CPID=$!
exec 8> "$WORK/in.fifo"
printf 'get-mem rdtest_sleeper\n' >&8
wait_for grep -q 'RESPONSE:' "$WORK/sigterm.out"
kill -TERM "$CPID"
wait_for proc_gone "$CPID"
wait "$CPID"
STATUS=$?
exec 8>&-
check_contains "client stops on SIGTERM" "INFO: Interrupted" "$(cat "$WORK/sigterm.out")"
check_status "exit status after SIGTERM" 1 "$STATUS"
check_eq "history saved on SIGTERM" "get-mem rdtest_sleeper" "$(cat h4/.remote_history 2>/dev/null)"

echo "== Concurrent clients"
wait_for children_are 0
exec {IDLE_FD}<>"/dev/tcp/127.0.0.1/$PORT"
wait_for children_are 1
client_run "$WORK/home" $'get-mem rdtest_sleeper\n'
check_contains "an idle client does not block others" "INFO: Message accepted by the Server" "$OUT"
exec {IDLE_FD}<&-

before=$(grep -c "Incoming Remote Request" server.log)
start_ns=$(date +%s%N)
CLIENT_PIDS=()
for i in 1 2 3 4 5; do
    printf 'get-cpu-usage rdtest_sleeper\nget-mem rdtest_sleeper\n' |
        HOME="$WORK/home" timeout "$TIMEOUT" "$CLIENT_BIN" 127.0.0.1 "$PORT" > "$WORK/par$i.out" 2>&1 &
    CLIENT_PIDS+=("$!")
done
all_ok=1
for pid in "${CLIENT_PIDS[@]}"; do
    wait "$pid" || all_ok=0
done
elapsed_ms=$(( ($(date +%s%N) - start_ns) / 1000000 ))
for i in 1 2 3 4 5; do
    cat "$WORK/par$i.out" >> "$WORK/clients.log"
    check_eq "parallel client $i got both responses" 2 "$(grep -c '^RESPONSE: "PID' "$WORK/par$i.out")"
done
check_true "all parallel clients exited with 0" test "$all_ok" -eq 1
check_true "5 one-second CPU samples ran in parallel (${elapsed_ms} ms)" test "$elapsed_ms" -lt 4000
after=$(grep -c "Incoming Remote Request" server.log)
check_eq "server logged 5 more connections" 5 $((after - before))
check_true "no child processes left (all reaped, no zombies)" wait_for children_are 0

echo "== Malformed data on the socket"
raw_exchange 'this is not a frame at all\n'
check_contains "garbage: server answers with an error" "bad magic number" "$RAW_OUT"
raw_exchange '\x52\x4d\x01\x02\xff\xff\xff\xff'
check_contains "length 0xFFFFFFFF rejected without reading it" "message too large" "$RAW_OUT"
raw_exchange '\x52\x4d\x07\x02\x00\x00\x00\x00'
check_contains "unknown protocol version" "unsupported protocol version" "$RAW_OUT"
raw_exchange '\x52\x4d\x01\x63\x00\x00\x00\x00'
check_contains "unknown message type" "unknown message type" "$RAW_OUT"
raw_exchange '\x52\x4d\x01\x03\x00\x00\x00\x00'
check_contains "client sending a server-only message" "unexpected message type" "$RAW_OUT"
raw_exchange '\x52\x4d\x01\x02\x00\x00\x00\x05\x01\x00a;b\x52\x4d\x01\x02\x00\x00\x00\x04\x01\x00a\x00\x52\x4d\x01\x02\x00\x00\x00\x03\x09\x00a\x52\x4d\x01\x02\x00\x00\x00\x03\x01\x09a\x52\x4d\x01\x02\x00\x00\x00\x03\x05\x13a\x52\x4d\x01\x02\x00\x00\x00\x02\x01\x00\x52\x4d\x01\x02\x00\x00\x00\x10\x01\x00rdtest_sleeperGARBAGE!'
check_contains "invalid characters in a raw request" "invalid process name" "$RAW_OUT"
check_eq "NUL byte in the name rejected too" 2 "$(grep -o 'invalid process name' <<< "$RAW_OUT" | wc -l)"
check_contains "unknown command code" "unknown command code" "$RAW_OUT"
check_contains "signal on a command that takes none" "signal given for a command that does not take one" "$RAW_OUT"
check_contains "signal outside the allowed list (SIGSTOP)" "signal not allowed" "$RAW_OUT"
check_contains "request without a name" "request has no process name" "$RAW_OUT"
check_contains "connection stays usable after rejected requests" "rdtest_sleeper" "$RAW_OUT"
check_contains "garbage after valid requests closes the connection" "bad magic number" "$RAW_OUT"
check_true "the target was not touched by the raw requests" proc_alive "$SLEEPER"

exec 9<>"/dev/tcp/127.0.0.1/$PORT"
printf '\x52\x4d\x01' >&9
exec 9<&-
check_true "truncated frame is detected" \
    wait_for log_has "Connection lost: connection closed in the middle of a message"
check_true "server still running" kill -0 "$SERVER_PID"
client_run "$WORK/home" $'get-mem rdtest_sleeper\n'
check_contains "server still serves clients" "rdtest_sleeper" "$OUT"
check_true "server logged the protocol errors" log_has "Protocol error: bad magic number"
check_true "no child processes left after the bad connections" wait_for children_are 0

echo "== Busy server"
IDLE_FDS=()
for i in $(seq 1 32); do
    exec {fd}<>"/dev/tcp/127.0.0.1/$PORT" && IDLE_FDS+=("$fd")
done
check_true "32 clients served at the same time" wait_for children_are 32
client_run "$WORK/home" ""
check_contains "client 33 is refused" \
    "ERROR: The Server refused the connection: Server busy: too many clients, try again later" "$OUT"
check_status "refused client exits with 1" 1 "$STATUS"
for fd in "${IDLE_FDS[@]}"; do
    exec {fd}<&-
done
check_true "all 32 children exit when their clients leave" wait_for children_are 0
client_run "$WORK/home" $'get-mem rdtest_sleeper\n'
check_contains "server accepts clients again" "INFO: Message accepted by the Server" "$OUT"

echo "== Line editor (arrow keys) in a terminal"
if command -v script >/dev/null 2>&1 && script -qfec true /dev/null </dev/null >/dev/null 2>&1; then
    FRESH_PROMPT=$'Client] \e[0K\r\e[8C'

    pty_fresh_prompt() {    # the output ends with a newly drawn, empty prompt
        [ "$(tail -c ${#FRESH_PROMPT} "$PTY_OUT")" = "$FRESH_PROMPT" ]
    }
    pty_count() {           # <text>: number of occurrences in the terminal output
        grep -o -- "$1" "$PTY_OUT" | wc -l
    }
    pty_has_count() {       # <text> <n>
        [ "$(pty_count "$1")" -ge "$2" ]
    }
    pty_keys() {            # <printf format>: types keys, then waits for <text> <n>
        printf "$1" >&7
        wait_for pty_has_count "$2" "$3" && wait_for pty_fresh_prompt
    }

    mkdir -p h5 h6
    mkfifo "$WORK/keys.fifo" "$WORK/keys2.fifo"
    PTY_OUT="$WORK/pty.out"
    script -qfec "env HOME='$WORK/h5' '$CLIENT_BIN' 127.0.0.1 $PORT" /dev/null \
        < "$WORK/keys.fifo" > "$PTY_OUT" 2>&1 &
    SCRIPT_PID=$!
    exec 7> "$WORK/keys.fifo"
    wait_for pty_fresh_prompt
    pty_keys 'get-mem rdtest_sleeperX\177\r' 'RESPONSE:' 1           # backspace
    pty_keys 'get-open-fd rdtest_sleeper\r' 'RESPONSE:' 2
    pty_keys '\033[A\033[A\r' 'RESPONSE:' 3                          # Up Up Enter
    pty_keys 'et-cpu-usage rdtest_sleeper\001g\r' 'RESPONSE:' 4      # Ctrl+A, insert
    pty_keys 'get-ports-used rdtest_sleepr\033[De\r' 'RESPONSE:' 5   # Left, insert
    pty_keys 'partial line\003' '\^C' 1                              # Ctrl+C cancels
    pty_keys '\033[A\033[A\033[A\033[B\r' 'RESPONSE:' 6              # Up x3, Down
    pty_keys 'history\r' '^6 get-' 1
    printf '\004' >&7                                                # Ctrl+D exits
    wait_for proc_gone "$SCRIPT_PID"
    wait "$SCRIPT_PID"
    STATUS=$?
    exec 7>&-
    clean=$(tr -d '\r' < "$PTY_OUT")
    printf '%s\n' "$clean" >> "$WORK/clients.log"
    check_eq "edited and recalled commands ran in order" "1 get-mem rdtest_sleeper
2 get-open-fd rdtest_sleeper
3 get-mem rdtest_sleeper
4 get-cpu-usage rdtest_sleeper
5 get-ports-used rdtest_sleeper
6 get-cpu-usage rdtest_sleeper" "$(printf '%s\n' "$clean" | grep -E '^[0-9]+ get-')"
    check_not_contains "Ctrl+C discarded the typed line" "Unknown command 'partial'" "$clean"
    check_status "Ctrl+D on an empty line exits with 0" 0 "$STATUS"
    check_eq "history file written after Ctrl+D" 6 "$(wc -l < h5/.remote_history)"

    PTY_OUT="$WORK/pty2.out"
    script -qfec "stty -g > '$WORK/stty.before'; env HOME='$WORK/h6' '$CLIENT_BIN' 127.0.0.1 $PORT; stty -g > '$WORK/stty.after'" \
        /dev/null < "$WORK/keys2.fifo" > "$PTY_OUT" 2>&1 &
    SCRIPT_PID=$!
    exec 7> "$WORK/keys2.fifo"
    wait_for pty_fresh_prompt
    pty_keys 'get-mem rdtest_sleeper\r' 'RESPONSE:' 1
    printf 'get-' >&7
    wait_for grep -q 'Client] get-' "$PTY_OUT"
    for pid in $(descendant_by_comm "$SCRIPT_PID" client); do
        kill -TERM "$pid"
    done
    wait_for proc_gone "$SCRIPT_PID"
    wait "$SCRIPT_PID"
    exec 7>&-
    tr -d '\r' < "$PTY_OUT" >> "$WORK/clients.log"
    check_contains "SIGTERM while editing a line" "INFO: Interrupted" "$(cat "$PTY_OUT")"
    stty_restored() {
        [ -s "$WORK/stty.before" ] && [ "$(cat "$WORK/stty.before")" = "$(cat "$WORK/stty.after")" ]
    }
    check_true "terminal settings restored after SIGTERM" stty_restored
    check_eq "history saved after SIGTERM in raw mode" "get-mem rdtest_sleeper" \
        "$(cat h6/.remote_history 2>/dev/null)"
else
    skip "'script' (util-linux) not available - arrow key tests skipped"
fi

echo "== Permissions (server running as an unprivileged user)"
if [ "$(id -u)" -eq 0 ] && chroot --userspec=65534:65534 / true 2>/dev/null; then
    PORTU=$((PORT + 2))
    chroot --userspec=65534:65534 / "$SERVER_BIN" "$PORTU" > "$WORK/server_nobody.log" 2>&1 &
    SERVERU_PID=$!
    serveru_ready() {
        grep -q "Server Port Information" "$WORK/server_nobody.log" || ! kill -0 "$SERVERU_PID" 2>/dev/null
    }
    wait_for serveru_ready
    OUT=$(printf 'get-open-fd rdtest_sleeper\nkill rdtest_sleeper\nget-mem rdtest_sleeper\n' |
          HOME="$WORK/home" timeout "$TIMEOUT" "$CLIENT_BIN" 127.0.0.1 "$PORTU" 2>&1)
    printf '%s\n' "$OUT" >> "$WORK/clients.log"
    check_contains "cannot read the descriptors of another user's process" \
        "PID $SLEEPER (rdtest_sleeper): cannot read the open files: Permission denied" "$OUT"
    check_contains "cannot kill another user's process" \
        "PID $SLEEPER (rdtest_sleeper): could not send SIGTERM: Operation not permitted" "$OUT"
    check_true "that process is still running" proc_alive "$SLEEPER"
    check_line "memory of other users' processes is still shown" "^$SLEEPER +[0-9]+\\.[0-9]" "$OUT"
    kill -TERM "$SERVERU_PID" 2>/dev/null
    wait "$SERVERU_PID" 2>/dev/null
else
    skip "permission tests need root (they start a second server as user nobody)"
fi

echo "== Command line errors"
# run_cmd <command...>: OUT, STATUS; a program that wrongly starts is stopped by timeout
run_cmd() {
    OUT=$(timeout "$TIMEOUT" "$@" 2>&1 < /dev/null)
    STATUS=$?
    printf '%s\n' "$OUT" >> "$WORK/clients.log"
}
run_cmd "$SERVER_BIN" abc
check_status "server: bad port rejected" 1 "$STATUS"
check_contains "server: message for a bad port" "'abc' is not a valid port number" "$OUT"
run_cmd "$SERVER_BIN" 70000
check_status "server: port above 65535 rejected" 1 "$STATUS"
run_cmd "$SERVER_BIN" "$PORT"
check_status "server: port in use" 1 "$STATUS"
check_contains "server: message for a port in use" "Cannot listen on 127.0.0.1 port $PORT" "$OUT"
run_cmd "$SERVER_BIN" 4000 not-an-ip
check_status "server: invalid bind address" 1 "$STATUS"
run_cmd "$SERVER_BIN" 4000 127.0.0.1 extra
check_status "server: too many arguments" 1 "$STATUS"
run_cmd "$CLIENT_BIN" 127.0.0.1 abc
check_status "client: bad port rejected" 1 "$STATUS"
run_cmd "$CLIENT_BIN" 127.0.0.1 "$((PORT - 1))"
check_status "client: no server on that port" 1 "$STATUS"
check_contains "client: connection error message" "ERROR: Could not connect to the Server at 127.0.0.1 port $((PORT - 1))" "$OUT"

echo "== Server shutdown"
wait_for children_are 0
exec {IDLE_FD}<>"/dev/tcp/127.0.0.1/$PORT"
wait_for children_are 1
SESSION_PID=$(children_of "$SERVER_PID")
kill -TERM "$SERVER_PID"
wait_for proc_gone "$SERVER_PID"
wait "$SERVER_PID"
STATUS=$?
exec {IDLE_FD}<&-
check_status "server exits with 0 on SIGTERM" 0 "$STATUS"
check_true "server logged the shutdown" log_has "RMS Server: Shutting down"
session_stopped() {
    [ -n "$SESSION_PID" ] && proc_gone "$SESSION_PID"
}
check_true "the child serving a connected client was stopped too" session_stopped
SERVER_PID=""

echo "== Sanitizer reports"
reports=$(grep -l -E 'Sanitizer|runtime error' "$WORK"/*.log "$WORK"/*.out 2>/dev/null)
if [ -z "$reports" ]; then
    pass "no AddressSanitizer/UBSan/LeakSanitizer reports"
else
    fail "sanitizer reports found" "$(grep -h -E -A5 'Sanitizer|runtime error' $reports | head -n 40)"
fi

echo
echo "Passed: $PASS  Failed: $FAIL  Skipped: $SKIP"
[ "$FAIL" -eq 0 ]
