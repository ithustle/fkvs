# FKVS Wire Protocol

FKVS uses a frame-based binary protocol. All multi-byte integers are big-endian (network byte order).

## Request Frame Format

```
┌──────────────────────────────────────────────────────────────────┐
│ [2 bytes]      │ [1 byte]  │ [2 bytes]  │ [N bytes] │ ...       │
│ core_len       │ CMD byte  │ field1_len │ field1    │ more...   │
│ (big-endian)   │           │ (big-end)  │ (raw)     │           │
└──────────────────────────────────────────────────────────────────┘
Total frame size = 2 + core_len
```

`core_len` does NOT include its own 2 bytes. It covers CMD + all fields.

## Response Formats (Two Distinct Types)

### Type 1: Full frame via `send_reply()`

```
┌──────────────────────────────────────────────────────────────────┐
│ [2 bytes]  │ [1 byte]         │ [2 bytes]   │ [N bytes]         │
│ core_len   │ STATUS_SUCCESS   │ value_len   │ value bytes       │
│ (big-end)  │ (0x01)           │ (big-end)   │ (raw)             │
└──────────────────────────────────────────────────────────────────┘
core_len = value_len + 3  (status + value_len field)
```

### Type 2: Single byte via `send_ok()` / `send_error()`

```
send_ok():    [0x01]   (1 byte, no frame wrapper)
send_error(): [0x00]   (1 byte, no frame wrapper)
```

### Type 3: PING via `send_pong()` (exception)

```
┌──────────────────────────────────────────────────────────────────┐
│ [2 bytes]  │ [1 byte]     │ [2 bytes]   │ [N bytes]             │
│ core_len   │ CMD_PING     │ value_len   │ value bytes           │
│ (big-end)  │ (0x05)       │ (big-end)   │ (raw)                 │
└──────────────────────────────────────────────────────────────────┘
```

`send_pong()` places `CMD_PING` (0x05) in the status byte position, NOT `STATUS_SUCCESS`. This is the only exception.

## Command Layouts

### SET (0x01) — key + value
```
Request:  [core_len:2][0x01][key_len:2][key:N][value_len:2][value:N]
Response: send_reply → [core_len:2][0x01][value_len:2][value:N]
Error:    send_error → [0x00]
core_len = 1 + 2 + key_len + 2 + value_len
```

### GET (0x02) — key only
```
Request:  [core_len:2][0x02][key_len:2][key:N]
Response: send_reply → [core_len:2][0x01][value_len:2][value:N]
Error:    send_error → [0x00] (key not found or expired)
core_len = 3 + key_len
```

### INCR (0x03) — key only
```
Request:  [core_len:2][0x03][key_len:2][key:N]
Response: send_reply → new value
core_len = 3 + key_len
```

### INCRBY (0x04) — key + increment
```
Request:  [core_len:2][0x04][key_len:2][key:N][incr_len:2][increment:N]
Response: send_reply → new value
core_len = 1 + 2 + key_len + 2 + incr_len
```

### PING (0x05) — optional value
```
Request:  [core_len:2][0x05][value_len:2][value:N]
Response: send_pong → [core_len:2][0x05][value_len:2][value:N]
core_len = 3 + value_len
```

### DECR (0x06) — key only
```
Request:  [core_len:2][0x06][key_len:2][key:N]
Response: send_reply → new value
core_len = 3 + key_len
```

### INFO (0x07) — no arguments
```
Request:  [core_len:2][0x07]
Response: send_reply → formatted text
core_len = 1
```

### DECRBY (0x08) — key + decrement
```
Request:  [core_len:2][0x08][key_len:2][key:N][decr_len:2][decrement:N]
Response: send_reply → new value
core_len = 1 + 2 + key_len + 2 + decr_len
```

### EXPIRE (0x09) — key + seconds
```
Request:  [core_len:2][0x09][key_len:2][key:N][sec_len:2][seconds:N]
Response: send_reply → "1" (success) or "0" (key not found)
core_len = 1 + 2 + key_len + 2 + sec_len
```

### TTL (0x0A) — key only
```
Request:  [core_len:2][0x0A][key_len:2][key:N]
Response: send_reply → "-2" (not found), "-1" (no TTL), "N" (seconds remaining)
core_len = 3 + key_len
```

### SETEX (0x0B) — key + seconds + value
```
Request:  [core_len:2][0x0B][key_len:2][key:N][sec_len:2][sec:N][val_len:2][val:N]
Response: send_reply → echoed value
core_len = 1 + 2 + key_len + 2 + sec_len + 2 + value_len
```

### PERSIST (0x0C) — key only
```
Request:  [core_len:2][0x0C][key_len:2][key:N]
Response: send_reply → "1" (TTL removed) or "0" (key not found / no TTL)
core_len = 3 + key_len
```

## Layout Categories

| Type | Commands | core_len |
|------|----------|----------|
| Key only | GET, INCR, DECR, TTL, PERSIST | `3 + key_len` |
| Key + value | SET, INCRBY, DECRBY, EXPIRE | `1 + 2 + key_len + 2 + val_len` |
| Key + arg + value | SETEX | `1 + 2 + key_len + 2 + arg_len + 2 + val_len` |
| No args | INFO | `1` |
| Optional value | PING | `3 + value_len` |

## Big-Endian Encoding

Write:
```c
buffer[pos] = (len >> 8) & 0xFF;
buffer[pos + 1] = len & 0xFF;
```

Read:
```c
size_t len = buffer[pos] << 8 | buffer[pos + 1];
```

## Frame Parsing (Server Side)

`try_process_frames()` in `src/networking/networking.c`:
1. Need at least 2 bytes to read core_len
2. Calculate `frame_need = 2 + core_len`
3. Wait until buffer has enough bytes
4. `dispatch_command(client_fd, buffer, frame_len)` — extracts CMD at `buffer[2]`
5. `memmove()` to shift remaining bytes (pipelining support)
6. Reset `frame_need = -1` for next frame

## Response Functions (in command_registry.c)

```c
send_ok(client_fd)                       // Single byte [0x01]
send_error(client_fd)                    // Single byte [0x00]
send_reply(client_fd, data, len)         // Full frame: [core_len][STATUS_SUCCESS][value_len][data]
send_pong(client_fd, buffer)             // Full frame: [core_len][CMD_PING][value_len][value]
```

**Client dispatch limitation:** Since `send_reply()` always puts `STATUS_SUCCESS` (0x01) at `buffer[2]`, the client cannot distinguish which command generated the response by inspecting that byte. Only PING responses carry the `CMD_PING` byte. All other `send_reply()` responses fall through to the generic handler in `command_response_handler()`.
