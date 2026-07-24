#include "ash/ui/footer.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

enum { FOOTER_BAR_W = 12 };

static void copy_str(char *dst, size_t cap, const char *src)
{
    if (cap == 0)
        return;
    if (src == NULL) {
        dst[0] = 0;
        return;
    }
    size_t n = strlen(src);
    if (n >= cap)
        n = cap - 1;
    memcpy(dst, src, n);
    dst[n] = 0;
}

static void appendf(char *buf, size_t cap, size_t *len, const char *fmt, ...)
{
    if (*len >= cap)
        return;
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf + *len, cap - *len, fmt, ap);
    va_end(ap);
    if (n < 0)
        return;
    *len += (size_t)n < cap - *len ? (size_t)n : cap - *len - 1;
}

void ash_footer_init(ash_footer *f)
{
    memset(f, 0, sizeof *f);
    f->debounce_ms = 500;
    f->style.fg = ASH_RGBA_DEFAULT;
    f->style.bg = ASH_RGBA_DEFAULT;
    f->style.attr = 0;
}

void ash_footer_set_provider(ash_footer *f, const char *provider,
                             const char *model)
{
    copy_str(f->provider, sizeof f->provider, provider);
    copy_str(f->model, sizeof f->model, model);
}

void ash_footer_set_context(ash_footer *f, int64_t used, int64_t window)
{
    f->context_used = used < 0 ? 0 : used;
    f->context_window = window < 0 ? 0 : window;
}

void ash_footer_add_usage(ash_footer *f, int64_t in_delta, int64_t out_delta,
                          double cost_delta)
{
    f->in_tokens += in_delta;
    f->out_tokens += out_delta;
    f->cost_usd += cost_delta;
}

void ash_footer_set_status(ash_footer *f, const char *key, const char *text)
{
    if (key == NULL)
        return;
    for (int i = 0; i < f->ext_count; i++) {
        if (strcmp(f->ext[i].key, key) != 0)
            continue;
        if (text == NULL) {
            f->ext[i] = f->ext[f->ext_count - 1];
            f->ext_count--;
        } else {
            copy_str(f->ext[i].text, sizeof f->ext[i].text, text);
        }
        return;
    }
    if (text == NULL || f->ext_count == ASH_FOOTER_EXT_MAX)
        return;
    ash_footer_ext *e = &f->ext[f->ext_count++];
    copy_str(e->key, sizeof e->key, key);
    copy_str(e->text, sizeof e->text, text);
}

void ash_footer_clear_status(ash_footer *f)
{
    f->ext_count = 0;
}

size_t ash_footer_fmt_tokens(int64_t n, char *out, size_t cap)
{
    if (cap == 0)
        return 0;
    double v = (double)n;
    int w;
    if (n < 1000)
        w = snprintf(out, cap, "%lld", (long long)n);
    else if (n < 10000)
        w = snprintf(out, cap, "%.1fk", v / 1000.0);
    else if (n < 1000000)
        w = snprintf(out, cap, "%lldk", (long long)((v + 500.0) / 1000.0));
    else if (n < 10000000)
        w = snprintf(out, cap, "%.1fM", v / 1000000.0);
    else
        w = snprintf(out, cap, "%lldM", (long long)((v + 500000.0) / 1000000.0));
    if (w < 0) {
        out[0] = 0;
        return 0;
    }
    return (size_t)w < cap ? (size_t)w : cap - 1;
}

static int64_t stat_mtime_ns(const char *path)
{
    struct stat st;
    if (stat(path, &st) != 0)
        return -1;
    return (int64_t)st.st_mtim.tv_sec * 1000000000 + (int64_t)st.st_mtim.tv_nsec;
}

static int read_trim(const char *path, char *out, size_t cap)
{
    FILE *fp = fopen(path, "rb");
    if (fp == NULL)
        return 0;
    size_t n = fread(out, 1, cap - 1, fp);
    fclose(fp);
    out[n] = 0;
    while (n > 0 && (out[n - 1] == '\n' || out[n - 1] == '\r' ||
                     out[n - 1] == ' ' || out[n - 1] == '\t'))
        out[--n] = 0;
    return 1;
}

static int path_exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0;
}

int ash_footer_git_init(ash_footer *f, const char *cwd)
{
    f->git_ready = 0;
    f->has_polled = 0;
    f->have_branch = 0;
    f->branch[0] = 0;
    f->head_path[0] = 0;
    if (cwd == NULL)
        return 0;

    char dir[ASH_FOOTER_PATH_MAX];
    copy_str(dir, sizeof dir, cwd);

    for (;;) {
        char gitpath[ASH_FOOTER_PATH_MAX];
        int gn = snprintf(gitpath, sizeof gitpath, "%s/.git", dir);
        if (gn < 0 || (size_t)gn >= sizeof gitpath)
            return 0;

        struct stat st;
        if (stat(gitpath, &st) == 0) {
            if (S_ISDIR(st.st_mode)) {
                int hn = snprintf(f->head_path, sizeof f->head_path,
                                  "%s/HEAD", gitpath);
                if (hn > 0 && (size_t)hn < sizeof f->head_path &&
                    path_exists(f->head_path)) {
                    f->git_ready = 1;
                    return 1;
                }
                return 0;
            }
            char content[ASH_FOOTER_PATH_MAX];
            if (read_trim(gitpath, content, sizeof content) &&
                strncmp(content, "gitdir: ", 8) == 0) {
                const char *gd = content + 8;
                int hn;
                if (gd[0] == '/')
                    hn = snprintf(f->head_path, sizeof f->head_path,
                                  "%s/HEAD", gd);
                else
                    hn = snprintf(f->head_path, sizeof f->head_path,
                                  "%s/%s/HEAD", dir, gd);
                if (hn > 0 && (size_t)hn < sizeof f->head_path &&
                    path_exists(f->head_path)) {
                    f->git_ready = 1;
                    return 1;
                }
            }
            return 0;
        }

        char *slash = strrchr(dir, '/');
        if (slash == NULL || slash == dir)
            return 0;
        *slash = 0;
    }
}

int ash_footer_git_poll(ash_footer *f, int64_t now_ms)
{
    if (!f->git_ready)
        return 0;
    if (f->has_polled && now_ms - f->last_poll_ms < f->debounce_ms)
        return 0;
    f->has_polled = 1;
    f->last_poll_ms = now_ms;

    int64_t mt = stat_mtime_ns(f->head_path);
    if (mt < 0)
        return 0;
    if (f->have_branch && mt == f->head_mtime_ns)
        return 0;
    f->head_mtime_ns = mt;

    char content[ASH_FOOTER_PATH_MAX];
    if (!read_trim(f->head_path, content, sizeof content))
        return 0;

    char next[ASH_FOOTER_BRANCH_MAX];
    if (strncmp(content, "ref: refs/heads/", 16) == 0)
        copy_str(next, sizeof next, content + 16);
    else
        copy_str(next, sizeof next, "detached");

    if (f->have_branch && strcmp(next, f->branch) == 0)
        return 0;
    copy_str(f->branch, sizeof f->branch, next);
    f->have_branch = 1;
    return 1;
}

static ash_rgba level_color(double frac)
{
    if (frac < 0.70)
        return ash_rgb(0x3f, 0xb9, 0x50);
    if (frac < 0.90)
        return ash_rgb(0xd7, 0x9b, 0x1e);
    return ash_rgb(0xe7, 0x4c, 0x3c);
}

static void ext_sorted(const ash_footer *f, int *order)
{
    for (int i = 0; i < f->ext_count; i++)
        order[i] = i;
    for (int i = 1; i < f->ext_count; i++) {
        int key = order[i];
        int j = i - 1;
        while (j >= 0 && strcmp(f->ext[order[j]].key, f->ext[key].key) > 0) {
            order[j + 1] = order[j];
            j--;
        }
        order[j + 1] = key;
    }
}

static int col_width(const char *s, size_t len)
{
    int w = 0;
    size_t i = 0;
    while (i < len) {
        uint32_t cp;
        size_t a = ash_utf8_decode(s + i, len - i, &cp);
        if (a == 0)
            break;
        int cw = ash_char_width(cp);
        if (cw > 0)
            w += cw;
        i += a;
    }
    return w;
}

enum {
    SEG_MODEL = 0,
    SEG_TOKENS,
    SEG_COST,
    SEG_CTX,
    SEG_BRANCH,
    SEG_EXTS,
    SEG_COUNT
};

typedef struct footer_seg {
    int  prio;
    int  width;
    int  present;
} footer_seg;

static int draw_clipped(ash_fb *fb, int x, int y, int limit, ash_style st,
                        const char *s, size_t len)
{
    int room = limit - x;
    if (room <= 0)
        return 0;
    if (!ash_fb_clip_push(fb, (ash_rect){ x, y, room, 1 }))
        return 0;
    ash_fb_put_text(fb, x, y, st, s, len);
    ash_fb_clip_pop(fb);
    return 1;
}

void ash_footer_render(const ash_footer *f, ash_fb *fb, ash_rect r)
{
    if (r.w <= 0 || r.h <= 0)
        return;
    if (!ash_fb_clip_push(fb, r))
        return;

    ash_style base = f->style;
    ash_fb_fill_rect(fb, r, base, ' ');
    int y = r.y;

    char in_s[16], out_s[16];
    ash_footer_fmt_tokens(f->in_tokens, in_s, sizeof in_s);
    ash_footer_fmt_tokens(f->out_tokens, out_s, sizeof out_s);

    char model_s[ASH_FOOTER_NAME_MAX * 2 + 8];
    size_t mlen = 0;
    model_s[0] = 0;
    appendf(model_s, sizeof model_s, &mlen, "%s",
            f->provider[0] ? f->provider : "?");
    if (f->model[0])
        appendf(model_s, sizeof model_s, &mlen, " \xc2\xb7 %s", f->model);

    char tok_s[48];
    size_t tlen = 0;
    tok_s[0] = 0;
    appendf(tok_s, sizeof tok_s, &tlen, "\xe2\x86\x91%s \xe2\x86\x93%s",
            in_s, out_s);

    char cost_s[24];
    size_t clen = 0;
    cost_s[0] = 0;
    appendf(cost_s, sizeof cost_s, &clen, "$%.3f", f->cost_usd);

    char branch_s[ASH_FOOTER_BRANCH_MAX + 4];
    size_t blen = 0;
    branch_s[0] = 0;
    if (f->have_branch)
        appendf(branch_s, sizeof branch_s, &blen, "(%s)", f->branch);

    char exts_s[ASH_FOOTER_EXT_MAX * (ASH_FOOTER_EXT_TEXT + 4)];
    size_t elen = 0;
    exts_s[0] = 0;
    int order[ASH_FOOTER_EXT_MAX];
    ext_sorted(f, order);
    for (int i = 0; i < f->ext_count; i++)
        appendf(exts_s, sizeof exts_s, &elen, i == 0 ? "%s" : " \xc2\xb7 %s",
                f->ext[order[i]].text);

    double frac = 0.0;
    if (f->context_window > 0) {
        frac = (double)f->context_used / (double)f->context_window;
        if (frac < 0.0)
            frac = 0.0;
        if (frac > 1.0)
            frac = 1.0;
    }

    char ub[16], wb[16];
    ash_footer_fmt_tokens(f->context_used, ub, sizeof ub);
    ash_footer_fmt_tokens(f->context_window, wb, sizeof wb);
    char usedwin[40];
    int uwlen;
    if (f->context_window > 0)
        uwlen = snprintf(usedwin, sizeof usedwin, "%s/%s", ub, wb);
    else
        uwlen = snprintf(usedwin, sizeof usedwin, "%s/?", ub);
    if (uwlen < 0)
        uwlen = 0;

    char pctbuf[8];
    int pctlen;
    if (f->context_window > 0) {
        int pct = (int)(frac * 100.0 + 0.5);
        pctlen = snprintf(pctbuf, sizeof pctbuf, "%d%%", pct);
    } else {
        pctbuf[0] = '?';
        pctbuf[1] = 0;
        pctlen = 1;
    }
    if (pctlen < 0)
        pctlen = 0;

    int bar_w = FOOTER_BAR_W;
    int ctx_w = uwlen + 1 + bar_w;

    footer_seg seg[SEG_COUNT];
    seg[SEG_MODEL]  = (footer_seg){ 100, col_width(model_s, mlen), 1 };
    seg[SEG_TOKENS] = (footer_seg){ 40, col_width(tok_s, tlen), 1 };
    seg[SEG_COST]   = (footer_seg){ 30, col_width(cost_s, clen), 1 };
    seg[SEG_CTX]    = (footer_seg){ 50, ctx_w, 1 };
    seg[SEG_BRANCH] = (footer_seg){ 20, col_width(branch_s, blen),
                                    f->have_branch };
    seg[SEG_EXTS]   = (footer_seg){ 10, col_width(exts_s, elen),
                                    f->ext_count > 0 };

    static const int show_order[SEG_COUNT] = {
        SEG_MODEL, SEG_TOKENS, SEG_COST, SEG_CTX, SEG_BRANCH, SEG_EXTS
    };

    int avail = r.w - 1;
    for (;;) {
        int need = 0, first = 1;
        for (int i = 0; i < SEG_COUNT; i++) {
            footer_seg *s = &seg[show_order[i]];
            if (!s->present)
                continue;
            need += first ? 0 : 2;
            need += s->width;
            first = 0;
        }
        if (need <= avail)
            break;
        int victim = -1, worst = 1000;
        for (int i = 0; i < SEG_COUNT; i++) {
            footer_seg *s = &seg[show_order[i]];
            if (s->present && s->prio < 100 && s->prio < worst) {
                worst = s->prio;
                victim = show_order[i];
            }
        }
        if (victim < 0)
            break;
        seg[victim].present = 0;
    }

    ash_rgba lvl = level_color(frac);
    ash_rgba track = ash_rgb(0x33, 0x33, 0x33);
    int filled = (int)(frac * (double)bar_w + 0.5);
    if (filled < 0)
        filled = 0;
    if (filled > bar_w)
        filled = bar_w;

    ash_style st_dim = base;
    st_dim.fg = ash_rgb(0x88, 0x88, 0x88);
    ash_style st_val = base;
    st_val.fg = ASH_RGBA_DEFAULT;
    ash_style st_green = base;
    st_green.fg = ash_rgb(0x3f, 0xb9, 0x50);

    int x = r.x + 1;
    int limit = r.x + r.w;
    int first = 1;
    for (int i = 0; i < SEG_COUNT; i++) {
        int kind = show_order[i];
        footer_seg *s = &seg[kind];
        if (!s->present)
            continue;
        if (!first) {
            draw_clipped(fb, x, y, limit, base, "  ", 2);
            x += 2;
        }
        first = 0;

        if (kind == SEG_CTX) {
            draw_clipped(fb, x, y, limit, st_val, usedwin, (size_t)uwlen);
            int bar_x = x + uwlen + 1;
            for (int k = 0; k < bar_w; k++) {
                int col = bar_x + k;
                if (col >= limit)
                    break;
                ash_rgba bg = k < filled ? lvl : track;
                ash_style bs = { ash_fb_contrasted(fb, bg), bg, 0 };
                ash_fb_fill_rect(fb, (ash_rect){ col, y, 1, 1 }, bs, ' ');
            }
            int pstart = bar_x + (bar_w - pctlen) / 2;
            for (int k = 0; k < pctlen; k++) {
                int col = pstart + k;
                if (col < bar_x || col >= bar_x + bar_w || col >= limit)
                    continue;
                ash_rgba bg = (col - bar_x) < filled ? lvl : track;
                ash_style bs = { ash_fb_contrasted(fb, bg), bg, 0 };
                ash_fb_put_text(fb, col, y, bs, &pctbuf[k], 1);
            }
        } else if (kind == SEG_TOKENS) {
            int lx = x;
            draw_clipped(fb, lx, y, limit, st_dim, "\xe2\x86\x91", 3);
            lx += 1;
            size_t inl = strlen(in_s);
            draw_clipped(fb, lx, y, limit, st_val, in_s, inl);
            lx += (int)inl;
            draw_clipped(fb, lx, y, limit, base, " ", 1);
            lx += 1;
            draw_clipped(fb, lx, y, limit, st_dim, "\xe2\x86\x93", 3);
            lx += 1;
            draw_clipped(fb, lx, y, limit, st_val, out_s, strlen(out_s));
        } else {
            ash_style st = base;
            const char *txt = model_s;
            size_t len = mlen;
            switch (kind) {
            case SEG_COST:   txt = cost_s;   len = clen; st = st_green; break;
            case SEG_BRANCH: txt = branch_s; len = blen; st = st_green; break;
            case SEG_EXTS:   txt = exts_s;   len = elen; break;
            default: break;
            }
            draw_clipped(fb, x, y, limit, st, txt, len);
        }
        x += s->width;
    }

    ash_fb_clip_pop(fb);
}
