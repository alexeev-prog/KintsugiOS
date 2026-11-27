# KintsugiOS GUI: анализ, архитектура и стиль NeXTSTEP

Этот документ описывает, как устроен графический стек KintsugiOS, почему он
подлагивал, какие паттерны/алгоритмы/структуры данных применены и как выглядит
новый UI/UX — гибрид NeXTSTEP/macOS, UNIX/X11 и Windows 95.

## 1. Что было не так (разбор по слоям)

| Слой | Проблема | Причина | Решение |
| --- | --- | --- | --- |
| Framebuffer | Полный кадр 1280×800 (4 МБ) на каждое изменение | `compose()` всегда помечала весь экран dirty | Region-based invalidation: в Present уходит только объединённый dirty-rect |
| Framebuffer | Один dword за инструкцию в C-цикле | `for (col) dst[col]=src[col]` | `rep movsl` / `rep stosl` (одна строка = одна строковая инструкция) |
| Композитор | Перерисовка всех 12 window surfaces при каждом нажатии клавиши | Цикл `for (...) draw_window()` без filtrации | Retained-mode surfaces + выборочный repaint активного окна |
| KGTERM | `kfetch` показывал `?[36m` и рвал строки | Сырые ANSI-байты и разрывы между читаемыми чанками файла | Инкрементальный ANSI/SGR-парсер с состоянием строки в `gui_window` |
| KGTERM | После команд оставался «хвост» старого промпта | `gfx_text` с фоном 0 не очищает ячейки | Очистка строк промпта/вывода перед отрисовкой |
| KGED | Холст фиксирован 240×160, остальное место пустое | Canvas создавался один раз при создании окна | Canvas = client area, пересоздаётся с сохранением содержимого при resize/zoom |
| Меню | Пункты «не всегда запускаются» | Три разные ветки запуска (CLI/TUI/GUI) и дублирующие таблицы | Одиночная таблица `program_names` + `term_launch()`-диспетчер |
| KGFM | Контекстное меню заголовка «уезжало» вниз списка | Клэмп к низу экрана вместо переворота | Flip-алгоритм: если меню не влезает — открываем над курсором |

## 2. Архитектурные паттерны (исторические аналоги)

* **Retained mode + backing store** (NeXTSTEP `NSBackingStoreBuffered`, X11
  backing store, Win95 `InvalidateRect`): окно владеет поверхностью `gfx_surface`,
  приложение меняетSurface, компоновщик только копирует её в кадровый буфер.
* **Dirty rectangles / damage tracking** (X11 `Expose`, Win32 `WM_PAINT`,
  macOS `setNeedsDisplay`): каждый виджет/окно помечает прямоугольник;
  алгоритм объединения (`gfx_dirty_add`) даёт минимальный Present.
* **Event pump + hit testing** (X server event loop, AppKit `NSApplication`,
  Win32 message loop): один цикл читает мышь/клавиатуру, маршрутизирует событие
  через z-order, фокус и активный клиент.
* **Teams/Responder chain** (AppKit): глобальное меню меняется под активным
  окном; действие меню адресуется_first responder.
* **Document-centric app** (NeXTSTEP): Notepad/KGED привязаны к документу,
  `Save As` — отдельная модальная панель, а не запись в жёстко заданный путь.
* **Shell as a tool, not a UI** (UNIX): `kfetch` — обычная CLI-программа, её
  stdout перехватывается и рендерится терминалом; TUI-программы используют
  syscall 7 и capture-буфер.

## 3. Структуры данных

```c
gui_window { surface, canvas, kind, x/y/w/h, z, title,
             file_path, text, output[][], pending ANSI-строка,
             sys_tab, sys_selected, draw_damage };
zorder[MAX_WINDOWS]      // порядок перекрытия сверху вниз (массив индексов)
gfx_dirty                // прямоугольник повреждения: left/top/right/bottom/valid
external_surface[8]      // кэш поверхностей внешних X11-подобных клиентов
sys_processes[16]        // снимок process table (syscall 16)
service table            // декларативная таблица сервисов
program_names[5][9]      // единственный источник для Start/Programs/меню
```

Алгоритмы: hit-test по z-order (линейный, 12 окон), merge dirty-rects (O(n)),
вставка в z-order (swap-to-top), ANSI-конечный автомат (ESC → CSI → параметры →
команда), переворот контекстного меню (boundary flip), инкрементальный watchdog
для дочерних процессов (опрос каждые 5 кадров, не каждый).

## 4. Визуальный язык: NeXTSTEP + macOS + UNIX/Windows

* **Глобальная menus bar сверху** (NeXTSTEP/macOS): левый «K»-пункт даёт
  Windows-удобство (Programs / Search / Run / Settings / Shut Down), дальше —
  контекстные меню активного приложения File/Edit/View/Help, справа — статус
  (EN/RU, RAM, uptime, часы).
* **Traffic-light кнопки окна слева** (NeXTSTEP/macOS): close / miniaturize /
  zoom, с плоскими пиктограммами и мягкими цветами. Windows-привычка
  «закрыть крестиком справа» сохранена как hotspot на правом краю заголовка.
* **Панель/doc снизу** (macOS Dock + Windows taskbar): кнопки запущенных
  приложений, активная подсвечена, справа системный трей.
* **Сетка ровно по 12 px** (пиксельный шрифт 5×7 масштаба 2×): все панели,
  отступы и строки кратны 12/15 — текст «ровный», без «лестницы».
* **Палитра NeXTSTEP**: серый `#c0c0c0`, белый верхний блик, тёмный
  `#606060` нижняя тень, синий акцент заголовка, точечный «тистл»-орнамент
  в меню/тайтлах.
* **Терминал**: чёрный client area, зелёный/циановый промпт, моноширинная
  сетка 12×15, live-вывод, ANSI-цвета, история ↑/↓, Ctrl+C.
* **TUI-библиотека**: та же сетка, CP866 + box-drawing, цветовые роли
  (keyword/status/comment) не меняются — GUI и TUI используют один шрифт.
* **Framebuffer**: single linear framebuffer, `rep movsl`, present только
  damaged region; двойная буферизация в surface компоновщика.

## 5. Порядок внедрения

1. `rep movsl/stosl` в framebuffer и gfx.c (готово).
2. Damage-tracking в окнах; present только изменённой полосы (готово/дорабатывается).
3. Полноразмерный canvas KGED с resize без потери рисунка.
4. Исправление промпта KGTERM и очистка строк.
5. Глобальная menubar + dock + NeXTSTEP-декорация окон.
6. Документация и регрессионная проверка в QEMU.

## 6. Реализованное расширение (вторая итерация)

* **Единый каркас**: `gui_theme_t` (5 тем: Classic, NeXTSTEP, Millennium,
  Humanity, Fruit) и `gfx_theme_set()`. Все примитивы `gfx_button/gfx_bevel/
  gfx_groupbox/gfx_slider/gfx_tab/gfx_progress` читают тему; приложения не
  дублируют цвета.
* **Обои**: генератор паттернов (Solid/Grid/Diagonal/Checker/Dots/Stripes) с
  двумя цветами и масштабом. Паттерн строится один раз в `wallpaper_surface`,
  композитор делает один retained-blit.
* **Settings document**: вкладки Appearance / Dock / Sound / System, occlusion
  сохраняется в `/home/user/KGUI.CFG` (парсер `key=value`), применяется при старте.
* **Звук**: ядерный микшер с каналами `master/music/effects`; syscall 91;
  эффективное усиление `master*channel/100`; OPL2 attenuation `63-63*gain/100`.
  Foreground-проверка у регулятора громкости снята.
* **KGFM**: Shelf (Home/Bin/Etc/Logs/Root), Home по умолчанию, колонки
  Name/Size/Type/Date, дата из `kos_stat.mtime` (добавлен в ABI и заполняется
  ext2/kfs2), Properties.
* **Dock**: закреплённые приложения + запущенные окна, размер иконок, компактный
  режим.
* **Производительность**: `rep movsl/stosl` в framebuffer и gfx, damage-rect
  present, кэш поверхностей внешних клиентов, только изменённые окна.

Критерий удобства: все изменения видны сразу, переживают перезапуск и не
требуют пересборки приложений — они используют общий каркас `gfx_*`.

## 7. Третья итерация: чистый NeXTSTEP

* Dock переписан с «виндовых» текстовых плиток на центрированную иконную
  панель: крупные квадратные иконки, разделитель, точка-индикатор запущенного
  приложения, общая геометрия для рисования и hit-test (`dock_layout`).
* Menu bar и выпадающие меню: обычный текст, инверсия только у открытого
  пункта, принудительная обрезка строк (`gfx_text_clipped`).
* Заголовки окон: монохромные утопленные кнопки x/-/+, без цветных «светофоров»
  — каноничный NeXTSTEP. Кнопки и текст не выходят за границы.
* Палитра расширена до 8 тем: добавлены Graphite, Ocean, Solar.
* Композитор копирует из retained-wallpaper только повреждённый прямоугольник;
  курсор не перерисовывается при отсутствии движения и получил мягкую тень.
* Makefile: `-cpu max -smp 1 -no-reboot` во всех режимах запуска и новый
  `make run_fast` без эмуляции звука.
* TUI: буфер presentation очищается перед attach, что убирает мерцание
  старыми ячейками при повторном запуске.
