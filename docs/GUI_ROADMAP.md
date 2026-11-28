# План работ: UI/UX, темы, KGFM, звук, UNIX-база

Документ отвечает на вопросы **что**, **почему**, **как** и в каком порядке.
Источник вдохновения: NeXTSTEP/AppKit, macOS, UNIX/X11, Windows 2000.

## 0. Принципы

1. **Единый каркас.** Все GUI-приложения рисуют через `gfx_*` и читают
   глобальную тему `gui_theme`. Приложение неhardcode-ит цвета и геометрию —
   оно использует те же примитивы, что и компоновщик.
2. **Retained surface + damage rect.** Окно владеетsurface; компоновщик
   перерисовывает только повреждённую область; Present идёт в framebuffer
   строковыми инструкциями (`rep movsl`).
3. **Документ = состояние.** Настройки, закладки KGFM и микшер хранятся в
   текстовом конфиге `/home/user/KGUI.CFG` (ключ=значение), читаются при
   старте и записываются по изменению.
4. **Позволь пользователю.** Shelf, Home, Dock, темы, паттерны, микшер —
   это не декорация, а рабочие инструменты.

## 1. Аудит (что найдено)

| Подсистема | Найденное | Решение |
| --- | --- | --- |
| gfx | Палитра захардкожена макросами `GFX_FACE/GFX_SHADOW/...`; `gfx_button/gfx_bevel` не знают про темы | Ввести `gui_theme_t`, все примитивы читают её |
| Settings (`kind==5`) | 7 сплошных цветов, язык, список аккаунтов. Нет обоев, паттернов, тем, микшера, Dock | Переписан как Settings document с вкладками |
| KGFM | `path=/bin`, 80 записей, колонки Name/Size, нет Shelf/Home/даты/типа | Модель + Shelf + колонки + Home |
| fs ABI | `kos_stat` = size/directory/readonly, нет mtime | Добавить `u32 mtime`; заполнить в ext2/kfs2 |
| sound | Один master volume; syscall требует foreground; нет каналов | Каналы master/music/effects, снять foreground-проверку |
| Titlebar | Кнопки рисуются в `x+8/26/44` при `x=2`, hit-test `8..62`; заголовок может вылезти за плашку | Точные прямоугольники + clipped text |
| Start menu | Строка «KintsugiOS 95» | «KintsugiOS OS» |
| Dock | Есть плитки запущенных окон, нет закреплённых приложений и настроек | Pin + autohide + size |

## 2. Структуры данных

```c
typedef struct {
    const char *name, *author;
    u32 face, shadow, light, title, title_text, text, accent,
        desktop, selection, success, warning, error;
    int flat;           /* 0 = 3D bevel, 1 = minimal */
    int icon_brightness;/* Fruit/NeXTSTEP: яркие иконки */
} gui_theme_t;

typedef enum { WALL_SOLID, WALL_GRID, WALL_DIAGONAL, WALL_CHECKER,
               WALL_DOTS, WALL_STRIPES, WALL_PATTERN_COUNT } wallpaper_kind;

typedef struct {
    int theme, wallpaper, wall_color_a, wall_color_b, wall_scale;
    int dock_size, dock_autohide, dock_visible;
    int volume_master, volume_music, volume_effects;
    char home[160], favorites[8][160];
} gui_settings_t;

typedef struct {
    char path[160];
    kos_long_dirent entries[80];
    int count, selected, top;
    char shelf[8][160];
    int shelf_count, shelf_selected;
} fm_model_t;  // встраивается в gui_window
```

## 3. Алгоритмы

* **3D-bevel**: две полосы света и тени, `light = mix(face, white, k)`,
  `shadow = mix(face, black, k)`; для flat-тем `k=0`.
* **Wallpaper pattern**: детерминированная функция `pattern(x,y,scale)` на
  модульной арифметике (grid/diagonal/checker/dots/stripes), рисуется один раз
  в `desktop`surface при смене темы/пар — не в каждом кадре.
* **Clipped text**: отбрасываются глифы за пределами плашки/колонки —
  устранение выхода за границы.
* **Hit-test**: прямоугольники кнопок вычисляются теми же константами, что и
  отрисовка (единый `title_button_rect`).
* **Mix**: эффективная громкость = `master * channel / 100`; OPL2 attenuation
  `63 - 63*eff/100`; PC speaker mute при `eff==0`.
* **Config I/O**: парser `key=value` с фиксированным буфером, запись через
  `syscall(10, path, data, len)` (VFS replace).
* **Dirty union**: `gfx_dirty_add` остаётся; темы/обои меняют только `desktop`
  и помечают полный кадр один раз.

## 4. Этапы и критерии приёмки

1. **Границы и naming.** Исправить hit-test/отрисовку кнопок, добавить
   clipped title, заменить «Kintsugi 95». Критерий: клики по кнопкам точно
   совпадают с рисунком, строка в меню обновлена.
2. **Unified theme framework.** `gui_theme_t`, 5 тем, `gfx_theme_set`,
   примитивы используют тему. Критерий: смена темы меняет окна, меню, Dock, кнопки.
3. **Settings document.** Вкладки Appearance (тема+паттерн+цвета), Dock,
   Sound (микшер), System (язык/аккаунты). Сохранение в KGUI.CFG.
   Критерий: настройки применяются сразу и переживают перезапуск.
4. **Sound mixer.** Ядерные каналы + syscall 91; UI-слайдеры.
   Критерий: регулятор меняет реальную громкость OPL2/speaker.
5. **KGFM.** Shelf (Home, /bin, /etc, Trash, избранное), Home по умолчанию,
   колонки Name/Size/Type/Date, Properties. Критерий: навигация мышью и
   клавиатурой, дата из mtime, Shelf кликабелен.
6. **Dock advanced.** Закреплённые приложения, размер, скрытие.
   Критерий: лаунчеры запускают приложения, настройки работают.
7. **UNIX-база.** `stat.mtime`, снятие foreground-ограничения у звука.
8. **Оптимизация.** Профилирование в QEMU, уменьшение Present-областей.
9. **Документация и регрессия.** `make all`, `make test-tui`, QMP-скриншоты.

## 6. Четвёртая итерация (NeXTSTEP редизайн) — в работе

* Спец-шрифт (bold) через синтез глифов, единый глиф-пайплайн GUI+TUI.
* Анимация открытия/закрытия окон (open/close), pressed-state кнопок.
* Dock magnification, курсор с мягкой тенью и расширенным hit-area, дефолт
  темы NeXTSTEP, исправление titlebar «невидимого» состояния.
* Визуальные ограничения: сетка 12/15 px, диалоги в рабочей области, клип текста.


## 5. Риски

* Изменение `kos_stat` ломает ABI — пересобрать всё (`make -B all`, образы).
* Тема Fruit requiring яркие иконки — использовать существующие `draw_icon`
  с палитрой темы, без новых растров.
* Ограниченное место на floppy — новые ELF не нужны, меняются только
  KGUI/ext2-образ.
