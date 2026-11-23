#include "ktuilib.h"

/* KRrawl - Kage no Meikyu. Deterministic roguelike with fog of war,
 * line-of-sight enemy AI, an inventory and a PC-speaker music sequencer. */
#define W 52
#define H 16
#define MONSTERS 12
#define ITEMS 8

typedef struct { int x, y, hp, alive, kind, alert; } monster;
typedef struct { int x, y, kind; } item; /* 0 potion, 1 mana crystal, 2 scroll */

static unsigned seed = 0x4b1d5a7u;
static unsigned rnd(unsigned n) { seed = seed * 1664525u + 1013904223u; return n ? (seed >> 16) % n : 0; }
static char map[H][W];
static unsigned char explored[H][W];
static monster foes[MONSTERS];
static item items[ITEMS];
static int px, py, hp, maxhp, mana, maxmana, level, score, xp, spell_flash;
static int inv[3];
static int music_on = 1, music_pos, frame;
static const unsigned melody[] = {
    294, 0, 330, 392, 440, 0, 392, 330,
    294, 0, 262, 294, 330, 294, 0, 0
};

static void sleep_ms(u32 ms) { syscall(KOS_SYS_SLEEP, ms, 0, 0, 0); }
static void tone(unsigned n, u32 ms) { syscall(KOS_SYS_SOUND_TONE, n, ms, 0, 0); }
static int foe_at(int x, int y);

static int dist2(int x, int y) { int dx = x - px, dy = y - py; return dx * dx + dy * dy; }
static int visible(int x, int y) { return dist2(x, y) <= 49; }

/* Bresenham line of sight: walls block vision. */
static int los(int x0, int y0, int x1, int y1) {
    int dx = x1 > x0 ? x1 - x0 : x0 - x1, dy = y1 > y0 ? y1 - y0 : y0 - y1;
    int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, err = dx - dy;
    while (x0 != x1 || y0 != y1) {
        int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 < dx) { err += dx; y0 += sy; }
        if ((x0 != x1 || y0 != y1) && map[y0][x0] == '#') return 0;
    }
    return 1;
}

static void carve_room(int x0, int y0, int w, int h) {
    for (int y = y0; y < y0 + h && y < H - 1; ++y)
        for (int x = x0; x < x0 + w && x < W - 1; ++x)
            if (x > 0 && y > 0) map[y][x] = '.';
}

static void generate(void) {
    for (int i = 0; i < MONSTERS; ++i) foes[i].alive = 0;
    for (int i = 0; i < ITEMS; ++i) items[i].kind = -1;
    for (int y = 0; y < H; ++y) for (int x = 0; x < W; ++x) { map[y][x] = '#'; explored[y][x] = 0; }
    int shape = level % 3;
    if (shape == 0) {
        carve_room(2, 2, W - 5, H - 5);
        for (int x = 6; x < W - 3; x += 9) for (int y = 4; y < H - 3; ++y) map[y][x] = '#';
    } else if (shape == 1) {
        for (int y = 2; y < H - 2; ++y) for (int x = 2; x < W - 2; ++x) {
            int dx = x - W / 2, dy = y - H / 2;
            if (dx * dx * 2 + dy * dy * 8 < W * W) map[y][x] = '.';
        }
        for (int x = 3; x < W - 3; ++x) map[H / 2][x] = '.';
    } else {
        for (int x = 2; x < W - 2; ++x) {
            int y = 2 + ((x / 6) & 1) * (H - 6);
            for (int dy = -1; dy <= 1; ++dy) if (y + dy > 0 && y + dy < H - 1) map[y + dy][x] = '.';
        }
    }
    px = 3; py = 3; map[py][px] = '.';
    for (int x = 3; x <= W - 4; ++x) map[3][x] = '.';
    for (int y = 3; y <= H - 4; ++y) map[y][W - 4] = '.';
    map[H - 4][W - 4] = '>';
    for (int i = 0; i < MONSTERS; ++i) {
        do { foes[i].x = 3 + rnd(W - 6); foes[i].y = 3 + rnd(H - 6); }
        while (map[foes[i].y][foes[i].x] != '.' || foe_at(foes[i].x, foes[i].y) >= 0
               || (foes[i].x == px && foes[i].y == py));
        foes[i].hp = 2 + level / 3; foes[i].alive = 1;
        foes[i].kind = i % 3; foes[i].alert = 0;
    }
    for (int i = 0; i < ITEMS; ++i) {
        int x, y;
        do { x = 3 + rnd(W - 6); y = 3 + rnd(H - 6); }
        while (map[y][x] != '.' || (x == px && y == py) || foe_at(x, y) >= 0);
        items[i].x = x; items[i].y = y; items[i].kind = i % 3;
    }
    maxhp = 12 + level * 2; maxmana = 5 + level;
    hp = maxhp; mana = maxmana;
}

static int foe_at(int x, int y) {
    for (int i = 0; i < MONSTERS; ++i) if (foes[i].alive && foes[i].x == x && foes[i].y == y) return i;
    return -1;
}
static void gain_xp(int amount) {
    xp += amount;
    while (xp >= level * 25) {
        xp -= level * 25; ++level; maxhp += 2; maxmana += 1;
        hp = maxhp; mana = maxmana;
        tone(523, 90); sleep_ms(100); tone(659, 90); sleep_ms(100); tone(784, 140);
        tui_toast("Level up! HP and MP restored", 40);
    }
}
static void slay(int i, int bonus) {
    foes[i].alive = 0; score += 10 + bonus; gain_xp(12 + bonus);
    tone(880, 70);
}
static void strike(int dx, int dy, int damage) {
    int i = foe_at(px + dx, py + dy);
    if (i < 0) return;
    foes[i].hp -= damage;
    foes[i].alert = 1;
    if (foes[i].hp <= 0) slay(i, 0); else tone(660, 40);
}
static void spell(void) {
    if (mana < 2) { tui_toast("Not enough MP", 25); return; }
    mana -= 2; spell_flash = 6;
    tone(523, 60); sleep_ms(70); tone(659, 60); sleep_ms(70); tone(784, 120);
    for (int i = 0; i < MONSTERS; ++i) if (foes[i].alive && visible(foes[i].x, foes[i].y)) {
        foes[i].hp -= 3 + level / 2; foes[i].alert = 1;
        if (foes[i].hp <= 0) slay(i, 8);
    }
}
static int can_step(int x, int y) {
    return x > 0 && y > 0 && x < W - 1 && y < H - 1 && map[y][x] != '#' && foe_at(x, y) < 0;
}
static void monsters(void) {
    for (int i = 0; i < MONSTERS; ++i) if (foes[i].alive) {
        int dx = px - foes[i].x, dy = py - foes[i].y;
        int d2 = dx * dx + dy * dy;
        if (!foes[i].alert && d2 <= 36 && los(foes[i].x, foes[i].y, px, py)) foes[i].alert = 1;
        if (!foes[i].alert) {
            if (rnd(3) == 0) {
                int sx = (int)rnd(3) - 1, sy = (int)rnd(3) - 1;
                if (can_step(foes[i].x + sx, foes[i].y + sy)) { foes[i].x += sx; foes[i].y += sy; }
            }
            continue;
        }
        int ay = dy > 0 ? 1 : dy < 0 ? -1 : 0;
        if ((dx == 0 && (dy == 1 || dy == -1)) || (dy == 0 && (dx == 1 || dx == -1))
            || (dx * dx == 1 && dy * dy == 1)) {
            hp -= 1 + level / 5; tone(180, 60);
            continue;
        }
        /* Kitsune try to flank: aim beside the hero instead of straight at. */
        int tx = px, ty = py;
        if (foes[i].kind == 2) { tx = px + (dy > 0 ? 1 : -1); ty = py + (dx > 0 ? 1 : -1); }
        int mx = tx - foes[i].x, my = ty - foes[i].y;
        int sx = mx > 0 ? 1 : mx < 0 ? -1 : 0, sy = my > 0 ? 1 : my < 0 ? -1 : 0;
        if (foes[i].kind == 1 && rnd(4) == 0) { sx = 0; sy = ay; } /* yurei drift */
        if (sx && can_step(foes[i].x + sx, foes[i].y)) foes[i].x += sx;
        else if (sy && can_step(foes[i].x, foes[i].y + sy)) foes[i].y += sy;
    }
}
static void pickup(void) {
    for (int i = 0; i < ITEMS; ++i) if (items[i].kind >= 0 && items[i].x == px && items[i].y == py) {
        ++inv[items[i].kind];
        items[i].kind = -1;
        tone(784, 50);
        tui_toast("Picked up an item (I: inventory)", 30);
    }
}
static void use_item(int kind) {
    if (kind < 0 || kind > 2 || !inv[kind]) { tui_toast("Nothing to use", 25); return; }
    if (kind == 0) {
        if (hp >= maxhp) { tui_toast("HP already full", 25); return; }
        --inv[0]; hp += 6; if (hp > maxhp) hp = maxhp;
        tone(392, 80); sleep_ms(90); tone(523, 120);
    } else if (kind == 1) {
        if (mana >= maxmana) { tui_toast("MP already full", 25); return; }
        --inv[1]; mana += 4; if (mana > maxmana) mana = maxmana;
        tone(587, 80); sleep_ms(90); tone(784, 120);
    } else {
        --inv[2]; spell_flash = 6;
        tone(880, 60); sleep_ms(70); tone(988, 60); sleep_ms(70); tone(1175, 140);
        for (int i = 0; i < MONSTERS; ++i) if (foes[i].alive && visible(foes[i].x, foes[i].y)) {
            foes[i].hp -= 4; foes[i].alert = 1;
            if (foes[i].hp <= 0) slay(i, 8);
        }
    }
}
static void inventory(void) {
    char labels[3][48];
    const char* names[] = { "Potion (heal 6 HP)", "Mana crystal (+4 MP)", "Lightning scroll (AoE)" };
    const char* items_menu[3];
    for (int i = 0; i < 3; ++i) {
        u32 n = 0;
        while (names[i][n] && n < 40) { labels[i][n] = names[i][n]; ++n; }
        labels[i][n++] = ' '; labels[i][n++] = 'x'; labels[i][n++] = '0' + inv[i]; labels[i][n] = 0;
        items_menu[i] = labels[i];
    }
    int choice = tui_menu("Inventory - Enter uses item", items_menu, 3, 0);
    if (choice >= 0) use_item(choice);
}
static void music_tick(void) {
    if (!music_on) return;
    if (++frame >= 10) {
        frame = 0;
        unsigned n = melody[music_pos];
        music_pos = (music_pos + 1) % 16;
        if (n) tone(n, 260);
    }
}

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    level = 1; score = xp = 0; inv[0] = 2; inv[1] = 1; inv[2] = 0;
    generate();
    for (;;) {
        for (int y = 0; y < H; ++y) for (int x = 0; x < W; ++x)
            if (visible(x, y)) explored[y][x] = 1;
        tui_clear(TUI_DESKTOP);
        tui_bar(0, "KRrawl - Kage no Meikyu | @ hero  o oni  y yurei  k kitsune");
        tui_box(2, 2, W + 2, H + 2, "The Moonlit Shattered Depths", TUI_BORDER);
        for (int y = 0; y < H; ++y) for (int x = 0; x < W; ++x) {
            char c[2] = { ' ', 0 }; unsigned color = 0x08;
            if (visible(x, y)) {
                c[0] = map[y][x]; color = map[y][x] == '#' ? 0x18 : 0x17;
                for (int i = 0; i < ITEMS; ++i) if (items[i].kind >= 0 && items[i].x == x && items[i].y == y) {
                    c[0] = items[i].kind == 0 ? '!' : items[i].kind == 1 ? '*' : '?';
                    color = TUI_WARNING;
                }
                int i = foe_at(x, y);
                if (i >= 0) { c[0] = foes[i].kind == 0 ? 'o' : foes[i].kind == 1 ? 'y' : 'k'; color = TUI_ERROR; }
                if (x == px && y == py) { c[0] = '@'; color = TUI_SUCCESS; }
            } else if (explored[y][x]) {
                c[0] = map[y][x]; color = 0x08;
            }
            tui_text(3 + x, 3 + y, c, color);
        }
        tui_text(58, 4, "KAGE NO MEIKYU", TUI_KEYWORD);
        tui_text(58, 6, "HP", TUI_STATUS); tui_gauge(62, 6, 12, hp > 0 ? hp : 0, maxhp); tui_number(75, 6, hp, TUI_NUMBER);
        tui_text(58, 7, "MP", TUI_STATUS); tui_gauge(62, 7, 12, mana, maxmana); tui_number(75, 7, mana, TUI_NUMBER);
        tui_text(58, 8, "LV", TUI_STATUS); tui_number(62, 8, level, TUI_NUMBER);
        tui_text(66, 8, "XP", TUI_STATUS); tui_number(70, 8, xp, TUI_NUMBER);
        tui_text(58, 9, "SCORE", TUI_STATUS); tui_number(66, 9, score, TUI_NUMBER);
        tui_text(58, 11, "BAG", TUI_KEYWORD);
        tui_text(58, 12, "! potion", TUI_STATUS); tui_number(70, 12, inv[0], TUI_NUMBER);
        tui_text(58, 13, "* mana", TUI_STATUS); tui_number(70, 13, inv[1], TUI_NUMBER);
        tui_text(58, 14, "? scroll", TUI_STATUS); tui_number(70, 14, inv[2], TUI_NUMBER);
        tui_text(58, 16, "WASD move  Space hit", TUI_COMMENT);
        tui_text(58, 17, "F spell  I bag", TUI_COMMENT);
        tui_text(58, 18, music_on ? "M music: on" : "M music: off", TUI_COMMENT);
        tui_text(58, 19, "Q quit", TUI_COMMENT);
        if (spell_flash) { tui_text(30, 1, "* MOONFIRE *", TUI_WARNING); --spell_flash; }
        tui_status(24); tui_present(3 + px, 3 + py);
        music_tick();
        int key = tui_key(0), dx = 0, dy = 0, acted = 0;
        if (key == 'q' || key == 27) { tone(0, 0); return 0; }
        if (key == 'm') { music_on = !music_on; if (!music_on) tone(0, 0); }
        if (key == TUI_LEFT || key == 'a') dx = -1;
        else if (key == TUI_RIGHT || key == 'd') dx = 1;
        else if (key == TUI_UP || key == 'w') dy = -1;
        else if (key == TUI_DOWN || key == 's') dy = 1;
        else if (key == ' ') { strike(1, 0, 1); strike(-1, 0, 1); strike(0, 1, 1); strike(0, -1, 1); acted = 1; }
        else if (key == 'f') { spell(); acted = 1; }
        else if (key == 'h') { use_item(0); acted = 1; }
        else if (key == 'i') inventory();
        else if (key == '1') { use_item(0); acted = 1; }
        else if (key == '2') { use_item(1); acted = 1; }
        else if (key == '3') { use_item(2); acted = 1; }
        if ((dx || dy) && can_step(px + dx, py + dy)) { px += dx; py += dy; pickup(); acted = 1; }
        if (acted) monsters();
        if (map[py][px] == '>') {
            ++level; score += 25;
            tone(392, 80); sleep_ms(90); tone(440, 80); sleep_ms(90); tone(523, 160);
            generate();
        }
        if (hp <= 0) {
            tone(0, 0); tone(196, 400); sleep_ms(420); tone(147, 600); sleep_ms(620);
            tui_message("KRrawl", "You fell beneath the torii gate. Press any key to return.");
            return 1;
        }
        sleep_ms(30);
    }
}