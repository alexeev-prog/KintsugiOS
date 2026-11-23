#include "ktuilib.h"

/* KRrawl is intentionally deterministic: it is playable in QEMU and easy to
 * regression-test, while still having the important roguelike loop. */
#define W 52
#define H 16
#define MONSTERS 12
typedef struct { int x, y, hp, alive; } monster;
static unsigned seed = 0x4b1d5a7u;
static unsigned rnd(unsigned n) { seed = seed * 1664525u + 1013904223u; return n ? (seed >> 16) % n : 0; }
static char map[H][W];
static monster foes[MONSTERS];
static int px, py, hp, maxhp, mana, maxmana, level, score, xp, potions, spell_flash;
static int foe_at(int x, int y);
static int visible(int x, int y) {
    int dx = x - px, dy = y - py;
    return dx * dx + dy * dy <= 49;
}

static void carve_room(int x0, int y0, int w, int h) {
    for (int y = y0; y < y0 + h && y < H - 1; ++y)
        for (int x = x0; x < x0 + w && x < W - 1; ++x)
            if (x > 0 && y > 0) map[y][x] = '.';
}

static void generate(void) {
    for (int i = 0; i < MONSTERS; ++i) foes[i].alive = 0;
    int shape = level % 3;
    for (int y = 0; y < H; ++y) for (int x = 0; x < W; ++x) map[y][x] = '#';
    if (shape == 0) { /* connected halls and chambers */
        carve_room(2, 2, W - 5, H - 5);
        for (int x = 3; x < W - 3; x += 7) for (int y = 2; y < H - 2; ++y) map[y][x] = '.';
    } else if (shape == 1) { /* island / shrine */
        for (int y = 2; y < H - 2; ++y) for (int x = 2; x < W - 2; ++x) {
            int dx = x - W / 2, dy = y - H / 2;
            if (dx * dx * 2 + dy * dy * 8 < W * W) map[y][x] = '.';
        }
        for (int x = 3; x < W - 3; ++x) map[H / 2][x] = '.';
    } else { /* zig-zag mountain passage */
        for (int x = 2; x < W - 2; ++x) {
            int y = 2 + ((x / 6) & 1) * (H - 6);
            for (int dy = -1; dy <= 1; ++dy) if (y + dy > 0 && y + dy < H - 1) map[y + dy][x] = '.';
        }
        for (int y = 2; y < H - 2; y += 3) for (int x = 2; x < W - 2; ++x) if (map[y][x] == '.') map[y][x] = '.';
    }
    px = 3; py = 3; map[py][px] = '.';
    /* Ensure the exit is reachable by a visible corridor for every shape. */
    for (int x = 3; x <= W - 4; ++x) map[3][x] = '.';
    for (int y = 3; y <= H - 4; ++y) map[y][W - 4] = '.';
    map[H - 4][W - 4] = '>';
    for (int i = 0; i < MONSTERS; ++i) {
        do { foes[i].x = 3 + rnd(W - 6); foes[i].y = 3 + rnd(H - 6); }
        while (map[foes[i].y][foes[i].x] != '.' || foe_at(foes[i].x, foes[i].y) >= 0
               || (foes[i].x == px && foes[i].y == py));
        foes[i].hp = 2 + level / 3; foes[i].alive = 1;
    }
    hp = maxhp = 12 + level * 2;
    mana = maxmana = 5 + level;
    potions = 2 + level / 2;
}
static int foe_at(int x, int y) {
    for (int i = 0; i < MONSTERS; ++i) if (foes[i].alive && foes[i].x == x && foes[i].y == y) return i;
    return -1;
}
static void gain_xp(int amount) {
    xp += amount;
    while (xp >= level * 25) { xp -= level * 25; ++level; maxhp += 2; maxmana += 1; hp = maxhp; mana = maxmana; }
}
static void strike(int dx, int dy, int damage) {
    int i = foe_at(px + dx, py + dy);
    if (i < 0) return;
    foes[i].hp -= damage;
    if (foes[i].hp <= 0) { foes[i].alive = 0; score += 10; gain_xp(12); }
}
static void spell(void) {
    if (mana < 2) return;
    mana -= 2; spell_flash = 2;
    for (int i = 0; i < MONSTERS; ++i) if (foes[i].alive) {
        int dx = foes[i].x - px, dy = foes[i].y - py;
        if (dx * dx + dy * dy <= 9) { foes[i].hp -= 3 + level / 2; if (foes[i].hp <= 0) { foes[i].alive = 0; score += 20; gain_xp(18); } }
    }
}
static void monsters(void) {
    for (int i = 0; i < MONSTERS; ++i) if (foes[i].alive) {
        int dx = px - foes[i].x, dy = py - foes[i].y;
        if (dx * dx + dy * dy <= 25) {
            int sx = dx > 0 ? 1 : dx < 0 ? -1 : 0, sy = dy > 0 ? 1 : dy < 0 ? -1 : 0;
            if (foes[i].x + sx == px && foes[i].y + sy == py) --hp;
            else if (map[foes[i].y][foes[i].x + sx] != '#' && foe_at(foes[i].x + sx, foes[i].y) < 0) foes[i].x += sx;
            else if (map[foes[i].y + sy][foes[i].x] != '#' && foe_at(foes[i].x, foes[i].y + sy) < 0) foes[i].y += sy;
        }
    }
}
int main(int argc, char** argv) {
    (void)argc; (void)argv; level = 1; score = xp = 0; generate();
    for (;;) {
        tui_clear(TUI_DESKTOP); tui_bar(0, "KRrawl - Kage no Meikyu | @ hero  g oni  > exit");
        tui_box(2, 2, W + 2, H + 2, "The Moonlit Shattered Depths", TUI_BORDER);
        for (int y = 0; y < H; ++y) for (int x = 0; x < W; ++x) {
            int seen = visible(x, y);
            char c[2] = { seen ? map[y][x] : '.', 0 }; unsigned color = seen && map[y][x] == '#' ? 0x18 : 0x17;
            int i = seen ? foe_at(x, y) : -1; if (i >= 0) { c[0] = 'g'; color = TUI_ERROR; }
            if (x == px && y == py) { c[0] = '@'; color = TUI_SUCCESS; }
            if (!seen) { c[0] = (x + y + level) % 7 == 0 ? '.' : ' '; color = 0x08; }
            tui_text(3 + x, 3 + y, c, color);
        }
        tui_text(58, 4, "KAGE NO MEIKYU", TUI_KEYWORD);
        tui_text(58, 6, "HP", TUI_STATUS); tui_number(64, 6, hp, TUI_NUMBER);
        tui_text(58, 7, "MP", TUI_STATUS); tui_number(64, 7, mana, TUI_NUMBER);
        tui_text(58, 8, "LEVEL", TUI_STATUS); tui_number(64, 8, level, TUI_NUMBER);
        tui_text(58, 9, "XP", TUI_STATUS); tui_number(64, 9, xp, TUI_NUMBER);
        tui_text(58, 10, "POTIONS", TUI_STATUS); tui_number(64, 10, potions, TUI_NUMBER);
        tui_text(58, 11, "SCORE", TUI_STATUS); tui_number(64, 11, score, TUI_NUMBER);
        tui_text(58, 13, "Arrows/WASD move", TUI_STATUS); tui_text(58, 14, "Space slash  F spell", TUI_STATUS);
        tui_text(58, 15, "H heal  Q quit", TUI_STATUS);
        if (spell_flash) { tui_text(58, 17, "* MOONFIRE *", TUI_WARNING); --spell_flash; }
        tui_status(24); tui_present(3 + px, 3 + py);
        int key = tui_key(1), dx = 0, dy = 0;
        if (key == 'q' || key == 27) return 0;
        if (key == TUI_LEFT || key == 'a') dx = -1; else if (key == TUI_RIGHT || key == 'd') dx = 1;
        else if (key == TUI_UP || key == 'w') dy = -1; else if (key == TUI_DOWN || key == 's') dy = 1;
        else if (key == ' ') { strike(1, 0, 1); strike(-1, 0, 1); strike(0, 1, 1); strike(0, -1, 1); monsters(); }
        else if (key == 'f') { spell(); monsters(); }
        else if (key == 'h' && potions > 0 && hp < maxhp) { --potions; hp += 6; if (hp > maxhp) hp = maxhp; monsters(); }
        if ((dx || dy) && map[py + dy][px + dx] != '#' && foe_at(px + dx, py + dy) < 0) { px += dx; py += dy; monsters(); }
        if (map[py][px] == '>') { ++level; score += 25; generate(); }
        if (hp <= 0) { tui_message("KRrawl", "You fell beneath the torii gate. Press any key to return."); return 1; }
    }
}
