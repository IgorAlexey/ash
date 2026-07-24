#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "ash/base/arena.h"

#if defined(__SANITIZE_ADDRESS__)
#  define ASH_ASAN 1
#elif defined(__has_feature)
#  if __has_feature(address_sanitizer)
#    define ASH_ASAN 1
#  endif
#endif

#ifdef ASH_ASAN
#  include <sanitizer/asan_interface.h>
#  define ASH_POISON(p, n)   __asan_poison_memory_region((p), (n))
#  define ASH_UNPOISON(p, n) __asan_unpoison_memory_region((p), (n))
#  define ASH_REDZONE        ((size_t)16)
#else
#  define ASH_POISON(p, n)   ((void)(p), (void)(n))
#  define ASH_UNPOISON(p, n) ((void)(p), (void)(n))
#  define ASH_REDZONE        ((size_t)0)
#endif

struct ash_arena_chunk {
    ash_arena_chunk *prev;
    ash_arena_chunk *next;
    size_t           cap;
    size_t           used;
    _Alignas(16) unsigned char data[];
};

size_t ash_checked_mul(size_t x, size_t y)
{
    if (y != 0 && x > SIZE_MAX / y)
        ash_die("array size overflow: %zu * %zu", x, y);
    return x * y;
}

static ash_status grow(ash_arena *a, size_t need)
{
    ash_arena_chunk *c = a->current;
    ash_arena_chunk *tail = c;
    for (ash_arena_chunk *scan = c ? c->next : NULL; scan; scan = scan->next) {
        if (scan->cap >= need) {
            scan->used = 0;
            a->current = scan;
            ASH_POISON(scan->data, scan->cap);
            return ASH_OK;
        }
        tail = scan;
    }

    size_t cap = need > a->chunk_size ? need : a->chunk_size;
    ash_arena_chunk *n = malloc(sizeof *n + cap);
    if (!n)
        return ASH_ERR_NOMEM;

    n->next = NULL;
    n->cap = cap;
    n->used = 0;
    if (tail) {
        n->prev = tail;
        tail->next = n;
    } else {
        n->prev = NULL;
        a->head = n;
    }
    a->current = n;
    ASH_POISON(n->data, cap);
    a->high_water += cap;
    return ASH_OK;
}

ash_status ash_arena_create(ash_arena *a, const char *name, size_t chunk_size)
{
    a->head = NULL;
    a->current = NULL;
    a->chunk_size = chunk_size ? chunk_size : (size_t)65536;
    a->high_water = 0;
    a->name = name;
    return grow(a, a->chunk_size);
}

void *ash_arena_alloc(ash_arena *a, size_t size, size_t align)
{
    assert(align != 0 && (align & (align - 1)) == 0 && align <= 16);

    ash_arena_chunk *c = a->current;
    size_t aligned = (c->used + (align - 1)) & ~(align - 1);
    if (aligned > c->cap || size > c->cap - aligned) {
        if (size > SIZE_MAX - align)
            ash_die("arena '%s': allocation size %zu overflows",
                    a->name ? a->name : "?", size);
        if (grow(a, size + align) != ASH_OK)
            ash_die("arena '%s': out of memory for %zu bytes (high water %zu)",
                    a->name ? a->name : "?", size, a->high_water);
        c = a->current;
        aligned = 0;
    }

    void *p = c->data + aligned;
    size_t end = aligned + size + ASH_REDZONE;
    c->used = end <= c->cap ? end : c->cap;
    ASH_UNPOISON(p, size);
    return p;
}

ash_arena_mark ash_arena_mark_get(const ash_arena *a)
{
    ash_arena_mark m = { a->current, a->current->used };
    return m;
}

void ash_arena_rewind(ash_arena *a, ash_arena_mark m)
{
    ash_arena_chunk *c = a->current;
    while (c != m.chunk) {
        ASH_POISON(c->data, c->used);
        c->used = 0;
        c = c->prev;
    }
    ASH_POISON(c->data + m.used, c->used - m.used);
    c->used = m.used;
    a->current = c;
}

void ash_arena_reset(ash_arena *a)
{
    ash_arena_mark base = { a->head, 0 };
    ash_arena_rewind(a, base);
}

void ash_arena_destroy(ash_arena *a)
{
    ash_arena_chunk *c = a->head;
    while (c) {
        ash_arena_chunk *next = c->next;
        free(c);
        c = next;
    }
    memset(a, 0, sizeof *a);
}

ash_status ash_mem_create(ash_mem *m)
{
    memset(m, 0, sizeof *m);
    ASH_TRY(ash_arena_create(&m->session, "session", 1u << 20));
    ASH_TRY(ash_arena_create(&m->turn, "turn", 1u << 18));
    ASH_TRY(ash_arena_create(&m->scratch, "scratch", 1u << 16));
    ASH_TRY(ash_arena_create(&m->stack, "stack", 1u << 20));
    return ASH_OK;
}

void ash_mem_destroy(ash_mem *m)
{
    ash_arena_destroy(&m->stack);
    ash_arena_destroy(&m->scratch);
    ash_arena_destroy(&m->turn);
    ash_arena_destroy(&m->session);
}
