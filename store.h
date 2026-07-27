#ifndef STORE_H
#define STORE_H

#include <time.h>

#define TABLE_SIZE 128     // number of buckets in hash table
#define MAX_KEYS 100       // capacity before LRU eviction kicks in
#define KEY_LEN 256
#define VAL_LEN 256

// One key-value entry. Doubles as a hash-table chain node
// (via `next`) AND a doubly linked list node for LRU order
// (via `lru_prev` / `lru_next`).
typedef struct Entry {
    char key[KEY_LEN];
    char value[VAL_LEN];
    time_t expires_at;      // 0 = no expiry
    struct Entry *next;     // next entry in same hash bucket (collision chain)
    struct Entry *lru_prev; // previous entry in LRU list (more recently used)
    struct Entry *lru_next; // next entry in LRU list (less recently used)
} Entry;

typedef struct {
    Entry *buckets[TABLE_SIZE];  // hash table: array of chain heads
    Entry *lru_head;             // most recently used
    Entry *lru_tail;             // least recently used (eviction target)
    int count;                   // current number of keys stored
} Store;

void store_init(Store *s);
void store_set(Store *s, const char *key, const char *value, int ttl_seconds);
const char *store_get(Store *s, const char *key); // NULL if missing/expired
int store_del(Store *s, const char *key);         // 1 if deleted, 0 if not found

#endif
