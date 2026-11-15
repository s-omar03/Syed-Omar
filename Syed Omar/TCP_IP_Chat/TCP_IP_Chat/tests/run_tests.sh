#!/bin/bash
#
# Automated tests for the TCP/IP chat room.
# Run from the project directory:  make test   (or: bash tests/run_tests.sh)
#
# The server and the clients run in a temporary directory, on a random high
# port. Every client reads its keyboard input from its own FIFO and writes its
# screen output to its own file. The tests never sleep for a fixed time to
# synchronise: they poll an output file until the expected text appears, or
# give up after TEST_TIMEOUT seconds (default 10; raise it for slow sanitizer
# builds, e.g. TEST_TIMEOUT=30 make test).
#
# Needs only bash (4.1 or newer) and coreutils. Raw protocol tests use
# bash's /dev/tcp.

export LC_ALL=C
trap '' PIPE                    # A write to a dead client must not kill the tests

PROJECT_DIR=$(cd "$(dirname "$0")/.." && pwd)
SERVER_BIN="$PROJECT_DIR/server"
CLIENT_BIN="$PROJECT_DIR/client"
TIMEOUT=${TEST_TIMEOUT:-10}
MAX_SESSIONS=$(sed -n 's/^#define SERVER_MAX_SESSIONS *\([0-9][0-9]*\).*/\1/p' "$PROJECT_DIR/session.h")
PASS=0
FAIL=0

for bin in "$SERVER_BIN" "$CLIENT_BIN"; do
    if [ ! -x "$bin" ]; then
        echo "$bin not found - run 'make' first" >&2
        exit 1
    fi
done
if [ -z "$MAX_SESSIONS" ]; then
    echo "SERVER_MAX_SESSIONS not found in session.h" >&2
    exit 1
fi

WORK=$(mktemp -d) || exit 1
declare -A CLIENT_FD CLIENT_PID RAW_FD RAW_PID
SERVER_PID=""
SERVER_RUN=0

cleanup() {
    local name
    for name in "${!CLIENT_PID[@]}"; do kill -9 "${CLIENT_PID[$name]}" 2>/dev/null; done
    for name in "${!RAW_PID[@]}"; do kill -9 "${RAW_PID[$name]}" 2>/dev/null; done
    [ -n "$SERVER_PID" ] && kill -9 "$SERVER_PID" 2>/dev/null
    wait 2>/dev/null
    cd / && rm -rf "$WORK"
}
trap cleanup EXIT
cd "$WORK" || exit 1

# ---------------------------------------------------------------- reporting

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

check() {               # check <description> <command...>: passes when the command succeeds
    local desc=$1
    shift
    if "$@"; then pass "$desc"; else fail "$desc"; fi
}

# ---------------------------------------------------------------- waiting

count_in() {            # count_in <file> <text>: occurrences of a fixed string
    grep -a -o -F -- "$2" "$1" 2>/dev/null | wc -l
}

wait_text() {           # wait_text <file> <text> [count]: wait until text occurs count times
    local deadline=$((SECONDS + TIMEOUT))
    while [ "$(count_in "$1" "$2")" -lt "${3:-1}" ]; do
        [ "$SECONDS" -ge "$deadline" ] && return 1
        sleep 0.05
    done
    return 0
}

expect() {              # expect <description> <client> <text> [count]
    if wait_text "$2.out" "$3" "${4:-1}"; then
        pass "$1"
    else
        fail "$1" "expected [$3] x${4:-1} in $2.out, last lines:
$(tail -n 12 "$2.out")"
    fi
}

wait_gone() {           # wait_gone <pid>: wait until the process has ended
    local deadline=$((SECONDS + TIMEOUT))
    while kill -0 "$1" 2>/dev/null; do
        [ "$SECONDS" -ge "$deadline" ] && return 1
        sleep 0.05
    done
    return 0
}

# ---------------------------------------------------------------- server

start_server() {        # start_server [new]: start on $PORT, or on a fresh random port
    local log tries
    for tries in 1 2 3 4 5; do
        if [ "$1" = new ]; then PORT=$((20000 + RANDOM % 40000)); fi
        SERVER_RUN=$((SERVER_RUN + 1))
        log="server$SERVER_RUN.log"
        SERVER_LOG=$log
        "$SERVER_BIN" -v "$PORT" > "$log" 2>&1 &
        SERVER_PID=$!
        local deadline=$((SECONDS + TIMEOUT))
        while [ "$SECONDS" -lt "$deadline" ] && kill -0 "$SERVER_PID" 2>/dev/null; do
            grep -q "listening on port $PORT" "$log" && return 0
            sleep 0.05
        done
        kill -9 "$SERVER_PID" 2>/dev/null
        wait "$SERVER_PID" 2>/dev/null
        SERVER_PID=""
        [ "$1" = new ] || return 1  # The same port must work at once (SO_REUSEADDR)
    done
    return 1
}

stop_server() {         # stop_server: SIGTERM, then sets SERVER_STATUS
    kill -TERM "$SERVER_PID" 2>/dev/null
    if wait_gone "$SERVER_PID"; then
        wait "$SERVER_PID"
        SERVER_STATUS=$?
    else
        SERVER_STATUS=timeout
    fi
    SERVER_PID=""
}

# ---------------------------------------------------------------- clients

start_client() {        # start_client <name>: client reading <name>.in, writing <name>.out
    local fd
    rm -f "$1.in"
    mkfifo "$1.in" || return 1
    "$CLIENT_BIN" 127.0.0.1 "$PORT" < "$1.in" > "$1.out" 2>&1 &
    CLIENT_PID[$1]=$!
    exec {fd}> "$1.in"
    CLIENT_FD[$1]=$fd
}

say() {                 # say <name> <line>...: type lines into a client
    local name=$1
    shift
    printf '%s\n' "$@" >&"${CLIENT_FD[$name]}" 2>/dev/null
}

end_input() {           # end_input <name>: close the client's stdin (EOF)
    local fd=${CLIENT_FD[$1]}
    [ -n "$fd" ] && exec {fd}>&-
    unset "CLIENT_FD[$1]"
}

wait_client_exit() {    # wait_client_exit <name>: sets CLIENT_STATUS
    local pid=${CLIENT_PID[$1]}
    end_input "$1"
    if wait_gone "$pid"; then
        wait "$pid" 2>/dev/null
        CLIENT_STATUS=$?
    else
        CLIENT_STATUS=timeout
    fi
    unset "CLIENT_PID[$1]"
}

kill_client() {         # kill_client <name>: SIGKILL, no chance to log out
    local pid=${CLIENT_PID[$1]}
    { kill -9 "$pid"; wait "$pid"; } 2>/dev/null
    end_input "$1"
    unset "CLIENT_PID[$1]"
}

# ---------------------------------------------------------------- raw protocol

u32() {                 # u32 <value>: 32-bit big-endian integer
    printf "\\x$(printf %02x $((($1 >> 24) & 255)))\\x$(printf %02x $((($1 >> 16) & 255)))"
    printf "\\x$(printf %02x $((($1 >> 8) & 255)))\\x$(printf %02x $(($1 & 255)))"
}

field() {               # field <text> <size>: text padded with NUL bytes
    printf '%s' "$1"
    head -c $(($2 - ${#1})) /dev/zero
}

pkt_auth() {            # pkt_auth <name> <password> <option>
    u32 1; u32 68; field "$1" 32; field "$2" 32; u32 "$3"
}

pkt_chat() {            # pkt_chat <sender> <receiver> <text> <option>
    u32 3; u32 580; field "$1" 32; field "$2" 32; field "$3" 512; u32 "$4"
}

pkt_list() {
    u32 5; u32 0
}

raw_open() {            # raw_open <name>: connect; received bytes go to <name>.raw
    local fd
    exec {fd}<>"/dev/tcp/127.0.0.1/$PORT" || return 1
    RAW_FD[$1]=$fd
    cat <&"$fd" > "$1.raw" 2>/dev/null &     # A reset connection is expected here
    RAW_PID[$1]=$!
}

raw_send() {            # raw_send <name> <generator...>: send generator output
    local name=$1
    shift
    "$@" >&"${RAW_FD[$name]}" 2>/dev/null
}

raw_close() {           # raw_close <name>: drop our end; the reader ends with the server's
    local fd=${RAW_FD[$1]}
    [ -n "$fd" ] && exec {fd}>&-
    unset "RAW_FD[$1]"
}

raw_closed_by_server() { # raw_closed_by_server <name>: true when the server closed it
    local pid=${RAW_PID[$1]}
    if wait_gone "$pid"; then
        wait "$pid" 2>/dev/null
        unset "RAW_PID[$1]"
        return 0
    fi
    return 1
}

raw_kill() {            # raw_kill <name>: close the connection from our side
    raw_close "$1"
    { kill -9 "${RAW_PID[$1]}"; wait "${RAW_PID[$1]}"; } 2>/dev/null
    unset "RAW_PID[$1]"
}

server_alive() {
    kill -0 "$SERVER_PID" 2>/dev/null
}

# ================================================================ tests

echo "== Server start"
if start_server new; then pass "server listens on port $PORT"; else fail "server start"; exit 1; fi
check "no users.db before the first registration" test ! -e users.db

echo "== Register and login"
start_client alice
say alice 1 alice pw_alice
expect "alice registers" alice "message : Registered successfully"
expect "alice gets an empty online list" alice "No other users are online"
expect "menu offers single, group, logout and online users" alice "4 ) Online Users"
check "users.db created" test -f users.db
check "users.db has mode 600" test "$(stat -c %a users.db)" = 600
check "users.db holds 'alice pw_alice'" grep -qx 'alice pw_alice' users.db

start_client bob
say bob 1 bob pw_bob
expect "bob registers" bob "message : Registered successfully"
expect "bob's online list shows alice" bob "User alice is ONLINE"
expect "alice is told that bob joined" alice "User bob is ONLINE"
expect "join notice comes from the server" alice "Incoming Message from server :"

echo "== Login errors"
start_client carol
say carol 1 alice other_pw
expect "duplicate registration refused" carol "message : Duplicate user-name"
say carol 2 nobody some_pw
expect "unknown user refused" carol "message : user-name not found"
say carol 2 alice wrong_pw
expect "wrong password refused" carol "message : Password not matching"
say carol 2 alice pw_alice
expect "second login of an online user refused" carol "message : User is already logged in"
expect "client returns to the register / login menu" carol "Enter option : 1 ) Register" 5
say carol 1 carol pw_carol
expect "carol registers after four failures" carol "message : Registered successfully"
expect "alice is told that carol joined" alice "User carol is ONLINE"
expect "bob is told that carol joined" bob "User carol is ONLINE"

echo "== Online users (client option 4)"
say alice 4
expect "list shows bob" alice "User bob is ONLINE" 2
expect "list shows carol" alice "User carol is ONLINE" 2
check "list never shows the user itself" test "$(count_in alice.out 'User alice is ONLINE')" -eq 0

echo "== Group chat"
say alice 2 "hello from alice"
expect "sender gets a confirmation" alice "Message sent successfully"
expect "bob receives the group message" bob "hello from alice"
expect "carol receives the group message" carol "hello from alice"
expect "message shows the sender" carol "Incoming Message from alice :"
check "sender does not receive its own message" test "$(count_in alice.out 'Incoming Message from alice')" -eq 0

echo "== Private chat"
say bob 1 carol "psst carol"
expect "private message confirmed" bob "Message sent successfully"
expect "carol receives the private message" carol "psst carol"
expect "private message is marked" carol "Incoming Message from bob (private) :"
say alice "sync message"
expect "alice's next message is confirmed" alice "Message sent successfully" 2
check "alice did not receive the private message" test "$(count_in alice.out 'psst carol')" -eq 0

echo "== Chat errors"
say bob /back 1 nobody "hello?"
expect "unknown receiver" bob "Receiver not found"
say bob /back 1 bob
expect "client refuses a chat with yourself" bob "Cannot send a message to yourself"
say bob /back
start_client dave
say dave 1 dave pw_dave
expect "dave registers" dave "message : Registered successfully"
say dave 3
expect "dave logs out" dave "message : Logged out successfully"
expect "alice is told that dave left" alice "User dave is OFFLINE"
say bob 1 dave "are you there?"
expect "offline receiver" bob "Receiver is not online"
say bob /back
say dave 3
wait_client_exit dave
check "client exits with status 0 from the menu" test "$CLIENT_STATUS" = 0

echo "== Logout and login again"
say bob 3
expect "logout confirmed" bob "message : Logged out successfully"
expect "client returns to the register / login menu" bob "Enter option : 1 ) Register" 2
expect "alice is told that bob left" alice "User bob is OFFLINE"
say bob 2 bob pw_bob
expect "bob logs in again" bob "message : Logged in successfully"
expect "alice is told that bob is back" alice "User bob is ONLINE" 3

echo "== Client killed abruptly"
kill_client carol
expect "alice is told that carol went offline" alice "User carol is OFFLINE"
expect "bob is told that carol went offline" bob "User carol is OFFLINE"
start_client carol2
say carol2 2 carol pw_carol
expect "carol can log in again at once" carol2 "message : Logged in successfully"

echo "== Too many failed attempts"
start_client mallory
say mallory 2 alice bad1 2 alice bad2 2 alice bad3 2 alice bad4 2 alice bad5
expect "four refusals" mallory "message : Password not matching" 4
expect "fifth failure ends the session" mallory "message : Too many failed attempts"
wait_client_exit mallory
check "client exits with status 1" test "$CLIENT_STATUS" = 1
check "server logs the attempt" grep -q "too many failed attempts" "$SERVER_LOG"

echo "== Invalid input in the client"
long_name=$(head -c 40 /dev/zero | tr '\0' n)
long_line=$(head -c 2000 /dev/zero | tr '\0' x)
long_message=$(head -c 600 /dev/zero | tr '\0' m)
start_client typo
say typo 9
expect "bad menu option" typo "Invalid option, please enter 1, 2 or 3"
say typo 1 "bad name"
expect "user name with a space" typo "Invalid username"
say typo "$long_name"
expect "user name longer than 31 characters" typo "Invalid username" 2
say typo frank "bad pass"
expect "password with a space" typo "Invalid password"
say typo "$long_line"
expect "over-long input line" typo "Input line too long"
say typo pw_frank
expect "registration still works afterwards" typo "message : Registered successfully"
say typo 7
expect "bad option in the chat menu" typo "Invalid option, please enter 1, 2, 3 or 4"
say typo 2 "$long_message"
expect "message longer than 511 characters" typo "Message too long (max 511 characters)"
say typo /back 3 3
wait_client_exit typo
check "client exits normally" test "$CLIENT_STATUS" = 0

echo "== Server-side checks with a raw protocol client"
raw_open r1
raw_send r1 pkt_list
wait_text r1.raw "Please log in first"
check "requests before login are refused" test "$(count_in r1.raw 'Please log in first')" -eq 1
check "reply header is type 2, length 132, big-endian" \
    test "$(head -c 8 r1.raw | od -An -tx1 | tr -d ' \n')" = 0000000200000084
raw_send r1 pkt_auth rawuser pw_raw 1
check "raw client registers" wait_text r1.raw "Registered successfully"
raw_send r1 pkt_chat rawuser rawuser "to myself" 1
check "server refuses a message to yourself" wait_text r1.raw "Cannot send a message to yourself"
raw_send r1 pkt_chat rawuser "" "   " 2
check "server refuses an empty message" wait_text r1.raw "Message is empty"
raw_send r1 pkt_chat rawuser nobody "hi" 1
check "server refuses an unknown receiver" wait_text r1.raw "Receiver not found"
raw_send r1 pkt_chat rawuser "" "hi" 9
check "server refuses an unknown chat option" wait_text r1.raw "Invalid chat option"
alice_before=$(count_in bob.out "Incoming Message from alice :")
raw_send r1 pkt_chat alice "" "$(printf 'evil\033[2Jtext')" 2
expect "server replaces a spoofed sender" bob "Incoming Message from rawuser :"
expect "client prints control characters as '?'" bob "evil?[2Jtext"
check "no message appears to come from alice" \
    test "$(count_in bob.out 'Incoming Message from alice :')" -eq "$alice_before"
check "no escape byte reaches the screen" test "$(count_in bob.out "$(printf '\033')")" -eq 0
raw_send r1 pkt_auth rawuser pw_raw 2
check "second login on one session refused" wait_text r1.raw "Already logged in, log out first"
raw_send r1 pkt_chat rawuser "" "" 3
check "raw logout" wait_text r1.raw "Logged out successfully"
raw_send r1 pkt_chat rawuser "" "" 3
check "logout when not logged in" wait_text r1.raw "Couldn't log out"
raw_kill r1

echo "== Malformed data"
raw_open g1
raw_send g1 printf 'GARBAGE\001\002\003 this is not a packet at all ......\n'
check "garbage: server closes the connection" raw_closed_by_server g1
raw_open g2
raw_send g2 eval 'u32 1; u32 2147483647'
check "oversized length: server closes the connection" raw_closed_by_server g2
raw_open g3
raw_send g3 eval 'u32 1; u32 68; printf "%032d" 7; field pw 32; u32 2'
check "string without NUL: server closes the connection" raw_closed_by_server g3
raw_open g4
raw_send g4 eval 'u32 99; u32 0'
check "unknown packet type: server closes the connection" raw_closed_by_server g4
raw_open g5
raw_send g5 eval 'u32 3; u32 580; field half 100'
raw_kill g5
check "server logs the protocol errors" test "$(count_in "$SERVER_LOG" 'protocol error')" -ge 3
check "server is still running" server_alive
start_client frank
say frank 2 frank pw_frank
expect "server still serves new clients" frank "message : Logged in successfully"

echo "== 15 clients in a group chat at the same time"
GROUP=15
ROUNDS=5
for i in $(seq 1 $GROUP); do
    start_client "g$i"
    say "g$i" 1 "guser$i" "pw_$i"
done
for i in $(seq 1 $GROUP); do
    wait_text "g$i.out" "message : Registered successfully" || fail "guser$i registers"
done
for i in $(seq 1 $GROUP); do
    say "g$i" 2
    for r in $(seq 1 $ROUNDS); do
        say "g$i" "group-msg from guser$i #$r"
    done
done
expected=$(((GROUP - 1) * ROUNDS))
bad=""
for i in $(seq 1 $GROUP); do
    deadline=$((SECONDS + TIMEOUT * 3))
    while [ "$(grep -c '^group-msg from guser' "g$i.out")" -lt "$expected" ] && [ "$SECONDS" -lt "$deadline" ]; do
        sleep 0.05
    done
    got=$(grep -c '^group-msg from guser' "g$i.out")
    distinct=$(grep '^group-msg from guser' "g$i.out" | sort -u | wc -l)
    own=$(grep -c "^group-msg from guser$i #" "g$i.out")
    sent=$(count_in "g$i.out" "Message sent successfully")
    if [ "$got" -ne "$expected" ] || [ "$distinct" -ne "$expected" ] || [ "$own" -ne 0 ] || [ "$sent" -ne "$ROUNDS" ]; then
        bad="$bad guser$i(received $got, distinct $distinct, own $own, confirmed $sent)"
    fi
done
if [ -z "$bad" ]; then
    pass "each of $GROUP clients received all $expected messages of the others exactly once"
else
    fail "group messages" "$bad"
fi
check "alice received all $((GROUP * ROUNDS)) group messages" \
    wait_text alice.out "group-msg from guser" $((GROUP * ROUNDS))
for i in $(seq 1 $GROUP); do
    end_input "g$i"
done
expect "logout at end of input is announced" alice "User guser$GROUP is OFFLINE"
bad=""
for i in $(seq 1 $GROUP); do
    wait_client_exit "g$i"
    [ "$CLIENT_STATUS" = 0 ] || bad="$bad g$i($CLIENT_STATUS)"
    [ "$(count_in "g$i.out" "message : Logged out successfully")" -eq 1 ] || bad="$bad g$i(no logout)"
done
if [ -z "$bad" ]; then pass "all $GROUP clients logged out and exited at end of input"; else fail "client exits" "$bad"; fi

echo "== Connection limit ($MAX_SESSIONS sessions)"
disconnected_before=$(count_in "$SERVER_LOG" " disconnected")
opened=0
for i in $(seq 1 $((MAX_SESSIONS + 1))); do
    exec {fd}<>"/dev/tcp/127.0.0.1/$PORT" || break
    LIMIT_FD[$i]=$fd
    opened=$i
done
fd=${LIMIT_FD[$opened]}
timeout "$TIMEOUT" cat <&"$fd" > full.raw
check "connection over the limit gets 'Server is full'" grep -a -q "Server is full" full.raw
for i in $(seq 1 $opened); do
    fd=${LIMIT_FD[$i]}
    exec {fd}>&-
done
rejected=$(count_in "$SERVER_LOG" "rejected: server is full")
wait_text "$SERVER_LOG" " disconnected" $((disconnected_before + opened - rejected))
check "server is still running" server_alive
start_client after_limit
say after_limit 1 grace pw_grace
expect "sessions are released when connections close" after_limit "message : Registered successfully"

echo "== Server shutdown and restart"
users_before=$(wc -l < users.db)
stop_server
check "server exits with status 0 on SIGTERM" test "$SERVER_STATUS" = 0
check "server reports that it stopped" grep -q "Server stopped" "$SERVER_LOG"
bad=""
for name in alice bob carol2 frank after_limit; do
    wait_text "$name.out" "Connection closed by the server" || bad="$bad $name(not told)"
    wait_client_exit "$name"
    [ "$CLIENT_STATUS" = 1 ] || bad="$bad $name(exit status $CLIENT_STATUS)"
done
if [ -z "$bad" ]; then
    pass "connected clients are told and exit with status 1"
else
    fail "clients after the server stopped" "$bad"
fi
if start_server; then pass "server restarts on the same port"; else fail "server restart"; exit 1; fi
check "registered users are loaded again" grep -q "($users_before registered users)" "$SERVER_LOG"
start_client alice2
say alice2 2 alice pw_alice
expect "alice logs in after the restart" alice2 "message : Logged in successfully"
say alice2 2 "anyone here?"
expect "group message with nobody online" alice2 "No other users are online"
wait_client_exit alice2
check "client logs out and exits at end of input" test "$CLIENT_STATUS" = 0
check "logout reached the server" grep -q "'alice' logged out" "$SERVER_LOG"
stop_server
check "server exits with status 0" test "$SERVER_STATUS" = 0

echo "== Logs and reports"
leaks=""
for secret in pw_alice pw_bob pw_carol bad1 "hello from alice" "psst carol" "group-msg"; do
    grep -q -F -- "$secret" server*.log && leaks="$leaks [$secret]"
done
if [ -z "$leaks" ]; then pass "server log never contains passwords or message text"; else fail "server log leaks" "$leaks"; fi
SANITIZER_PATTERN="AddressSanitizer|LeakSanitizer|ThreadSanitizer|runtime error:"
reports=$(grep -l -E "$SANITIZER_PATTERN" server*.log ./*.out 2>/dev/null)
if [ -z "$reports" ]; then
    pass "no sanitizer reports in the server log or the client outputs"
else
    fail "sanitizer reports in:" "$reports
$(grep -h -A 20 -E "$SANITIZER_PATTERN" $reports 2>/dev/null | head -n 60)"
fi

echo
echo "Passed: $PASS  Failed: $FAIL"
[ "$FAIL" -eq 0 ]
