# TFTP Client and Server (RFC 1350)

A TFTP server and an interactive TFTP client in C for Linux, built on the
Emertxe *Linux Internals* project skeleton. TFTP (Trivial File Transfer
Protocol) copies files over UDP: a client can **get** a file from a server or
**put** a file onto it. TFTP has no login and no directory listing.

- `tftp_server` runs in the background and waits for requests. For every
  request it forks a child process, and the child answers from a new UDP port
  (20000, 20001, ...). That port is the transfer's "transfer ID".
- `tftp_client` shows a `tftp] ` prompt with the commands `connect`, `get`,
  `put`, `mode`, `help` and `bye` / `quit`.

The assignment is in
[tftp_project_requirements_and_design.pdf](tftp_project_requirements_and_design.pdf).

## Building

Needs Linux (Ubuntu under WSL is fine) with gcc and make. Only the C library
is used.

```
sudo apt install build-essential   # only if gcc or make are missing
make            # builds ./tftp_server and ./tftp_client
make server     # builds only the server
make client     # builds only the client
make test       # builds everything, then runs the automated tests (226 checks, ~30 s)
make asan       # rebuilds with AddressSanitizer + UndefinedBehaviorSanitizer;
                # run "make test" afterwards to test that build
make clean      # removes the programs, the test helpers and the object files
```

After `make asan`, run `make clean && make` to go back to the normal build.

## Running

Start the server in the background. It prints nothing while it runs.

```
$ mkdir -p /tmp/tftpboot
$ ./tftp_server -d /tmp/tftpboot &
```

| Server option | Meaning |
|---|---|
| `-p <port>` | UDP port to listen on. Default **6969**. The standard TFTP port is 69, but ports below 1024 need root: `sudo ./tftp_server -p 69 &` |
| `-d <dir>` | Directory whose files are served and where uploads are stored. Default: the current directory |
| `-v` | Log every request and every finished transfer to stderr, e.g. `./tftp_server -v 2>server.log &` |
| `-h` | Show the options |

Stop the server with `kill %1` (or `kill <pid>`). Transfers that are still
running finish in their own processes.

A client session (this is real output; `syslog` was a 35 KB file in
`/tmp/tftpboot`, `notes.txt` a 26 KB file in the client's directory):

```
$ ./tftp_client
Type help to get supported features
tftp] help
connect <server-ip> [port]  : Connect to server
get <file-name> [local]     : Receive a file from server
put <file-name> [remote]    : Send a file to the server
mode [octet|netascii]       : Transfer mode(s) (octet, netascii; mail is obsolete)
help                        : Show this list
bye / quit                  : Exit the application
Default server port is 6969, default mode is netascii.
tftp] get syslog
ERROR: Not connected to server
tftp] connect 127.0.0.1
INFO: Connected to server
tftp] put notes.txt
INFO: File: notes.txt
INFO: Transfer Mode: netascii
###
INFO: File transfer successful
tftp] put notes.txt
INFO: File: notes.txt
INFO: Transfer Mode: netascii
ERROR: File already exist
tftp] get log
INFO: File: log
INFO: Transfer Mode: netascii
ERROR: File not found
tftp] get syslog
INFO: File: syslog
INFO: Transfer Mode: netascii
####
INFO: File receive successful
tftp] mode octet
INFO: Transfer Mode: octet
tftp] mode mail
ERROR: Mode 'mail' is obsolete (RFC 1350) and not supported; use octet or netascii
tftp] tata
Invalid command
tftp] bye
```

The server can also be given on the command line, which connects at once:

```
$ ./tftp_client 127.0.0.1
Type help to get supported features
INFO: Connected to server
tftp] get syslog
...
```

`./tftp_client 192.168.1.20 69` talks to a standard TFTP server on port 69.

### Client commands

| Command | What it does |
|---|---|
| `connect <server-ip> [port]` | Sets the server: an IPv4 address or a host name, port 6969 unless given. Prints `INFO: Connected to server`. |
| `get <file> [local]` | Downloads `<file>` into the current directory, saved under the last part of its name (`get logs/a.txt` saves `a.txt`), or as `[local]`. |
| `put <file> [remote]` | Uploads a local file. On the server it is stored under the last part of its local name, or as `[remote]` (which may name a subdirectory of the served directory). |
| `mode [octet\|netascii]` | Sets the transfer mode; without an argument it shows the mode. Default `netascii`, as in the PDF. |
| `help` | Lists the commands. |
| `bye` / `quit` | Leaves the client. End of input (Ctrl-D) and Ctrl-C at the prompt do the same. |

Ctrl-C during a transfer cancels it: the server is told with an ERROR packet
and the client returns to the prompt.

## How it behaves

These are the points the PDF leaves open, and the choices made:

- **Ports.** The PDF's flow diagram has the server waiting on port 69, but
  that needs root, so the default is 6969 for both programs and `-p 69`
  restores the standard port. Every request (each `get` or `put`) is served
  from a new port: 20000 for the first, then 20001, and so on. A port that is
  busy is skipped; after 65535 the numbers start again at 20000. The client
  learns the new port from the server's first answer, as RFC 1350 describes.
- **`connect`** only stores the address, because UDP has no connections. A
  wrong address shows up at the first `get`/`put` as
  `ERROR: Server not responding (transfer timed out)`, after about 11 seconds.
- **Served directory.** Files are served from the `-d` directory and its
  subdirectories. Refused with "Access violation": absolute names, any `..`
  component, symbolic links anywhere in the name, and anything that is not a
  regular file (directories, devices, FIFOs). So no request can reach a file
  outside the directory.
- **Uploads never overwrite.** `put` of a name that already exists on the
  server fails with `ERROR: File already exist` (the PDF's wording). New files
  get permissions 0644 (minus the server's umask). If an upload fails or is
  abandoned, the incomplete file is deleted.
- **Downloads are safe.** `get` writes to `<name>.<pid>.part` and renames it
  to `<name>` only when the transfer succeeded. A failed `get` (for example
  "File not found") leaves an existing local file untouched; a successful one
  replaces it, like the standard `tftp` client.
- **Modes.** `octet` sends the bytes unchanged. `netascii` converts Unix text
  to the network form on the way out (LF becomes CR LF, a bare CR becomes
  CR NUL) and back on the way in. Both programs convert, so a text file
  arrives byte for byte identical. The conversion works for any bytes, but
  binary files should use `octet`, which sends less data. Mode names are
  case-insensitive (`OCTET` works). `mail` is listed in the PDF's command list,
  but both the PDF and RFC 1350 call it obsolete: the client refuses
  `mode mail`, and the server answers a mail request with error 4.
- **Messages.** The client prints the PDF's lines: `INFO: File: ...`,
  `INFO: Transfer Mode: ...`, `INFO: File transfer successful` (put),
  `INFO: File receive successful` (get), `ERROR: File not found` (error 1) and
  `ERROR: File already exist` (error 6). Other server errors are printed as
  `ERROR: <message from the server>`. The PDF's help text says `connet`; that
  typo is fixed. In the PDF's sample, `get log` prints `INFO: File: syslog`;
  the client prints the name that was asked for.
- **Progress bar.** One `#` per 8 KB (16 blocks), at least one per
  successful transfer, 64 per line. TFTP does not tell the client the file
  size in advance, so it cannot be a percentage.
- **File size.** Block numbers are 16-bit. After block 65535 they continue at
  0, so there is no size limit (the tests transfer 33 MB = 65538 blocks).
  Some other TFTP programs stop at 65535 blocks (32 MB).
- **Timeouts.** A packet that gets no answer within 1 second is sent again,
  at most 10 times; then the transfer is abandoned.
- **The server is quiet.** It writes nothing unless `-v` is given, so it can
  share a terminal with the client as in the PDF's figures. Start-up errors
  (port in use, missing directory) are always printed.
- **Limits.** At most 64 transfers run at the same time; more requests get
  error 0 "Server busy, try again later". The client's prompt splits commands
  at spaces, so file names containing spaces cannot be typed.

## Files

| File | Contents |
|---|---|
| `client.c` | Interactive client: prompt, commands, progress bar |
| `server.c` | Server: options, listening loop, fork per request, child process |
| `tftp.c`, `tftp.h` | Protocol: constants, packet formation and parsing, error codes, and the transfer engine (DATA sender, DATA receiver) shared by both programs |
| `file.c`, `file.h` | File operations: reading/writing blocks, netascii on the fly, safe opening below the served directory, download temporary file |
| `netascii.c`, `netascii.h` | Netascii encoder and decoder that keep their state between blocks |
| `udp.c`, `udp.h` | UDP sockets: listening socket, transfer port allocation, send, receive with timeout, addresses |
| `makefile` | Build, test and sanitizer targets |
| `tests/run_tests.sh` | Automated tests (bash + coreutils only) |
| `tests/unit_tests.c` | Unit tests of netascii, packets, file and path functions |
| `tests/tftp_probe.c` | Test helper that sends hand-made TFTP packets and prints the replies |
| `tftp_project_requirements_and_design.pdf` | The assignment |

## Design

### Packets (RFC 1350)

All numbers are 16-bit, big-endian. The largest valid packet is 516 bytes.

```
RRQ / WRQ   | 01 or 02 | file name ... | 0 | mode ... | 0 |
DATA        | 03       | block number  | 0 to 512 bytes of data |
ACK         | 04       | block number  |
ERROR       | 05       | error code    | message ... | 0 |
```

| Code | Meaning (RFC 1350) | Sent by this server when |
|---|---|---|
| 0 | Not defined, see error message | Other errors (text from `strerror`), server busy, transfer cancelled |
| 1 | File not found | RRQ of a missing file, WRQ into a missing directory |
| 2 | Access violation | Unsafe name, symlink, directory, no permission |
| 3 | Disk full or allocation exceeded | `ENOSPC`, `EDQUOT`, `EFBIG` while writing |
| 4 | Illegal TFTP operation | Malformed packet, wrong opcode, mode `mail` or unknown mode |
| 5 | Unknown transfer ID | A packet from the wrong address/port during a transfer |
| 6 | File already exists | WRQ of an existing name |
| 7 | No such user | Never (only for the obsolete mail mode) |

The PDF's error table is shifted from 5 on (it lists "Illegal TFTP operation"
twice and has no "No such user"). The code follows RFC 1350.

### Server

```
parent: open directory, bind listening port
  loop forever:
    reap finished children (waitpid WNOHANG; SIGCHLD wakes the loop)
    wait up to 1 s for a datagram on the listening port
    not a valid RRQ/WRQ  -> error 4 (an ERROR packet is never answered)
    64 children running  -> error 0 "Server busy"
    bind a socket to the next free port from 20000 -> fork()
child: close the listening socket
    check mode and name, open the file  -> on failure: ERROR from the new port
    RRQ: send the file   WRQ: send ACK 0, receive the file
    log the result, exit
```

All answers to a request, errors included, come from the child's new port,
as in the PDF's flow diagram. The parent never touches files and never blocks
on anything but `poll()`, so one slow client cannot hold up the others.

### Transfer IDs

The client opens a new socket (a new port chosen by the kernel) for every
transfer and sends the request to the server's listening port. The first valid
answer from the server's IP address (DATA 1 or ERROR for a `get`, ACK 0 or
ERROR for a `put`) fixes the server's port for the rest of the transfer. From
then on, on both sides, a packet from any other address or port is answered
with error 5 and otherwise ignored; the transfer carries on.

### Transfer engine (`tftp.c`)

The same two functions do every transfer, as the PDF suggests:
`tftp_send_file()` is used by the server for an RRQ and by the client for
`put`; `tftp_receive_file()` by the server for a WRQ and by the client for
`get`.

```
Sender                                      Receiver
  [client: send WRQ, wait for ACK 0]          [client: send RRQ]  [server: send ACK 0]
  read block n (512 bytes, less at the end)   wait for DATA n:
  send DATA n, wait for ACK n:                  DATA n      -> write it, send ACK n;
    ACK n      -> n is acknowledged;                           if < 512 bytes: done
                  if < 512 bytes: done          DATA n-1    -> our ACK was lost:
                  else n = n + 1                               send ACK n-1 again
    other ACK  -> ignore (duplicate)            other DATA  -> ignore
    timeout    -> send DATA n again             timeout     -> send last ACK (or RRQ) again
    ERROR      -> stop, report it               ERROR       -> stop, report it
    other      -> send error 4, stop            other       -> send error 4, stop
```

- **End of file.** A DATA block shorter than 512 bytes ends the transfer.
  A file whose size is a multiple of 512 ends with an empty DATA block.
- **Timeouts.** Each wait has a deadline of 1 second. Packets that are
  ignored (duplicates, strangers) do not move the deadline, so they cannot
  keep a dead transfer alive. After 10 retransmissions of the same packet the
  transfer is abandoned.
- **Sorcerer's Apprentice bug.** The sender only retransmits on a timeout,
  never because of a duplicate ACK, so a delayed ACK cannot double every
  following packet (RFC 1123, 4.2.3.1). The receiver does answer a repeated
  DATA block with its ACK again, which is what makes a lost ACK recoverable.
- **Final ACK.** The receiver writes and closes the file before it sends the
  final ACK, so a file is complete as soon as the other side reports success.
  If the final ACK is lost the sender repeats the last block; the server
  therefore waits 2 seconds after the final ACK of an upload (it "dallies")
  and acknowledges such repeats again.
- **Block numbers** are kept in 64-bit counters and sent modulo 65536.

### Netascii (`netascii.c`)

The encoder turns LF into CR LF and CR into CR NUL. Each DATA block must be
exactly 512 bytes until the last one, so when only the CR of a pair fits in a
block, the encoder keeps the second byte and puts it first in the next block.
The decoder turns CR LF into LF and CR NUL into CR; a CR at the end of a block
is held until the next block shows what follows it. A CR followed by anything
else (not valid netascii) is kept as a CR, so no data is lost. Unit tests
check `decode(encode(x)) == x` for thousands of random inputs split at random
places.

### Never trusting the network

- Datagrams are received into a 517-byte buffer, so anything larger than
  516 bytes is detected and rejected.
- File name and mode must be non-empty and NUL-terminated inside the packet;
  ACK must be exactly 4 bytes; DATA at least 4. Options after the mode
  (RFC 2347) are ignored, which RFC 2347 allows.
- ERROR messages and names are shown with control characters replaced by `?`,
  so a hostile server cannot send terminal escape sequences.
- Files are opened one path component at a time with `openat()` and
  `O_NOFOLLOW` from the served directory, which rules out `..` and symbolic
  links even if the directory changes during the request.

## Testing

`make test` runs `tests/run_tests.sh`. It works in a temporary directory,
starts servers on random high ports of 127.0.0.1, drives `tftp_client`
through its standard input and compares files with `cmp`. It prints one
PASS/FAIL line per check, then `Passed: N  Failed: M`, and exits non-zero if
anything failed. Every wait polls with a time limit (`TEST_TIMEOUT`, default
30 seconds), so a hang fails a test instead of blocking.

| Area | Checks |
|---|---|
| Unit tests (`tests/unit_tests`): netascii, packet parsing of good and malformed packets, block reading/writing, path checks with symlinks | 54 |
| Client commands without a server: help, commands before `connect`, invalid command, `mode mail`, bad address/port, `quit`, end of input | 24 |
| Server options, client timeout with no server | 10 |
| `get` and `put` in octet and netascii mode of 0, 1, 511, 512, 513, 1024, 1048576 and 1000001 bytes of random binary data | 42 |
| Netascii: text, CR/LF/NUL data, pairs on block boundaries, bytes on the wire | 12 |
| Errors and options: missing file, existing file, directories, subdirectories, failed `get` keeps the local file, successful `get` replaces it, host name in `connect` | 16 |
| Path traversal: `..`, absolute paths, symlinks to files and directories, for `get` and `put` | 11 |
| Raw packets (`tests/tftp_probe`): error codes, new port per request, case-insensitive modes, options ignored, malformed requests, unknown transfer ID gets error 5, duplicate ACK does not resend DATA | 22 |
| 300 malformed and random datagrams sent with bash's `/dev/udp`; the server survives and keeps serving | 2 |
| 6 clients transferring at the same time | 12 |
| Ctrl-C during a download: transfer cancelled, no partial file, client back at the prompt | 5 |
| 20% simulated packet loss on both sides | 5 |
| Disk full (server under `ulimit -f`): error 3, partial file removed | 3 |
| 33 MB file (block numbers wrap past 65535), both directions | 2 |
| Server start and health: runs in the background, connect from the command line, still running at the end, no zombie processes, no sanitizer reports | 6 |

All 226 checks pass with the normal build and with `make asan`
(AddressSanitizer + LeakSanitizer + UndefinedBehaviorSanitizer); the build has
no warnings with `-Wall -Wextra`.

Test settings and hooks:

| Variable | Used by | Effect |
|---|---|---|
| `TEST_TIMEOUT=<s>` | `run_tests.sh` | Time limit of every wait (default 30) |
| `TEST_LARGE=0` | `run_tests.sh` | Skips the 33 MB test |
| `TFTP_DROP_PERCENT=<n>` | both programs | Drops n% (at most 90) of the packets they send, to test retransmission |
| `TFTP_TIMEOUT_MS=<ms>` | both programs | Retransmission timeout instead of 1 s (10 to 60000) |

For example, to watch retransmission by hand:
`TFTP_DROP_PERCENT=30 ./tftp_server -v &` and
`TFTP_DROP_PERCENT=30 ./tftp_client 127.0.0.1`.

## Changes to the skeleton

The skeleton's module layout (`client.c`, `server.c`, `tftp.c/h`, `file.c/h`,
`udp.c/h`) is kept; `netascii.c/h` was added. Problems fixed:

- `README` described a one-shot client (`./client <ip> <port> <file> <out>
  [get|put]`) and binaries called `server`/`client`. The PDF asks for an
  interactive client and shows `tftp_server`/`tftp_client`; this README
  replaces it.
- `makefile`: no compiler flags (no warnings), targets without dependencies
  (edits were never rebuilt), `clean` failed when a binary was missing. Now:
  `-std=c99 -Wall -Wextra -O2`, object files with header dependencies, `test`,
  `asan` and a `clean` that always works.
- `tftp.h`: error string 6 ("File already exists.") was missing, string 0
  differed from RFC 1350, and `ECODE_NONE` (8) is not a TFTP code. All
  prototypes had empty parameter lists `()`, which turns off argument
  checking in C; they now have full prototypes. Packet data uses
  `unsigned char`.
- File headers: the stray copyright sign after every file name (a UTF-8
  character that showed up as mojibake) was removed, the date and the
  Descriptions/Usage/Output fields were filled in, and the `.h` files got the
  same header block.

## Limitations and security notes

- **TFTP has no authentication or encryption.** Anyone who can reach the
  port can read every file in the served directory and add new files to it.
  Serve a directory that contains only files meant to be public, run the
  server as an ordinary user, and do not expose it to untrusted networks.
- Running with `sudo` for port 69 runs the whole server as root. The path
  rules still keep it inside the directory, but a high port as a normal user
  is safer.
- IPv4 only. On a machine with several network interfaces the server's
  answer might come from a different address than the one the client used;
  the client then ignores it (as it must) and times out. Using the address of
  the interface the client reaches avoids this.
- `connect` cannot check the address (UDP); an unreachable server is noticed
  only after the 11-second timeout.
- No TFTP options (RFC 2347: block size, transfer size, timeout) and no
  mail mode.
- The server does not ignore SIGHUP: if it was started with `&` from a
  terminal that is then closed, use `nohup ./tftp_server &` instead.
