/* Included by ktuilib.c: document widgets share its drawing/clipping API. */
static u32 doc_start(const char* t, u32 p) {
    while (p && t[p - 1] != '\n') {
        --p;
    }
    return p;
}

static u32 doc_end(const char* t, u32 n, u32 p) {
    while (p < n && t[p] != '\n') {
        ++p;
    }
    return p;
}

void tui_editor_destroy(tui_editor* e) {
    for (u32 i = 0; i < e->history_count; ++i) {
        free(e->history[i]);
    }
    e->history_count = e->history_position = 0;
}

static int doc_snapshot(tui_editor* e) {
    char* copy = malloc(e->length + 1);
    if (!copy) {
        return 0;
    }
    for (u32 i = 0; i <= e->length; ++i) {
        copy[i] = e->text[i];
    }
    while (e->history_count > e->history_position) {
        free(e->history[--e->history_count]);
    }
    if (e->history_count == 8) {
        free(e->history[0]);
        for (u32 i = 1; i < 8; ++i) {
            e->history[i - 1] = e->history[i];
            e->lengths[i - 1] = e->lengths[i];
            e->cursors[i - 1] = e->cursors[i];
        }
        --e->history_count;
    }
    u32 at = e->history_count++;
    e->history[at] = copy;
    e->lengths[at] = e->length;
    e->cursors[at] = e->cursor;
    e->history_position = e->history_count;
    return 1;
}

int tui_editor_key(tui_editor* e, int key) {
    if (!e || !e->text || e->length > e->capacity) {
        return -1;
    }
    if (e->cursor > e->length) {
        e->cursor = e->length;
    }
    if (key == 26 || key == 18) {
        if ((key == 26 && !e->history_position) || (key == 18 && e->history_position == e->history_count)) {
            return 0;
        }
        u32 at = key == 26 ? --e->history_position : e->history_position++;
        char* copy = malloc(e->length + 1);
        if (!copy) {
            if (key == 26) {
                ++e->history_position;
            } else {
                --e->history_position;
            }
            return -1;
        }
        for (u32 i = 0; i <= e->length; ++i) {
            copy[i] = e->text[i];
        }
        u32 n = e->length, p = e->cursor;
        e->length = e->lengths[at];
        e->cursor = e->cursors[at];
        for (u32 i = 0; i <= e->length; ++i) {
            e->text[i] = e->history[at][i];
        }
        free(e->history[at]);
        e->history[at] = copy;
        e->lengths[at] = n;
        e->cursors[at] = p;
        return 1;
    }
    if (key == TUI_LEFT) {
        if (e->cursor) {
            --e->cursor;
        }
        return 0;
    }
    if (key == TUI_RIGHT) {
        if (e->cursor < e->length) {
            ++e->cursor;
        }
        return 0;
    }
    if (key == TUI_HOME) {
        e->cursor = doc_start(e->text, e->cursor);
        return 0;
    }
    if (key == TUI_END) {
        e->cursor = doc_end(e->text, e->length, e->cursor);
        return 0;
    }
    if (key == TUI_UP || key == TUI_DOWN || key == TUI_PAGE_UP || key == TUI_PAGE_DOWN) {
        u32 col = e->cursor - doc_start(e->text, e->cursor);
        u32 steps = key == TUI_PAGE_UP || key == TUI_PAGE_DOWN ? 20 : 1;
        while (steps--) {
            u32 start = doc_start(e->text, e->cursor), end = doc_end(e->text, e->length, e->cursor);
            if (key == TUI_UP || key == TUI_PAGE_UP) {
                if (!start) {
                    break;
                }
                end = start - 1;
                start = doc_start(e->text, end);
            } else {
                if (end == e->length) {
                    break;
                }
                start = end + 1;
                end = doc_end(e->text, e->length, start);
            }
            e->cursor = start + (col < end - start ? col : end - start);
        }
        return 0;
    }
    int remove = (key == 8 && e->cursor) || (key == TUI_DELETE && e->cursor < e->length);
    u32 add = key == '\t' ? 4 : key == '\n' || (key >= 32 && key < 127) ? 1 : 0;
    if (!remove && !add) {
        return 0;
    }
    if (add > e->capacity - e->length || !doc_snapshot(e)) {
        return -1;
    }
    if (remove) {
        if (key == 8) {
            --e->cursor;
        }
        for (u32 i = e->cursor; i < e->length; ++i) {
            e->text[i] = e->text[i + 1];
        }
        --e->length;
    } else {
        for (u32 i = e->length + 1; i > e->cursor; --i) {
            e->text[i + add - 1] = e->text[i - 1];
        }
        for (u32 i = 0; i < add; ++i) {
            e->text[e->cursor++] = key == '\t' ? ' ' : key;
        }
        e->length += add;
    }
    return 1;
}

int tui_document_key(int key, u32* top, u32 total, u32 visible) {
    if (!top || !visible) {
        return 0;
    }
    u32 maximum = total > visible ? total - visible : 0, old = *top;
    if (*top > maximum) {
        *top = maximum;
    }
    if (key == TUI_HOME) {
        *top = 0;
    } else if (key == TUI_END) {
        *top = maximum;
    } else if (key == TUI_UP || key == TUI_SCROLL_UP || key == TUI_PAGE_UP) {
        u32 n = key == TUI_PAGE_UP ? visible : 1;
        *top = *top > n ? *top - n : 0;
    } else if (key == TUI_DOWN || key == TUI_SCROLL_DOWN || key == TUI_PAGE_DOWN || key == ' ') {
        u32 n = key == TUI_PAGE_DOWN || key == ' ' ? visible : 1;
        *top += n < maximum - *top ? n : maximum - *top;
    }
    return old != *top;
}

void tui_editor_draw(tui_editor* e, int x, int y, int width, int height) {
    if (width < 2 || width > 80 || height < 1 || height > 25) {
        return;
    }
    u32 row = 0, col = 0, cr = 0, cc = 0;
    for (u32 i = 0; i <= e->length; ++i) {
        if (i == e->cursor) {
            cr = row;
            cc = col;
        }
        if (i == e->length) {
            break;
        }
        if (e->text[i] == '\n' || ++col == (u32)width) {
            ++row;
            col = 0;
        }
    }
    if (cr < e->top) {
        e->top = cr;
    }
    if (cr >= e->top + height) {
        e->top = cr - height + 1;
    }
    tui_fill(x, y, width, height, 0x07);
    u32 r = 0, c = 0;
    for (u32 i = 0; i < e->length; ++i) {
        char ch = e->text[i];
        if (ch != '\n' && r >= e->top && r < e->top + height) {
            char out[2] = { ch >= 32 && ch < 127 ? ch : ' ', 0 };
            tui_text(x + c, y + r - e->top, out, 0x07);
        }
        if (ch == '\n' || ++c == (u32)width) {
            ++r;
            c = 0;
        }
    }
    char caret[2] = { e->cursor < e->length && e->text[e->cursor] >= 32 ? e->text[e->cursor] : ' ', 0 };
    tui_text(x + cc, y + cr - e->top, caret, TUI_FOCUS);
    tui_scrollbar(x + width, y, height, e->top, height, row + 1);
}

typedef struct {
    int x, y, width, height;
    u32 top, row, col;
} doc_layout;

static void doc_put(doc_layout* d, char ch, unsigned char color) {
    if (d->col == (u32)d->width) {
        ++d->row;
        d->col = 0;
    }
    if (d->row >= d->top && d->row - d->top < (u32)d->height) {
        char out[2] = { ch >= 32 && ch < 127 ? ch : ' ', 0 };
        tui_text(d->x + d->col, d->y + d->row - d->top, out, color);
    }
    ++d->col;
}

u32 tui_markdown(const char* text, u32 length, int x, int y, int width, int height, u32 top) {
    if (!text || width < 4 || width > 80 || height < 0 || height > 25) {
        return 0;
    }
    doc_layout d = { x, y, width, height, top, 0, 0 };
    if (height) {
        tui_fill(x, y, width, height, 0x07);
    }
    int fence = 0;
    for (u32 start = 0; start < length;) {
        u32 end = start;
        while (end < length && text[end] != '\n') {
            ++end;
        }
        u32 p = start;
        unsigned char base = 0x07;
        if (end - p >= 3 && text[p] == '`' && text[p + 1] == '`' && text[p + 2] == '`') {
            fence = !fence;
            base = TUI_BORDER;
            for (int i = 0; i < width; ++i) {
                doc_put(&d, '-', base);
            }
        } else {
            if (fence) {
                base = TUI_STRING;
            } else {
                u32 hashes = 0;
                while (p + hashes < end && text[p + hashes] == '#' && hashes < 6) {
                    ++hashes;
                }
                if (hashes && p + hashes < end && text[p + hashes] == ' ') {
                    p += hashes + 1;
                    base = TUI_KEYWORD;
                } else if (p < end && text[p] == '>') {
                    ++p;
                    if (p < end && text[p] == ' ') {
                        ++p;
                    }
                    base = TUI_COMMENT;
                    doc_put(&d, '|', base);
                    doc_put(&d, ' ', base);
                } else if (
                    end - p >= 2 && (text[p] == '-' || text[p] == '*' || text[p] == '+')
                    && text[p + 1] == ' ') {
                    p += 2;
                    doc_put(&d, ' ', TUI_MARKED);
                    doc_put(&d, '*', TUI_MARKED);
                    doc_put(&d, ' ', base);
                } else {
                    u32 q = p;
                    while (q < end && text[q] >= '0' && text[q] <= '9') {
                        ++q;
                    }
                    if (q > p && q + 1 < end && text[q] == '.' && text[q + 1] == ' ') {
                        doc_put(&d, ' ', base);
                        while (p <= q) {
                            doc_put(&d, text[p++], TUI_MARKED);
                        }
                        ++p;
                        doc_put(&d, ' ', base);
                    }
                }
                if (end - p >= 3 && text[p] == '-' && text[p + 1] == '-' && text[p + 2] == '-') {
                    u32 q = p;
                    while (q < end && (text[q] == '-' || text[q] == ' ' || text[q] == '\r')) {
                        ++q;
                    }
                    if (q == end) {
                        for (int i = 0; i < width; ++i) {
                            doc_put(&d, '-', TUI_BORDER);
                        }
                        p = end;
                    }
                }
            }
            int bold = 0, italic = 0, code = 0;
            while (p < end) {
                char ch = text[p];
                if (ch == '\r') {
                    ++p;
                    continue;
                }
                if (!fence && ch == '\\' && p + 1 < end) {
                    ++p;
                    ch = text[p++];
                    doc_put(&d, ch, base);
                    continue;
                }
                if (!fence && ch == '`') {
                    u32 close = p + 1;
                    while (close < end && text[close] != '`') {
                        ++close;
                    }
                    if (code || close < end) {
                        code = !code;
                        ++p;
                        continue;
                    }
                }
                if (!fence && !code && ch == '*') {
                    int strong = p + 1 < end && text[p + 1] == '*';
                    u32 close = p + (strong ? 2 : 1);
                    while (
                        close < end
                        && !(
                            text[close] == '*' && (!strong || (close + 1 < end && text[close + 1] == '*')))) {
                        ++close;
                    }
                    if ((strong ? bold : italic) || close < end) {
                        if (strong) {
                            bold = !bold;
                            p += 2;
                        } else {
                            italic = !italic;
                            ++p;
                        }
                        continue;
                    }
                }
                /* Word wrapping before a source word; long words still hard-wrap. */
                if (!fence && ch != ' ' && (p == start || text[p - 1] == ' ')) {
                    u32 word = 0;
                    while (p + word < end && text[p + word] > ' ') {
                        ++word;
                    }
                    if (word <= (u32)width && d.col && word > (u32)width - d.col) {
                        ++d.row;
                        d.col = 0;
                    }
                }
                doc_put(&d, ch, code ? TUI_STRING : bold ? TUI_KEYWORD : italic ? TUI_WARNING : base);
                ++p;
            }
        }
        ++d.row;
        d.col = 0;
        start = end < length ? end + 1 : end;
    }
    return d.row ? d.row : 1;
}
