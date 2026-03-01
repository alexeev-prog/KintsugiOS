/*------------------------------------------------------------------------------
 *  Kintsugi OS Drivers source code
 *  File: kernel/drivers/history.c
 *  Title: Реализация истории команд
 *  Author: ElioNeto (fork: alexeev-prog)
 *  License: MIT License
 * ------------------------------------------------------------------------------
 *  Description:
 *    Реализация циклического буфера истории команд для Keramika Shell.
 *
 *    Алгоритм работы циклического буфера:
 *      - head всегда указывает на следующую свободную позицию
 *      - Индекс N команды в буфере: (head - N - 1 + HISTORY_SIZE) % HISTORY_SIZE
 *      - При переполнении самая старая команда перезаписывается
 *
 *    Навигация:
 *      - current = -1: не в режиме навигации (новый ввод)
 *      - current = 0: последняя добавленная команда
 *      - current = count-1: самая старая команда
 *      - При Up: current увеличивается (уход в прошлое)
 *      - При Down: current уменьшается (возврат к настоящему)
 * ----------------------------------------------------------------------------*/

#include "history.h"

#include "../kklibc/stdio.h"
#include "../kklibc/stdlib.h"
#include "screen.h"

/* ────────────────────────────────────────────────────────────────────────── */
/* Глобальная структура истории                                               */
/* ────────────────────────────────────────────────────────────────────────── */

static history_t history;

/* ────────────────────────────────────────────────────────────────────────── */
/* Внутренние вспомогательные функции                                         */
/* ────────────────────────────────────────────────────────────────────────── */

/**
 * @brief Получить реальный индекс в буфере для N-ой команды с конца
 *
 * Последняя добавленная команда: offset = 0
 * Предпоследняя:                offset = 1
 * И т.д.
 *
 * @param offset Смещение от конца истории (0 = последняя)
 * @return Индекс в циклическом буфере или -1 при ошибке
 */
static int history_get_index(int offset) {
    if (offset < 0 || offset >= history.count) {
        return -1;    /* Выход за границы истории */
    }

    /* Вычисляем индекс в циклическом буфере */
    int index = (history.head - offset - 1 + HISTORY_SIZE) % HISTORY_SIZE;
    return index;
}

/* ────────────────────────────────────────────────────────────────────────── */
/* Публичные функции API                                                      */
/* ────────────────────────────────────────────────────────────────────────── */

void history_init(void) {
    /* Обнуляем всю структуру */
    for (int i = 0; i < HISTORY_SIZE; i++) {
        history.buffer[i][0] = '\0';
    }

    history.head    = 0;
    history.count   = 0;
    history.current = -1;
    history.temp_line[0] = '\0';
}

void history_add(const char *command) {
    /* Игнорируем пустые строки */
    if (!command || command[0] == '\0') {
        return;
    }

    /* Игнорируем дубликат последней команды */
    if (history.count > 0) {
        int last_index = history_get_index(0);
        if (strcmp(history.buffer[last_index], command) == 0) {
            return;    /* Такая же команда уже в истории */
        }
    }

    /* Копируем команду в текущую позицию head */
    strncpy(history.buffer[history.head], command, HISTORY_LINE_MAX - 1);
    history.buffer[history.head][HISTORY_LINE_MAX - 1] = '\0';

    /* Двигаем указатель head по кругу */
    history.head = (history.head + 1) % HISTORY_SIZE;

    /* Увеличиваем счётчик, но не больше максимума */
    if (history.count < HISTORY_SIZE) {
        history.count++;
    }

    /* Сбрасываем навигацию */
    history.current = -1;
}

const char *history_up(const char *current_line) {
    /* Если истории нет — ничего не делаем */
    if (history.count == 0) {
        return NULL;
    }

    /* Если только начали навигацию (current == -1), сохраняем текущий ввод */
    if (history.current == -1) {
        strncpy(history.temp_line, current_line, HISTORY_LINE_MAX - 1);
        history.temp_line[HISTORY_LINE_MAX - 1] = '\0';
        history.current = 0;    /* Начинаем с последней команды */
    } else {
        /* Уже в режиме навигации — идём глубже в историю */
        if (history.current < history.count - 1) {
            history.current++;
        } else {
            /* Уже на самой старой команде — остаёмся на месте */
            return history.buffer[history_get_index(history.current)];
        }
    }

    int index = history_get_index(history.current);
    return history.buffer[index];
}

const char *history_down(void) {
    /* Если не в режиме навигации — ничего не делаем */
    if (history.current == -1) {
        return NULL;
    }

    /* Двигаемся к более новым командам */
    history.current--;

    /* Если вернулись к текущему вводу (current стал < 0) */
    if (history.current < 0) {
        history.current = -1;    /* Выход из режима навигации */
        return history.temp_line;    /* Возвращаем сохранённый ввод */
    }

    /* Иначе возвращаем команду из истории */
    int index = history_get_index(history.current);
    return history.buffer[index];
}

void history_reset_navigation(void) {
    history.current = -1;
    history.temp_line[0] = '\0';
}

void history_print(void) {
    if (history.count == 0) {
        kprint("History is empty\n");
        return;
    }

    printf("Command history (%d total):\n", history.count);

    /* Печатаем от самой старой к самой новой */
    for (int i = history.count - 1; i >= 0; i--) {
        int index = history_get_index(i);
        /* Номер команды: более старые имеют меньший номер */
        int cmd_number = history.count - i;
        printf("  %3d  %s\n", cmd_number, history.buffer[index]);
    }
}

int history_get_count(void) {
    return history.count;
}

void history_clear(void) {
    /* Полная очистка буфера */
    for (int i = 0; i < HISTORY_SIZE; i++) {
        history.buffer[i][0] = '\0';
    }

    history.head    = 0;
    history.count   = 0;
    history.current = -1;
    history.temp_line[0] = '\0';
}
