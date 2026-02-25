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
    hash_table_entry_t *entry = set_value(table,
        (const unsigned char *)"key", 3, "value", 5, VALUE_ENTRY_TYPE_RAW);
    assert(entry != NULL);

    // Verify
    hash_table_entry_t *found = find_entry(table, (const unsigned char *)"key", 3);
    assert(found != NULL);
    assert(found->value->value_len == 5);
    assert(memcmp(found->value->ptr, "value", 5) == 0);

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

    hash_table_entry_t *entry = set_value(table,
        (const unsigned char *)"k", 1, "v", 1, VALUE_ENTRY_TYPE_RAW);
    assert(entry != NULL);

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

### TTL / Expiration

```c
#include <time.h>

// Local version (server's is static in utils.h, not exported)
static int64_t now_monotonic_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000LL + ts.tv_nsec / 1000000LL;
}

void test_ttl()
{
    hashtable_t *table = create_hash_table(64);
    set_value(table, (const unsigned char *)"temp", 4, "data", 4,
              VALUE_ENTRY_TYPE_RAW);

    hash_table_entry_t *entry = find_entry(table, (const unsigned char *)"temp", 4);
    assert(entry != NULL);
    assert(entry->value->expire_at == 0);  // default: no TTL

    int64_t now = now_monotonic_ms();
    entry->value->expire_at = now + 2000;
    assert(entry->value->expire_at > now);

    free_hash_table(table);
    printf("PASS: test_ttl\n");
}
```

### Delete

```c
void test_delete()
{
    hashtable_t *table = create_hash_table(64);
    set_value(table, (const unsigned char *)"k1", 2, "v1", 2, VALUE_ENTRY_TYPE_RAW);

    assert(find_entry(table, (const unsigned char *)"k1", 2) != NULL);
    assert(delete_entry(table, (const unsigned char *)"k1", 2) == true);
    assert(find_entry(table, (const unsigned char *)"k1", 2) == NULL);
    assert(delete_entry(table, (const unsigned char *)"k1", 2) == false);

    free_hash_table(table);
    printf("PASS: test_delete\n");
}
```

### TTL Preservation on Update

```c
void test_update_preserves_ttl()
{
    hashtable_t *table = create_hash_table(64);
    set_value(table, (const unsigned char *)"k", 1, "100", 3, VALUE_ENTRY_TYPE_INT);

    hash_table_entry_t *entry = find_entry(table, (const unsigned char *)"k", 1);
    entry->value->expire_at = 999999;

    // set_value automatically preserves expire_at on updates
    set_value(table, (const unsigned char *)"k", 1, "200", 3, VALUE_ENTRY_TYPE_INT);

    entry = find_entry(table, (const unsigned char *)"k", 1);
    assert(entry->value->expire_at == 999999);
    assert(memcmp(entry->value->ptr, "200", 3) == 0);

    free_hash_table(table);
    printf("PASS: test_update_preserves_ttl\n");
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
