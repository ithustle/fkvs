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
4. Register 1-second timer for active key expiration
5. Infinite loop:
   a. Wait for events
   b. Timer → expire_keys_cycle()
   c. server.fd → accept() loop until EAGAIN
   d. Client fd → recv() loop → try_process_frames()
   e. EOF/error → close_and_drop_client()
```

## kqueue (macOS)

### Setup
```c
const int kq = kqueue();

// Register listening socket (edge-triggered with EV_CLEAR)
struct kevent ch;
EV_SET(&ch, server.fd, EVFILT_READ, EV_ADD | EV_ENABLE | EV_CLEAR, 0, 0, NULL);

// Expiration timer (1 second, EVFILT_TIMER)
#define EXPIRE_TIMER_IDENT 0xDEAD
struct kevent timer_ev;
EV_SET(&timer_ev, EXPIRE_TIMER_IDENT, EVFILT_TIMER, EV_ADD | EV_ENABLE, 0, 1000, NULL);
```

### Event Identification
```c
// IMPORTANT: check BOTH filter AND ident to avoid collision with fd numbers
if (evs[i].filter == EVFILT_TIMER && ident_fd == EXPIRE_TIMER_IDENT) {
    expire_keys_cycle();
}
```

### Register Client
```c
// udata stores client_t pointer — avoids list lookup
EV_SET(&ch, c->fd, EVFILT_READ, EV_ADD | EV_ENABLE | EV_CLEAR, 0, 0, c);
```

## epoll (Linux)

### Setup
```c
const int epfd = epoll_create1(0);

// Listening socket (edge-triggered with EPOLLET)
struct epoll_event ev = { .events = EPOLLIN | EPOLLET, .data.fd = server.fd };
epoll_ctl(epfd, EPOLL_CTL_ADD, server.fd, &ev);

// Timer via timerfd (level-triggered — NO EPOLLET for timer)
int tfd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
struct itimerspec its = { .it_interval = {.tv_sec = 1}, .it_value = {.tv_sec = 1} };
timerfd_settime(tfd, 0, &its, NULL);

struct epoll_event tev = { .events = EPOLLIN, .data.fd = tfd };  // No EPOLLET
epoll_ctl(epfd, EPOLL_CTL_ADD, tfd, &tev);
```

### Consuming Timer
```c
if (events[i].data.fd == tfd) {
    uint64_t expirations;
    read(tfd, &expirations, sizeof(expirations));  // MUST read to re-arm
    expire_keys_cycle();
}
```

## io_uring (Linux)

### Critical Requirements

1. **Buffers MUST be static or heap** — io_uring is async; stack locals are invalid when CQE arrives:
```c
// WRONG — stack corruption
uint64_t timer_buf;
io_uring_prep_read(sqe, tfd, &timer_buf, sizeof(timer_buf), 0);

// CORRECT — file-scope static
static uint64_t expire_timer_buf;
io_uring_prep_read(sqe, tfd, &expire_timer_buf, sizeof(expire_timer_buf), 0);
```

2. **Sentinel for timer CQE identification:**
```c
static client_t expire_timer_sentinel = {.fd = -1};

// During setup: sentinel fd is changed to timerfd
expire_timer_sentinel.fd = tfd;

io_uring_sqe_set_data(sqe, &expire_timer_sentinel);

// In CQE handler: compare pointer
if (c == &expire_timer_sentinel) { expire_keys_cycle(); /* re-arm... */ }
```

3. **Re-arm timer after each completion:**
```c
if (c == &expire_timer_sentinel) {
    expire_keys_cycle();
    struct io_uring_sqe *tsqe = io_uring_get_sqe(&ring);
    if (tsqe) {
        io_uring_prep_read(tsqe, expire_timer_sentinel.fd,
                           &expire_timer_buf, sizeof(expire_timer_buf), 0);
        io_uring_sqe_set_data(tsqe, &expire_timer_sentinel);
        io_uring_submit(&ring);
    }
    io_uring_cqe_seen(&ring, cqe);
    continue;
}
```

4. **Batch submission** via `BATCH_SUBMIT_THRESHOLD` (32).

## Adding a New Timer

### kqueue:
```c
#define NEW_TIMER_IDENT 0xBEEF
EV_SET(&timer_ev, NEW_TIMER_IDENT, EVFILT_TIMER, EV_ADD | EV_ENABLE, 0, 5000, NULL);

if (evs[i].filter == EVFILT_TIMER && ident_fd == NEW_TIMER_IDENT) {
    do_periodic_work();
}
```

### epoll:
```c
int new_tfd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
struct itimerspec its = { .it_interval = {.tv_sec = 5}, .it_value = {.tv_sec = 5} };
timerfd_settime(new_tfd, 0, &its, NULL);
// Register with EPOLLIN (no EPOLLET), check events[i].data.fd
```

## Shared Functions (in networking.c)

```c
set_nonblocking(int fd)
set_tcp_no_delay(int fd)
client_t *init_client(int client_fd, struct sockaddr_storage ss, enum socket_domain socket_domain)
try_process_frames(client_t *c)
```
