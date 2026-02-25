# FKVS Debugging Checklist

Systematic checklist for code review, bug analysis, and debugging FKVS.

## 1. Memory Management

- [ ] Every `get_value()` has `free(value->ptr)` and `free(value)` on ALL paths?
- [ ] Every `uint64_to_string()` / `int64_to_string()` has matching `free()`?
- [ ] Every `malloc()` is null-checked?
- [ ] Every `construct_*_command()` has `free()` after send?
- [ ] No `free()` on stack/client buffers?

## 2. Frame Validation

- [ ] Bounds checking for key_len and value_len before buffer access?
- [ ] `pos + 2 > bytes_read` checked before reading length fields?
- [ ] core_len consistency verified against bytes_read?

**Two validation patterns in the codebase:**

Pattern 1 (GET, INCR, DECR):
```c
if (bytes_read - 2 != command_length) { send_error; return; }
```

Pattern 2 (SET): Extensive validation with core_len:
```c
const uint16_t core_len = ((uint16_t)buffer[0] << 8) | buffer[1];
if (bytes_read < (size_t)core_len + 2) { send_error; return; }
// + individual field validation...
```

## 3. Operation Order in Handlers

```
1. Extract lengths from buffer
2. Validate frame integrity (sizes vs bytes_read)
3. Validate data format (is_integer, bounds, etc.)
4. Main operation (get_value, set_value, etc.)
5. Respond (send_reply, send_error, send_ok)
6. Free memory
```

## 4. set_value() Return

- [ ] Return checked for false (`bool`)?
- [ ] Error reported to client on false?
- [ ] Previously allocated memory freed before return?

## 5. Async / Event Loop

- [ ] io_uring: buffers are `static` or heap (NOT stack local)?
- [ ] kqueue: events identified by filter type and ident?
- [ ] epoll: events dispatched based on fd comparison?

## 6. Strings and Buffers

- [ ] Copied data has null terminator when used with printf/strlen?
- [ ] `memcpy` uses correct length (value_len, not buffer size)?
- [ ] No buffer overflow in snprintf/memcpy?
- [ ] Integers converted via strtoull/strtoll validated with is_integer first?

## Known Bug Patterns

### Memory leak on error path
```c
// BUG                              // FIX
if (bad_condition) {                 if (bad_condition) {
    send_error(client);                  send_error(client);
    return; // LEAK                      free(value->ptr);
}                                        free(value);
                                         return;
                                     }
```

### free() on non-malloc'd buffer
```c
free(buffer);  // BUG: buffer is client->buffer (stack)
// FIX: remove the free
```

### Stack buffer in io_uring
```c
uint64_t timer_buf;  // BUG: stack local, corrupted when CQE arrives
static uint64_t timer_buf;  // FIX: file-scope static
```

### Uninitialized pointer
```c
value_entry_t *old_value;     // BUG: uninitialized
free(old_value);              // undefined behavior
value_entry_t *old_value = NULL;  // FIX
```

## Analysis Tools

```bash
# Build with warnings
cmake -S . -B build -DCMAKE_C_FLAGS="-Wall -Wextra -Wuninitialized" && cmake --build build

# Valgrind (Linux)
valgrind --leak-check=full ./build/fkvs-server

# Address Sanitizer
cmake -S . -B build -DCMAKE_C_FLAGS="-fsanitize=address -g" && cmake --build build

# Run tests
cd build && ctest --output-on-failure
```

## Severity Classification

| Level | Criteria | Examples |
|-------|----------|----------|
| CRITICAL | Crash, memory corruption, security | stack corruption, use-after-free, buffer overflow |
| HIGH | Wrong data, hot path memory leak | leak in GET/SET, wrong result, unchecked set_value |
| MEDIUM | Edge case leak, unexpected behavior | leak in rare error path, printf without null term |
| LOW | Code quality, performance | dead code, unnecessary copy, over-allocation |
