#include "ktuilib.h"

#define PW 72
#define PH 17
static unsigned char canvas[PW * PH], backup[PW * PH];
static int load(const char* path) {
    unsigned char file[8 + PW * PH];
    if (syscall(2, (u32)path, 0, (u32)file, sizeof(file)) != sizeof(file)) return 0;
    if (file[0]!='K' || file[1]!='P' || file[2]!='T' || file[3]!='1' || file[4]!=PW || file[5]!=PH) return 0;
    for (unsigned i = 0; i < sizeof(canvas); ++i) if (file[8+i] > 15) return 0;
    for (unsigned i = 0; i < sizeof(canvas); ++i) canvas[i] = file[8+i];
    return 1;
}
static void snapshot(void) { for (unsigned i = 0; i < sizeof(canvas); ++i) backup[i] = canvas[i]; }
static void stroke(int x0, int y0, int x1, int y1, unsigned color) {
    int dx = x1 > x0 ? x1 - x0 : x0 - x1, sx = x0 < x1 ? 1 : -1;
    int dy = y1 > y0 ? y0 - y1 : y1 - y0, sy = y0 < y1 ? 1 : -1, error = dx + dy;
    for (;;) {
        if (x0 >= 0 && x0 < PW && y0 >= 0 && y0 < PH) canvas[y0 * PW + x0] = color;
        if (x0 == x1 && y0 == y1) break;
        int twice = error * 2;
        if (twice >= dy) { error += dy; x0 += sx; }
        if (twice <= dx) { error += dx; y0 += sy; }
    }
}
int main(int argc, char** argv) {
    char path[256] = "/HOME/PAINT.KPT";
    if (argc > 1) tui_copy(path, argv[1], sizeof(path));
    int x = 0, y = 0, px = 0, py = 0, dragging = 0, dirty = 0, pen = 0;
    unsigned color = 10;
    const char* status = "Mouse: left draw, right erase | Arrows move | Space paint | P continuous pen";
    for (unsigned i = 0; i < sizeof(canvas); ++i) canvas[i] = 0;
    if (argc > 1) status = load(path) ? "Drawing loaded" : "Cannot load drawing";
    snapshot();
    for (;;) {
        tui_clear(TUI_DESKTOP); tui_bar(0, "KPaint - Kintsugi Paint | cell-art canvas 72x17");
        tui_box(3, 2, PW + 2, PH + 2, path, TUI_BORDER);
        for (int row = 0; row < PH; ++row)
            for (int col = 0; col < PW; ++col)
                tui_sample(4 + col, 3 + row, " ", canvas[row * PW + col] << 4);
        for (unsigned i = 0; i < 16; ++i) {
            tui_sample(4 + i * 4, 21, "    ", i << 4);
            if (i == color) tui_sample(4 + i * 4, 21, " [] ", (i << 4) | (i < 8 ? 15 : 0));
        }
        tui_bar(23, status); tui_bar(24, "S save O open N clear U undo +/- color | Q quit | UTF-8-safe TUI / ring3");
        tui_present(4 + x, 3 + y);
        tui_event e; tui_event_read(&e, 1);
        int key = e.key;
        if (key == 'q' || key == 27) { if (!dirty || tui_confirm("Discard unsaved drawing?")) return 0; }
        if (key == TUI_LEFT && x) --x;
        if (key == TUI_RIGHT && x < PW - 1) ++x;
        if (key == TUI_UP && y) --y;
        if (key == TUI_DOWN && y < PH - 1) ++y;
        if (key == '+' || key == '=') color = (color + 1) & 15;
        if (key == '-') color = (color + 15) & 15;
        if (key == 'p') pen = !pen;
        if (e.pressed & 1 && tui_hit(4, 21, 64, 1, &e)) color = (e.mouse_x - 4) / 4;
        int painting = (e.buttons & 3) && tui_hit(4, 3, PW, PH, &e);
        if (painting) {
            x = e.mouse_x - 4; y = e.mouse_y - 3;
            if (!dragging) { snapshot(); px = x; py = y; }
            stroke(px, py, x, y, e.buttons & 2 ? 0 : color);
            px = x; py = y; dirty = 1;
        } else if (key == ' ' || (pen && key >= TUI_UP && key <= TUI_RIGHT)) {
            snapshot(); canvas[y * PW + x] = color; dirty = 1;
        }
        dragging = painting;
        if (key == 'u') { for (unsigned i = 0; i < sizeof(canvas); ++i) { unsigned char v = canvas[i]; canvas[i] = backup[i]; backup[i] = v; } dirty = 1; }
        if (key == 'n' && tui_confirm("Clear canvas?")) { snapshot(); for (unsigned i = 0; i < sizeof(canvas); ++i) canvas[i] = 0; dirty = 1; }
        if (key == 's' && tui_prompt("Save KPT cell-art file:", path, sizeof(path))) {
            unsigned char file[8 + PW * PH];
            file[0]='K'; file[1]='P'; file[2]='T'; file[3]='1'; file[4]=PW; file[5]=PH; file[6]=file[7]=0;
            for (unsigned i = 0; i < sizeof(canvas); ++i) file[8+i] = canvas[i];
            int result = syscall(10, (u32)path, (u32)file, sizeof(file), 0);
            status = result ? "Save failed" : "Drawing saved"; if (!result) dirty = 0;
        }
        if (key == 'o' && (!dirty || tui_confirm("Discard drawing and open file?")) && tui_prompt("Open KPT file:", path, sizeof(path))) {
            snapshot();
            int valid = load(path);
            if (valid) dirty = 0;
            status = valid ? "Drawing loaded" : "Invalid or unavailable KPT file";
        }
    }
}
