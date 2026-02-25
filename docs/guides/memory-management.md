# FKVS Memory Management

FKVS is a single-threaded C server with no garbage collector. Every allocation must be freed on every execution path, including error returns.

## Ownership Rules

### Hashtable owns entries

The hashtable stores entries internally. You cannot access entries directly — use `get_value()` which returns a deep copy.

### get_value() returns a DEEP COPY

```c
// Key param: unsigned char * (no const)
value_entry_t *value;
size_t value_len;
if (get_value(table, key, key_len, &value, &value_len)) {
    // value is malloc'd, value->ptr is malloc'd
    // MANDATORY: free both when done

    // ... use value->ptr and value_len ...

    free(value->ptr);  // free data
    free(value);       // free struct
}
```

**Common mistake — leaking on error paths:**
```c
// WRONG — MEMORY LEAK
if (get_value(table, key, key_len, &value, &value_len)) {
    if (value->encoding != VALUE_ENTRY_TYPE_INT) {
        send_error(client);
        return;  // LEAK: value and value->ptr not freed
    }
}

// CORRECT
if (get_value(table, key, key_len, &value, &value_len)) {
    if (value->encoding != VALUE_ENTRY_TYPE_INT) {
        send_error(client);
        free(value->ptr);
        free(value);
        return;
    }
    // ... use ...
    free(value->ptr);
    free(value);
}
```

### set_value() returns bool

```c
// Signature: bool set_value(const hashtable_t *table,
//     const unsigned char *key, size_t key_len,
//     const void *value, size_t value_len, int value_type)
if (!set_value(table, key, key_len, val, val_len, encoding)) {
    // Returns false on allocation failure
    send_error(client);
    return;
}
// Hashtable makes internal copies of key and value
```

### uint64_to_string() and int64_to_string() return malloc'd

```c
char *str = uint64_to_string(42);    // malloc'd, max 22 bytes
char *str2 = int64_to_string(-7);    // malloc'd, max 32 bytes
// MUST be freed
free(str);
free(str2);
```

**Note:** These functions are `static` in `src/utils.h`, guarded by `#ifdef SERVER`. Only available in server code.

### construct_*_command() returns malloc'd

```c
unsigned char *binary_cmd = construct_set_command(key, value, &cmd_len);
send(fd, binary_cmd, cmd_len, 0);
free(binary_cmd);  // MANDATORY
```

## Correct Pattern for Command Handlers

```c
void handle_example_command(client_t *client, unsigned char *buffer, size_t bytes_read)
{
    // 1. Validations that don't allocate — can return freely
    if (bytes_read - 2 != command_length) {
        send_error(client);
        return;  // OK — nothing allocated
    }

    // 2. First allocation (get_value returns a copy)
    value_entry_t *value;
    size_t value_len;
    if (!get_value(table, key, key_len, &value, &value_len)) {
        send_error(client);
        return;  // OK — get_value failed, nothing allocated
    }

    // 3. From here, EVERY return must free value
    if (value->encoding != VALUE_ENTRY_TYPE_INT) {
        send_error(client);
        free(value->ptr);
        free(value);
        return;
    }

    // 4. Extract data and free copy ASAP
    const uint64_t current = strtoull(value->ptr, NULL, 10);
    free(value->ptr);
    free(value);

    // 5. Second allocation (result string)
    char *result = uint64_to_string(current + 1);

    if (!set_value(table, key, key_len, result, strlen(result), VALUE_ENTRY_TYPE_INT)) {
        send_error(client);
        free(result);
        return;
    }

    send_reply(client, (const unsigned char *)result, strlen(result));
    free(result);
}
```

## Golden Rules

1. **Free copies ASAP after extracting data** — reduces error window
2. **Every malloc() must have exactly one free() on every execution path**
3. **Client buffer (client->buffer, 65536 bytes)** — NOT malloc'd, do NOT free
4. **Data passed to set_value()** — hashtable makes an internal copy, you can free the original
5. **get_value() key is `unsigned char *` (no const)**

## Common Pitfalls

| Mistake | Consequence | Fix |
|---------|-------------|-----|
| `free(buffer)` on stack/client buffer | Crash/corruption | Don't free non-malloc'd buffers |
| Forgetting `free(value->ptr)` from get_value | Memory leak | Always free ptr before value |
| Forgetting `free(str)` from int64/uint64_to_string | Memory leak | Always free converted strings |
| Not checking `set_value() == false` | Unhandled error | Always check return |
| Stack local as io_uring buffer | Stack corruption | Use `static` or heap for async buffers |
