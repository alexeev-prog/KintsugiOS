/*------------------------------------------------------------------------------
 *  Kintsugi OS Drivers source code
 *  File: kernel/drivers/mouse.c
 *  Title: Драйвер мыши PS/2
 *  Author: ElioNeto (fork: alexeev-prog)
 *  License: MIT License
 * ------------------------------------------------------------------------------
 *  Description:
 *    Реализация драйвера мыши PS/2 для KintsugiOS.
 *
 *    Протокол:
 *      - Мышь подключена к вспомогательному каналу PS/2 (IRQ12, вектор 44).
 *      - Каждый пакет состоит из 3 байт:
 *          Байт 0: флаги кнопок + знаки дельты
 *          Байт 1: дельта X (со знаком)
 *          Байт 2: дельта Y (со знаком, ось инвертирована)
 *      - Курсор отображается символом 0xDB поверх видеопамяти VGA.
 *      - Перед отрисовкой курсора сохраняется исходный символ/атрибут,
 *        при перемещении он восстанавливается.
 *
 *    Ограничения текущей реализации:
 *      - Поддержка только стандартного 3-байтового пакета (без scroll-wheel).
 *      - Курсор работает только в текстовом режиме VGA 80x25.
 *      - Нет очереди событий: обработчик обновляет глобальное состояние.
 * ----------------------------------------------------------------------------*/

#include "mouse.h"

#include "../cpu/isr.h"
#include "lowlevel_io.h"
#include "screen.h"

/* ──────────────────────────────────────────────────────────────────────────
 * Внутренние переменные модуля
 * ────────────────────────────────────────────────────────────────────────── */

/* Глобальное состояние мыши (доступно снаружи только через API)            */
static mouse_state_t mouse_state;

/* Счётчик байт в текущем пакете (0, 1, 2)                                  */
static u8 mouse_cycle = 0;

/* Буфер трёх байт пакета                                                   */
static u8 mouse_packet[3];

/* Флаг: курсор сейчас отрисован на экране                                  */
static int cursor_visible = 0;

/* ──────────────────────────────────────────────────────────────────────────
 * Вспомогательные функции (статические — только для этого модуля)
 * ────────────────────────────────────────────────────────────────────────── */

/**
 * @brief Ожидание готовности контроллера PS/2 для записи
 *
 * Контроллер PS/2 принимает данные только когда бит 1 регистра
 * статуса (порт 0x64) равен 0.
 */
static void mouse_wait_write(void) {
    /* Ожидаем пока буфер ввода контроллера не освободится */
    u32 timeout = 100000;
    while (timeout--) {
        if (!(port_byte_in(MOUSE_CMD_PORT) & 0x02)) {
            return;
        }
    }
}

/**
 * @brief Ожидание готовности контроллера PS/2 для чтения
 *
 * Данные в выходном буфере готовы, когда бит 0 регистра статуса равен 1.
 */
static void mouse_wait_read(void) {
    /* Ожидаем пока данные появятся в выходном буфере */
    u32 timeout = 100000;
    while (timeout--) {
        if (port_byte_in(MOUSE_CMD_PORT) & 0x01) {
            return;
        }
    }
}

/**
 * @brief Отправить команду непосредственно мыши
 *
 * Алгоритм:
 *   1. Ждём готовности контроллера.
 *   2. Сообщаем контроллеру, что следующий байт — для мыши (0xD4).
 *   3. Ждём снова и пишем байт команды в порт данных (0x60).
 *
 * @param cmd Байт команды
 */
static void mouse_send_cmd(u8 cmd) {
    /* Сообщаем контроллеру: следующий байт — для AUX-устройства */
    mouse_wait_write();
    port_byte_out(MOUSE_CMD_PORT, MOUSE_CMD_WRITE_AUX);

    /* Отправляем саму команду */
    mouse_wait_write();
    port_byte_out(MOUSE_DATA_PORT, cmd);
}

/**
 * @brief Прочитать байт из порта данных PS/2 (с ожиданием)
 *
 * @return Прочитанный байт
 */
static u8 mouse_read_byte(void) {
    mouse_wait_read();
    return port_byte_in(MOUSE_DATA_PORT);
}

/* ──────────────────────────────────────────────────────────────────────────
 * Управление курсором в видеопамяти VGA
 * ────────────────────────────────────────────────────────────────────────── */

/**
 * @brief Рассчитать смещение в видеопамяти по колонке и строке
 *
 * В текстовом режиме VGA каждый символ занимает 2 байта (символ + атрибут).
 * Смещение = (строка * 80 + колонка) * 2.
 *
 * @param col Колонка (0..79)
 * @param row Строка  (0..24)
 * @return Смещение в байтах от начала VIDEO_ADDRESS
 */
static inline int vga_offset(int col, int row) {
    return (row * MAX_COLS + col) * 2;
}

/**
 * @brief Нарисовать курсор мыши на экране
 *
 * Сохраняет оригинальный символ и атрибут под курсором,
 * затем записывает символ курсора с заданным атрибутом.
 */
static void cursor_draw(void) {
    if (cursor_visible) {
        return;    /* Курсор уже отрисован — ничего не делаем */
    }

    int offset = vga_offset(mouse_state.x, mouse_state.y);
    u8 *video  = (u8 *)VIDEO_ADDRESS;

    /* Сохраняем исходный контент ячейки */
    mouse_state.prev_char = video[offset];
    mouse_state.prev_attr = video[offset + 1];

    /* Рисуем курсор */
    video[offset]     = MOUSE_CURSOR_CHAR;
    video[offset + 1] = MOUSE_CURSOR_COLOR;

    cursor_visible = 1;
}

/**
 * @brief Стереть курсор мыши (восстановить оригинальный символ)
 */
static void cursor_erase(void) {
    if (!cursor_visible) {
        return;    /* Курсор уже скрыт */
    }

    int offset = vga_offset(mouse_state.x, mouse_state.y);
    u8 *video  = (u8 *)VIDEO_ADDRESS;

    /* Восстанавливаем то, что было под курсором */
    video[offset]     = mouse_state.prev_char;
    video[offset + 1] = mouse_state.prev_attr;

    cursor_visible = 0;
}

/* ──────────────────────────────────────────────────────────────────────────
 * Обработчик прерывания IRQ12
 * ────────────────────────────────────────────────────────────────────────── */

/**
 * @brief Обработчик прерывания мыши (IRQ12 → вектор 44)
 *
 * Накапливает байты пакета (3 штуки) и после получения полного пакета:
 *   1. Проверяет валидность: бит MOUSE_ALWAYS_ONE должен быть установлен.
 *   2. Пропускает пакет при переполнении осей.
 *   3. Вычисляет знаковые дельты X и Y.
 *   4. Стирает старый курсор, обновляет позицию, рисует новый.
 *   5. Обновляет маску кнопок.
 *
 * @param regs Структура регистров (не используется здесь)
 */
static void mouse_callback(registers_t regs) {
    u8 data = port_byte_in(MOUSE_DATA_PORT);

    switch (mouse_cycle) {
        case 0:
            /* Первый байт: флаги. Проверяем бит валидности */
            if (!(data & MOUSE_ALWAYS_ONE)) {
                /* Пакет невалиден — сбрасываем синхронизацию */
                mouse_cycle = 0;
                return;
            }
            mouse_packet[0] = data;
            mouse_cycle     = 1;
            break;

        case 1:
            /* Второй байт: дельта X */
            mouse_packet[1] = data;
            mouse_cycle     = 2;
            break;

        case 2: {
            /* Третий байт: дельта Y — пакет полный, обрабатываем */
            mouse_packet[2] = data;
            mouse_cycle     = 0;

            u8 flags = mouse_packet[0];

            /* Пропускаем пакеты с переполнением осей */
            if ((flags & MOUSE_X_OVERFLOW) || (flags & MOUSE_Y_OVERFLOW)) {
                break;
            }

            /* Вычисляем знаковую дельту X */
            int dx = (int)mouse_packet[1];
            if (flags & MOUSE_X_SIGN) {
                dx -= 256;    /* Дополнение до двух для отрицательных значений */
            }

            /* Вычисляем знаковую дельту Y (ось Y инвертирована в PS/2) */
            int dy = (int)mouse_packet[2];
            if (flags & MOUSE_Y_SIGN) {
                dy -= 256;
            }
            dy = -dy;    /* Инвертируем: в VGA Y растёт вниз */

            /* ── Обновляем состояние мыши ── */

            /* Стираем курсор со старой позиции */
            cursor_erase();

            /* Обновляем координаты с ограничением по границам экрана */
            mouse_state.dx = dx;
            mouse_state.dy = dy;
            mouse_state.x += dx;
            mouse_state.y += dy;

            /* Зажимаем координаты в допустимых пределах (clamp) */
            if (mouse_state.x < MOUSE_MIN_X) mouse_state.x = MOUSE_MIN_X;
            if (mouse_state.x > MOUSE_MAX_X) mouse_state.x = MOUSE_MAX_X;
            if (mouse_state.y < MOUSE_MIN_Y) mouse_state.y = MOUSE_MIN_Y;
            if (mouse_state.y > MOUSE_MAX_Y) mouse_state.y = MOUSE_MAX_Y;

            /* Обновляем состояние кнопок */
            mouse_state.buttons = flags & (MOUSE_BTN_LEFT |
                                           MOUSE_BTN_RIGHT |
                                           MOUSE_BTN_MIDDLE);

            /* Рисуем курсор на новой позиции */
            cursor_draw();
            break;
        }

        default:
            /* Не должно случаться — сбрасываем цикл */
            mouse_cycle = 0;
            break;
    }

    /* Подавляем предупреждение компилятора о неиспользуемом параметре */
    (void)regs;
}

/* ──────────────────────────────────────────────────────────────────────────
 * Публичное API
 * ────────────────────────────────────────────────────────────────────────── */

void mouse_init(void) {
    /* ── Шаг 1: Включаем вспомогательный порт PS/2 (мышь) ── */
    mouse_wait_write();
    port_byte_out(MOUSE_CMD_PORT, MOUSE_CMD_ENABLE_AUX);

    /* ── Шаг 2: Читаем текущий байт конфигурации Compaq ── */
    mouse_wait_write();
    port_byte_out(MOUSE_CMD_PORT, MOUSE_CMD_GET_COMPAQ);
    mouse_wait_read();
    u8 status = port_byte_in(MOUSE_DATA_PORT);

    /* Бит 1: включить IRQ12; бит 5: отключить mouse clock (сброс = включить) */
    status |=  0x02;    /* Включить прерывание IRQ12                          */
    status &= ~0x20;    /* Снять маску тактирования мыши (разрешить тактирование) */

    /* ── Шаг 3: Записываем обновлённый байт конфигурации ── */
    mouse_wait_write();
    port_byte_out(MOUSE_CMD_PORT, MOUSE_CMD_SET_COMPAQ);
    mouse_wait_write();
    port_byte_out(MOUSE_DATA_PORT, status);

    /* ── Шаг 4: Применяем параметры по умолчанию мыши ── */
    mouse_send_cmd(MOUSE_SET_DEFAULTS);
    mouse_read_byte();    /* Читаем ACK */

    /* ── Шаг 5: Включаем передачу пакетов ── */
    mouse_send_cmd(MOUSE_ENABLE_PACKETS);
    mouse_read_byte();    /* Читаем ACK */

    /* ── Шаг 6: Инициализируем состояние структуры ── */
    mouse_state.x        = MAX_COLS / 2;    /* Начальная позиция: центр экрана */
    mouse_state.y        = MAX_ROWS / 2;
    mouse_state.dx       = 0;
    mouse_state.dy       = 0;
    mouse_state.buttons  = 0;
    mouse_state.prev_char = ' ';
    mouse_state.prev_attr = 0x07;    /* Светло-серый на чёрном              */

    mouse_cycle    = 0;
    cursor_visible = 0;

    /* ── Шаг 7: Регистрируем обработчик IRQ12 в IDT ── */
    register_interrupt_handler(IRQ12, mouse_callback);

    /* ── Шаг 8: Рисуем курсор в начальной позиции ── */
    cursor_draw();
}

int mouse_get_x(void) {
    return mouse_state.x;
}

int mouse_get_y(void) {
    return mouse_state.y;
}

int mouse_button_left(void) {
    return (mouse_state.buttons & MOUSE_BTN_LEFT) ? 1 : 0;
}

int mouse_button_right(void) {
    return (mouse_state.buttons & MOUSE_BTN_RIGHT) ? 1 : 0;
}

int mouse_button_middle(void) {
    return (mouse_state.buttons & MOUSE_BTN_MIDDLE) ? 1 : 0;
}

const mouse_state_t *mouse_get_state(void) {
    return &mouse_state;
}

void mouse_redraw_cursor(void) {
    /* Força redesenho: apaga flag e chama draw */
    /* Принудительно перерисовываем курсор (например, после clear_screen) */
    cursor_visible = 0;
    cursor_draw();
}

void mouse_hide_cursor(void) {
    cursor_erase();
}
