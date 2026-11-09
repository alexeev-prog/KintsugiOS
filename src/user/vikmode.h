/* Bounded Vim-style motions/operators, shared with host tests. No terminal I/O. */
typedef struct {
    u32 count, operator_count, anchor;
    int op, visual, linewise, reg, choose_reg;
    char* registers[27];
    u32 sizes[27];
    unsigned char lines[27];
} vik_state;

static u32 vik_start(const char* t, u32 p) {
    while (p && t[p - 1] != '\n') {
        --p;
    }
    return p;
}

static u32 vik_end(const char* t, u32 n, u32 p) {
    while (p < n && t[p] != '\n') {
        ++p;
    }
    return p;
}

static int vik_class(char c) {
    if (c <= ' ') {
        return 0;
    }
    return ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_') ? 1 : 2;
}

static u32 vik_motion(const char* t, u32 n, u32 p, int key, u32 count) {
    while (count--) {
        u32 start = vik_start(t, p), end = vik_end(t, n, p), col = p - start;
        if (key == 'h' || key == TUI_LEFT) {
            if (p > start) {
                --p;
            }
        } else if (key == 'l' || key == TUI_RIGHT) {
            if (p < end) {
                ++p;
            }
        } else if (key == '0' || key == TUI_HOME) {
            p = start;
        } else if (key == '$' || key == TUI_END) {
            p = end > start ? end - 1 : end;
        } else if (key == '^') {
            p = start;
            while (p < end && (t[p] == ' ' || t[p] == '\t')) {
                ++p;
            }
        } else if (key == 'j' || key == TUI_DOWN) {
            if (end < n) {
                p = end + 1;
                end = vik_end(t, n, p);
                p += col < end - p ? col : end - p;
            }
        } else if (key == 'k' || key == TUI_UP) {
            if (start) {
                end = start - 1;
                p = vik_start(t, end);
                p += col < end - p ? col : end - p;
            }
        } else if (key == 'w') {
            int kind = p < n ? vik_class(t[p]) : 0;
            while (p < n && vik_class(t[p]) == kind) {
                ++p;
            }
            while (p < n && !vik_class(t[p])) {
                ++p;
            }
        } else if (key == 'b') {
            while (p && !vik_class(t[p - 1])) {
                --p;
            }
            int kind = p ? vik_class(t[p - 1]) : 0;
            while (p && vik_class(t[p - 1]) == kind) {
                --p;
            }
        } else if (key == 'e') {
            if (p < n) {
                ++p;
            }
            while (p < n && !vik_class(t[p])) {
                ++p;
            }
            int kind = p < n ? vik_class(t[p]) : 0;
            while (p + 1 < n && vik_class(t[p + 1]) == kind) {
                ++p;
            }
        }
    }
    return p;
}

static void vik_yank(vik_state* v, const char* t, u32 begin, u32 end, int lines) {
    char* copy = malloc(end - begin + 1);
    if (!copy) {
        return;
    }
    for (u32 i = begin; i < end; ++i) {
        copy[i - begin] = t[i];
    }
    copy[end - begin] = 0;
    free(v->registers[v->reg]);
    v->registers[v->reg] = copy;
    v->sizes[v->reg] = end - begin;
    v->lines[v->reg] = lines;
}

/* Returns 1 when consumed; ordinary insert/Ex/search remain in the editor. */
static int vik_key(vik_state* v, char* t, u32* n, u32* p, int key, int readonly, int* insert, int* dirty) {
    if (key == 27) {
        v->count = v->operator_count = 0;
        v->op = v->visual = v->choose_reg = 0;
        return 1;
    }
    if (v->choose_reg) {
        v->reg = key >= 'a' && key <= 'z' ? key - 'a' + 1 : 0;
        v->choose_reg = 0;
        return 1;
    }
    if (key == '"') {
        v->choose_reg = 1;
        return 1;
    }
    if (key >= '0' && key <= '9' && (key != '0' || v->count)) {
        if (v->count < 1000) {
            v->count = v->count * 10 + key - '0';
        }
        return 1;
    }
    u32 count = v->count ? v->count : 1;
    int explicit_count = v->count != 0;
    v->count = 0;
    if (key == 'v' || key == 'V') {
        v->visual = !v->visual;
        v->linewise = key == 'V';
        v->anchor = *p;
        return 1;
    }
    if ((key == 'd' || key == 'c' || key == 'y') && !v->op && !v->visual) {
        v->op = key;
        v->operator_count = count;
        return 1;
    }
    u32 begin = *p, end = *p;
    int lines = 0, op = v->op;
    if (v->visual && (key == 'd' || key == 'c' || key == 'y' || key == 'x')) {
        begin = *p < v->anchor ? *p : v->anchor;
        end = *p > v->anchor ? *p : v->anchor;
        if (end < *n) {
            ++end;
        }
        lines = v->linewise;
        op = key == 'x' ? 'd' : key;
        if (lines) {
            begin = vik_start(t, begin);
            end = vik_end(t, *n, end ? end - 1 : 0);
            if (end < *n) {
                ++end;
            }
        }
    } else if (op && key == op) {
        lines = 1;
        begin = vik_start(t, *p);
        end = begin;
        count *= v->operator_count;
        while (count--) {
            end = vik_end(t, *n, end);
            if (end < *n) {
                ++end;
            }
        }
    } else if (key == 'p' || key == 'P') {
        u32 size = v->sizes[v->reg];
        if (!readonly && size && count <= (CAPACITY - *n) / size) {
            remember(t, *n, *p);
            u32 at = *p;
            if (v->lines[v->reg]) {
                at = key == 'P' ? vik_start(t, at) : vik_end(t, *n, at);
                if (key == 'p' && at < *n) {
                    ++at;
                }
            } else if (key == 'p' && at < *n && t[at] != '\n') {
                ++at;
            }
            u32 separator = v->lines[v->reg] && key == 'p' && at == *n && at && t[at - 1] != '\n';
            u32 total = size * count + separator;
            if (total > CAPACITY - *n) {
                return 1;
            }
            for (u32 i = *n; i > at; --i) {
                t[i + total - 1] = t[i - 1];
            }
            if (separator) {
                t[at] = '\n';
            }
            for (u32 i = separator; i < total; ++i) {
                t[at + i] = v->registers[v->reg][(i - separator) % size];
            }
            *n += total;
            *p = at;
            *dirty = 1;
        }
        v->reg = 0;
        return 1;
    } else if (key == 'x' || key == 'D' || key == 'C') {
        op = key == 'C' ? 'c' : 'd';
        end = key == 'x' ? *p + (count < *n - *p ? count : *n - *p) : vik_end(t, *n, *p);
    } else if (key == 'G') {
        end = explicit_count ? 0 : vik_start(t, *n);
        if (explicit_count) {
            while (--count && end < *n) {
                end = vik_end(t, *n, end);
                if (end < *n) {
                    ++end;
                }
            }
        }
        if (!op) {
            *p = end;
            return 1;
        }
    } else if (
        key == 'h' || key == 'j' || key == 'k' || key == 'l' || key == 'w' || key == 'b' || key == 'e'
        || key == '0' || key == '$' || key == '^' || (key >= TUI_UP && key <= TUI_END)) {
        end = vik_motion(t, *n, *p, key, count * (op ? v->operator_count : 1));
        if (!op) {
            *p = end;
            return 1;
        }
        if ((key == '$' || key == 'e') && end < *n) {
            ++end;
        }
        if (end < begin) {
            u32 swap = begin;
            begin = end;
            end = swap;
        }
        if (key == 'j' || key == 'k' || key == TUI_UP || key == TUI_DOWN) {
            lines = 1;
            begin = vik_start(t, begin);
            end = vik_end(t, *n, end);
            if (end < *n) {
                ++end;
            }
        }
    } else {
        v->op = 0;
        return 0;
    }
    if (op) {
        if (!readonly || op == 'y') {
            vik_yank(v, t, begin, end, lines);
            if (op != 'y' && end > begin) {
                remember(t, *n, *p);
                for (u32 i = end; i < *n; ++i) {
                    t[begin + i - end] = t[i];
                }
                *n -= end - begin;
                *p = begin;
                *dirty = 1;
            }
            if (op == 'c') {
                *insert = 1;
            }
        }
        v->op = v->visual = v->reg = 0;
        return 1;
    }
    return 0;
}
