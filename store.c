#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "store.h"

// djb2 hash function — simple, fast, well-known string hash
static unsigned long hash_key(const char *key) {
    unsigned long hash = 5381;
    int c;
    while ((c = *key++)) {
        hash = ((hash << 5) + hash) + c; // hash * 33 + c
    }
    return hash % TABLE_SIZE;
}

void store_init(Store *s) {
    memset(s, 0, sizeof(Store));
}

// --- LRU list helpers ---

// Remove entry from wherever it currently sits in the LRU list
static void lru_unlink(Store *s, Entry *e) {
    if (e->lru_prev) e->lru_prev->lru_next = e->lru_next;
    else s->lru_head = e->lru_next; // e was head

    if (e->lru_next) e->lru_next->lru_prev = e->lru_prev;
    else s->lru_tail = e->lru_prev; // e was tail

    e->lru_prev = e->lru_next = NULL;
}

// Insert entry at the head (most recently used position)
static void lru_push_front(Store *s, Entry *e) {
    e->lru_prev = NULL;
    e->lru_next = s->lru_head;
    if (s->lru_head) s->lru_head->lru_prev = e;
    s->lru_head = e;
    if (!s->lru_tail) s->lru_tail = e; // list was empty
}

// Call whenever a key is accessed (SET or GET) — moves it to "most recent"
static void lru_touch(Store *s, Entry *e) {
    lru_unlink(s, e);
    lru_push_front(s, e);
}

// --- Internal lookup (returns entry even if expired, caller checks) ---
static Entry *find_entry(Store *s, const char *key) {
    unsigned long idx = hash_key(key);
    Entry *e = s->buckets[idx];
    while (e) {
        if (strcmp(e->key, key) == 0) return e;
        e = e->next;
    }
    return NULL;
}

static void remove_from_bucket(Store *s, Entry *target) {
    unsigned long idx = hash_key(target->key);
    Entry *e = s->buckets[idx];
    Entry *prev = NULL;
    while (e) {
        if (e == target) {
            if (prev) prev->next = e->next;
            else s->buckets[idx] = e->next;
            return;
        }
        prev = e;
        e = e->next;
    }
}

static void evict_lru(Store *s) {
    if (!s->lru_tail) return;
    Entry *victim = s->lru_tail;
    printf("[evict] LRU evicting key '%s'\n", victim->key);
    remove_from_bucket(s, victim);
    lru_unlink(s, victim);
    free(victim);
    s->count--;
}

void store_set(Store *s, const char *key, const char *value, int ttl_seconds) {
    Entry *e = find_entry(s, key);

    if (e) {
        // key exists: overwrite value + expiry, then mark as recently used
        strncpy(e->value, value, VAL_LEN - 1);
        e->value[VAL_LEN - 1] = '\0';
        e->expires_at = ttl_seconds > 0 ? time(NULL) + ttl_seconds : 0;
        lru_touch(s, e);
        return;
    }

    // evict before inserting if we're at capacity
    if (s->count >= MAX_KEYS) {
        evict_lru(s);
    }

    e = malloc(sizeof(Entry));
    strncpy(e->key, key, KEY_LEN - 1);
    e->key[KEY_LEN - 1] = '\0';
    strncpy(e->value, value, VAL_LEN - 1);
    e->value[VAL_LEN - 1] = '\0';
    e->expires_at = ttl_seconds > 0 ? time(NULL) + ttl_seconds : 0;
    e->next = NULL;
    e->lru_prev = e->lru_next = NULL;

    unsigned long idx = hash_key(key);
    e->next = s->buckets[idx];
    s->buckets[idx] = e;

    lru_push_front(s, e);
    s->count++;
}

const char *store_get(Store *s, const char *key) {
    Entry *e = find_entry(s, key);
    if (!e) return NULL;

    if (e->expires_at != 0 && time(NULL) >= e->expires_at) {
        // lazily expired: remove it now
        remove_from_bucket(s, e);
        lru_unlink(s, e);
        free(e);
        s->count--;
        return NULL;
    }

    lru_touch(s, e);
    return e->value;
}

int store_del(Store *s, const char *key) {
    Entry *e = find_entry(s, key);
    if (!e) return 0;
    remove_from_bucket(s, e);
    lru_unlink(s, e);
    free(e);
    s->count--;
    return 1;
}
