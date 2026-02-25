# Adding New Commands to FKVS

Step-by-step guide to implement a new command in FKVS. Each command touches **8 files** across the server and client.

## Prerequisites

Read [Wire Protocol](wire-protocol.md) and [Memory Management](memory-management.md) before starting.

## Step 1: Define the Command Byte

File: `src/commands/common/command_defs.h`

```c
#define CMD_NEW_CMD 0x09  // Next available after CMD_DECR_BY=0x08
```

## Step 2: Implement the Server Handler

File: `src/commands/server/server_command_handlers.h`

```c
void handle_newcmd_command(client_t *client, unsigned char *buffer,
                           size_t bytes_read);
```

File: `src/commands/server/server_command_handlers.c`

The handler accesses the static `table` variable (`hashtable_t *`), initialized in `init_command_handlers(hashtable_t *ht)`.

### Key-only command pattern (like GET, INCR, DECR):

```c
void handle_newcmd_command(client_t *client, unsigned char *buffer,
                           size_t bytes_read)
{
    // 1. Extract lengths from buffer
    const size_t command_length = buffer[0] << 8 | buffer[1];
    const size_t key_len = buffer[3] << 8 | buffer[4];
    const size_t offset = 2;

    // 2. Validate frame integrity BEFORE any data access
    if (bytes_read - offset != command_length) {
        fprintf(stderr, "Incomplete command data for NEWCMD.\n");
        send_error(client);
        return;
    }

    // 3. Command logic...
    // Use get_value() when a copy is needed (MUST free value->ptr and value)

    // 4. Respond
    send_reply(client, result, result_len);
}
```

### Key + value command pattern (like SET, INCRBY):

```c
void handle_newcmd_command(client_t *client, unsigned char *buffer,
                           size_t bytes_read)
{
    const size_t command_length = buffer[0] << 8 | buffer[1];
    const size_t key_len = buffer[3] << 8 | buffer[4];
    const size_t pos = 5 + key_len;
    const size_t offset = 2;

    // Validate value length field is present
    if (pos + 2 > bytes_read) {
        send_error(client);
        return;
    }

    const size_t value_length = buffer[pos] << 8 | buffer[pos + 1];
    if (pos + 2 + value_length > bytes_read) {
        send_error(client);
        return;
    }

    if (bytes_read - offset != command_length) {
        send_error(client);
        return;
    }

    // Value data starts at buffer[pos + 2], length is value_length
    // ... command logic ...

    send_reply(client, result, result_len);
}
```

## Step 3: Register the Handler

In `init_command_handlers()` within `server_command_handlers.c`:

```c
register_command(CMD_NEW_CMD, handle_newcmd_command);
```

## Step 4: Build the Client Frame Constructor

File: `src/commands/common/command_parser.h`

```c
unsigned char *construct_newcmd_command(const char *key, size_t *command_len);
```

File: `src/commands/common/command_parser.c`

**Key-only frame:**
```c
unsigned char *construct_newcmd_command(const char *key, size_t *command_len)
{
    size_t key_len = strlen(key);
    const size_t core_cmd_len = 3 + key_len;
    *command_len = 2 + core_cmd_len;

    unsigned char *binary_cmd = malloc(*command_len);
    if (!binary_cmd) return NULL;

    binary_cmd[0] = (core_cmd_len >> 8) & 0xFF;
    binary_cmd[1] = core_cmd_len & 0xFF;
    binary_cmd[2] = CMD_NEW_CMD;
    binary_cmd[3] = (key_len >> 8) & 0xFF;
    binary_cmd[4] = key_len & 0xFF;
    memcpy(&binary_cmd[5], key, key_len);

    return binary_cmd;
}
```

**Key + value frame:**
```c
unsigned char *construct_newcmd_command(const char *key, const char *value,
                                        size_t *command_len)
{
    size_t key_len = strlen(key);
    size_t value_len = strlen(value);
    const size_t core_cmd_len = 1 + 2 + key_len + 2 + value_len;
    *command_len = 2 + core_cmd_len;

    unsigned char *binary_cmd = malloc(*command_len);
    if (!binary_cmd) return NULL;

    binary_cmd[0] = (core_cmd_len >> 8) & 0xFF;
    binary_cmd[1] = core_cmd_len & 0xFF;
    binary_cmd[2] = CMD_NEW_CMD;
    binary_cmd[3] = (key_len >> 8) & 0xFF;
    binary_cmd[4] = key_len & 0xFF;
    memcpy(&binary_cmd[5], key, key_len);

    const size_t pos = 5 + key_len;
    binary_cmd[pos + 0] = (value_len >> 8) & 0xFF;
    binary_cmd[pos + 1] = value_len & 0xFF;
    memcpy(&binary_cmd[pos + 2], value, value_len);

    return binary_cmd;
}
```

## Step 5: Implement the Client Handler

File: `src/commands/client/client_command_handlers.h`

```c
void cmd_newcmd(command_args_t args, void (*response_cb)(client_t *client));
```

File: `src/commands/client/client_command_handlers.c`

**Note:** The header declares without `const`, but implementations use `const command_args_t`. Follow the existing pattern:

```c
void cmd_newcmd(const command_args_t args,
                void (*response_cb)(client_t *client))
{
    if (strncasecmp(args.cmd, "NEWCMD ", 7) != 0)
        return;

    char key[MAX_KEY_LEN];
    if (sscanf(args.cmd, "NEWCMD %511s", key) != 1) {
        printf("(error) ERR wrong number of arguments for 'newcmd' command\n");
        printf("(info) Usage: NEWCMD <key>\n");
        return;
    }

    size_t cmd_len;
    unsigned char *binary_cmd = construct_newcmd_command(key, &cmd_len);
    if (!binary_cmd) {
        fprintf(stderr, "Failed to construct NEWCMD command\n");
        return;
    }

    send(args.client->fd, binary_cmd, cmd_len, 0);
    free(binary_cmd);
    response_cb(args.client);
}
```

## Step 6: Register in Command Table and cmd_unknown

In `client_command_handlers.c`:

```c
// Add to command_table[] (BEFORE cmd_unknown and cmd_info)
const cmd_t command_table[] = {
    // ... existing commands ...
    {"cmd_newcmd", cmd_newcmd},
    {"cmd_unknown", cmd_unknown},  // fallback - must be second to last
    {"cmd_info", cmd_info}         // last
};

// Add to cmd_unknown() exclusion list
strncasecmp(args.cmd, "NEWCMD ", 7) &&
```

## Step 7: Understand Response Display

In `command_response_handler()` in `client_command_handlers.c`:

**Critical design detail:** `send_reply()` always places `STATUS_SUCCESS` (0x01) at `buffer[2]`, NOT the original CMD byte. The only exception is `send_pong()` which places `CMD_PING` (0x05) at `buffer[2]`.

Since `STATUS_SUCCESS = 0x01` and no CMD byte (other than `CMD_SET = 0x01`) matches this value, the response handler cannot distinguish which command generated a `send_reply()` response. All `send_reply()` responses fall through to the generic `else` branch, which prints the value as a quoted string or "OK" if empty.

**For most new commands, no changes to `command_response_handler()` are needed.** The default `else` branch handles string output automatically.

If you need custom formatting (e.g., `(integer)` prefix), you have two options:

1. **Use a dedicated send function** (like `send_pong()`) that places your CMD byte at `buffer[2]`, then add a dispatch branch.
2. **Handle formatting on the client side** before the response callback, based on which command was sent (the client knows what it requested).

## Memory Rules (MANDATORY)

1. `get_value()` returns a deep copy — ALWAYS `free(value->ptr)` and `free(value)`
2. `uint64_to_string()` and `int64_to_string()` return malloc'd strings — ALWAYS free
3. `set_value()` returns `bool` — false on failure
4. Free ALL allocated memory in ALL error paths

## File Checklist

- [ ] `src/commands/common/command_defs.h` — CMD_* constant
- [ ] `src/commands/server/server_command_handlers.h` — handler declaration
- [ ] `src/commands/server/server_command_handlers.c` — implementation + register in init_command_handlers
- [ ] `src/commands/common/command_parser.h` — constructor declaration
- [ ] `src/commands/common/command_parser.c` — frame builder implementation
- [ ] `src/commands/client/client_command_handlers.h` — cmd_* declaration
- [ ] `src/commands/client/client_command_handlers.c` — implementation + command_table + cmd_unknown + response_handler
- [ ] `tests/` — unit tests
- [ ] `CMakeLists.txt` — if new source files (add to ALL fkvs-server variants)

## Build and Test

```bash
cmake -S . -B build && cmake --build build
cd build && ctest --output-on-failure
```
