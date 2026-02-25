# FKVS Event Dispatchers (Deep Dive)

For a high-level overview, see [Event Dispatchers](../event-dispatchers.md). This guide covers implementation details for modifying the event loop.

## Architecture

FKVS is single-threaded with non-blocking I/O. Three event loop implementations, selected at **compile time by CMakeLists.txt** (which chooses which `.c` file to include):

| Platform | File | Guard | Mechanism |
|----------|------|-------|-----------|
| macOS | `src/io/event_dispatcher_kqueue.c` | `#ifdef __APPLE__` | kqueue + kevent |
| Linux | `src/io/event_dispatcher_epoll.c` | `#ifdef __linux__` | epoll + timerfd |
| Linux (io_uring) | `src/io/event_dispatcher_io_uring.c` | `#ifdef __linux__` | io_uring + liburing |

**Note:** Both epoll and io_uring use `#ifdef __linux__`. The selection is done by CMakeLists.txt which compiles only one file. The `IO_URING_ENABLED` define is a target compile definition added when liburing is available, but it is NOT used as a guard in source files.

All export: `int run_event_loop(void)` (declared in `event_dispatcher.h`).

## Common Flow

```
1. Make server.fd non-blocking
2. Create event loop instance
3. Register server.fd for read events
4. Infinite loop:
   a. Wait for events
   b. server.fd → accept() loop until EAGAIN
   c. Client fd → recv() loop → try_process_frames()
   d. EOF/error → close_and_drop_client()
```

## kqueue (macOS)

### Setup
```c
const int kq = kqueue();

// Register listening socket (edge-triggered with EV_CLEAR)
struct kevent ch;
EV_SET(&ch, server.fd, EVFILT_READ, EV_ADD | EV_ENABLE | EV_CLEAR, 0, 0, NULL);
```

### Register Client
```c
// udata stores client_t pointer — avoids list lookup
EV_SET(&ch, c->fd, EVFILT_READ, EV_ADD | EV_ENABLE | EV_CLEAR, 0, 0, c);
```

### Event Identification
```c
if (ident_fd == server.fd) {
    // Accept new connections
} else {
    client_t *c = (client_t *)evs[i].udata;
    // Handle client I/O
}
```

## epoll (Linux)

### Setup
```c
const int epfd = epoll_create1(0);

// Listening socket (edge-triggered with EPOLLET)
struct epoll_event ev = { .events = EPOLLIN | EPOLLET, .data.fd = server.fd };
epoll_ctl(epfd, EPOLL_CTL_ADD, server.fd, &ev);
```

### Register Client
```c
struct epoll_event ev;
memset(&ev, 0, sizeof(ev));
ev.events = EPOLLIN | EPOLLET;
ev.data.ptr = client;
epoll_ctl(epfd, EPOLL_CTL_ADD, client->fd, &ev);
```

## io_uring (Linux)

### Critical Requirements

1. **Buffers MUST be static or heap** — io_uring is async; stack locals are invalid when CQE arrives:
```c
// WRONG — stack corruption
uint64_t timer_buf;
io_uring_prep_read(sqe, tfd, &timer_buf, sizeof(timer_buf), 0);

// CORRECT — file-scope static
static uint64_t timer_buf;
io_uring_prep_read(sqe, tfd, &timer_buf, sizeof(timer_buf), 0);
```

2. **Batch submission** via `BATCH_SUBMIT_THRESHOLD` (32).

## Adding a Timer

### kqueue:
```c
#define NEW_TIMER_IDENT 0xBEEF
EV_SET(&timer_ev, NEW_TIMER_IDENT, EVFILT_TIMER, EV_ADD | EV_ENABLE, 0, 5000, NULL);

// IMPORTANT: check BOTH filter AND ident to avoid collision with fd numbers
if (evs[i].filter == EVFILT_TIMER && ident_fd == NEW_TIMER_IDENT) {
    do_periodic_work();
}
```

### epoll:
```c
int new_tfd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
struct itimerspec its = { .it_interval = {.tv_sec = 5}, .it_value = {.tv_sec = 5} };
timerfd_settime(new_tfd, 0, &its, NULL);
// Register with EPOLLIN (no EPOLLET for timers), check events[i].data.fd

// MUST read timerfd to re-arm
uint64_t expirations;
read(new_tfd, &expirations, sizeof(expirations));
```

### io_uring:
```c
// Use a sentinel client_t for timer identification
static client_t timer_sentinel = {.fd = -1};
timer_sentinel.fd = tfd;

io_uring_sqe_set_data(sqe, &timer_sentinel);

// In CQE handler: compare pointer
if (c == &timer_sentinel) {
    do_periodic_work();
    // Re-arm: submit new read SQE for the timerfd
}
```

## Shared Functions (in networking.c)

```c
set_nonblocking(int fd)
set_tcp_no_delay(int fd)
client_t *init_client(int client_fd, struct sockaddr_storage ss, enum socket_domain socket_domain)
try_process_frames(client_t *c)
```
