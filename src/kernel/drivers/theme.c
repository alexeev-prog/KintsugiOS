/*------------------------------------------------------------------------------
 *  Kintsugi OS Drivers source code
 *  File: kernel/drivers/theme.c
 *  Title: Реализация системы тем терминала
 *  Author: ElioNeto (fork: alexeev-prog)
 *  License: MIT License
 * ------------------------------------------------------------------------------
 *  Description:
 *    Реализация управления цветовыми схемами терминала.
 *
 *    VGA цвета (биты 0-3 = foreground, биты 4-7 = background):
 *      0x0 = Чёрный        0x8 = Тёмно-серый
 *      0x1 = Синий         0x9 = Светло-синий
 *      0x2 = Зелёный       0xA = Светло-зелёный
 *      0x3 = Бирюзовый     0xB = Светло-бирюзовый
 *      0x4 = Красный       0xC = Светло-красный
 *      0x5 = Фиолетовый    0xD = Светло-фиолетовый
 *      0x6 = Коричневый    0xE = Жёлтый
 *      0x7 = Светло-серый  0xF = Белый
 *
 *    Формат атрибута: (background << 4) | foreground
 *    Например: (0x0 << 4) | 0xF = 0x0F = белый на чёрном
 * ----------------------------------------------------------------------------*/

#include "theme.h"

#include "../kklibc/stdio.h"
#include "screen.h"
#include "terminal.h"

/* ────────────────────────────────────────────────────────────────────────── */
/* Определения VGA цветов для удобства                                       */
/* ────────────────────────────────────────────────────────────────────────── */

#define VGA_BLACK         0x0
#define VGA_BLUE          0x1
#define VGA_GREEN         0x2
#define VGA_CYAN          0x3
#define VGA_RED           0x4
#define VGA_MAGENTA       0x5
#define VGA_BROWN         0x6
#define VGA_LIGHT_GRAY    0x7
#define VGA_DARK_GRAY     0x8
#define VGA_LIGHT_BLUE    0x9
#define VGA_LIGHT_GREEN   0xA
#define VGA_LIGHT_CYAN    0xB
#define VGA_LIGHT_RED     0xC
#define VGA_LIGHT_MAGENTA 0xD
#define VGA_YELLOW        0xE
#define VGA_WHITE         0xF

/* Макрос для создания VGA атрибута: (bg << 4) | fg */
#define VGA_ATTR(bg, fg) (((bg) << 4) | (fg))

/* ────────────────────────────────────────────────────────────────────────── */
/* Массив предустановленных тем                                               */
/* ────────────────────────────────────────────────────────────────────────── */

static const theme_t themes[THEME_COUNT] = {
    /* THEME_DEFAULT: классическая схема — белый на чёрном */
    {
        .name      = "Default",
        .bg        = VGA_ATTR(VGA_BLACK, VGA_WHITE),         /* Белый на чёрном      */
        .fg        = VGA_ATTR(VGA_BLACK, VGA_WHITE),         /* Белый на чёрном      */
        .prompt    = VGA_ATTR(VGA_BLACK, VGA_LIGHT_GREEN),   /* Зелёный промпт       */
        .error     = VGA_ATTR(VGA_BLACK, VGA_LIGHT_RED),     /* Красный для ошибок   */
        .success   = VGA_ATTR(VGA_BLACK, VGA_LIGHT_GREEN),   /* Зелёный для успеха   */
        .highlight = VGA_ATTR(VGA_BLACK, VGA_YELLOW)         /* Жёлтая подсветка     */
    },

    /* THEME_MATRIX: зелёный на чёрном (стиль "Матрицы") */
    {
        .name      = "Matrix",
        .bg        = VGA_ATTR(VGA_BLACK, VGA_GREEN),         /* Зелёный на чёрном    */
        .fg        = VGA_ATTR(VGA_BLACK, VGA_GREEN),         /* Зелёный текст        */
        .prompt    = VGA_ATTR(VGA_BLACK, VGA_LIGHT_GREEN),   /* Яркий зелёный        */
        .error     = VGA_ATTR(VGA_BLACK, VGA_LIGHT_RED),     /* Красный              */
        .success   = VGA_ATTR(VGA_BLACK, VGA_LIGHT_GREEN),   /* Яркий зелёный        */
        .highlight = VGA_ATTR(VGA_BLACK, VGA_LIGHT_CYAN)     /* Бирюзовый            */
    },

    /* THEME_HACKER: ярко-зелёный (классический хакерский терминал) */
    {
        .name      = "Hacker",
        .bg        = VGA_ATTR(VGA_BLACK, VGA_LIGHT_GREEN),   /* Яркий зелёный        */
        .fg        = VGA_ATTR(VGA_BLACK, VGA_LIGHT_GREEN),   /* Яркий зелёный        */
        .prompt    = VGA_ATTR(VGA_BLACK, VGA_YELLOW),        /* Жёлтый промпт        */
        .error     = VGA_ATTR(VGA_BLACK, VGA_LIGHT_RED),     /* Красный              */
        .success   = VGA_ATTR(VGA_BLACK, VGA_LIGHT_CYAN),    /* Бирюзовый            */
        .highlight = VGA_ATTR(VGA_BLACK, VGA_WHITE)          /* Белый                */
    },

    /* THEME_NORD: синие холодные тона (скандинавская палитра Nord) */
    {
        .name      = "Nord",
        .bg        = VGA_ATTR(VGA_BLACK, VGA_LIGHT_GRAY),    /* Светло-серый         */
        .fg        = VGA_ATTR(VGA_BLACK, VGA_LIGHT_GRAY),    /* Светло-серый         */
        .prompt    = VGA_ATTR(VGA_BLACK, VGA_LIGHT_BLUE),    /* Светло-синий         */
        .error     = VGA_ATTR(VGA_BLACK, VGA_LIGHT_RED),     /* Красный              */
        .success   = VGA_ATTR(VGA_BLACK, VGA_LIGHT_GREEN),   /* Зелёный              */
        .highlight = VGA_ATTR(VGA_BLACK, VGA_LIGHT_CYAN)     /* Бирюзовый            */
    },

    /* THEME_DRACULA: фиолетовая тёмная тема (популярная палитра Dracula) */
    {
        .name      = "Dracula",
        .bg        = VGA_ATTR(VGA_BLACK, VGA_LIGHT_GRAY),    /* Светло-серый         */
        .fg        = VGA_ATTR(VGA_BLACK, VGA_LIGHT_GRAY),    /* Светло-серый         */
        .prompt    = VGA_ATTR(VGA_BLACK, VGA_LIGHT_MAGENTA), /* Светло-фиолетовый    */
        .error     = VGA_ATTR(VGA_BLACK, VGA_LIGHT_RED),     /* Красный              */
        .success   = VGA_ATTR(VGA_BLACK, VGA_LIGHT_GREEN),   /* Зелёный              */
        .highlight = VGA_ATTR(VGA_BLACK, VGA_LIGHT_CYAN)     /* Бирюзовый            */
    },

    /* THEME_SOLARIZED_DARK: жёлто-синяя схема (Solarized для тёмного фона) */
    {
        .name      = "Solarized",
        .bg        = VGA_ATTR(VGA_BLACK, VGA_LIGHT_GRAY),    /* Светло-серый         */
        .fg        = VGA_ATTR(VGA_BLACK, VGA_LIGHT_GRAY),    /* Светло-серый         */
        .prompt    = VGA_ATTR(VGA_BLACK, VGA_YELLOW),        /* Жёлтый               */
        .error     = VGA_ATTR(VGA_BLACK, VGA_LIGHT_RED),     /* Красный              */
        .success   = VGA_ATTR(VGA_BLACK, VGA_GREEN),         /* Зелёный              */
        .highlight = VGA_ATTR(VGA_BLACK, VGA_CYAN)           /* Бирюзовый            */
    },

    /* THEME_MONOKAI: оранжево-серая схема (цвета Monokai) */
    {
        .name      = "Monokai",
        .bg        = VGA_ATTR(VGA_BLACK, VGA_LIGHT_GRAY),    /* Светло-серый         */
        .fg        = VGA_ATTR(VGA_BLACK, VGA_LIGHT_GRAY),    /* Светло-серый         */
        .prompt    = VGA_ATTR(VGA_BLACK, VGA_YELLOW),        /* Жёлтый               */
        .error     = VGA_ATTR(VGA_BLACK, VGA_LIGHT_RED),     /* Красный              */
        .success   = VGA_ATTR(VGA_BLACK, VGA_LIGHT_GREEN),   /* Зелёный              */
        .highlight = VGA_ATTR(VGA_BLACK, VGA_LIGHT_MAGENTA)  /* Фиолетовый           */
    },

    /* THEME_GRUVBOX: бежево-коричневая ретро-схема (Gruvbox) */
    {
        .name      = "Gruvbox",
        .bg        = VGA_ATTR(VGA_BLACK, VGA_LIGHT_GRAY),    /* Светло-серый         */
        .fg        = VGA_ATTR(VGA_BLACK, VGA_LIGHT_GRAY),    /* Светло-серый         */
        .prompt    = VGA_ATTR(VGA_BLACK, VGA_BROWN),         /* Коричневый           */
        .error     = VGA_ATTR(VGA_BLACK, VGA_RED),           /* Красный              */
        .success   = VGA_ATTR(VGA_BLACK, VGA_GREEN),         /* Зелёный              */
        .highlight = VGA_ATTR(VGA_BLACK, VGA_YELLOW)         /* Жёлтый               */
    }
};

/* ────────────────────────────────────────────────────────────────────────── */
/* Глобальное состояние системы тем                                           */
/* ────────────────────────────────────────────────────────────────────────── */

static theme_id_t current_theme_id = THEME_DEFAULT;

/* ────────────────────────────────────────────────────────────────────────── */
/* Публичные функции API                                                      */
/* ────────────────────────────────────────────────────────────────────────── */

void theme_init(void) {
    current_theme_id = THEME_DEFAULT;
    /* Применяем тему по умолчанию к терминалу */
    theme_apply_to_terminal();
}

int theme_set(theme_id_t id) {
    if (id < 0 || id >= THEME_COUNT) {
        return -1;    /* Некорректный ID */
    }

    current_theme_id = id;
    theme_apply_to_terminal();
    return 0;
}

const theme_t *theme_get_current(void) {
    return &themes[current_theme_id];
}

const theme_t *theme_get_by_id(theme_id_t id) {
    if (id < 0 || id >= THEME_COUNT) {
        return NULL;
    }
    return &themes[id];
}

theme_id_t theme_get_current_id(void) {
    return current_theme_id;
}

const char *theme_get_name(theme_id_t id) {
    const theme_t *t = theme_get_by_id(id);
    return t ? t->name : "Unknown";
}

void theme_apply_to_terminal(void) {
    /* Устанавливаем цвет по умолчанию для терминала */
    const theme_t *theme = theme_get_current();
    terminal_set_color(theme->fg);
}

void theme_list_all(void) {
    printf("Available themes (%d total):\n\n", THEME_COUNT);

    for (int i = 0; i < THEME_COUNT; i++) {
        const theme_t *t = &themes[i];

        /* Печатаем ID и название */
        if (i == current_theme_id) {
            printf("  [%d] %s (active)\n", i, t->name);
        } else {
            printf("  [%d] %s\n", i, t->name);
        }

        /* Показываем примеры цветов темы */
        u8 old_color = terminal_get_color();

        kprint("      Text: ");
        terminal_set_color(t->fg);
        kprint("Sample text");
        terminal_set_color(old_color);

        kprint(" | Prompt: ");
        terminal_set_color(t->prompt);
        kprint("!#>");
        terminal_set_color(old_color);

        kprint(" | Error: ");
        terminal_set_color(t->error);
        kprint("ERROR");
        terminal_set_color(old_color);

        kprint(" | Success: ");
        terminal_set_color(t->success);
        kprint("OK");
        terminal_set_color(old_color);

        kprint("\n\n");
    }

    printf("\nUsage: theme set <id>\n");
}
