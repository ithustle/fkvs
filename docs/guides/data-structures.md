# FKVS Data Structures

## Hashtable

Location: `src/core/hashtable.h` and `src/core/hashtable.c`

### Structures

```c
typedef struct hashtable {
    hash_table_entry_t **buckets;  // Array of pointers to collision chains
    size_t size;                   // Number of buckets (TABLE_SIZE=8092 in server.h)
} hashtable_t;

typedef struct hashtable_entry_t {
    unsigned char *key;              // Key bytes (NOT null-terminated)
    size_t key_len;
    value_entry_t *value;
    struct hashtable_entry_t *next;  // Next in collision chain
} hash_table_entry_t;

typedef struct value_entry_t {
    void *ptr;               // Pointer to value bytes
    unsigned type : 4;       // Bit field (currently uninitialized in code)
    unsigned encoding : 4;   // VALUE_ENTRY_TYPE_INT(1) or VALUE_ENTRY_TYPE_RAW(2)
    size_t value_len;        // Value length in bytes
    int64_t expire_at;       // 0=no TTL, >0=CLOCK_MONOTONIC ms timestamp
} value_entry_t;
```

### Hash Function: DJB2

```c
hash = 5381;
for each byte: hash = hash * 33 + byte;
return hash % table_size;
```

### API

| Function | Returns | Allocates? | Notes |
|----------|---------|------------|-------|
| `create_hash_table(size)` | `hashtable_t*` | Yes | Caller must free with `free_hash_table` |
| `set_value(table, key, key_len, val, val_len, value_type)` | `hash_table_entry_t*` | Yes (internal) | NULL=error. Copies key and value. Preserves expire_at on updates |
| `get_value(table, key, key_len, &value, &value_len)` | `bool` | Yes (copy) | Caller MUST free `value->ptr` and `value`. Key param is `unsigned char *` (no const) |
| `find_entry(table, key, key_len)` | `hash_table_entry_t*` | No | Direct pointer. NULL if not found. Key param is `const unsigned char *` |
| `delete_entry(table, key, key_len)` | `bool` | No (frees) | true=deleted, false=not found |
| `free_hash_table(table)` | `void` | No (frees) | Frees everything: entries, keys, values, buckets |

### set_value Behavior

New key: allocates entry, key copy, value_entry_t, value copy. Sets expire_at = 0.

Existing key: saves previous expire_at, allocates new value_entry_t and copy, frees old, assigns new with preserved expire_at.

Inserts new entries at the HEAD of the bucket chain for O(1) insert.

## Linked List

Location: `src/core/list.h` and `src/core/list.c`

Used for client connection management.

```c
typedef struct list_node_t {
    struct list_node_t *prev, *next;
    void *val;
} list_node_t;

typedef struct list_t {
    list_node_t *head, *tail;
    int len;
    void (*free)(void *ptr);  // Optional custom free
} list_t;
```

| Function | Notes |
|----------|-------|
| `listCreate()` | Creates empty list |
| `listAddNodeToTail(list, value)` | Appends. Returns the list |
| `listDeleteNode(list, node)` | Removes node (does NOT free node->val) |
| `listFindNode(list, node, value)` | Linear search. `node` is start point (NULL = head) |

### Usage for Clients

```c
server.clients = listAddNodeToTail(server.clients, client);

list_node_t *node = listFindNode(server.clients, NULL, (void *)(intptr_t)fd);
client_t *c = (client_t *)node->val;

listDeleteNode(server.clients, node);
free(node->val);  // free(client_t) — separate from listDeleteNode
```

## Value Types

```c
#define VALUE_ENTRY_TYPE_INT 1  // Stored as ASCII digits ("42", "-7")
#define VALUE_ENTRY_TYPE_RAW 2  // Arbitrary binary data
```

Integers are stored as ASCII strings. INCR/DECR convert via `strtoull()`/`strtoll()`, operate, and store results via `uint64_to_string()`/`int64_to_string()` (static functions in `src/utils.h`, guarded by `#ifdef SERVER`).

## TTL / Expiration

- `expire_at = 0` — no expiration
- `expire_at > 0` — absolute CLOCK_MONOTONIC timestamp in milliseconds
- **Lazy deletion** — checked via `check_and_delete_if_expired()` on: GET, INCR, INCRBY, DECR, DECRBY, EXPIRE, PERSIST. TTL does its own inline expiration check (returns `"-2"` instead of `send_error()`). SET and SETEX skip lazy expiration (they overwrite unconditionally)
- **Active expiration** — `expire_keys_cycle()` every 1s, scans 20 keys per cycle (cursor-based)
- `set_value()` preserves expire_at automatically on updates
- SET explicitly clears expire_at to 0 after set_value (Redis convention)
- SETEX sets expire_at after set_value

## Server State

```c
// src/server.h
typedef struct { hashtable_t *store; } db_t;  // TABLE_SIZE=8092

typedef struct server_t {
    list_t *clients;              // Connected clients
    db_t *database;               // Contains hashtable_t *store (NOT hashtable directly)
    size_t expire_cursor;         // Active expiration scan position
    counter_t metrics;            // Commands executed, memory, disconnects
    int fd;                       // Listening socket
    int port;                     // TCP port (default 5995)
    // ... config fields: verbose, daemonize, show_logo, etc.
} server_t;
```

**Note:** `database` is `db_t *`, NOT `hashtable_t *`. Access the hashtable via `server.database->store`.

The global `server` variable is declared in `src/main.h` as `extern server_t server`.

## Client State

```c
// src/client.h
typedef struct client_t {
    unsigned char buffer[65536];            // 64KB receive buffer
    size_t buf_used;                        // Bytes currently in buffer
    ssize_t frame_need;                     // -1=unknown, >0=bytes needed for complete frame
    int fd;                                 // Socket fd
    char ip_str[INET6_ADDRSTRLEN];
    int port;
    enum socket_domain socket_domain;       // TCP_IP or UNIX
    bool benchmark_mode;
    bool interactive_mode;
    bool verbose;
    // ... other fields: command_type, config_file_path, uds_socket_path
} client_t;
```
