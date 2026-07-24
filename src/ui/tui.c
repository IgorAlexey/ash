#include "ash/ui/tui.h"

#include <limits.h>
#include <string.h>

#define ROOT_ID 0x14057B7EF767814FULL
#define FNV_BASIS 0xcbf29ce484222325ULL
#define FNV_PRIME 0x100000001b3ULL

typedef enum content_kind {
    C_NONE = 0,
    C_TEXT,
    C_LIST,
    C_MODAL,
    C_SCROLL,
    C_TEXTAREA
} content_kind;

typedef struct chunk {
    size_t   offset;
    ash_rgba fg;
    uint16_t attr;
} chunk;

struct ash_node {
    ash_node *flat_next;
    ash_node *stack_parent;

    uint64_t    id;
    const char *classname;
    ash_node   *parent;
    int         depth;

    ash_node *sib_prev;
    ash_node *sib_next;
    ash_node *child_first;
    ash_node *child_last;
    int       child_count;

    int      has_float;
    float    fl_gx, fl_gy, fl_ox, fl_oy;
    ash_position position;
    ash_padding  pad;
    ash_rgba bg, fg;
    int      reverse, bordered, focusable, focus_well, focus_void;
    int      has_intrinsic;

    content_kind kind;

    ash_buf text;
    chunk   *chunks;
    int      nchunks, cap_chunks;

    uint64_t  list_selected;
    ash_node *list_selected_node;

    const char *title;

    int scroll_y;
    int thumb_h;

    ash_textarea *ta;

    int is_w, is_h;
    ash_rect outer, inner, outer_clip, inner_clip;
};

struct ash_tree {
    ash_node *root_first;
    ash_node *root_last;
    ash_node *flat_head;
    ash_node *flat_tail;
    ash_node *last_node;
    ash_node *current_node;
    size_t    count;
    uint64_t  checksum;
};

struct ash_nodemap {
    ash_node **slots;
    size_t     nslots;
    int        shift;
    uint64_t   mask;
};

struct ash_ctx {
    ash_tui  *tui;
    ash_tree *tree;

    uint32_t    input_key;
    uint32_t    input_mods;
    const char *input_text;
    uint32_t    input_text_len;
    int         input_consumed;
    const ash_input_event *ev;

    ash_node *last_modal;
    uint64_t  next_mixin;
    int       needs_settling;
};

static uint64_t hash_str(uint64_t seed, const char *s)
{
    uint64_t h = seed ^ FNV_BASIS;
    while (*s) {
        h ^= (uint8_t)*s++;
        h *= FNV_PRIME;
    }
    return h;
}

static uint64_t hash_mix(uint64_t id, uint64_t mixin)
{
    uint64_t x = id ^ (mixin * 0x9E3779B97F4A7C15ULL);
    x ^= x >> 29;
    x *= 0xBF58476D1CE4E5B9ULL;
    x ^= x >> 32;
    return x;
}

static int rect_empty(ash_rect r)
{
    return r.w <= 0 || r.h <= 0;
}

static ash_rect rect_intersect(ash_rect a, ash_rect b)
{
    int x0 = a.x > b.x ? a.x : b.x;
    int y0 = a.y > b.y ? a.y : b.y;
    int x1 = a.x + a.w < b.x + b.w ? a.x + a.w : b.x + b.w;
    int y1 = a.y + a.h < b.y + b.h ? a.y + a.h : b.y + b.h;
    ash_rect r = { x0, y0, x1 > x0 ? x1 - x0 : 0, y1 > y0 ? y1 - y0 : 0 };
    return r;
}

static int disp_width(const char *p, size_t len)
{
    int w = 0;
    size_t i = 0;
    while (i < len) {
        uint32_t cp;
        size_t a = ash_utf8_decode(p + i, len - i, &cp);
        if (a == 0)
            break;
        int cw = ash_char_width(cp);
        if (cw > 0)
            w += cw;
        i += a;
    }
    return w;
}

static ash_node *alloc_node(ash_arena *arena)
{
    ash_node *n = ash_new(arena, ash_node);
    memset(n, 0, sizeof *n);
    ash_buf_init(&n->text, arena);
    return n;
}

static ash_tree *tree_new(ash_arena *arena)
{
    ash_tree *t = ash_new(arena, ash_tree);
    memset(t, 0, sizeof *t);
    ash_node *root = alloc_node(arena);
    root->id = ROOT_ID;
    root->classname = "root";
    root->focusable = 1;
    root->focus_well = 1;
    t->root_first = root;
    t->root_last = root;
    t->flat_head = root;
    t->flat_tail = root;
    t->last_node = root;
    t->current_node = root;
    t->count = 1;
    t->checksum = ROOT_ID;
    return t;
}

static void tree_push(ash_tree *t, ash_node *n)
{
    ash_node *p = t->current_node;
    n->parent = p;
    n->stack_parent = p;
    n->depth = p->depth + 1;
    n->sib_prev = p->child_last;
    if (p->child_last)
        p->child_last->sib_next = n;
    if (!p->child_first)
        p->child_first = n;
    p->child_last = n;
    p->child_count++;

    t->flat_tail->flat_next = n;
    t->flat_tail = n;
    t->last_node = n;
    t->current_node = n;
    t->count++;
    t->checksum = t->checksum * FNV_PRIME ^ n->id;
}

static void tree_pop(ash_tree *t)
{
    ash_node *c = t->current_node;
    t->last_node = c;
    if (c->stack_parent)
        t->current_node = c->stack_parent;
}

static void tree_move_to_root(ash_tree *t, ash_node *n, ash_node *anchor)
{
    ash_node *parent = n->parent;
    if (!parent)
        return;
    if (n->sib_prev)
        n->sib_prev->sib_next = n->sib_next;
    if (n->sib_next)
        n->sib_next->sib_prev = n->sib_prev;
    if (parent->child_first == n)
        parent->child_first = n->sib_next;
    if (parent->child_last == n)
        parent->child_last = n->sib_prev;
    parent->child_count--;

    n->parent = anchor;
    n->depth = anchor ? anchor->depth + 1 : 0;
    n->sib_prev = t->root_last;
    n->sib_next = NULL;
    t->root_last->sib_next = n;
    t->root_last = n;
}

static ash_nodemap *map_new(ash_arena *arena, ash_tree *tree)
{
    ash_nodemap *m = ash_new(arena, ash_nodemap);
    size_t target = 4 * tree->count + 1;
    int width = 1;
    while (((size_t)1 << width) < target)
        width++;
    m->nslots = (size_t)1 << width;
    m->shift = 64 - width;
    m->mask = m->nslots - 1;
    m->slots = ash_array(arena, ash_node *, m->nslots);
    memset(m->slots, 0, m->nslots * sizeof(ash_node *));

    for (ash_node *n = tree->flat_head; n; n = n->flat_next) {
        uint64_t slot = n->id >> m->shift;
        while (m->slots[slot]) {
            if (m->slots[slot]->id == n->id)
                break;
            slot = (slot + 1) & m->mask;
        }
        m->slots[slot] = n;
    }
    return m;
}

static ash_node *map_get(ash_nodemap *m, uint64_t id)
{
    if (!m)
        return NULL;
    uint64_t slot = id >> m->shift;
    for (;;) {
        ash_node *n = m->slots[slot];
        if (!n)
            return NULL;
        if (n->id == id)
            return n;
        slot = (slot + 1) & m->mask;
    }
}

static void build_path(ash_tui *t, ash_node *n)
{
    if (!n) {
        t->focus_path[0] = ROOT_ID;
        t->focus_len = 1;
        return;
    }
    uint64_t tmp[32];
    int k = 0;
    for (ash_node *p = n; p && k < 32; p = p->parent)
        tmp[k++] = p->id;
    t->focus_len = k;
    for (int i = 0; i < k; i++)
        t->focus_path[i] = tmp[k - 1 - i];
}

static int node_is_focused(ash_tui *t, uint64_t id)
{
    return t->focus_len > 0 && t->focus_path[t->focus_len - 1] == id;
}

static int node_subtree_focused(ash_tui *t, ash_node *n)
{
    return n->depth < t->focus_len && t->focus_path[n->depth] == n->id;
}

static void steal_focus_for(ash_ctx *c, ash_node *n)
{
    if (!node_is_focused(c->tui, n->id)) {
        build_path(c->tui, n);
        c->needs_settling = 1;
    }
}

static int pop_focusable(ash_tui *t, int pop_min)
{
    uint64_t before = t->focus_len ? t->focus_path[t->focus_len - 1] : 0;
    int limit = t->focus_len - pop_min;
    if (limit < 0)
        limit = 0;
    int newlen = 0;
    for (int i = 0; i < limit; i++) {
        ash_node *n = map_get(t->prev_map, t->focus_path[i]);
        if (!n)
            break;
        if (pop_min != 0 && n->focus_void)
            break;
        if (n->focusable)
            newlen = i + 1;
    }
    t->focus_len = newlen;
    if (t->focus_len == 0) {
        t->focus_path[0] = ROOT_ID;
        t->focus_len = 1;
    }
    return before != t->focus_path[t->focus_len - 1];
}

void ash_block_begin(ash_ctx *c, const char *classname)
{
    ash_node *parent = c->tree->current_node;
    uint64_t id = hash_str(parent->id, classname);
    if (c->next_mixin != 0) {
        id = hash_mix(id, c->next_mixin);
        c->next_mixin = 0;
    }
    ash_node *n = alloc_node(c->tui->arena_next);
    n->id = id;
    n->classname = classname;
    tree_push(c->tree, n);
}

static void block_end_move_focus(ash_ctx *c);

void ash_block_end(ash_ctx *c)
{
    tree_pop(c->tree);
    block_end_move_focus(c);
}

void ash_next_id_mixin(ash_ctx *c, uint64_t mixin)
{
    c->next_mixin = mixin;
}

static void gather_stops(ash_node *node, ash_node **out, int *n, int cap)
{
    for (ash_node *ch = node->child_first; ch; ch = ch->sib_next) {
        if (ch->focusable && *n < cap)
            out[(*n)++] = ch;
        if (ch->focus_void)
            continue;
        gather_stops(ch, out, n, cap);
    }
}

static void block_end_move_focus(ash_ctx *c)
{
    ash_tui *t = c->tui;
    ash_node *well = c->tree->last_node;
    if (!well->focus_well || c->input_consumed)
        return;
    if (!node_subtree_focused(t, well))
        return;
    if (c->input_key != ASH_TK_TAB)
        return;
    int fwd = (c->input_mods & ASH_MOD_SHIFT) ? 0 : 1;

    ash_node *stops[256];
    int ns = 0;
    gather_stops(well, stops, &ns, 256);
    if (ns == 0)
        return;

    uint64_t cur = t->focus_path[t->focus_len - 1];
    int ci = -1;
    for (int i = 0; i < ns; i++)
        if (stops[i]->id == cur)
            ci = i;

    int ti;
    if (ci < 0)
        ti = fwd ? 0 : ns - 1;
    else
        ti = fwd ? (ci + 1) % ns : (ci - 1 + ns) % ns;

    build_path(t, stops[ti]);
    c->input_consumed = 1;
    c->needs_settling = 1;
}

void ash_attr_border(ash_ctx *c)
{
    c->tree->last_node->bordered = 1;
}

void ash_attr_padding(ash_ctx *c, ash_padding p)
{
    if (p.left < 0)
        p.left = 0;
    if (p.top < 0)
        p.top = 0;
    if (p.right < 0)
        p.right = 0;
    if (p.bottom < 0)
        p.bottom = 0;
    c->tree->last_node->pad = p;
}

void ash_attr_position(ash_ctx *c, ash_position pos)
{
    c->tree->last_node->position = pos;
}

void ash_attr_float(ash_ctx *c, ash_float_spec spec)
{
    ash_node *n = c->tree->last_node;
    ash_node *anchor = NULL;
    switch (spec.anchor) {
    case ASH_ANCHOR_LAST:
        anchor = n->sib_prev ? n->sib_prev : n->parent;
        break;
    case ASH_ANCHOR_PARENT:
        anchor = n->parent;
        break;
    case ASH_ANCHOR_ROOT:
        anchor = NULL;
        break;
    }
    tree_move_to_root(c->tree, n, anchor);
    n->focus_well = 1;
    n->has_float = 1;
    float gx = spec.gravity_x < 0 ? 0 : spec.gravity_x > 1 ? 1 : spec.gravity_x;
    float gy = spec.gravity_y < 0 ? 0 : spec.gravity_y > 1 ? 1 : spec.gravity_y;
    n->fl_gx = gx;
    n->fl_gy = gy;
    n->fl_ox = spec.offset_x;
    n->fl_oy = spec.offset_y;
}

void ash_attr_bg(ash_ctx *c, ash_rgba bg)
{
    c->tree->last_node->bg = bg;
}

void ash_attr_fg(ash_ctx *c, ash_rgba fg)
{
    c->tree->last_node->fg = fg;
}

void ash_attr_reverse(ash_ctx *c)
{
    c->tree->last_node->reverse = 1;
}

void ash_attr_focus_well(ash_ctx *c)
{
    c->tree->last_node->focus_well = 1;
}

void ash_attr_intrinsic_size(ash_ctx *c, int w, int h)
{
    ash_node *n = c->tree->last_node;
    n->is_w = w < 0 ? 0 : w;
    n->is_h = h < 0 ? 0 : h;
    n->has_intrinsic = 1;
}

void ash_focus_on_first_present(ash_ctx *c)
{
    ash_node *n = c->tree->last_node;
    n->focusable = 1;
    if (map_get(c->tui->prev_map, n->id) == NULL)
        steal_focus_for(c, n);
}

void ash_steal_focus(ash_ctx *c)
{
    steal_focus_for(c, c->tree->last_node);
}

void ash_inherit_focus(ash_ctx *c)
{
    ash_node *n = c->tree->last_node;
    ash_node *parent = n->parent;
    if (!parent)
        return;
    n->focusable = 1;
    parent->focusable = 1;
    if (node_is_focused(c->tui, parent->id)) {
        c->needs_settling = 1;
        if (c->tui->focus_len < 32)
            c->tui->focus_path[c->tui->focus_len++] = n->id;
    }
}

void ash_toss_focus_up(ash_ctx *c)
{
    if (pop_focusable(c->tui, 1))
        c->needs_settling = 1;
}

int ash_is_focused(ash_ctx *c)
{
    return node_is_focused(c->tui, c->tree->last_node->id);
}

int ash_contains_focus(ash_ctx *c)
{
    return node_subtree_focused(c->tui, c->tree->last_node);
}

int ash_consume_shortcut(ash_ctx *c, uint32_t key, uint32_t mods)
{
    if (!c->input_consumed && c->input_key == key && c->input_mods == mods) {
        c->input_consumed = 1;
        return 1;
    }
    return 0;
}

static void node_push_chunk(ash_ctx *c, ash_node *n, size_t off,
                            ash_rgba fg, uint16_t attr)
{
    if (n->nchunks == n->cap_chunks) {
        int nc = n->cap_chunks ? n->cap_chunks * 2 : 4;
        chunk *a = ash_array(c->tui->arena_next, chunk, (size_t)nc);
        if (n->nchunks)
            memcpy(a, n->chunks, (size_t)n->nchunks * sizeof(chunk));
        n->chunks = a;
        n->cap_chunks = nc;
    }
    n->chunks[n->nchunks].offset = off;
    n->chunks[n->nchunks].fg = fg;
    n->chunks[n->nchunks].attr = attr;
    n->nchunks++;
}

static void styled_add(ash_ctx *c, const char *p, size_t len)
{
    ash_node *n = c->tree->last_node;
    if (n->kind != C_TEXT)
        return;
    ash_buf_append(&n->text, p, len);
}

void ash_styled_label_begin(ash_ctx *c, const char *classname)
{
    ash_block_begin(c, classname);
    c->tree->last_node->kind = C_TEXT;
}

void ash_styled_label_fg(ash_ctx *c, ash_rgba fg)
{
    ash_node *n = c->tree->last_node;
    if (n->kind != C_TEXT)
        return;
    size_t last_off = n->nchunks ? n->chunks[n->nchunks - 1].offset : SIZE_MAX;
    ash_rgba last_fg = n->nchunks ? n->chunks[n->nchunks - 1].fg : 0;
    uint16_t last_attr = n->nchunks ? n->chunks[n->nchunks - 1].attr : 0;
    if (last_off != n->text.len && last_fg != fg)
        node_push_chunk(c, n, n->text.len, fg, last_attr);
}

void ash_styled_label_attr(ash_ctx *c, uint16_t attr)
{
    ash_node *n = c->tree->last_node;
    if (n->kind != C_TEXT)
        return;
    size_t last_off = n->nchunks ? n->chunks[n->nchunks - 1].offset : SIZE_MAX;
    ash_rgba last_fg = n->nchunks ? n->chunks[n->nchunks - 1].fg : 0;
    uint16_t last_attr = n->nchunks ? n->chunks[n->nchunks - 1].attr : 0;
    if (last_off != n->text.len && last_attr != attr)
        node_push_chunk(c, n, n->text.len, last_fg, attr);
}

void ash_styled_label_text(ash_ctx *c, const char *text)
{
    styled_add(c, text, strlen(text));
}

void ash_styled_label_end(ash_ctx *c)
{
    ash_node *n = c->tree->last_node;
    if (n->kind == C_TEXT) {
        n->is_w = disp_width((const char *)n->text.data, n->text.len);
        n->is_h = 1;
        n->has_intrinsic = 1;
    }
    ash_block_end(c);
}

void ash_label(ash_ctx *c, const char *classname, const char *text)
{
    ash_styled_label_begin(c, classname);
    ash_styled_label_text(c, text);
    ash_styled_label_end(c);
}

ash_button_style ash_button_default(void)
{
    ash_button_style s = { 0, -1, 1 };
    return s;
}

static void button_label(ash_ctx *c, const char *classname, const char *text,
                         ash_button_style style)
{
    ash_styled_label_begin(c, classname);
    if (style.bracketed)
        ash_styled_label_text(c, "[");
    if (style.checked >= 0)
        ash_styled_label_text(c, style.checked ? "\xF0\x9F\x97\xB9 " : "  ");

    if (style.accel >= 'A' && style.accel <= 'Z') {
        size_t len = strlen(text);
        size_t off = len;
        for (size_t i = 0; i < len; i++) {
            uint8_t ch = (uint8_t)text[i];
            if ((uint32_t)ch == style.accel) {
                off = i;
                break;
            }
            if ((uint32_t)(ch & ~0x20u) == style.accel && off == len)
                off = i;
        }
        if (off < len) {
            styled_add(c, text, off);
            ash_styled_label_attr(c, ASH_ATTR_UNDERLINE);
            styled_add(c, text + off, 1);
            ash_styled_label_attr(c, ASH_ATTR_NONE);
            styled_add(c, text + off + 1, len - off - 1);
        } else {
            char a = (char)style.accel;
            ash_styled_label_text(c, text);
            ash_styled_label_text(c, "(");
            ash_styled_label_attr(c, ASH_ATTR_UNDERLINE);
            styled_add(c, &a, 1);
            ash_styled_label_attr(c, ASH_ATTR_NONE);
            ash_styled_label_text(c, ")");
        }
    } else {
        ash_styled_label_text(c, text);
    }

    if (style.bracketed)
        ash_styled_label_text(c, "]");
    ash_styled_label_end(c);
}

static int button_activated(ash_ctx *c)
{
    if (!c->input_consumed &&
        (c->input_key == ASH_TK_ENTER || c->input_key == ASH_TK_SPACE) &&
        node_is_focused(c->tui, c->tree->last_node->id)) {
        c->input_consumed = 1;
        return 1;
    }
    return 0;
}

int ash_button(ash_ctx *c, const char *classname, const char *text,
               ash_button_style style)
{
    button_label(c, classname, text, style);
    c->tree->last_node->focusable = 1;
    if (ash_is_focused(c))
        c->tree->last_node->reverse = 1;
    return button_activated(c);
}

int ash_checkbox(ash_ctx *c, const char *classname, const char *text,
                 int *checked)
{
    ash_styled_label_begin(c, classname);
    c->tree->last_node->focusable = 1;
    if (ash_is_focused(c))
        c->tree->last_node->reverse = 1;
    ash_styled_label_text(c, *checked ? "[\xF0\x9F\x97\xB9 " : "[\xE2\x98\x90 ");
    ash_styled_label_text(c, text);
    ash_styled_label_text(c, "]");
    ash_styled_label_end(c);

    int activated = button_activated(c);
    if (activated)
        *checked = !*checked;
    return activated;
}

void ash_list_begin(ash_ctx *c, const char *classname)
{
    ash_block_begin(c, classname);
    ash_node *n = c->tree->last_node;
    n->focusable = 1;
    n->focus_void = 1;
    n->kind = C_LIST;
    ash_node *prev = map_get(c->tui->prev_map, n->id);
    if (prev && prev->kind == C_LIST)
        n->list_selected = prev->list_selected;
}

ash_list_sel ash_list_item(ash_ctx *c, int select, const char *text)
{
    ash_node *list = c->tree->current_node;
    ash_next_id_mixin(c, (uint64_t)list->child_count);
    ash_styled_label_begin(c, "item");
    ash_styled_label_text(c, "  ");
    c->tree->last_node->focusable = 1;
    ash_styled_label_text(c, text);
    ash_styled_label_end(c);

    list = c->tree->current_node;
    ash_node *item = c->tree->last_node;
    int selected_before = list->list_selected == item->id;
    int focused = node_is_focused(c->tui, item->id);
    int selected_now = selected_before ||
                       (select && list->list_selected == 0) || focused;
    if (selected_now) {
        list->list_selected_node = item;
        if (!selected_before) {
            list->list_selected = item->id;
            c->needs_settling = 1;
        }
    }
    int entered = focused && selected_before && !c->input_consumed &&
                  c->input_key == ASH_TK_ENTER;
    if (entered)
        c->input_consumed = 1;

    if (selected_before && entered)
        return ASH_LIST_ACTIVATED;
    if (selected_now && !selected_before)
        return ASH_LIST_SELECTED;
    return ASH_LIST_UNCHANGED;
}

void ash_list_end(ash_ctx *c)
{
    ash_block_end(c);
    ash_node *list = c->tree->last_node;
    int contains_focus = node_subtree_focused(c->tui, list);
    ash_node *sel_now = list->list_selected_node;
    ash_node *sel_next = sel_now ? sel_now : list->child_first;
    if (!sel_next)
        return;

    if (contains_focus && !c->input_consumed && c->input_key && sel_now) {
        ash_node *prev = map_get(c->tui->prev_map, list->id);
        if (prev) {
            int consumed = 1;
            switch (c->input_key) {
            case ASH_KEY_UP:
                sel_next = sel_now->sib_prev ? sel_now->sib_prev
                                             : list->child_last;
                break;
            case ASH_KEY_DOWN:
                sel_next = sel_now->sib_next ? sel_now->sib_next
                                             : list->child_first;
                break;
            case ASH_KEY_HOME:
                sel_next = list->child_first;
                break;
            case ASH_KEY_END:
                sel_next = list->child_last;
                break;
            case ASH_KEY_PGUP: {
                int steps = prev->inner_clip.h - 1;
                for (int i = 0; i < steps && sel_next->sib_prev; i++)
                    sel_next = sel_next->sib_prev;
                break;
            }
            case ASH_KEY_PGDN: {
                int steps = prev->inner_clip.h - 1;
                for (int i = 0; i < steps && sel_next->sib_next; i++)
                    sel_next = sel_next->sib_next;
                break;
            }
            default:
                consumed = 0;
                break;
            }
            if (consumed)
                c->input_consumed = 1;
        }
    }

    if (sel_next != sel_now)
        list->list_selected_node = sel_next;

    if (sel_next->kind == C_TEXT && sel_next->text.len > 0)
        sel_next->text.data[0] = '>';

    if (contains_focus) {
        sel_next->reverse = 1;
        steal_focus_for(c, sel_next);
    }
}

void ash_scrollarea_begin(ash_ctx *c, const char *classname,
                          int intrinsic_w, int intrinsic_h)
{
    ash_block_begin(c, classname);
    ash_node *container = c->tree->last_node;
    container->kind = C_SCROLL;
    if (intrinsic_w > 0 || intrinsic_h > 0) {
        container->is_w = intrinsic_w > 0 ? intrinsic_w : 0;
        container->is_h = intrinsic_h > 0 ? intrinsic_h : 0;
        container->has_intrinsic = 1;
    }
    ash_node *prev = map_get(c->tui->prev_map, container->id);
    if (prev && prev->kind == C_SCROLL)
        container->scroll_y = prev->scroll_y;

    ash_block_begin(c, "content");
    ash_inherit_focus(c);
    c->tree->last_node = container;
}

void ash_scrollarea_end(ash_ctx *c)
{
    ash_block_end(c);
    ash_block_end(c);
    ash_node *container = c->tree->last_node;

    if (c->input_consumed || !node_subtree_focused(c->tui, container) ||
        !c->input_key)
        return;
    ash_node *prev = map_get(c->tui->prev_map, container->id);
    if (!prev)
        return;
    int page = prev->inner_clip.h;
    int consumed = 1;
    switch (c->input_key) {
    case ASH_KEY_PGUP:
        container->scroll_y -= page;
        break;
    case ASH_KEY_PGDN:
        container->scroll_y += page;
        break;
    case ASH_KEY_HOME:
        container->scroll_y = 0;
        break;
    case ASH_KEY_END:
        container->scroll_y = INT_MAX / 2;
        break;
    default:
        consumed = 0;
        break;
    }
    if (consumed) {
        c->input_consumed = 1;
        c->needs_settling = 1;
    }
}

void ash_modal_begin(ash_ctx *c, const char *classname, const char *title)
{
    ash_block_begin(c, classname);
    ash_float_spec spec = { ASH_ANCHOR_ROOT, 0.5f, 0.5f,
                            (float)c->tui->w * 0.5f, (float)c->tui->h * 0.5f };
    ash_attr_float(c, spec);
    ash_attr_border(c);
    ash_attr_focus_well(c);
    ash_focus_on_first_present(c);

    ash_node *n = c->tree->last_node;
    n->kind = C_MODAL;
    if (title && *title) {
        size_t len = strlen(title);
        char *buf = ash_array(c->tui->arena_next, char, len + 3);
        buf[0] = ' ';
        memcpy(buf + 1, title, len);
        buf[len + 1] = ' ';
        buf[len + 2] = 0;
        n->title = buf;
    }
    c->last_modal = n;
}

int ash_modal_end(ash_ctx *c)
{
    ash_block_end(c);
    if (node_subtree_focused(c->tui, c->tree->last_node)) {
        int exit = !c->input_consumed && c->input_key == ASH_TK_ESCAPE;
        c->input_consumed = 1;
        return exit;
    }
    return 0;
}

void ash_textarea_widget(ash_ctx *c, const char *classname, ash_textarea *ta)
{
    ash_block_begin(c, classname);
    ash_node *n = c->tree->last_node;
    n->kind = C_TEXTAREA;
    n->ta = ta;
    n->focusable = 1;
    n->is_w = ta->width > 0 ? ta->width : 20;
    n->is_h = ash_textarea_height(ta);
    n->has_intrinsic = 1;

    if (node_is_focused(c->tui, n->id) && c->ev && !c->input_consumed) {
        ash_key k = ash_key_map(c->ev);
        if (k.cmd != ASH_EC_NONE) {
            ash_textarea_apply(ta, k);
            c->input_consumed = 1;
            c->needs_settling = 1;
        }
    }
    ash_block_end(c);
}

static int border_lb(const ash_node *n)
{
    return n->bordered ? 1 : 0;
}

static int border_r(const ash_node *n)
{
    return (n->bordered || n->kind == C_SCROLL) ? 1 : 0;
}

static ash_rect outer_to_inner(const ash_node *n, ash_rect o)
{
    int l = n->pad.left + border_lb(n);
    int t = n->pad.top + border_lb(n);
    int r = n->pad.right + border_r(n);
    int b = n->pad.bottom + border_lb(n);
    ash_rect in = { o.x + l, o.y + t, o.w - l - r, o.h - t - b };
    if (in.w < 0)
        in.w = 0;
    if (in.h < 0)
        in.h = 0;
    return in;
}

static void intrinsic_to_outer(const ash_node *n, int *w, int *h)
{
    *w = n->is_w + n->pad.left + n->pad.right + border_lb(n) + border_r(n);
    *h = n->is_h + n->pad.top + n->pad.bottom + 2 * border_lb(n);
}

static void compute_intrinsic(ash_node *n)
{
    int maxw = 0;
    int totalh = 0;
    for (ash_node *ch = n->child_first; ch; ch = ch->sib_next) {
        compute_intrinsic(ch);
        int ow, oh;
        intrinsic_to_outer(ch, &ow, &oh);
        if (ow > maxw)
            maxw = ow;
        totalh += oh;
    }
    if (!n->has_intrinsic) {
        n->is_w = maxw;
        n->is_h = totalh;
        n->has_intrinsic = 1;
    }
}

static void layout_children(ash_node *n, ash_rect clip)
{
    if (!n->child_first || rect_empty(n->inner))
        return;

    if (n->kind == C_SCROLL) {
        ash_node *content = n->child_first;
        int sy = n->inner.h;
        int cy = content->is_h > sy ? content->is_h : sy;
        int oy = n->scroll_y;
        if (oy > cy - sy)
            oy = cy - sy;
        if (oy < 0)
            oy = 0;
        n->scroll_y = oy;
        content->outer.x = n->inner.x;
        content->outer.y = n->inner.y - oy;
        content->outer.w = n->inner.w;
        content->outer.h = cy;
        content->inner = outer_to_inner(content, content->outer);
        content->outer_clip = rect_intersect(content->outer, n->inner_clip);
        content->inner_clip = rect_intersect(content->inner, n->inner_clip);
        layout_children(content, content->inner_clip);
        return;
    }

    int width = n->inner.w;
    int x = n->inner.x;
    int y = n->inner.y;
    for (ash_node *ch = n->child_first; ch; ch = ch->sib_next) {
        int ow, oh;
        intrinsic_to_outer(ch, &ow, &oh);
        int remaining = width - ow;
        if (remaining < 0)
            remaining = 0;
        int offx;
        int cw;
        switch (ch->position) {
        case ASH_POS_STRETCH:
            offx = 0;
            cw = width;
            break;
        case ASH_POS_LEFT:
            offx = 0;
            cw = ow;
            break;
        case ASH_POS_CENTER:
            offx = remaining / 2;
            cw = ow;
            break;
        case ASH_POS_RIGHT:
            offx = remaining;
            cw = ow;
            break;
        default:
            offx = 0;
            cw = width;
            break;
        }
        ch->outer.x = x + offx;
        ch->outer.y = y;
        ch->outer.w = cw;
        ch->outer.h = oh;
        ch->outer = rect_intersect(ch->outer, n->inner);
        ch->inner = outer_to_inner(ch, ch->outer);
        ch->outer_clip = rect_intersect(ch->outer, clip);
        ch->inner_clip = rect_intersect(ch->inner, clip);
        y += oh;
    }
    for (ash_node *ch = n->child_first; ch; ch = ch->sib_next)
        layout_children(ch, clip);
}

static ash_style eff_style(const ash_node *n, ash_style parent)
{
    ash_style s;
    s.bg = n->bg ? n->bg : parent.bg;
    s.fg = n->fg ? n->fg : parent.fg;
    s.attr = (uint16_t)(parent.attr | (n->reverse ? ASH_ATTR_REVERSE : 0));
    return s;
}

static void draw_border(ash_fb *fb, ash_rect r, ash_style st)
{
    if (r.w < 2 || r.h < 1)
        return;
    int x0 = r.x, y0 = r.y, x1 = r.x + r.w - 1, y1 = r.y + r.h - 1;
    ash_fb_put_text(fb, x0, y0, st, "\xE2\x94\x8C", 3);
    ash_fb_put_text(fb, x1, y0, st, "\xE2\x94\x90", 3);
    ash_fb_put_text(fb, x0, y1, st, "\xE2\x94\x94", 3);
    ash_fb_put_text(fb, x1, y1, st, "\xE2\x94\x98", 3);
    for (int x = x0 + 1; x < x1; x++) {
        ash_fb_put_text(fb, x, y0, st, "\xE2\x94\x80", 3);
        ash_fb_put_text(fb, x, y1, st, "\xE2\x94\x80", 3);
    }
    for (int y = y0 + 1; y < y1; y++) {
        ash_fb_put_text(fb, x0, y, st, "\xE2\x94\x82", 3);
        ash_fb_put_text(fb, x1, y, st, "\xE2\x94\x82", 3);
    }
}

static void draw_scrollbar(ash_fb *fb, ash_rect track, ash_style st,
                           int offset, int content_total)
{
    int th = track.h;
    if (th <= 0)
        return;
    int thumb = content_total > th
                    ? (th * th + content_total - 1) / content_total
                    : th;
    if (thumb < 1)
        thumb = 1;
    if (thumb > th)
        thumb = th;
    int span = content_total - th;
    int trackable = th - thumb;
    int pos = 0;
    if (span > 0 && trackable > 0) {
        pos = offset * trackable / span;
        if (pos < 0)
            pos = 0;
        if (pos > trackable)
            pos = trackable;
    }
    for (int i = 0; i < th; i++) {
        int in_thumb = i >= pos && i < pos + thumb;
        ash_fb_put_text(fb, track.x, track.y + i, st,
                        in_thumb ? "\xE2\x96\x88" : "\xE2\x96\x91", 3);
    }
}

static void render_text(ash_fb *fb, const ash_node *n, ash_style eff)
{
    const char *text = (const char *)n->text.data;
    size_t len = n->text.len;
    int x = n->inner.x;
    int y = n->inner.y;

    size_t prev_off = 0;
    ash_rgba cur_fg = eff.fg;
    uint16_t cur_attr = eff.attr;

    for (int i = 0; i <= n->nchunks; i++) {
        size_t end = i < n->nchunks ? n->chunks[i].offset : len;
        if (end > prev_off) {
            ash_style st = { cur_fg, eff.bg, cur_attr };
            ash_fb_put_text(fb, x, y, st, text + prev_off, end - prev_off);
            x += disp_width(text + prev_off, end - prev_off);
        }
        if (i < n->nchunks) {
            cur_fg = n->chunks[i].fg ? n->chunks[i].fg : eff.fg;
            cur_attr = (uint16_t)(eff.attr | n->chunks[i].attr);
            prev_off = n->chunks[i].offset;
        }
    }
}

static void render_node(ash_fb *fb, ash_node *n, ash_style parent)
{
    if (rect_empty(n->outer_clip))
        return;
    ash_style eff = eff_style(n, parent);

    ash_style fill = { eff.fg, eff.bg, eff.attr };
    ash_fb_fill_rect(fb, n->outer_clip, fill, ' ');

    if (n->bordered)
        draw_border(fb, n->outer_clip, eff);

    if (n->kind == C_MODAL && n->title) {
        ash_rect t = { n->outer.x + 2, n->outer.y, n->outer.w - 3, 1 };
        if (t.w > 0 && !rect_empty(n->outer_clip)) {
            int savetop = fb->clip_top;
            if (ash_fb_clip_push(fb, t)) {
                ash_fb_put_text(fb, t.x, t.y, eff, n->title, strlen(n->title));
                ash_fb_clip_pop(fb);
            }
            fb->clip_top = savetop;
        }
    }

    if (!rect_empty(n->inner_clip)) {
        int savetop = fb->clip_top;
        if (ash_fb_clip_push(fb, n->inner_clip)) {
            if (n->kind == C_TEXT)
                render_text(fb, n, eff);
            else if (n->kind == C_TEXTAREA && n->ta)
                ash_textarea_render(n->ta, fb, n->inner_clip, eff);
            ash_fb_clip_pop(fb);
        }
        fb->clip_top = savetop;
    }

    if (n->kind == C_SCROLL && n->child_first) {
        ash_rect track = { n->inner.x + n->inner.w, n->inner.y, 1, n->inner.h };
        draw_scrollbar(fb, track, eff, n->scroll_y, n->child_first->is_h);
    }

    for (ash_node *ch = n->child_first; ch; ch = ch->sib_next)
        render_node(fb, ch, eff);
}

ash_status ash_tui_init(ash_tui *t)
{
    memset(t, 0, sizeof *t);
    ASH_TRY(ash_arena_create(&t->arena_a, "tui-a", 1u << 16));
    ash_status st = ash_arena_create(&t->arena_b, "tui-b", 1u << 16);
    if (st != ASH_OK) {
        ash_arena_destroy(&t->arena_a);
        return st;
    }
    t->arena_prev = &t->arena_a;
    t->arena_next = &t->arena_b;
    t->focus_path[0] = ROOT_ID;
    t->focus_len = 1;
    t->focus_for_scroll = ROOT_ID;
    t->settling_have = 0;
    t->settling_want = 1;
    return ASH_OK;
}

void ash_tui_destroy(ash_tui *t)
{
    ash_arena_destroy(&t->arena_a);
    ash_arena_destroy(&t->arena_b);
}

int ash_tui_settling(const ash_tui *t)
{
    return t->settling_have <= t->settling_want;
}

ash_ctx *ash_tui_begin(ash_tui *t, int w, int h, const ash_input_event *ev)
{
    ash_arena *tmp = t->arena_prev;
    t->arena_prev = t->arena_next;
    t->arena_next = tmp;
    ash_arena_reset(t->arena_next);

    t->w = w;
    t->h = h;

    int no_input = ev == NULL || ev->kind == ASH_EV_NONE;
    int input_consumed = ash_tui_settling(t) && no_input;
    if (!input_consumed) {
        t->settling_have = 0;
        t->settling_want = 1;
    }

    ash_ctx *c = ash_new(t->arena_next, ash_ctx);
    memset(c, 0, sizeof *c);
    c->tui = t;
    c->tree = tree_new(t->arena_next);
    c->input_consumed = input_consumed;
    c->ev = ev;

    if (ev && ev->kind == ASH_EV_KEY) {
        c->input_key = ev->key;
        c->input_mods = ev->mods;
    } else if (ev && ev->kind == ASH_EV_TEXT) {
        c->input_text = ev->text;
        c->input_text_len = ev->len;
        if (ev->len == 1) {
            c->input_key = (uint8_t)ev->text[0];
            c->input_mods = 0;
        }
    }

    t->ctx = c;
    return c;
}

void ash_tui_end(ash_ctx *c)
{
    ash_tui *t = c->tui;

    ash_block_end(c);

    if (c->last_modal && !node_subtree_focused(t, c->last_modal))
        steal_focus_for(c, c->last_modal);

    int needs = c->needs_settling;
    needs |= (t->prev_checksum != c->tree->checksum);

    t->prev_tree = c->tree;
    t->prev_map = map_new(t->arena_next, c->tree);
    t->prev_checksum = c->tree->checksum;

    int pop_min = 0;
    if (!c->input_consumed && c->input_key == ASH_TK_ESCAPE) {
        pop_min = 1;
        c->input_consumed = 1;
    }
    needs |= pop_focusable(t, pop_min);

    t->settling_have += 1;
    if (needs)
        t->settling_want = (t->settling_have + 1 < 20) ? t->settling_have + 1 : 20;

    ash_rect viewport = { 0, 0, t->w, t->h };
    for (ash_node *root = t->prev_tree->root_first; root; root = root->sib_next)
        compute_intrinsic(root);

    for (ash_node *root = t->prev_tree->root_first; root; root = root->sib_next) {
        if (root->has_float) {
            int x = 0, y = 0;
            if (root->parent) {
                x = root->parent->outer.x;
                y = root->parent->outer.y;
            }
            int ow, oh;
            intrinsic_to_outer(root, &ow, &oh);
            x += (int)(root->fl_ox - root->fl_gx * (float)ow);
            y += (int)(root->fl_oy - root->fl_gy * (float)oh);
            ash_rect o = { x, y, ow, oh };
            root->outer = rect_intersect(o, viewport);
        } else {
            root->outer = viewport;
        }
        root->inner = outer_to_inner(root, root->outer);
        root->outer_clip = root->outer;
        root->inner_clip = root->inner;
        layout_children(root, root->outer);
    }
}

void ash_tui_render(ash_tui *t, ash_fb *fb)
{
    if (!t->prev_tree)
        return;
    ash_style base = { ASH_RGBA_DEFAULT, ASH_RGBA_DEFAULT, 0 };
    for (ash_node *root = t->prev_tree->root_first; root; root = root->sib_next)
        render_node(fb, root, base);
}
