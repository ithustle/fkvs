# FKVS Build System & Configuration

## Quick Build

```bash
# Full setup (first time — clones submodules + builds)
make -f Makefile.fkvs setup-and-build

# Rebuild after changes
cmake --build build

# From scratch
cmake -S . -B build && cmake --build build

# Run tests
cd build && ctest --output-on-failure

# With sanitizers
cmake -S . -B build -DCMAKE_C_FLAGS="-g -fsanitize=address" && cmake --build build
```

## Executables

| Executable | Description | Define | CMake Scope |
|-----------|-------------|--------|-------------|
| `fkvs-server` | KV store server | `SERVER` | Inside `if(APPLE)` / `elseif(LINUX)` blocks |
| `fkvs-cli` | Interactive REPL client | `CLI` | **Outside** conditional blocks |
| `fkvs-benchmark` | Multi-threaded benchmark | `CLI` | Inside platform blocks |
| `test_*` | Unit tests | — | Outside conditional blocks |

## Conditional Compilation

Shared headers use `#ifdef SERVER` / `#ifdef CLI`:
```c
#ifdef SERVER
// Server-only code (utils.h: is_integer, uint64_to_string, format_uptime, etc.)
#endif
```

**Important:** Many utility functions (`is_integer`, `uint64_to_string`, `int64_to_string`, `format_uptime`) are `static` inline defined directly in `src/utils.h`, guarded by `#ifdef SERVER`. They are NOT in a separate `.c` file.

## Adding Source Files

**To the server:** Add to ALL `add_executable(fkvs-server ...)` variants in CMakeLists.txt:
- `if(APPLE)` block (1 variant)
- `elseif(LINUX)` with io_uring (1 variant)
- `elseif(LINUX)` without io_uring (1 variant)

**To the CLI:** Add to `add_executable(fkvs-cli ...)` (outside conditional blocks).

**To benchmark:** Add to `add_executable(fkvs-benchmark ...)` inside platform blocks.

**New test:**
```cmake
add_executable(test_name tests/test_name.c src/dependency.c)
target_link_libraries(test_name)
add_test(NAME NameTest COMMAND test_name)
```

## Requirements

- CMake >= **3.31.2**
- C standard: **C23**
- pthreads (via `find_package(Threads)`)
- liburing (Linux, optional — `find_library(LIBURING liburing)`)
- linenoise (submodule in `deps/linenoise/`)

## Server Configuration

File: `server.conf` (project root, or custom path via `-c`)

```conf
port 5995                                    # TCP listen port (default 5995)
unixsocket /var/run/fkvs/fkvs.sock           # Unix Domain Socket (alternative to TCP)
event-loop-max-events 100000                 # MAX_EVENTS per poll
use-io-uring true                            # Linux: use io_uring if available
show-logo true                               # ASCII art on startup
verbose false                                # Debug logging
daemonize false                              # Run as daemon
log-enabled true                             # File logging (with daemonize)
```

Parsed by `src/config.c` using `sscanf(line, "%s %s", key, value)`.

## Client Configuration

File: `client.conf`

```conf
bind 127.0.0.1                               # Server IP
port 5995                                    # Server port
verbose false                                # Debug output
unixsocket /var/run/fkvs/fkvs.sock           # UDS (alternative to TCP)
```

## Project Structure

```
fkvs/
├── CMakeLists.txt                   # 3 server variants + cli + benchmark + tests
├── Makefile.fkvs                    # Wrapper: setup-and-build, build, tests, codesign-server
├── server.conf / client.conf
├── src/
│   ├── main.h                       # extern server_t server
│   ├── server.c / server.h          # Entry point, server_t, db_t (contains hashtable_t *store)
│   ├── client.c / client.h          # client_t struct, init_client()
│   ├── utils.h                      # Static inline functions (#ifdef SERVER)
│   ├── config.c / config.h          # Config parsing
│   ├── response_defs.h              # STATUS_SUCCESS=0x01, STATUS_FAILURE=0x00
│   ├── commands/
│   │   ├── common/
│   │   │   ├── command_defs.h       # CMD_SET=0x01 ... CMD_PERSIST=0x0C
│   │   │   ├── command_parser.c/h   # construct_*_command() frame builders
│   │   │   └── command_registry.c/h # dispatch, send_reply, send_error, send_ok, send_pong
│   │   ├── server/                  # handle_*_command(), init_command_handlers, expire_keys_cycle
│   │   └── client/                  # cmd_*(), command_table, command_response_handler
│   ├── core/
│   │   ├── hashtable.c/h            # DJB2, chaining, set/get/find/delete
│   │   └── list.c/h                 # Doubly-linked list
│   ├── io/
│   │   ├── event_dispatcher.h       # Interface, MAX_EVENTS, enum kinds
│   │   └── event_dispatcher_*.c     # kqueue / epoll / io_uring
│   └── networking/
│       ├── networking.c/h           # Socket setup, try_process_frames
│       └── modes.h                  # enum socket_domain { TCP_IP, UNIX }
├── tests/                           # test_counter.c, test_string_utils.c, test_ttl.c
└── deps/linenoise/                  # CLI line editing (git submodule)
```
