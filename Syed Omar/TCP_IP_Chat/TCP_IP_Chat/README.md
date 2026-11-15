# TCP/IP Chat Room

A small chat system written in C for Linux. It has two programs:

- `server` runs in the background and serves many clients at the same
  time. It keeps registered users in a file (`users.db`), tells everyone
  when a user comes online or goes offline, and passes messages between
  users.
- `client` is the program each user runs. A user registers or logs in,
  sees who is online, sends private messages to one user ("single chat")
  or messages to everyone ("group chat", like a WhatsApp group), and logs
  out.

They talk over TCP on port 6333. The assignment is in
[tcp_ip_chat_srs.pdf](tcp_ip_chat_srs.pdf) (Emertxe "TCP/IP chat room"
requirements and design document).

## Building

Needs Linux (Ubuntu, including Ubuntu under WSL, is fine) with `gcc` and
`make`. On Ubuntu: `sudo apt install build-essential`. Only the C library,
POSIX threads and POSIX sockets are used.

```
make          # builds ./server and ./client
make test     # runs the automated tests (101 checks, a few seconds)
make clean    # removes the programs and object files (users.db is kept)
```

`make asan` and `make tsan` rebuild with AddressSanitizer +
UndefinedBehaviorSanitizer or with ThreadSanitizer; run `make test`
afterwards to test that build (`TEST_TIMEOUT=30 make test` gives the
slower sanitizer builds more time).

Under WSL, keep the project inside the Linux file system (for example
`~/TCP_IP_Chat`) rather than under `/mnt/c`: it builds faster and file
permissions work as on a normal Linux system.

## Running

Start the server once, in the background:

```
./server &                  # port 6333, no output except one start line
./server -v &               # same, plus an activity log on stderr
./server -v 7000 2> server.log &    # other port, log written to a file
```

Stop it with `kill %1` (or `kill <pid>`), or with Ctrl+C if it runs in
the foreground. It closes every connection and prints `Server stopped`.

Then open one terminal per user and start a client:

```
./client                    # connects to 127.0.0.1 port 6333
./client 192.168.1.20       # server on another machine
./client 192.168.1.20 7000  # ... on another port
```

The server stores registered users in `users.db` in the directory where
it was started, so start it from the same directory each time.

## Example session

Two users in two terminals. Typed input is shown after the prompts; the
password is not shown on the screen.

Terminal 1 (alice):

```
$ ./client
Socket created
Connected

Enter option : 1 ) Register
2 ) Login
3 ) Exit
1
Enter username : alice
Enter password :
Server reply :
message : Registered successfully
List of Online users
No other users are online
Select
1 ) Single Chat
2 ) Group Chat
3 ) Logout
4 ) Online Users

Incoming Message from server :
User bob is ONLINE
2
Group chat: messages go to every online user. Type /back to return to the menu.
Enter the message to send : hi bob, welcome!
Message sent successfully
Enter the message to send :
Incoming Message from bob (private) :
thanks alice
Enter the message to send : /back
Select
1 ) Single Chat
2 ) Group Chat
3 ) Logout
4 ) Online Users

Incoming Message from server :
User bob is OFFLINE
3
Server reply :
message : Logged out successfully
Enter option : 1 ) Register
2 ) Login
3 ) Exit
```

Terminal 2 (bob):

```
$ ./client
Socket created
Connected

Enter option : 1 ) Register
2 ) Login
3 ) Exit
1
Enter username : bob
Enter password :
Server reply :
message : Registered successfully
List of Online users
User alice is ONLINE
Select
1 ) Single Chat
2 ) Group Chat
3 ) Logout
4 ) Online Users

Incoming Message from alice :
hi bob, welcome!
1
Enter receiver username : alice
Single chat with alice. Type /back to return to the menu.
Enter the message to send : thanks alice
Message sent successfully
Enter the message to send : /back
Select
1 ) Single Chat
2 ) Group Chat
3 ) Logout
4 ) Online Users
3
Server reply :
message : Logged out successfully
Enter option : 1 ) Register
2 ) Login
3 ) Exit
3
```

The server's log for this session (`./server -v`):

```
Chat server listening on port 6333 (0 registered users)
[2026-10-10 21:11:43] 127.0.0.1:44110 connected
[2026-10-10 21:11:43] 127.0.0.1:44110 registered as 'alice'
[2026-10-10 21:11:43] 127.0.0.1:44120 connected
[2026-10-10 21:11:43] 127.0.0.1:44120 registered as 'bob'
[2026-10-10 21:11:43] 'alice' sent a group message to 1 user(s)
[2026-10-10 21:11:43] 'bob' sent a private message to 'alice'
[2026-10-10 21:11:43] 'bob' logged out
[2026-10-10 21:11:43] 127.0.0.1:44120 disconnected
```

## Using the client

| Menu | Choice | What happens |
|---|---|---|
| Start menu | `1` Register | Asks for a user name and password, creates the account and logs in |
| | `2` Login | Asks for a user name and password and logs in |
| | `3` Exit | Ends the client |
| Main menu (logged in) | `1` Single Chat | Asks for the receiver, then sends each line you type to that user only, until you type `/back` |
| | `2` Group Chat | Sends each line you type to every online user, until you type `/back` |
| | `3` Logout | Logs out and returns to the start menu |
| | `4` Online Users | Shows who is online |

Messages from other users and "User X is ONLINE / OFFLINE" notices appear
as soon as they arrive, whatever menu you are in. If a prompt was waiting,
it is shown again below the message (what you had already typed is kept).

Ctrl+D (end of input) or Ctrl+C logs you out cleanly and ends the client.
The client also works with input from a file or pipe
(`./client < commands.txt`); it then prints each input line, so the output
reads like a terminal session.

## How it behaves

- **Accounts.** User names have 1 to 31 characters: letters, digits, `_`,
  `.` and `-`; they are case-sensitive. Passwords have 1 to 31 printable
  characters without spaces. The client checks this before sending (with
  a friendly message) and the server checks it again.
- **Registering logs you in**, as in the spec's sample screens, which show
  the online list right after "Registration successful".
- **Server replies** use the exact texts of spec section 3.2:
  `Registered successfully`, `Logged in successfully`,
  `Duplicate user-name`, `user-name not found`, `Password not matching`.
  The sample screens use slightly different wording ("Client Registration
  successful", "Duplicate", "Password Incorrect"); section 3.2 was
  followed. The logout replies are `Logged out successfully` and
  `Couldn't log out` (the spec's "Couldn't logged out", with the grammar
  fixed).
- **Extra replies** for cases the spec does not cover:
  `User is already logged in` (one login per user at a time),
  `Too many failed attempts` (after 5 refused requests the server closes
  the connection), `Server is full` (more than 64 connections), and for
  chat: `Message sent successfully`, `Receiver not found`,
  `Receiver is not online`, `No other users are online`,
  `Cannot send a message to yourself`, `Message is empty`.
- **Spec 2.1.3** ("server should provide client with a user-name &
  password upon successful connection") is read as: on connection the
  server greets the client and the client then asks for a user name and
  password. The server does not invent credentials.
- **Spec 2.1.9** ("logout, upon which their entry will get deleted from
  database") is read as: logout deletes the user's *online* entry. The
  status becomes offline and the socket is forgotten, but the
  registration stays in `users.db`, so the user can log in again. Deleting
  the account at every logout would make the Login option useless.
- **Group chat** (spec 2.1.10): a message goes to every other logged-in
  user, whatever menu they are in. The sender gets "Message sent
  successfully" instead of a copy. Users who are connected but not logged
  in receive nothing.
- **Single chat** goes to one logged-in user and is shown as
  `Incoming Message from <name> (private) :`.
- **Join and leave notices** (spec 2.1.8) are sent when a user registers,
  logs in, logs out, or disconnects without logging out (for example when
  the client is killed).
- **Online list** (spec 2.1.7): sent right after login, and on request
  (menu option 4, client requirement 2.2.3). It never includes yourself.
- **Messages** are up to 511 characters. Empty lines are ignored. Tabs
  and other control characters are sent as spaces.
- **Sender names cannot be faked**: the server always fills in the
  sender from the logged-in session, whatever the client put there.
- **The server log** (`-v`) shows connections, logins, logouts and who
  sent a message to whom, never passwords or message text.
- **Stopping the server** closes every connection; clients print
  "Connection closed by the server" and exit.

### users.db

One line per registered user, `username password`, appended when a user
registers and read when the server starts:

```
alice secret1
bob secret2
```

It is created with permissions `rw-------` (0600). Lines that are not
valid (wrong format, invalid name or password, duplicate name) are skipped
with a warning at start-up. The status and socket columns of the spec's
database format (section 3.7) are kept in memory only, because they are
meaningless after a restart: every user starts offline.

## Files

| File | Contents |
|---|---|
| `server.c` | Server main: command line, listening socket, accept loop, shutdown |
| `session.c`, `session.h` | One thread per connection: login phase, then chat phase; connection limit |
| `chat_room.c`, `chat_room.h` | Shared server state (user database + one mutex): register, login, logout, notices, private and group messages |
| `user_db.c`, `user_db.h` | User records in memory, `users.db` load and append |
| `logger.c`, `logger.h` | Time-stamped activity log of the server (`-v`) |
| `client.c` | Client main: `poll()` loop and the menu / state machine |
| `client_ui.c`, `client_ui.h` | Line reader, password echo off, safe printing of received text |
| `protocol.c`, `protocol.h` | Packet formats, reply texts, validation, send / receive (used by both programs) |
| `tcp.c`, `tcp.h` | Socket helpers (listen, connect, timeouts, addresses, port parsing) |
| `signal_pipe.c`, `signal_pipe.h` | Turns SIGINT / SIGTERM into an event for the main loop (both programs) |
| `makefile` | Build, test and sanitizer targets |
| `tests/run_tests.sh` | Automated tests |
| `tcp_ip_chat_srs.pdf` | The assignment |

## Design

### Architecture

```
  client (1 thread)                       server
  +-------------------+                   +-------------------------------------+
  | poll():           |    TCP 6333       | main thread: poll() + accept()      |
  |   keyboard        | <---------------> |   one detached thread per client:   |
  |   server socket   |                   |     session.c (login / chat phase)  |
  |   signal pipe     |                   |           |                         |
  | state machine     |                   |           v                         |
  +-------------------+                   |   chat_room.c  [one mutex]          |
                                          |     user_db.c  <-> users.db         |
                                          +-------------------------------------+
```

The sequence of spec section 3.8 is followed: request register / login ->
"request confirmed" (the reply) -> the new user gets the online list and
the others get "<user> is ONLINE" -> chat messages are forwarded -> logout
-> confirmed.

### Wire format

Every packet is an 8-byte header followed by a body. All integers are
32-bit unsigned in network byte order (big-endian). Text fields have a
fixed size and are padded with NUL bytes.

```
header:  uint32 type | uint32 length of the body in bytes
```

| Type | Packet | Direction | Body | Spec |
|---|---|---|---|---|
| 1 | AUTH_REQUEST | client -> server | `username[32]`, `password[32]`, `uint32 option` (1 register, 2 login) = 68 bytes | 3.1 |
| 2 | SERVER_REPLY | server -> client | `uint32 code` (0 ok, 1 error, 2 error and the server closes the connection), `message[128]` = 132 bytes | 3.2, 3.6 |
| 3 | CHAT | both | `sender[32]`, `receiver[32]`, `message[512]`, `uint32 chat_option` (1 single, 2 group, 3 logout) = 580 bytes | 3.3 - 3.5 |
| 4 | USER_STATUS | server -> client | `username[32]`, `uint32 status` (1 online, 0 offline) = 36 bytes | 2.1.8 |
| 5 | LIST_REQUEST | client -> server | empty | 2.2.3 |
| 6 | USER_LIST | server -> client | `uint32 total`, then `total` names of 32 bytes | 2.1.7 |

The logout request is a CHAT packet with option 3 (spec 3.5 "Ref 3.3"),
and the logout reply is a SERVER_REPLY (spec 3.6).

Both programs check every packet before using it: the length must be
exactly right for the type (USER_LIST: a whole number of names, at most
128), so no more than about 4 KB is ever read for one packet; every text
field must contain its terminating NUL byte; codes and status values must
be in range. Anything else is a protocol error and the connection is
closed. Packets are built field by field into zeroed buffers, so no stray
memory is ever sent.

### Concurrency and locking (server)

- The **main thread** waits with `poll()` on the listening socket and the
  signal pipe, accepts connections and starts a **session thread** for
  each (detached, 256 KB stack, at most 64 at a time).
- A session thread first sends a welcome reply, then handles one packet
  at a time: register / login requests until the user is logged in, then
  chat, list and logout requests.
- **One mutex** in `ChatRoom` protects the user database **and every send
  to any client socket**. A group message, a join notice and a reply can
  be produced by different threads for the same socket at the same time;
  sending under the lock keeps each packet whole and in order.
- Every client socket has a **5-second send timeout**. A client that stops
  reading can hold the others up for at most 5 seconds; after that the
  send fails.
- When a send fails, that socket is **shut down** (not closed). Its own
  session thread then sees end-of-file, takes the lock, marks the user
  offline, tells the others, and only then closes the socket. Because a
  user is always marked offline under the lock *before* the socket is
  closed, a socket number found in the database is always still open, so
  a message can never go to a recycled descriptor.
- The session manager has its own small mutex for its slot table and is
  never locked together with the room mutex, so there is no lock-order
  problem.
- **SIGINT / SIGTERM** are written into a pipe by the signal handler
  (the "self-pipe" trick) and handled by the main loop: it stops
  accepting, shuts down every session socket, waits up to 5 seconds for
  the threads to finish, frees everything and exits with status 0.
- SIGPIPE is ignored and sends use `MSG_NOSIGNAL`, so a vanished client
  only produces an error code. All socket reads and writes loop over
  partial transfers and `EINTR`.

### The client

The client is single-threaded. One `poll()` loop watches the server
socket, the signal pipe and, only in states that expect typing, the
keyboard. A state machine (waiting for the welcome, start menu, user
name, password, waiting for the reply, main menu, receiver, single chat,
group chat, waiting for the logout reply, ...) decides what an input line
means. Keyboard input is read with `read()` into a line buffer, so it
works with `poll()` and with piped input holding many lines at once;
lines are used strictly in order, one state at a time. Lines longer than
1024 characters are rejected.

After sending a request the client waits for its reply before it reads
more input, so the server never has more than one request per client to
answer. Messages and notices are printed whenever they arrive.

The password is typed with terminal echo switched off; echo is restored
when the password has been entered, and on every exit path (including
Ctrl+C). Text received from the network is printed through a filter that
turns control characters (such as ESC) and invalid bytes into `?`, so
nobody can send escape sequences to another user's terminal. Printable
ASCII and valid UTF-8 are shown normally.

## Testing

`make test` runs `tests/run_tests.sh` (101 checks, a few seconds). It
starts the server on a random high port in a temporary directory. Each
client reads its keyboard input from its own FIFO and writes to its own
output file; the tests wait for the expected text with a timeout instead
of sleeping. It needs only bash and coreutils, and talks raw protocol
through bash's `/dev/tcp`. It covers:

- register, login, `Duplicate user-name`, `user-name not found`,
  `Password not matching`, `User is already logged in`; `users.db`
  contents and permissions;
- the online list after login and on request; ONLINE / OFFLINE notices
  for login, logout, end of input and a client killed with SIGKILL;
- group chat, private chat (not seen by others), every chat error;
  logout and login again;
- 5 failed attempts closing the connection; invalid input in the client
  (bad menu choices, bad names and passwords, over-long lines and
  messages);
- raw protocol: requests before login, header byte order, spoofed sender
  replaced, escape sequences neutralised, double login, double logout;
- garbage bytes, an oversized length (2 GB), a text field without NUL,
  an unknown packet type and a truncated packet: the server closes that
  connection and keeps serving;
- 15 clients sending group messages at the same time: every client gets
  each of the other 70 messages exactly once;
- the 64-connection limit (`Server is full`) and slots being freed again;
- SIGTERM shutdown (clients are told, exit status 0), restart on the same
  port, users loaded again from `users.db`;
- the server log contains no passwords or message text, and no
  sanitizer report appears in any log or output.

All 101 checks also pass with the AddressSanitizer + UBSan build
(`make asan`) and the ThreadSanitizer build (`make tsan`), with no
reports; the suite was also run 25 times in a row (normal build) and 8
times with each sanitizer build without a failure. If a ThreadSanitizer
build stops at once with "FATAL: ThreadSanitizer: unexpected memory
mapping" (seen on newer kernels), run the tests as
`setarch $(uname -m) -R make test`.

## Limitations and security notes

This is a classroom project, not a production chat service.

- **Passwords are stored and sent in plain text.** `users.db` holds them
  as typed (the file is readable only by its owner) and the connection is
  not encrypted (no TLS). Use it only on a trusted network and never with
  a real password. A real system would store salted password hashes and
  use TLS.
- Messages are not stored: there is no history and nothing is delivered
  to users who are offline.
- Accounts cannot be deleted from the client; stop the server and edit
  `users.db` to remove one. Only one server may use a `users.db` at a time.
- The server listens on IPv4 only (the client can resolve IPv4 and IPv6
  names).
- Because all sends happen under one lock, a client that stops reading
  can delay the whole server for up to the 5-second send timeout before
  it is disconnected.
- Idle connections are never timed out, and the limit of 5 failed
  attempts is per connection, so a determined attacker could hold the 64
  connection slots or keep guessing passwords on new connections.
- User lookup is a linear search, which is fine for the hundreds of users
  a classroom server will see.
- There is no line editing beyond what the terminal provides; an incoming
  message printed while you type splits your line on the screen (the text
  you typed is still sent).
