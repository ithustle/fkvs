# FKVS Copilot Instructions

## What is this project?

FKVS is a high-performance key-value store in C (C23 standard). Single-threaded, non-blocking I/O, default port 5995. Three executables: `fkvs-server`, `fkvs-cli`, `fkvs-benchmark`.

## Build and test

```bash
cmake -S . -B build && cmake --build build
cd build && ctest --output-on-failure
```

## Key architecture

- 3 event dispatchers: kqueue (macOS), epoll (Linux), io_uring (Linux with liburing). Selected at compile time.
- Custom binary protocol. Command bytes defined as CMD_* constants in `src/commands/common/command_defs.h`.
- Files compiled with `SERVER` or `CLI` define. Shared headers have `#ifdef SERVER`/`#ifdef CLI` blocks.
- Split handler pattern: server handlers in `src/commands/server/`, client handlers in `src/commands/client/`, shared definitions in `src/commands/common/`.

## Memory rules

- `get_value()` returns a deep copy. Free both `value->ptr` and `value`. Key param: `unsigned char *`.
- `set_value()` returns `bool`, false on error. Hashtable copies key/value internally.
- `uint64_to_string()` / `int64_to_string()` return malloc'd strings. Must free.
- `construct_*_command()` returns malloc'd buffers. Must free after send.
- Free all allocations on every error path.

## Adding a new command (8 files to touch)

1. `src/commands/common/command_defs.h` -- CMD_* constant
2. `src/commands/server/server_command_handlers.h` -- handler declaration
3. `src/commands/server/server_command_handlers.c` -- handler impl + register in `init_command_handlers()`
4. `src/commands/common/command_parser.h` -- frame constructor declaration
5. `src/commands/common/command_parser.c` -- frame builder impl
6. `src/commands/client/client_command_handlers.h` -- cmd_* declaration
7. `src/commands/client/client_command_handlers.c` -- cmd_* impl + `command_table[]` + `cmd_unknown()` exclusion + response handler
8. `tests/` -- unit tests (update `CMakeLists.txt` if adding new source files)

**Response protocol note:** `send_reply()` places `STATUS_SUCCESS` (0x01) at `buffer[2]`, NOT the CMD byte. Only `send_pong()` puts `CMD_PING`. Client response dispatch by CMD byte at buffer[2] only works for PING — all other responses fall through to the generic else branch.

## Code style

LLVM-based formatting, 4-space indent, Linux brace style, no tabs. Config in `.clang-format`.

## Detailed guides

See `docs/guides/` for full documentation: adding-commands, wire-protocol, memory-management, testing-guide, data-structures, debugging-checklist, event-dispatchers-deep, build-and-config.
