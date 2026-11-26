#include "kms.h"
#include "ktuilib.h"

/* KRrawl - Kage no Meikyu. Deterministic roguelike with fog of war,
 * line-of-sight enemy AI, an inventory and a PC-speaker music sequencer. */
#define W 52
#define H 16
#define MONSTERS 14
#define ITEMS 10
#define ROOMS 12

typedef struct {
    int x, y, hp, maxhp, alive, kind, alert, cooldown;
} monster;

typedef struct {
    int x, y, kind;
} item; /* 0 potion, 1 mana, 2 scroll, 3 gold */

typedef struct {
    int x, y, w, h;
} room;

static unsigned seed = 0x4b1d5a7u;

static unsigned rnd(unsigned n) {
    seed = seed * 1664525u + 1013904223u;
    return n ? (seed >> 16) % n : 0;
}

static char map[H][W];
static unsigned char explored[H][W];
static monster foes[MONSTERS];
static item items[ITEMS];
static int px, py, hp, maxhp, mana, maxmana, level, score, xp, spell_flash, gold;
static int inv[4];
static room rooms[ROOMS];
static int room_count;
static int boss_level;
static int music_on = 1, music_pos, frame;
/* Japanese-flavoured OSTs are plain .KMS files, shared with KListen. */
static kms_track ost[4];
static int ost_count, ost_selected;
static const char* const ost_paths[] = {
    "/etc/music/sakura.kms", "/etc/music/tsuki.kms", "/etc/music/matsuri.kms", "/etc/music/kage.kms"
};
static const char* const ost_names[] = {
    "Sakura drift", "Tsuki no Kodomo", "Matsuri no Yoru", "Kage no Meikyu"
};

static void sleep_ms(u32 ms) {
    syscall(KOS_SYS_SLEEP, ms, 0, 0, 0);
}

static void tone(unsigned n, u32 ms) {
    syscall(KOS_SYS_SOUND_TONE, n, ms, 0, 0);
}

static int foe_at(int x, int y);

static int dist2(int x, int y) {
    int dx = x - px, dy = y - py;
    return dx * dx + dy * dy;
}

static int visible(int x, int y) {
    return dist2(x, y) <= 49;
}

/* Bresenham line of sight: walls block vision. */
static int los(int x0, int y0, int x1, int y1) {
    int dx = x1 > x0 ? x1 - x0 : x0 - x1, dy = y1 > y0 ? y1 - y0 : y0 - y1;
    int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, err = dx - dy;
    while (x0 != x1 || y0 != y1) {
        int e2 = 2 * err;
        if (e2 > -dy) {
            err -= dy;
            x0 += sx;
        }
        if (e2 < dx) {
            err += dx;
            y0 += sy;
        }
        if ((x0 != x1 || y0 != y1) && map[y0][x0] == '#') {
            return 0;
        }
    }
    return 1;
}

static void carve_room(int x0, int y0, int w, int h) {
    for (int y = y0; y < y0 + h && y < H - 1; ++y) {
        for (int x = x0; x < x0 + w && x < W - 1; ++x) {
            if (x > 0 && y > 0) {
                map[y][x] = '.';
            }
        }
    }
}

static void carve_corridor(int x0, int y0, int x1, int y1) {
    if (rnd(2)) {
        for (int x = x0; x <= x1; ++x) {
            map[y0][x] = '.';
        }
        int step = y1 > y0 ? 1 : -1;
        for (int y = y0; y != y1 + step; y += step) {
            map[y][x1] = '.';
        }
    } else {
        for (int y = y0; y <= y1; ++y) {
            map[y][x0] = '.';
        }
        int step = x1 > x0 ? 1 : -1;
        for (int x = x0; x != x1 + step; x += step) {
            map[y1][x] = '.';
        }
    }
}

static void generate(void) {
    for (int i = 0; i < MONSTERS; ++i) {
        foes[i].alive = 0;
    }
    for (int i = 0; i < ITEMS; ++i) {
        items[i].kind = -1;
    }
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            map[y][x] = '#';
            explored[y][x] = 0;
        }
    }
    room_count = 0;
    /* Rooms placed in a grid, each linked to the previous one. */
    for (int i = 0; i < ROOMS; ++i) {
        int rw = 5 + rnd(7), rh = 4 + rnd(5);
        int rx = 2 + rnd(W - rw - 4), ry = 2 + rnd(H - rh - 4);
        rooms[room_count++] = (room){ rx, ry, rw, rh };
        carve_room(rx, ry, rw, rh);
        if (i) {
            room* a = &rooms[i - 1];
            carve_corridor(a->x + a->w / 2, a->y + a->h / 2, rx + rw / 2, ry + rh / 2);
        }
    }
    /* Guaranteed path: carve a spine from spawn to exit. */
    room* first = &rooms[0];
    room* last = &rooms[room_count - 1];
    px = first->x + 1;
    py = first->y + 1;
    map[py][px] = '.';
    carve_corridor(px, py, last->x + last->w / 2, last->y + last->h / 2);
    int ex = last->x + last->w / 2, ey = last->y + last->h / 2;
    map[ey][ex] = '>';
    boss_level = (level % 5 == 0);
    for (int i = 0; i < MONSTERS; ++i) {
        int x, y, tries = 0;
        do {
            x = 3 + rnd(W - 6);
            y = 3 + rnd(H - 6);
            ++tries;
        } while ((map[y][x] != '.' || foe_at(x, y) >= 0 || (x == px && y == py)) && tries < 200);
        if (tries >= 200) {
            continue;
        }
        foes[i].x = x;
        foes[i].y = y;
        foes[i].kind = boss_level && i == 0 ? 3 : (int)rnd(3);
        foes[i].hp = foes[i].maxhp = 2 + level / 3 + (foes[i].kind == 3 ? 8 : 0);
        foes[i].alive = 1;
        foes[i].alert = 0;
        foes[i].cooldown = 0;
    }
    for (int i = 0; i < ITEMS; ++i) {
        int x, y, tries = 0;
        do {
            x = 3 + rnd(W - 6);
            y = 3 + rnd(H - 6);
            ++tries;
        } while ((map[y][x] != '.' || (x == px && y == py) || foe_at(x, y) >= 0) && tries < 200);
        if (tries >= 200) {
            continue;
        }
        items[i].x = x;
        items[i].y = y;
        items[i].kind = (int)rnd(4);
    }
    maxhp = 12 + level * 2;
    maxmana = 5 + level;
    hp = maxhp;
    mana = maxmana;
}

static int foe_at(int x, int y) {
    for (int i = 0; i < MONSTERS; ++i) {
        if (foes[i].alive && foes[i].x == x && foes[i].y == y) {
            return i;
        }
    }
    return -1;
}

static void gain_xp(int amount) {
    xp += amount;
    while (xp >= level * 25) {
        xp -= level * 25;
        ++level;
        maxhp += 2;
        maxmana += 1;
        hp = maxhp;
        mana = maxmana;
        tone(523, 90);
        sleep_ms(100);
        tone(659, 90);
        sleep_ms(100);
        tone(784, 140);
        tui_toast("Level up! HP and MP restored", 40);
    }
}

static void slay(int i, int bonus) {
    foes[i].alive = 0;
    score += 10 + bonus;
    gain_xp(12 + bonus);
    tone(880, 70);
}

static void strike(int dx, int dy, int damage) {
    int i = foe_at(px + dx, py + dy);
    if (i < 0) {
        return;
    }
    foes[i].hp -= damage;
    foes[i].alert = 1;
    if (foes[i].hp <= 0) {
        slay(i, 0);
    } else {
        tone(660, 40);
    }
}

static void spell(void) {
    if (mana < 2) {
        tui_toast("Not enough MP", 25);
        return;
    }
    mana -= 2;
    spell_flash = 6;
    tone(523, 60);
    sleep_ms(70);
    tone(659, 60);
    sleep_ms(70);
    tone(784, 120);
    for (int i = 0; i < MONSTERS; ++i) {
        if (foes[i].alive && visible(foes[i].x, foes[i].y)) {
            foes[i].hp -= 3 + level / 2;
            foes[i].alert = 1;
            if (foes[i].hp <= 0) {
                slay(i, 8);
            }
        }
    }
}

static int can_step(int x, int y) {
    return x > 0 && y > 0 && x < W - 1 && y < H - 1 && map[y][x] != '#' && foe_at(x, y) < 0;
}

static void monsters(void) {
    for (int i = 0; i < MONSTERS; ++i) {
        if (foes[i].alive) {
            if (foes[i].cooldown) {
                --foes[i].cooldown;
            }
            int dx = px - foes[i].x, dy = py - foes[i].y;
            int d2 = dx * dx + dy * dy;
            int sight = foes[i].kind == 3 ? 64 : 36;
            if (!foes[i].alert && d2 <= sight && los(foes[i].x, foes[i].y, px, py)) {
                foes[i].alert = 1;
            }
            if (!foes[i].alert) {
                if (rnd(3) == 0) {
                    int sx = (int)rnd(3) - 1, sy = (int)rnd(3) - 1;
                    if (can_step(foes[i].x + sx, foes[i].y + sy)) {
                        foes[i].x += sx;
                        foes[i].y += sy;
                    }
                }
                continue;
            }
            int near = (dx * dx == 1 && dy * dy <= 1) || (dy * dy == 1 && dx * dx <= 1);
            if (near) {
                if (!foes[i].cooldown) {
                    int dmg = foes[i].kind == 3 ? 3 + level / 4 : 1 + level / 5;
                    hp -= dmg;
                    foes[i].cooldown = 2;
                    tone(180, 60);
                    tui_toast("An enemy strikes!", 15);
                }
                continue;
            }
            int ax = dx > 0 ? 1 : dx < 0 ? -1 : 0, ay = dy > 0 ? 1 : dy < 0 ? -1 : 0;
            int sx = ax, sy = ay;
            /* yurei drifts unpredictably; kitsune flanks; oni charges; boss hunts. */
            if (foes[i].kind == 1 && rnd(4) == 0) {
                sx = 0;
                sy = ay;
            }
            if (foes[i].kind == 2) {
                sx = ay;
                sy = ax;
            }
            if (foes[i].kind == 3 && d2 > 16 && rnd(2)) {
                sx = 0;
            }
            if (sx && can_step(foes[i].x + sx, foes[i].y)) {
                foes[i].x += sx;
            } else if (sy && can_step(foes[i].x, foes[i].y + sy)) {
                foes[i].y += sy;
            } else if (can_step(foes[i].x + ax, foes[i].y)) {
                foes[i].x += ax;
            } else if (can_step(foes[i].x, foes[i].y + ay)) {
                foes[i].y += ay;
            }
        }
    }
}

static void pickup(void) {
    for (int i = 0; i < ITEMS; ++i) {
        if (items[i].kind >= 0 && items[i].x == px && items[i].y == py) {
            ++inv[items[i].kind];
            if (items[i].kind == 3) {
                gold += 5;
                score += 5;
            }
            items[i].kind = -1;
            tone(784, 50);
            tui_toast("Picked up an item (I: inventory)", 30);
        }
    }
}

static void use_item(int kind) {
    if (kind < 0 || kind > 2 || !inv[kind]) {
        tui_toast("Nothing to use", 25);
        return;
    }
    if (kind == 0) {
        if (hp >= maxhp) {
            tui_toast("HP already full", 25);
            return;
        }
        --inv[0];
        hp += 6;
        if (hp > maxhp) {
            hp = maxhp;
        }
        tone(392, 80);
        sleep_ms(90);
        tone(523, 120);
    } else if (kind == 1) {
        if (mana >= maxmana) {
            tui_toast("MP already full", 25);
            return;
        }
        --inv[1];
        mana += 4;
        if (mana > maxmana) {
            mana = maxmana;
        }
        tone(587, 80);
        sleep_ms(90);
        tone(784, 120);
    } else {
        --inv[2];
        spell_flash = 6;
        tone(880, 60);
        sleep_ms(70);
        tone(988, 60);
        sleep_ms(70);
        tone(1175, 140);
        for (int i = 0; i < MONSTERS; ++i) {
            if (foes[i].alive && visible(foes[i].x, foes[i].y)) {
                foes[i].hp -= 4;
                foes[i].alert = 1;
                if (foes[i].hp <= 0) {
                    slay(i, 8);
                }
            }
        }
    }
}

static void inventory(void) {
    char labels[4][48];
    const char* names[] = {
        "Potion (heal 6 HP)", "Mana crystal (+4 MP)", "Lightning scroll (AoE)", "Gold coins (score)"
    };
    const char* items_menu[4];
    for (int i = 0; i < 4; ++i) {
        u32 n = 0;
        while (names[i][n] && n < 40) {
            labels[i][n] = names[i][n];
            ++n;
        }
        labels[i][n++] = ' ';
        labels[i][n++] = 'x';
        labels[i][n++] = '0' + inv[i];
        labels[i][n] = 0;
        items_menu[i] = labels[i];
    }
    int choice = tui_menu("Inventory - Enter uses item", items_menu, 4, 0);
    if (choice == 3) {
        tui_toast("Gold is collected automatically", 20);
        return;
    }
    if (choice >= 0) {
        use_item(choice);
    }
}

static void load_ost(void) {
    ost_count = 0;
    for (unsigned i = 0; i < 4; ++i) {
        if (kms_load(ost_paths[i], &ost[ost_count])) {
            ++ost_count;
        }
    }
    if (!ost_count) {
        music_on = 0;
    }
}

static const char* ost_title(void) {
    if (!ost_count) {
        return "silent";
    }
    return ost[ost_selected].title[0] ? ost[ost_selected].title : ost_names[ost_selected];
}

static void music_tick(void) {
    if (!music_on || !ost_count) {
        return;
    }
    kms_track* track = &ost[ost_selected];
    u32 duration = track->notes[music_pos].duration;
    u32 delay = duration < 120 ? 5 : duration < 220 ? 7 : duration < 320 ? 9 : 12;
    if (++frame >= (int)delay) {
        frame = 0;
        kms_step(track, (unsigned*)&music_pos);
    }
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    level = 1;
    score = xp = 0;
    inv[0] = 2;
    inv[1] = 1;
    inv[2] = 0;
    load_ost();
    generate();
    for (;;) {
        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W; ++x) {
                if (visible(x, y)) {
                    explored[y][x] = 1;
                }
            }
        }
        tui_clear(TUI_DESKTOP);
        tui_bar(0, "KRrawl - Kage no Meikyu | @ hero  o oni  y yurei  k kitsune");
        tui_box(2, 2, W + 2, H + 2, "The Moonlit Shattered Depths", TUI_BORDER);
        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W; ++x) {
                char c[2] = { ' ', 0 };
                unsigned color = 0x08;
                if (visible(x, y)) {
                    c[0] = map[y][x];
                    color = map[y][x] == '#' ? 0x18 : 0x17;
                    for (int i = 0; i < ITEMS; ++i) {
                        if (items[i].kind >= 0 && items[i].x == x && items[i].y == y) {
                            c[0] = items[i].kind == 0 ? '!'
                                : items[i].kind == 1  ? '*'
                                : items[i].kind == 2  ? '?'
                                                      : '$';
                            color = TUI_WARNING;
                        }
                    }
                    int i = foe_at(x, y);
                    if (i >= 0) {
                        c[0] = foes[i].kind == 0 ? 'o'
                            : foes[i].kind == 1  ? 'y'
                            : foes[i].kind == 2  ? 'k'
                                                 : 'B';
                        color = foes[i].kind == 3 ? TUI_WARNING : TUI_ERROR;
                    }
                    if (x == px && y == py) {
                        c[0] = '@';
                        color = TUI_SUCCESS;
                    }
                } else if (explored[y][x]) {
                    c[0] = map[y][x];
                    color = 0x08;
                }
                tui_text(3 + x, 3 + y, c, color);
            }
        }
        tui_text(58, 4, "KAGE NO MEIKYU", TUI_KEYWORD);
        tui_text(58, 6, "HP", TUI_STATUS);
        tui_gauge(62, 6, 12, hp > 0 ? hp : 0, maxhp);
        tui_number(75, 6, hp, TUI_NUMBER);
        tui_text(58, 7, "MP", TUI_STATUS);
        tui_gauge(62, 7, 12, mana, maxmana);
        tui_number(75, 7, mana, TUI_NUMBER);
        tui_text(58, 8, "LV", TUI_STATUS);
        tui_number(62, 8, level, TUI_NUMBER);
        tui_text(66, 8, "XP", TUI_STATUS);
        tui_number(70, 8, xp, TUI_NUMBER);
        tui_text(58, 9, "SCORE", TUI_STATUS);
        tui_number(66, 9, score, TUI_NUMBER);
        tui_text(58, 11, "DEPTH", TUI_KEYWORD);
        tui_number(66, 11, level, TUI_NUMBER);
        tui_text(58, 12, "BAG", TUI_KEYWORD);
        tui_text(58, 13, "! potion", TUI_STATUS);
        tui_number(70, 13, inv[0], TUI_NUMBER);
        tui_text(58, 14, "* mana", TUI_STATUS);
        tui_number(70, 14, inv[1], TUI_NUMBER);
        tui_text(58, 15, "? scroll", TUI_STATUS);
        tui_number(70, 15, inv[2], TUI_NUMBER);
        tui_text(58, 16, "$ gold", TUI_STATUS);
        tui_number(70, 16, gold, TUI_NUMBER);
        if (boss_level) {
            tui_text(58, 17, "!! ONI LORD !!", TUI_WARNING);
        }
        tui_text(58, 18, "WASD move  Space hit", TUI_COMMENT);
        tui_text(58, 19, "F spell  I bag  M next OST", TUI_COMMENT);
        tui_text(58, 20, "Shift+M on/off  Q quit", TUI_COMMENT);
        tui_text(4, 20, "OST:", TUI_KEYWORD);
        tui_text(9, 20, music_on ? ost_title() : "off", TUI_VISUAL);
        if (spell_flash) {
            tui_text(30, 1, "* MOONFIRE *", TUI_WARNING);
            --spell_flash;
        }
        tui_status(24);
        tui_present(3 + px, 3 + py);
        music_tick();
        int key = tui_key(0), dx = 0, dy = 0, acted = 0;
        if (key == 'q' || key == 27) {
            tone(0, 0);
            return 0;
        }
        if (key == 'm' && ost_count) {
            ost_selected = (ost_selected + 1) % ost_count;
            music_pos = 0;
            frame = 0;
            tui_toast(ost_title(), 20);
        }
        if (key == 'M') {
            music_on = !music_on;
            if (!music_on) {
                tone(0, 0);
            }
        }
        if (key == TUI_LEFT || key == 'a') {
            dx = -1;
        } else if (key == TUI_RIGHT || key == 'd') {
            dx = 1;
        } else if (key == TUI_UP || key == 'w') {
            dy = -1;
        } else if (key == TUI_DOWN || key == 's') {
            dy = 1;
        } else if (key == ' ') {
            strike(1, 0, 1);
            strike(-1, 0, 1);
            strike(0, 1, 1);
            strike(0, -1, 1);
            acted = 1;
        } else if (key == 'f') {
            spell();
            acted = 1;
        } else if (key == 'h') {
            use_item(0);
            acted = 1;
        } else if (key == 'i') {
            inventory();
        } else if (key == '1') {
            use_item(0);
            acted = 1;
        } else if (key == '2') {
            use_item(1);
            acted = 1;
        } else if (key == '3') {
            use_item(2);
            acted = 1;
        }
        if ((dx || dy) && can_step(px + dx, py + dy)) {
            px += dx;
            py += dy;
            pickup();
            acted = 1;
        }
        if (acted) {
            monsters();
        }
        if (map[py][px] == '>') {
            ++level;
            score += 25;
            tone(392, 80);
            sleep_ms(90);
            tone(440, 80);
            sleep_ms(90);
            tone(523, 160);
            generate();
        }
        if (hp <= 0) {
            tone(0, 0);
            tone(196, 400);
            sleep_ms(420);
            tone(147, 600);
            sleep_ms(620);
            tui_message("KRrawl", "You fell beneath the torii gate. Press any key to return.");
            return 1;
        }
        sleep_ms(30);
    }
}
