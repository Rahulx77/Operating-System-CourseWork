# Client-Server IPC Demo (C, POSIX sockets)

A multithreaded TCP client-server application demonstrating inter-process
communication with a custom framed protocol, authentication, concurrent
client handling, and defensive error/input handling.

## Files

| File        | Purpose                                                        |
|-------------|-----------------------------------------------------------------|
| `common.h`  | Wire protocol definitions shared by client and server           |
| `common.c`  | Reliable send/recv framing helpers + logging                    |
| `server.c`  | Multithreaded TCP server (auth, command handling, shutdown)     |
| `client.c`  | Interactive TCP client                                          |
| `Makefile`  | Builds both binaries with `-Wall -Wextra -Werror`                |

## Build

```bash
make            # builds ./server and ./client
make clean      # removes binaries/object files
```

## Run

Terminal 1 (server, optional port argument, default 5555):

```bash
./server 5555
```

Terminal 2+ (one or more clients — try running several at once):

```bash
./client 127.0.0.1 5555 ram
```

Demo accounts (seeded in-memory, see `server.c`):

| Username | Password       |
|----------|----------------|
| ram      | ram789         |
| pradip   | pradip456      |
| admin    | admin123       |

Once authenticated, the client is a small REPL. Supported commands:

```
TIME               -> server's current date/time
PING               -> replies PONG
UPPER <text>       -> uppercases <text>
REVERSE <text>     -> reverses <text>
<anything else>    -> echoed back as "ECHO: <text>"
quit / exit        -> sends a clean disconnect and closes
```

## How each requirement is met

**1. Server and client using sockets** — Both are built on the standard
POSIX `socket()/bind()/listen()/accept()` (server) and `socket()/connect()`
(client) BSD sockets API over TCP/IPv4.

**2. Simple protocol for data exchange** — Every message is a fixed
12-byte header (`magic`, `type`, `length`, all sent in network byte order)
followed by `length` bytes of payload (see `msg_header_t` in `common.h`).
`send_msg()`/`recv_msg()` in `common.c` frame and unframe messages
reliably, looping over `send()`/`recv()` to handle partial I/O. On top of
this framing, a tiny application-level command language (`TIME`, `PING`,
`UPPER`, `REVERSE`, echo) demonstrates request/response data exchange.

**3. Multiple concurrent client connections** — The server's `accept()`
loop hands each new connection to its own detached `pthread` running
`handle_client()`. A mutex-protected counter (`g_client_count`) caps
concurrent sessions at `MAX_CLIENTS` (50) so the server degrades
gracefully (rejects new connections with an explicit `MSG_ERROR`) instead
of exhausting resources. Verified manually by launching 5 clients in
parallel — all authenticated and were served independently.

**4. Basic security measures**
- **Authentication**: clients must send valid credentials
  (`MSG_AUTH_REQUEST`) before any data command is accepted; unauthenticated
  connections cannot reach `process_command()`.
- **No plaintext password storage**: the server only ever stores a hash
  (djb2) of each password, computed once at startup — never the raw
  password.
- **Login throttling**: a connection is dropped after `MAX_AUTH_ATTEMPTS`
  (3) failed attempts, mitigating brute-force guessing.
- **Hidden password entry**: the client disables terminal echo
  (`termios`) while reading the password, and wipes the password buffer
  from memory (`memset`) immediately after use.
- **Strict input/data validation**: `recv_msg()` rejects any message whose
  magic number doesn't match, and — critically — rejects any message
  whose *claimed* payload length exceeds the receiver's actual buffer
  size, before ever touching `recv()` for the body. This stops malformed
  or hostile length fields from causing a buffer overflow. Credential
  parsing additionally requires explicit NUL terminators inside the
  received bounds before treating any bytes as a C string, and rejects
  empty/oversized usernames or passwords.
- **Limitations (by design, for a teaching-scope demo)**: traffic is not
  encrypted, so credentials are protected from casual shoulder-surfing at
  the terminal but not from a network eavesdropper. For real deployment,
  wrap the socket in TLS (e.g. OpenSSL) and use a proper salted hash
  (bcrypt/Argon2) for stored credentials instead of djb2.

**5. Error handling and connection management**
- Every socket call (`socket`, `bind`, `listen`, `setsockopt`) checks its
  return value and fails loudly with `perror()`.
- `read_full()`/`write_full()` loop until the full amount is transferred,
  retry on `EINTR`, and distinguish a clean peer disconnect (`-2`) from a
  hard error (`-1`) so callers can log/handle each appropriately.
- `SIGPIPE` is ignored so a client vanishing mid-write can't kill the
  server process; `SIGINT`/`SIGTERM` trigger a graceful shutdown that
  closes the listening socket and lets the accept loop exit cleanly
  (verified: `kill -INT` on the server produces a clean "Server shutting
  down" log line and no orphaned process).
- Per-client sockets are always `close()`d and the shared client counter
  decremented in a single cleanup path, even on early error exits.

## Testing performed

- Full authenticated session exercising every command (`TIME`, `UPPER`,
  `REVERSE`, `PING`, `quit`).
- Repeated wrong-password attempts confirmed lockout after 3 tries and
  connection close.
- Raw protocol-level tests (bypassing the client) confirmed the server
  rejects an oversized claimed payload length and a bad magic number
  without crashing.
- 5 clients launched concurrently were all authenticated and served
  correctly, confirming thread-per-connection concurrency.
- `SIGINT` to the server confirmed graceful shutdown.

## Notes / possible extensions

- Swap the plaintext-over-TCP transport for TLS (OpenSSL `SSL_read`/
  `SSL_write`) to protect credentials and data in transit.
- Replace the in-memory user table with a real datastore and a proper
  password hashing library.
- Add per-IP rate limiting in `main()`'s accept loop for extra DoS
  resistance.
