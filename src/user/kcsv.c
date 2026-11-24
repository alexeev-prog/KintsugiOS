#include "ktuilib.h"

/* KCSV - interactive CSV table editor.
 *
 * The grid is the primary interface: arrow keys move, Enter edits a cell,
 * a/i/d and A/I/D insert or delete rows and columns, 's' sorts the data body,
 * and w/W write the table back to disk with RFC 4180 quoting. */

#define MAX_ROWS 128
#define MAX_COLS 12
#define FIELD 40
#define SAVE_MAX 65536
#define VISIBLE_COLS 7
#define VISIBLE_ROWS 15
#define COL_WIDTH 10

static char table[MAX_ROWS][MAX_COLS][FIELD];
static char output[SAVE_MAX];
static char path[256];
static u32 rows = 1, cols = 1;
static u32 sel_row, sel_col, top_row, left_col;
static int dirty;

static void append_text(char* out, u32 capacity, u32* used, const char* text) {
    while (*text && *used + 1 < capacity) out[(*used)++] = *text++;
    out[*used] = 0;
}

static void append_number(char* out, u32 capacity, u32* used, u32 value) {
    char reversed[12];
    int n = 0;
    if (!value) {
        append_text(out, capacity, used, "0");
        return;
    }
    while (value && n < 11) {
        reversed[n++] = (char)('0' + value % 10);
        value /= 10;
    }
    while (n && *used + 1 < capacity) out[(*used)++] = reversed[--n];
    out[*used] = 0;
}

static void clear_table(void) {
    for (u32 r = 0; r < MAX_ROWS; ++r)
        for (u32 c = 0; c < MAX_COLS; ++c) table[r][c][0] = 0;
    rows = cols = 1;
    sel_row = sel_col = top_row = left_col = 0;
    dirty = 0;
}

static void append_field_char(u32 r, u32 c, char ch) {
    if (r >= MAX_ROWS || c >= MAX_COLS) return;
    u32 n = tui_len(table[r][c]);
    if (n + 1 < FIELD) {
        table[r][c][n] = ch;
        table[r][c][n + 1] = 0;
    }
}

/* RFC 4180 reader: quoted fields may contain commas, quotes (doubled) and
 * newlines. A trailing newline does not create a phantom final row. */
static void parse(const char* text, u32 length) {
    clear_table();
    u32 r = 0, c = 0, p = 0, max_cols = 1;
    int quote = 0, row_data = 0, last_newline = 0;
    while (p < length && r < MAX_ROWS) {
        char ch = text[p++];
        if (quote) {
            if (ch == '"') {
                if (p < length && text[p] == '"') {
                    append_field_char(r, c, '"');
                    ++p;
                } else {
                    quote = 0;
                }
            } else {
                append_field_char(r, c, ch);
            }
            row_data = 1;
            last_newline = 0;
            continue;
        }
        if (ch == '"') {
            quote = 1;
            row_data = 1;
            last_newline = 0;
        } else if (ch == ',') {
            if (c + 1 < MAX_COLS) ++c;
            row_data = 1;
            last_newline = 0;
        } else if (ch == '\n' || ch == '\r') {
            if (ch == '\r' && p < length && text[p] == '\n') ++p;
            if (c + 1 > max_cols) max_cols = c + 1;
            if (r + 1 < MAX_ROWS) {
                ++r;
                c = 0;
                row_data = 0;
                last_newline = 1;
            } else {
                break;
            }
        } else {
            append_field_char(r, c, ch);
            row_data = 1;
            last_newline = 0;
        }
    }
    if (c + 1 > max_cols) max_cols = c + 1;
    if (last_newline && r > 0 && !row_data) --r;
    rows = r + 1;
    if (rows > MAX_ROWS) rows = MAX_ROWS;
    cols = max_cols < MAX_COLS ? max_cols : MAX_COLS;
    if (!cols) cols = 1;
    if (!rows) rows = 1;
}

static int load(const char* file) {
    tui_stat stat;
    if (tui_stat_path(file, &stat) || stat.directory || stat.size > SAVE_MAX) return 0;
    char* text = malloc(stat.size + 1);
    if (!text) return 0;
    if (syscall(2, (u32)file, 0, (u32)text, stat.size) != (int)stat.size) {
        free(text);
        return 0;
    }
    text[stat.size] = 0;
    parse(text, stat.size);
    free(text);
    tui_copy(path, file, sizeof(path));
    dirty = 0;
    return 1;
}

static int needs_quote(const char* value) {
    for (const char* p = value; *p; ++p)
        if (*p == ',' || *p == '"' || *p == '\n' || *p == '\r') return 1;
    return 0;
}

/* Returns 1 saved, 0 nothing to write, -1 failure (status carries the reason). */
static int save(const char** status) {
    if (!path[0]) {
        *status = "No file name: press W to save as";
        return 0;
    }
    u32 used = 0;
    for (u32 r = 0; r < rows; ++r) {
        for (u32 c = 0; c < cols; ++c) {
            const char* value = table[r][c];
            int quote = needs_quote(value);
            if (quote) {
                if (used + 1 >= SAVE_MAX) goto full;
                output[used++] = '"';
            }
            for (const char* p = value; *p; ++p) {
                if (*p == '"') {
                    if (used + 2 >= SAVE_MAX) goto full;
                    output[used++] = '"';
                    output[used++] = '"';
                } else {
                    if (used + 1 >= SAVE_MAX) goto full;
                    output[used++] = *p;
                }
            }
            if (quote) {
                if (used + 1 >= SAVE_MAX) goto full;
                output[used++] = '"';
            }
            if (c + 1 < cols) {
                if (used + 1 >= SAVE_MAX) goto full;
                output[used++] = ',';
            }
        }
        if (used + 1 >= SAVE_MAX) goto full;
        output[used++] = '\n';
    }
    if (syscall(10, (u32)path, (u32)output, used, 0)) {
        *status = "Write failed: check directory permissions";
        return -1;
    }
    sync();
    dirty = 0;
    *status = "Saved";
    return 1;

full:
    *status = "Table too large for one save (64 KiB limit)";
    return -1;
}

static void ensure_visible(void) {
    if (sel_row >= 1) {
        u32 data = sel_row - 1;
        if (data < top_row) top_row = data;
        if (data >= top_row + VISIBLE_ROWS) top_row = data - VISIBLE_ROWS + 1;
    }
    if (sel_col < left_col) left_col = sel_col;
    if (sel_col >= left_col + VISIBLE_COLS) left_col = sel_col - VISIBLE_COLS + 1;
}

static void draw_cell(int x, int y, u32 r, u32 c, unsigned base) {
    int selected = (r == sel_row && c == sel_col);
    if (selected) tui_fill(x, y, COL_WIDTH - 1, 1, TUI_VISUAL);
    tui_textn(x, y, table[r][c], COL_WIDTH - 1, selected ? TUI_VISUAL : base);
}

static void draw(const char* status) {
    ensure_visible();
    tui_clear(TUI_DESKTOP);
    char title[300];
    u32 used = 0;
    title[0] = 0;
    append_text(title, sizeof(title), &used, "KCSV - ");
    append_text(title, sizeof(title), &used, path[0] ? path : "(unsaved)");
    if (dirty) append_text(title, sizeof(title), &used, " *");
    tui_bar(0, title);
    tui_box(1, 2, 78, 18, "CSV table", TUI_BORDER);
    for (u32 c = left_col; c < cols && c < left_col + VISIBLE_COLS; ++c) {
        int x = 2 + (int)(c - left_col) * COL_WIDTH;
        tui_text(x, 3, "#", (c == sel_col) ? TUI_VISUAL : TUI_KEYWORD);
        tui_number(x + 1, 3, c + 1, (c == sel_col) ? TUI_VISUAL : TUI_NUMBER);
    }
    for (u32 c = left_col; c < cols && c < left_col + VISIBLE_COLS; ++c)
        draw_cell(2 + (int)(c - left_col) * COL_WIDTH, 4, 0, c, TUI_KEYWORD);
    for (u32 slot = 0; slot < VISIBLE_ROWS; ++slot) {
        u32 r = 1 + top_row + slot;
        if (r >= rows) break;
        for (u32 c = left_col; c < cols && c < left_col + VISIBLE_COLS; ++c)
            draw_cell(2 + (int)(c - left_col) * COL_WIDTH, 5 + (int)slot, r, c, 0x1f);
    }
    char info[96];
    used = 0;
    info[0] = 0;
    append_text(info, sizeof(info), &used, "Row ");
    append_number(info, sizeof(info), &used, sel_row + 1);
    append_text(info, sizeof(info), &used, "  Column ");
    append_number(info, sizeof(info), &used, sel_col + 1);
    append_text(info, sizeof(info), &used, "   ");
    append_number(info, sizeof(info), &used, rows);
    append_text(info, sizeof(info), &used, " rows x ");
    append_number(info, sizeof(info), &used, cols);
    append_text(info, sizeof(info), &used, " cols");
    if (dirty) append_text(info, sizeof(info), &used, "   [modified]");
    tui_bar(21, info);
    tui_bar(22, status);
    tui_bar(23, "Arrows move  Enter edit  Del clear  a/i/d rows  A/I/D cols  s sort  w write  W save-as  o open  n new  q quit");
    tui_status(24);
    tui_present(79, 24);
}

static void edit_cell(const char** status) {
    char value[FIELD];
    tui_copy(value, table[sel_row][sel_col], sizeof(value));
    char label[64];
    u32 used = 0;
    label[0] = 0;
    append_text(label, sizeof(label), &used, "Row ");
    append_number(label, sizeof(label), &used, sel_row + 1);
    append_text(label, sizeof(label), &used, ", column ");
    append_number(label, sizeof(label), &used, sel_col + 1);
    append_text(label, sizeof(label), &used, ":");
    if (tui_prompt(label, value, sizeof(value))) {
        tui_copy(table[sel_row][sel_col], value, FIELD);
        dirty = 1;
        *status = "Cell updated";
    } else {
        *status = "Edit cancelled (Del clears a cell)";
    }
}

static int compare_cell(u32 a, u32 b) {
    const char* x = table[a][sel_col];
    const char* y = table[b][sel_col];
    while (*x && *x == *y) {
        ++x;
        ++y;
    }
    return (int)(unsigned char)*x - (int)(unsigned char)*y;
}

static void copy_row(u32 destination, u32 source) {
    for (u32 c = 0; c < MAX_COLS; ++c) tui_copy(table[destination][c], table[source][c], FIELD);
}

static void swap_rows(u32 a, u32 b) {
    for (u32 c = 0; c < MAX_COLS; ++c) {
        char tmp[FIELD];
        tui_copy(tmp, table[a][c], FIELD);
        tui_copy(table[a][c], table[b][c], FIELD);
        tui_copy(table[b][c], tmp, FIELD);
    }
}

static void sort_body(void) {
    /* Row 0 is the header and stays pinned. Insertion sort keeps the code
     * tiny and stable, which matters for already-ordered tables. */
    for (u32 i = 2; i < rows; ++i) {
        u32 j = i;
        while (j > 1 && compare_cell(j, j - 1) < 0) {
            swap_rows(j, j - 1);
            --j;
        }
    }
}

int main(int argc, char** argv) {
    clear_table();
    path[0] = 0;
    const char* status = "Enter edits a cell; W writes the table";
    if (argc > 1) {
        tui_stat stat;
        if (tui_stat_path(argv[1], &stat)) {
            tui_copy(path, argv[1], sizeof(path));
            dirty = 1;
            status = "New table - Enter edits, W saves";
        } else if (stat.directory || stat.size > SAVE_MAX) {
            tui_message("KCSV", "Cannot open CSV file (directory or larger than 64 KiB)");
            return 1;
        } else if (!load(argv[1])) {
            tui_message("KCSV", "Read failed");
            return 1;
        }
    }
    for (;;) {
        draw(status);
        int key = tui_key(1);
        if (key == 'q' || key == 27 || key == TUI_F10) {
            if (dirty && !tui_confirm("Discard unsaved changes?")) continue;
            return 0;
        }
        if (key == TUI_UP && sel_row) --sel_row;
        else if (key == TUI_DOWN && sel_row + 1 < rows) ++sel_row;
        else if (key == TUI_LEFT && sel_col) --sel_col;
        else if (key == TUI_RIGHT && sel_col + 1 < cols) ++sel_col;
        else if (key == TUI_HOME) sel_col = 0;
        else if (key == TUI_END) sel_col = cols - 1;
        else if (key == TUI_PAGE_UP) sel_row = sel_row > VISIBLE_ROWS ? sel_row - VISIBLE_ROWS : 0;
        else if (key == TUI_PAGE_DOWN) {
            sel_row += VISIBLE_ROWS;
            if (sel_row >= rows) sel_row = rows - 1;
        } else if (key == '\t') {
            sel_col = (sel_col + 1) % cols;
        } else if (key == '\n' || key == 'e') {
            edit_cell(&status);
        } else if (key == TUI_DELETE) {
            table[sel_row][sel_col][0] = 0;
            dirty = 1;
            status = "Cell cleared";
        } else if (key == 'a') {
            if (rows < MAX_ROWS) {
                for (u32 c = 0; c < MAX_COLS; ++c) table[rows][c][0] = 0;
                ++rows;
                sel_row = rows - 1;
                dirty = 1;
                status = "Row appended";
            } else {
                status = "Row limit reached";
            }
        } else if (key == 'i') {
            if (rows < MAX_ROWS) {
                for (u32 r = rows; r > sel_row; --r) copy_row(r, r - 1);
                for (u32 c = 0; c < MAX_COLS; ++c) table[sel_row][c][0] = 0;
                ++rows;
                dirty = 1;
                status = "Row inserted";
            } else {
                status = "Row limit reached";
            }
        } else if (key == 'd') {
            if (rows > 1) {
                for (u32 r = sel_row; r + 1 < rows; ++r) copy_row(r, r + 1);
                for (u32 c = 0; c < MAX_COLS; ++c) table[rows - 1][c][0] = 0;
                --rows;
                if (sel_row >= rows) sel_row = rows - 1;
                dirty = 1;
                status = "Row deleted";
            } else {
                status = "A table needs at least one row";
            }
        } else if (key == 'A') {
            if (cols < MAX_COLS) {
                for (u32 r = 0; r < MAX_ROWS; ++r) table[r][cols][0] = 0;
                ++cols;
                sel_col = cols - 1;
                dirty = 1;
                status = "Column appended";
            } else {
                status = "Column limit reached";
            }
        } else if (key == 'I') {
            if (cols < MAX_COLS) {
                for (u32 r = 0; r < MAX_ROWS; ++r)
                    for (u32 c = cols; c > sel_col; --c) tui_copy(table[r][c], table[r][c - 1], FIELD);
                for (u32 r = 0; r < MAX_ROWS; ++r) table[r][sel_col][0] = 0;
                ++cols;
                dirty = 1;
                status = "Column inserted";
            } else {
                status = "Column limit reached";
            }
        } else if (key == 'D') {
            if (cols > 1) {
                for (u32 r = 0; r < MAX_ROWS; ++r)
                    for (u32 c = sel_col; c + 1 < cols; ++c) tui_copy(table[r][c], table[r][c + 1], FIELD);
                for (u32 r = 0; r < MAX_ROWS; ++r) table[r][cols - 1][0] = 0;
                --cols;
                if (sel_col >= cols) sel_col = cols - 1;
                dirty = 1;
                status = "Column deleted";
            } else {
                status = "A table needs at least one column";
            }
        } else if (key == 's') {
            if (rows > 1) {
                sort_body();
                dirty = 1;
                status = "Rows sorted by the selected column";
            } else {
                status = "Add data rows before sorting";
            }
        } else if (key == 'w') {
            if (!path[0]) {
                char name[256];
                name[0] = 0;
                if (tui_prompt("Save as path:", name, sizeof(name))) tui_copy(path, name, sizeof(path));
            }
            save(&status);
        } else if (key == 'W') {
            char name[256];
            tui_copy(name, path, sizeof(name));
            if (tui_prompt("Save as path:", name, sizeof(name))) {
                tui_copy(path, name, sizeof(path));
                save(&status);
            } else {
                status = "Save cancelled";
            }
        } else if (key == 'o') {
            if (dirty && !tui_confirm("Discard unsaved changes and open another file?")) continue;
            char name[256];
            name[0] = 0;
            if (tui_prompt("Open CSV path:", name, sizeof(name))) {
                if (load(name)) status = "File opened";
                else {
                    status = "Cannot open file";
                    clear_table();
                    path[0] = 0;
                }
            }
        } else if (key == 'n') {
            if (dirty && !tui_confirm("Discard unsaved changes and start a new table?")) continue;
            char name[256];
            if (tui_prompt("New CSV path:", name, sizeof(name))) {
                clear_table();
                tui_copy(path, name, sizeof(path));
                dirty = 1;
                status = "New table - Enter edits, W saves";
            }
        }
    }
}
