# TCP/IP Based Remote Management Solution

A small remote diagnostics tool written in C for Linux. It has two programs:

- **`server`** runs on the machine you want to look at. It listens on a TCP
  port and answers questions about the processes running on that machine.
- **`client`** connects to the server (from the same machine or another
  one), shows a `Client] ` prompt and lets you type commands:

| Command | What it does |
|---|---|
| `get-mem <process-name>` | Memory usage of the process |
| `get-cpu-usage <process-name>` | CPU usage of the process |
| `get-ports-used <process-name>` | TCP/UDP ports (sockets) the process uses |
| `get-open-fd <process-name>` | Open file descriptors of the process |
| `kill <process-name> [signal]` | Stops the process (SIGTERM by default) |
| `history` | The last 50 commands (kept between sessions) |
| `help` | List of commands |
| `exit`, `quit`, Ctrl+D | Leave the client |
| Up / Down arrow keys | Recall earlier commands |

The server reads everything directly from the `/proc` file system. It never
runs shell commands such as `ps` or `netstat`.

The assignment is in [remote_diagnostics.pdf](remote_diagnostics.pdf)
(Emertxe, Linux Internals module).

## Building

You need Linux (Ubuntu on WSL works) with `gcc` and `make`. Only the
standard C library is used, so there is nothing else to install.

```
make          # builds ./server and ./client
make test     # builds, then runs the automated tests
make asan     # rebuilds with AddressSanitizer + UndefinedBehaviorSanitizer
make clean    # removes the programs and object files
```

After `make asan`, run `make test` to test the sanitizer build. Run
`make clean && make` to go back to the normal build.

## Running

Open two terminals in the project directory.

**Terminal 1, the server:**

```
./server                    # port 6330, only local clients (127.0.0.1)
./server 7000               # port 7000
./server 6330 0.0.0.0       # port 6330, clients from other machines too
```

It runs until you press Ctrl+C. Every connection and request is logged:

```
RMS Server: Server Port Information: 127.0.0.1:6330
RMS Server: Incoming Remote Request from 127.0.0.1:34580
RMS Server: [127.0.0.1:34580] Request: get-mem sleep
RMS Server: [127.0.0.1:34580] Request: kill yes (SIGTERM)
RMS Server: [127.0.0.1:34580] Client disconnected
```

**Terminal 2, the client:**

```
./client                    # server on 127.0.0.1, port 6330
./client 192.168.1.10       # server on another machine, port 6330
./client 192.168.1.10 7000  # another machine and port
```

| Setting | Default | How to change it |
|---|---|---|
| Server port | 6330 (the port in the spec's sample output) | `./server <port>` |
| Server listen address | 127.0.0.1 (this machine only) | `./server <port> <address>`, e.g. `0.0.0.0` (all IPv4), `::` (all IPv6) |
| Client server address | 127.0.0.1 | `./client <address>` (IPv4, IPv6 or a host name) |
| Client server port | 6330 | `./client <address> <port>` |

The server listens on 127.0.0.1 by default because there is no password
(see [Security notes](#security-notes)). To reach it from another machine,
start it with `0.0.0.0` and make sure no firewall blocks the port.

Exit status: the server exits with 0 when stopped with Ctrl+C/SIGTERM and
with 1 if it cannot start (bad arguments, port in use). The client exits
with 0 after `exit`, `quit` or Ctrl+D, and with 1 if it cannot connect, the
server refuses it, the connection is lost or it is interrupted by a signal.

## Example session

This was recorded with three `sleep` processes and one `yes` process
running on the server machine:

```
$ ./client
INFO: Connected to the Server
Type help to get supported commands
Client] help
get-mem <process-name>          : Gets the memory usage of given process
get-cpu-usage <process-name>    : Gets the CPU usage of given process
get-ports-used <process-name>   : Fetch the socket ports used by given process
get-open-fd <process-name>      : Fetch the open file descriptors of a given process
kill <process-name>             : Kill the given process
history                         : Show commands history
help                            : Shows this output
kill <process-name> <signal>    : Send TERM (default), KILL, INT, HUP or QUIT
exit / quit / Ctrl+D            : Close the connection and exit
Up / Down arrow keys            : Recall previous commands
Client] get-mem sleep
INFO: Message sent to the Server
INFO: Message accepted by the Server
RESPONSE: "PID       %MEM    RSS(kB)    VSZ(kB)   PEAK(kB)  SWAP(kB)  NAME
50         0.0       1524       2588       1524         0  sleep
51         0.0       1688       2588       1688         0  sleep
53         0.0       1636       2588       1636         0  sleep
(%MEM = RSS as a share of the 7797396 kB of RAM)"
Client] get-mem firefox
INFO: Message sent to the Server
INFO: Message accepted by the Server
RESPONSE: "No process named 'firefox' is running"
INFO: Server reported: no matching process
Client] history
1 get-mem sleep
2 get-mem firefox
Client] exit
INFO: Disconnected from the Server
```

Each remote command prints the same three steps as the spec's figures:

1. `INFO: Message sent to the Server`: the request has been sent.
2. `INFO: Message accepted by the Server`: the server checked the request
   and started working on it. If the server finds something wrong with the
   request, you get `INFO: Message rejected by the Server` and an `ERROR:`
   line with the reason instead.
3. `RESPONSE: "..."`: the answer, between quotes. Long answers span several
   lines; the closing quote comes after the last one. If the answer is not
   a plain success, one more line says so, for example
   `INFO: Server reported: no matching process`.

When several processes have the same name, every command reports each of
them separately, with its PID.

## Commands

### get-mem

```
Client] get-mem sleep
RESPONSE: "PID       %MEM    RSS(kB)    VSZ(kB)   PEAK(kB)  SWAP(kB)  NAME
50         0.0       1524       2588       1524         0  sleep
(%MEM = RSS as a share of the 7797396 kB of RAM)"
```

All values come from `/proc/<pid>/status`, in kB (1024 bytes):

| Column | Field | Meaning |
|---|---|---|
| RSS | `VmRSS` | Physical memory (RAM) the process uses now |
| VSZ | `VmSize` | Virtual memory: everything mapped into its address space, used or not |
| PEAK | `VmHWM` | Highest RSS since the process started |
| SWAP | `VmSwap` | Memory moved out to swap |
| %MEM | | RSS as a percentage of `MemTotal` in `/proc/meminfo` (like `ps`) |

RSS counts shared memory (shared libraries) in full for every process
that uses it, so adding up the RSS of several processes gives too much.
Kernel threads have no user memory and show `-`.

### get-cpu-usage

```
Client] get-cpu-usage yes
RESPONSE: "PID       %CPU  AVG%CPU    CPU-TIME THREADS STATE  NAME
52       100.0     99.3     0:01.34       1     R  yes
(%CPU = share of one CPU core used during the last 1.0 s; AVG%CPU = average since the process started;
 12 CPU core(s) online, so a multi-threaded process can reach 1200%)"
```

This command takes about one second, because the server measures:

1. It reads `utime + stime` (CPU time spent in user and kernel mode, in
   clock ticks) from `/proc/<pid>/stat`.
2. It waits one second (`CPU_SAMPLE_MS` in `diag.h`).
3. It reads them again.

| Column | Meaning |
|---|---|
| %CPU | CPU time used during that second divided by the second, x 100. 100 means one CPU core was busy all the time, as in `top`. A process with several busy threads can go above 100. |
| AVG%CPU | Total CPU time divided by the time since the process started, x 100. This is what `ps` shows. |
| CPU-TIME | Total CPU time, as minutes:seconds.hundredths |
| THREADS | Number of threads |
| STATE | R running, S sleeping, D waiting for I/O, T stopped, ... |

The kernel counts CPU time in ticks of usually 10 ms, so %CPU moves in
steps of about 1.

### get-ports-used

```
Client] get-ports-used server
RESPONSE: "Proto Recv-Q Send-Q Local Address           Foreign Address         State       PID/Program name
tcp        0      0 127.0.0.1:6330          0.0.0.0:*               LISTEN      48/server
tcp        0      8 127.0.0.1:6330          127.0.0.1:34580         ESTABLISHED 58/server"
```

The layout is that of `netstat -anp` (and of the spec's Fig 5.6). How it is
found:

1. Each entry in `/proc/<pid>/fd` that links to `socket:[<inode>]` is a
   socket of the process; the number is the socket's inode.
2. `/proc/<pid>/net/tcp`, `tcp6`, `udp` and `udp6` list all TCP and UDP
   sockets (IPv4 and IPv6) with their addresses, ports, state, queues and
   inode.
3. The lines whose inode belongs to the process are shown.

- **Recv-Q / Send-Q**: bytes received but not yet read by the process / sent
  but not yet acknowledged by the other side. For a listening socket,
  Recv-Q is the number of connections waiting to be accepted.
- **Foreign Address** `0.0.0.0:*` means "no peer" (listening or unconnected).
- UDP has no connection state. As in netstat, a connected UDP socket shows
  `ESTABLISHED` and an unconnected one shows nothing.
- IPv6 addresses are written in brackets: `[::1]:6330`.
- Sockets without a port (UNIX domain sockets, netlink, ...) are counted
  in a note below the table, e.g.
  `PID 812 (firefox): also 41 other sockets without a port (UNIX domain, netlink, ...)`.
  A process without any socket gets `PID 50 (sleep): no sockets open`.

### get-open-fd

```
Client] get-open-fd sleep
RESPONSE: "PID 53 (sleep): 5 open file descriptors
FD     MODE TARGET
0      r    /dev/null
1      w    /dev/null
2      w    /dev/null
3      rw   socket:[265926]
4      rw   /tmp/notes.txt"
```

- **FD**: descriptor number, from `/proc/<pid>/fd`.
- **TARGET**: what the descriptor refers to (`readlink` of
  `/proc/<pid>/fd/<n>`): a file path, `pipe:[...]`, `socket:[...]`,
  `anon_inode:[eventfd]`, ... A deleted file ends in `(deleted)`.
- **MODE**: `r`, `w` or `rw`, from the `flags:` line of
  `/proc/<pid>/fdinfo/<n>`.

At most 1024 descriptors are listed per process (`FD_LIST_LIMIT`); the
count line always shows the real total. Unusual bytes in file names are
shown as `\xNN`.

### kill

```
Client] kill yes
RESPONSE: "PID 52 (yes): SIGTERM sent - process terminated
1 of 1 matching process terminated"
```

- By default `kill` sends **SIGTERM**, which asks the process to exit and
  lets it clean up (like the `kill` shell command).
- You can choose another signal: `kill <name> KILL`, and also `INT`, `HUP`
  and `QUIT`. They may be written as `KILL`, `SIGKILL`, `-KILL`, `9` or
  `-9`, in any case. Other signals (for example STOP) are not allowed.
- **Every** process with that name gets the signal. The server then waits up
  to one second (`KILL_WAIT_MS`) and reports for each one whether it
  really ended:
  ```
  PID 77 (stubborn): SIGTERM sent - still running after 1000 ms (it may catch or ignore SIGTERM; try: kill stubborn KILL)
  PID 78 (sshd): could not send SIGTERM: Operation not permitted
  ```
- The server never signals PID 1 (init) or the server process that is
  answering the request. `kill server` stops the main server and the
  other clients' sessions.
- A process started in the background by a shell script (`cmd &` in a
  non-interactive shell) ignores SIGINT and SIGQUIT, so `kill <name> INT`
  reports it as still running.

### history

```
Client] history
1 get-mem firefox
2 get-ports-used firefox
```

- Lists the commands that were sent to the server, oldest first, numbered
  like the spec's Fig 5.7. As in that figure, `help`, `history` and lines
  with errors are not recorded.
- At most **50** commands are kept; when a 51st is added the oldest one is
  dropped.
- On exit the history is saved to **`$HOME/.remote_history`** (one command
  per line, readable only by you) and loaded again when the client starts,
  so `history` and the arrow keys also show commands from earlier sessions.
  If `HOME` is not set, `./.remote_history` in the current directory is used.
- It is saved whatever way the client ends: `exit`, Ctrl+D, Ctrl+C, the
  connection being lost, or the client being killed with SIGTERM/SIGHUP.
  Only `kill -9` loses the new commands.

### help, exit, quit

`help` prints the command list (the first seven lines are exactly those of
Fig 5.3). `exit`, `quit` or Ctrl+D on an empty line close the connection.

### Keys at the prompt

| Key | Action |
|---|---|
| Up / Down (also Ctrl+P / Ctrl+N) | Previous / next command from the history. Going down past the newest command brings back what you were typing. |
| Left / Right (Ctrl+B / Ctrl+F) | Move the cursor |
| Home / End (Ctrl+A / Ctrl+E) | Start / end of the line |
| Backspace, Delete | Delete a character |
| Ctrl+U / Ctrl+K | Delete to the start / end of the line |
| Ctrl+L | Clear the screen |
| Ctrl+C | Discard the line (while a request is running: exit the client) |
| Ctrl+D | Exit on an empty line, otherwise delete under the cursor |

## How processes are found

A process matches when its **kernel name** equals the name you typed
exactly (case-sensitive). That is the name `ps`, `top` and `pgrep` show,
from `/proc/<pid>/stat`.

The kernel keeps only the first 15 characters of a name. For a longer
name such as `gnome-terminal-server` the kernel name is `gnome-terminal-`,
and you may type either. The full name matches when the kernel name equals
its first 15 characters and the program file name (from
`/proc/<pid>/cmdline`) is the full name.

Zombie processes (exited, waiting for their parent) are skipped. At most 512
processes are reported per name.

Process names may contain letters, digits and `. _ - + : @ / ~ , = # ( ) [ ]`,
up to 64 characters, and may not start with `-`. Names with spaces cannot be
typed.

## Files

| File | Contents |
|---|---|
| `server.c` | Server main program: arguments, listening socket, `fork()` per client, reaping children, shutdown |
| `session.c`, `session.h` | One client connection on the server: requests, replies, activity log |
| `diag.c`, `diag.h` | `get-mem`, `get-cpu-usage`, `get-open-fd`, `kill` |
| `ports.c`, `ports.h` | `get-ports-used` |
| `procinfo.c`, `procinfo.h` | Reading `/proc`: finding processes by name, `stat`, `status`, descriptors |
| `strbuf.c`, `strbuf.h` | Growable text buffer with a size limit, used to build replies |
| `client.c` | Client main program: connecting, commands, `help`, printing replies |
| `lineedit.c`, `lineedit.h` | The line editor (arrow keys) using termios raw mode |
| `history.c`, `history.h` | History array (50 entries) and `.remote_history` |
| `protocol.c`, `protocol.h` | Message format shared by both programs, sending/receiving with timeouts, request checks |
| `util.c`, `util.h` | Clock, sleeping, port parsing, address formatting, escaping |
| `makefile` | Builds both programs; `test`, `asan`, `clean` targets |
| `tests/run_tests.sh` | Automated tests |
| `remote_diagnostics.pdf` | The assignment |

## Design

### Architecture

```
  client machine                                  server machine
 +----------------------+      TCP       +-------------------------------+
 | ./client             |                | ./server (parent)             |
 |  lineedit: prompt,   |   connect      |   accept() loop               |
 |    arrow keys        | -------------> |   fork() for every client ----+---+
 |  history (50)        |                +-------------------------------+   |
 |  help, history:      |                                                    v
 |    answered locally  |   REQUEST      +-------------------------------+
 |  get-*, kill:        | -------------> | child process (session.c)     |
 |    sent to server    | <------------- |   checks the request          |
 |                      |  ACK, DATA..., |   reads /proc (procinfo.c)    |
 |                      |  END           |   diag.c / ports.c build text |
 +----------------------+                +-------------------------------+
```

### Message format (protocol.h)

Every message is a frame with an 8-byte header followed by the payload. The
numbers are in network byte order (big-endian).

```
 0      2         3      4               8
 +------+---------+------+---------------+--------------------------+
 | "RM" | version | type | payload length| payload (0..4096 bytes)  |
 +------+---------+------+---------------+--------------------------+
```

| Type | Direction | Payload |
|---|---|---|
| HELLO | server -> client | none: "connection accepted" |
| REQUEST | client -> server | command (1 byte), signal (1 byte, kill only), process name (1-64 bytes) |
| ACK | server -> client | none: "request accepted, reply follows" |
| NACK | server -> client | reason text: request rejected (or server busy) |
| DATA | server -> client | part of the reply text (at most 4096 bytes per frame) |
| END | server -> client | status (1 byte): 0 ok, 1 no such process, 2 failed for some processes, 3 failed |

The request is the spec's "Command | Argument" format. The reply is text
("File data" in the spec) split into as many DATA frames as needed, so a
reply can be longer than one frame. The server limits a reply to 256 KB.

A session looks like this:

```
client                          server
   | ------- connect -------------> |
   | <------ HELLO ---------------- |   (or NACK "Server busy")
   | ------- REQUEST get-mem x ---> |
   | <------ ACK ------------------ |   "Message accepted by the Server"
   | <------ DATA ----------------- |   reply text
   | <------ END (status) --------- |
   | ------- REQUEST ... ---------> |   ... until the client disconnects
```

Checks on everything that comes from the network:

- The header is checked before any payload is read: wrong magic bytes, an
  unknown version or type, or a length above 4096 make the server send a
  NACK with the reason and close the connection. A huge length is never
  read or allocated.
- A REQUEST with an unknown command, a signal that is not allowed, a missing,
  too long or badly formed name gets a NACK, and the connection stays open.
- Once a frame has started, the rest must arrive within 10 seconds, so a
  client that sends half a message cannot block a server process forever.
  A client that sends nothing for 30 minutes is disconnected.
- The client checks the server's frames the same way and replaces any
  unprintable byte in the reply by `?`, so a server cannot send terminal
  control sequences.

### Concurrency (server.c)

- The server is concurrent: the parent only accepts connections and forks
  one child process per client. A slow request (such as the one-second CPU
  measurement) never delays other clients.
- Finished children are reaped with `waitpid(..., WNOHANG)`, so no zombie
  processes are left. SIGCHLD, SIGINT, SIGTERM and SIGHUP are blocked
  except inside `pselect()`, which avoids the classic race between checking
  a flag and starting to wait.
- At most 32 clients are served at once (`MAX_CLIENTS`). A 33rd client gets
  `ERROR: The Server refused the connection: Server busy: too many clients,
  try again later`.
- Ctrl+C, SIGTERM or SIGHUP stops the server: it closes the listening
  socket, sends SIGTERM to the children (SIGKILL after 2 seconds) and
  reaps them.
- Log lines are written with a single `write()` each, so lines from
  different children do not get mixed.

### Client: line editor and history

- The spec asks for arrow keys, but GNU readline is not always installed,
  so `lineedit.c` is a small editor of its own. While a line is being typed
  the terminal is in raw mode (termios: no echo, no line buffering, Ctrl+C
  arrives as a key), and the editor redraws the line after every key. Arrow
  keys arrive as escape sequences (`ESC [ A` for Up, ...).
- The terminal is put back to normal after every line, and also through
  `atexit()` and the signal handlers, so the shell is never left in raw mode.
- When stdin is not a terminal (a pipe or a file), the client reads plain
  lines and echoes them after the prompt, so a scripted session reads like
  the interactive one:
  `printf 'get-mem sleep\nhistory\n' | ./client`
- The history is an array of 50 strings (`history.c`). It is written to a
  temporary file that is then renamed over `.remote_history`, so an
  interrupted save cannot leave half a file. Lines in the file that are too
  long or contain odd bytes are ignored when loading.

### Robustness

- All sockets are non-blocking and every wait uses `poll()` with a timeout.
  Partial sends and receives are continued, and calls interrupted by a
  signal (EINTR) are retried.
- SIGPIPE is ignored and `send()` uses `MSG_NOSIGNAL`, so a client that
  disappears cannot kill the server.
- A process may exit while the server reads its `/proc` files. Every read
  can fail, and the server then reports "exited before it could be
  measured" (or skips it) instead of failing. PID reuse is detected by
  comparing the process start time.
- Reply texts are built in a growable buffer with a size limit; there are no
  fixed-size buffers that could overflow.
- Every function's return value is checked, and both programs pass the
  tests with AddressSanitizer and UndefinedBehaviorSanitizer (no leaks).

## Testing

`make test` runs `tests/run_tests.sh` (needs bash, coreutils, grep and
sed). It starts a server on a random port in a temporary directory and uses
copies of `sleep` and `bash` with unique names (`rdtest_sleeper`,
`rdtest_multi`, ...) as target processes, so nothing else on the machine is
touched. Each check prints PASS or FAIL, and the run ends with
`Passed: N  Failed: M`. Every wait polls with a time limit
(`TEST_TIMEOUT`, default 20 seconds) instead of sleeping for a fixed time.

What is tested:

- the outputs of the spec's figures: server start, connection, help,
  the request/ACK/response lines, history;
- every command against a known process, with the values compared to
  `/proc` (VSZ, the descriptor list and count, a listening TCP socket, a
  connected UDP socket, an IPv6 socket, a busy and an idle process);
- an unknown process, several processes with one name, names longer than
  15 characters;
- kill: SIGTERM, a process that ignores SIGTERM, `KILL`, `-sighup`, `9`;
- history: numbering, the 50-entry limit, saving and loading
  `.remote_history`, a damaged history file, saving on SIGTERM;
- invalid commands and arguments (none of them reaches the server);
- concurrency: five clients at once, an idle client not blocking others,
  the 32-client limit, no zombie processes;
- garbage bytes, an oversized length, bad version/type, invalid requests and
  a truncated frame sent with bash's `/dev/tcp`; the server must answer
  correctly and keep serving;
- the arrow keys, editing keys, Ctrl+C and Ctrl+D in a real terminal (with
  the `script` command; these tests are skipped if it is missing), and that
  the terminal settings are restored after the client is killed;
- when run as root: a second server running as user `nobody` cannot read
  another user's descriptors or kill their processes;
- command line errors and a clean server shutdown.

There are 150 checks. As a normal user 146 run and the permission test is
skipped (it needs root). All of them pass with the normal build and with
`make asan`, and the test script fails if a sanitizer reports anything. A
full run takes 15 to 30 seconds.

## Differences from the spec's sample output

- The prompt is `Client] ` as written in the requirements (the figures show
  `client]`).
- The spec's figures show a single number for `get-mem` (`"19.4"`). Here
  each command returns a small table with more detail, still inside
  `RESPONSE: "..."`; `%MEM` is the column that corresponds to that number.
- The server's first line reads "Server Port Information" (the figure has
  a typo).

## Limitations

- Process names are matched exactly; there are no wildcards, and names with
  spaces cannot be typed.
- A process can only be identified by its name, not by PID, so `kill` always
  hits all processes with that name.
- `get-ports-used` shows TCP and UDP sockets only. Raw, UNIX domain and
  netlink sockets are only counted.
- Between finding a process and signalling it there is a very short window
  in which the process could exit and its PID be reused. The server checks
  the start time just before sending the signal, which makes this extremely
  unlikely, but cannot rule it out completely.
- Only one history file is used per user; if two clients run at the same
  time, the one that exits last decides its contents.
- Lines longer than the terminal scroll sideways; a command line is at most
  255 characters.
- The server needs Linux (`/proc`). The client works on any POSIX system.

## Security notes

Please read this before starting the server on a network.

- **There is no authentication and no encryption.** Anyone who can connect
  to the port can see all processes and **kill** them. The traffic can be
  read by anyone on the network.
- That is why the server only listens on 127.0.0.1 unless you give another
  address. Only use `0.0.0.0` on a network you trust, or put a firewall
  rule in front of the port.
- The server can only do what the user who started it may do. Started as a
  normal user, it can read the memory and CPU figures of all processes but
  cannot list other users' file descriptors or sockets, and cannot kill
  their processes (it reports "Permission denied" / "Operation not
  permitted"). **Do not run the server as root** (for example with
  `sudo`) unless you really need it: then every client can kill any process
  on the machine.
- The server never starts a shell or another program, so a request cannot
  be turned into a command. Process names are checked against a short
  list of allowed characters and are only compared with process names.
