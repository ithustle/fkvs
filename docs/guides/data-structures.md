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
    unsigned expirable : 1;  // Expiration flag
    size_t value_len;        // Value length in bytes
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
| `set_value(table, key, key_len, val, val_len, value_type)` | `bool` | Yes (internal) | false=error. Copies key and value internally |
| `get_value(table, key, key_len, &value, &value_len)` | `bool` | Yes (copy) | Caller MUST free `value->ptr` and `value`. Key param is `unsigned char *` (no const) |
| `free_hash_table(table)` | `void` | No (frees) | Frees everything: entries, keys, values, buckets |
| `hash_function(key, key_len, table_size)` | `size_t` | No | DJB2 hash, returns index into bucket array |

### set_value Behavior

New key: allocates entry, key copy, value_entry_t, value copy.

Existing key: allocates new value_entry_t and copy, frees old, assigns new.

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

## Server State

```c
// src/server.h
typedef struct {
#define TABLE_SIZE 8092
    hashtable_t *store;
    hashtable_t *expires;
} db_t;

typedef struct server_t {
    list_t *clients;              // Connected clients
    char *config_file_path;
    db_t *database;               // Contains hashtable_t *store and *expires
    char *uds_socket_path;        // Unix domain socket path
    counter_t metrics;            // Commands executed, memory, disconnects
    int port;                     // TCP port (default 5995)
    int fd;                       // Listening socket
    int event_loop_fd;
    int event_loop_max_events;
    int32_t num_disconnected_clients;
    pid_t pid;
    u_int32_t num_clients;
    enum socket_domain socket_domain;
    event_loop_dispatcher_kind event_dispatcher_kind;
    bool use_io_uring;
    bool is_logging_enabled;
    bool verbose;
    bool show_logo;
    bool daemonize;
} server_t __attribute__((aligned(128)));
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
