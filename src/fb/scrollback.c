#include "ash/fb/scrollback.h"

#include <string.h>

void ash_sb_init(ash_scrollback *sb, ash_arena *a,
                 size_t cell_budget, size_t max_lines)
{
    memset(sb, 0, sizeof *sb);
    size_t cap_cells = cell_budget / sizeof(ash_cell);
    if (cap_cells == 0)
        cap_cells = 1;
    if (max_lines == 0)
        max_lines = 1;
    sb->cells = ash_array(a, ash_cell, cap_cells);
    sb->lines = ash_array(a, ash_sb_line, max_lines);
    sb->cap_cells = cap_cells;
    sb->cap_lines = max_lines;
    sb->view = -1;
}

void ash_sb_reset(ash_scrollback *sb)
{
    sb->head = 0;
    sb->count = 0;
    sb->write = 0;
    sb->next_seq = 0;
    sb->view = -1;
}

void ash_sb_append(ash_scrollback *sb, const ash_cell *cells, size_t n)
{
    ash_sb_append_wrapped(sb, cells, n, 0);
}

void ash_sb_append_wrapped(ash_scrollback *sb, const ash_cell *cells, size_t n,
                           int cont)
{
    if (n > sb->cap_cells)
        n = sb->cap_cells;

    if (sb->write + n > sb->cap_cells)
        sb->write = 0;

    while (sb->count > 0) {
        ash_sb_line *old = &sb->lines[sb->head];
        int hit = old->len > 0 && old->off < sb->write + n &&
                  sb->write < old->off + old->len;
        if (!hit)
            break;
        sb->head = (sb->head + 1) % sb->cap_lines;
        sb->count--;
    }

    if (sb->count == sb->cap_lines) {
        sb->head = (sb->head + 1) % sb->cap_lines;
        sb->count--;
    }

    size_t idx = (sb->head + sb->count) % sb->cap_lines;
    sb->lines[idx].seq = sb->next_seq++;
    sb->lines[idx].off = (uint32_t)sb->write;
    sb->lines[idx].len = (uint32_t)n;
    sb->lines[idx].cont = cont ? 1 : 0;
    if (n)
        memcpy(sb->cells + sb->write, cells, n * sizeof(ash_cell));
    sb->write += n;
    sb->count++;
}

size_t ash_sb_count(const ash_scrollback *sb)
{
    return sb->count;
}

uint64_t ash_sb_oldest(const ash_scrollback *sb)
{
    if (sb->count == 0)
        return sb->next_seq;
    return sb->lines[sb->head].seq;
}

uint64_t ash_sb_newest(const ash_scrollback *sb)
{
    return sb->next_seq;
}

int ash_sb_line_at(const ash_scrollback *sb, uint64_t seq,
                   const ash_cell **out, size_t *n)
{
    uint64_t oldest = ash_sb_oldest(sb);
    if (sb->count == 0 || seq < oldest || seq >= sb->next_seq)
        return 0;
    size_t rel = (size_t)(seq - oldest);
    size_t idx = (sb->head + rel) % sb->cap_lines;
    const ash_sb_line *line = &sb->lines[idx];
    if (out)
        *out = sb->cells + line->off;
    if (n)
        *n = line->len;
    return 1;
}

int ash_sb_cont(const ash_scrollback *sb, uint64_t seq)
{
    uint64_t oldest = ash_sb_oldest(sb);
    if (sb->count == 0 || seq < oldest || seq >= sb->next_seq)
        return 0;
    size_t idx = (sb->head + (size_t)(seq - oldest)) % sb->cap_lines;
    return sb->lines[idx].cont;
}

void ash_sb_rewind_to(ash_scrollback *sb, uint64_t seq)
{
    if (seq >= sb->next_seq)
        return;
    uint64_t oldest = ash_sb_oldest(sb);
    if (sb->count == 0 || seq <= oldest) {
        sb->head = 0;
        sb->count = 0;
        sb->write = 0;
        sb->next_seq = seq;
        return;
    }
    size_t rel = (size_t)(seq - oldest);
    size_t idx = (sb->head + rel) % sb->cap_lines;
    sb->write = sb->lines[idx].off;
    sb->count = rel;
    sb->next_seq = seq;
}

void ash_sb_scroll_to(ash_scrollback *sb, uint64_t seq)
{
    sb->view = (int64_t)seq;
}

void ash_sb_scroll_by(ash_scrollback *sb, int64_t delta, int viewport)
{
    if (viewport < 1)
        viewport = 1;

    uint64_t lo = ash_sb_oldest(sb);
    uint64_t hi = sb->next_seq;
    if (hi == lo)
        return;

    uint64_t vp = (uint64_t)viewport;
    uint64_t max_top = hi > vp ? hi - vp : lo;
    if (max_top < lo)
        max_top = lo;

    uint64_t base = ash_sb_view_top(sb, viewport);
    int64_t nv = (int64_t)base + delta;
    if (nv < (int64_t)lo)
        nv = (int64_t)lo;

    if (nv >= (int64_t)max_top) {
        sb->view = -1;
        return;
    }
    sb->view = nv;
}

void ash_sb_follow(ash_scrollback *sb)
{
    sb->view = -1;
}

int ash_sb_is_following(const ash_scrollback *sb)
{
    return sb->view < 0;
}

uint64_t ash_sb_view_top(const ash_scrollback *sb, int viewport)
{
    uint64_t lo = ash_sb_oldest(sb);
    uint64_t hi = sb->next_seq;
    if (hi == lo)
        return lo;
    if (viewport < 1)
        viewport = 1;

    if (sb->view < 0) {
        uint64_t vp = (uint64_t)viewport;
        uint64_t top = hi > vp ? hi - vp : 0;
        return top < lo ? lo : top;
    }

    uint64_t v = (uint64_t)sb->view;
    if (v < lo)
        v = lo;
    if (v > hi - 1)
        v = hi - 1;
    return v;
}
