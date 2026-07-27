# mini-Redis

A single-threaded, in-memory key-value store server written in C, inspired by Redis.
Communicates over raw TCP sockets using a simple custom text protocol.

## Features implemented
- `SET key value [ttl_seconds]`, `GET key`, `DEL key`
- Custom hash table (djb2 hash, separate chaining for collisions)
- TTL / key expiry (lazy expiry — checked on access)
- LRU eviction when the store hits capacity (hash map + doubly linked list, O(1) per operation)
- Single-threaded event loop (mirrors real Redis's concurrency model)

## Not yet implemented (in progress / next steps)
- RESP protocol (currently a simpler custom text protocol instead of Redis's real wire format)
- Persistence (snapshot to disk / append-only file + reload on startup)
- Concurrent connections (currently one client handled at a time; would extend via epoll/select)

## Design notes
- **Hash table**: array of 128 buckets, each a linked list (chaining) for collision handling.
  djb2 hash function chosen for simplicity and good distribution on short string keys.
- **LRU eviction**: every `Entry` struct doubles as a hash-bucket chain node AND a node in
  a doubly linked list ordered by recency of use. This gives O(1) lookup (hash table) and
  O(1) reordering/eviction (linked list) — the same design as LeetCode #146 (LRU Cache).
- **Single-threaded design**: chosen deliberately, same as real Redis — avoids locking
  complexity since only one command executes at a time.

## How to build and run
```bash
gcc -Wall server.c store.c -o mini-redis
./mini-redis
```

In another terminal:
```bash
nc localhost 6380
SET name John
GET name
SET session abc123 30
DEL name
```

## Files
- `store.h` / `store.c` — hash table + LRU cache implementation (the core data structure)
- `server.c` — TCP server, command parsing, connection handling
