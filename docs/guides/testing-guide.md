# FKVS Testing Guide

## Framework

FKVS uses `assert()` + `printf()` based tests with no external framework. Each test is a standalone executable.

## Test File Structure

```c
#include "../src/core/hashtable.h"  // includes stdlib.h, stdbool.h, stdint.h
#include <assert.h>
#include <stdio.h>
#include <string.h>

void test_descriptive_name()
{
    // Setup
    hashtable_t *table = create_hash_table(64);

    // Action
    bool ok = set_value(table,
        (const unsigned char *)"key", 3, "value", 5, VALUE_ENTRY_TYPE_RAW);
    assert(ok);

    // Verify
    value_entry_t *value;
    size_t value_len;
    assert(get_value(table, (unsigned char *)"key", 3, &value, &value_len));
    assert(value_len == 5);
    assert(memcmp(value->ptr, "value", 5) == 0);

    free(value->ptr);
    free(value);

    // Cleanup
    free_hash_table(table);
    printf("PASS: test_descriptive_name\n");
}

int main()
{
    test_descriptive_name();
    printf("\nAll tests passed!\n");
    return 0;
}
```

## Registering in CMakeLists.txt

```cmake
add_executable(test_name tests/test_name.c src/core/hashtable.c)
target_link_libraries(test_name)
add_test(NAME NameTest COMMAND test_name)
```

Include only the strictly necessary `.c` files. Tests do NOT link against the full server.

**Note:** Functions from `src/utils.h` (`is_integer`, `uint64_to_string`, etc.) are `static` inline, guarded by `#ifdef SERVER`. To use them in tests, you'd need to define `SERVER` or duplicate the logic locally.

## Test Patterns

### SET and GET

```c
void test_set_and_get()
{
    hashtable_t *table = create_hash_table(64);

    bool ok = set_value(table,
        (const unsigned char *)"k", 1, "v", 1, VALUE_ENTRY_TYPE_RAW);
    assert(ok);

    // get_value() returns a COPY — must be freed
    value_entry_t *value;
    size_t value_len;
    assert(get_value(table, (unsigned char *)"k", 1, &value, &value_len));
    assert(value_len == 1);
    assert(memcmp(value->ptr, "v", 1) == 0);

    free(value->ptr);
    free(value);
    free_hash_table(table);
    printf("PASS: test_set_and_get\n");
}
```

### Overwrite Existing Key

```c
void test_overwrite()
{
    hashtable_t *table = create_hash_table(64);

    set_value(table, (const unsigned char *)"k", 1, "v1", 2, VALUE_ENTRY_TYPE_RAW);
    set_value(table, (const unsigned char *)"k", 1, "v2", 2, VALUE_ENTRY_TYPE_RAW);

    value_entry_t *value;
    size_t value_len;
    assert(get_value(table, (unsigned char *)"k", 1, &value, &value_len));
    assert(memcmp(value->ptr, "v2", 2) == 0);

    free(value->ptr);
    free(value);
    free_hash_table(table);
    printf("PASS: test_overwrite\n");
}
```

### Integer Values

```c
void test_integer_value()
{
    hashtable_t *table = create_hash_table(64);

    set_value(table, (const unsigned char *)"counter", 7,
              "100", 3, VALUE_ENTRY_TYPE_INT);

    value_entry_t *value;
    size_t value_len;
    assert(get_value(table, (unsigned char *)"counter", 7, &value, &value_len));
    assert(value->encoding == VALUE_ENTRY_TYPE_INT);
    assert(memcmp(value->ptr, "100", 3) == 0);

    free(value->ptr);
    free(value);
    free_hash_table(table);
    printf("PASS: test_integer_value\n");
}
```

### Key Not Found

```c
void test_key_not_found()
{
    hashtable_t *table = create_hash_table(64);

    value_entry_t *value;
    size_t value_len;
    assert(!get_value(table, (unsigned char *)"missing", 7, &value, &value_len));

    free_hash_table(table);
    printf("PASS: test_key_not_found\n");
}
```

## Running Tests

```bash
cmake -S . -B build && cmake --build build
cd build && ctest --output-on-failure

# Single test
./build/test_name

# Verbose
cd build && ctest -V
```

## Conventions

1. Each test function starts with `test_`
2. Each test does setup, action, verification, cleanup
3. Always free memory: `free_hash_table()`, `free(value->ptr)`, `free(value)`
4. End with `printf("PASS: test_name\n");`
5. `main()` calls all tests, ends with `printf("\nAll tests passed!\n");`
6. Use small hashtable sizes (64) for tests
7. Keys and values don't need null terminators — use explicit lengths
