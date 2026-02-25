# FKVS Debugging Checklist

Systematic checklist for code review, bug analysis, and debugging FKVS.

## 1. Memory Management

- [ ] Every `get_value()` has `free(value->ptr)` and `free(value)` on ALL paths?
- [ ] Every `uint64_to_string()` / `int64_to_string()` has matching `free()`?
- [ ] Every `malloc()` is null-checked?
- [ ] Every `construct_*_command()` has `free()` after send?
- [ ] No `free()` on `find_entry()` returns?
- [ ] No `free()` on stack/client buffers?

## 2. Frame Validation

- [ ] Frame integrity validated BEFORE lazy expiration?
- [ ] Bounds checking for key_len and value_len before buffer access?
- [ ] `pos + 2 > bytes_read` checked before reading length fields?
- [ ] core_len consistency verified against bytes_read?

**Two validation patterns in the codebase:**

Pattern 1 (GET, INCR, DECR, TTL, PERSIST):
```c
if (bytes_read - 2 != command_length) { send_error; return; }
```

Pattern 2 (SET, SETEX): Extensive validation with core_len:
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
4. check_and_delete_if_expired() (lazy expiration)
5. Main operation (get_value, find_entry, etc.)
6. Respond (send_reply, send_error, send_ok)
7. Free memory
```

## 4. set_value() Return

- [ ] Return checked for NULL (`hash_table_entry_t*`)?
- [ ] Error reported to client on NULL?
- [ ] Previously allocated memory freed before return?
- [ ] Access to `entry->value->expire_at` safe after check?

## 5. Async / Event Loop

- [ ] io_uring: buffers are `static` or heap (NOT stack local)?
- [ ] kqueue: timer identified by BOTH `evs[i].filter == EVFILT_TIMER` AND ident?
- [ ] epoll: timerfd consumed via `read()` to re-arm?
- [ ] io_uring: timer re-armed with new `io_uring_prep_read` + `submit` after each CQE?

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
    send_error(fd);                      send_error(fd);
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

### Lazy expiration before frame validation
```c
// BUG: accesses buffer[5..key_len] before validating frame
check_and_delete_if_expired(&buffer[5], key_len);
if (bytes_read - 2 != command_length) { ... }  // should be BEFORE

// FIX: swap the order
```

### Stack buffer in io_uring
```c
uint64_t timer_buf;  // BUG: stack local, corrupted when CQE arrives
static uint64_t expire_timer_buf;  // FIX: file-scope static
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
