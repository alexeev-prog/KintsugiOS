
#ifndef UTILS_H
#define UTILS_H

/**
 * @brief Команда обертка шелла для бинарного возведения в степень
 *
 * @param args аргументы
 **/
void binary_pow_command(char** args);

/**
 * @brief Команда обертка шелла для генерации случайного числа
 *
 * @param args аргументы
 **/
void rand_command(char** args);

/**
 * @brief Команда обертка шелла для генерации случайного числа в диапазоне
 *
 * @param args аргументы
 **/
void rand_range_command(char** args);

/**
 * @brief Команда обертка шелла для перезагрузки
 *
 * @param args аргументы
 **/
void reboot_command(char** args);

/**
 * @brief Команда обертка шелла для ожидания
 *
 * @param args аргументы
 **/
void sleep_command(char** args);

/**
 * @brief Команда обертка шелла для выключения QEMU
 *
 * @param args аргументы
 **/
void shutdown_qemu(char** args);

/**
 * @brief Команда для халтинга процессора
 *
 * @param args аргументы
 **/
void halt_cpu(char** args);

/**
 * @brief Команда для фетчинга данных об ОС
 *
 * @param args аргументы
 **/
void info_command_shell(char** args);

/**
 * @brief Команда обертка шелла для дампа памяти
 *
 * @param args аргументы
 **/
void mem_dump(char** args);

/**
 * @brief Команда очистки
 *
 * @param args аргументы
 **/
void clear_screen_command(char** args);

/**
 * @brief Команда обертка шелла для аллокации памяти (kmalloc)
 *
 * @param args аргументы
 **/
void kmalloc_command(char** args);

/**
 * @brief Команда эхо-вывода
 *
 * @param args аргументы
 **/
void echo_command(char** args);

/**
 * @brief Команда обертка шелла для освобождения памяти (free)
 *
 * @param args аргументы
 **/
void free_command(char** args);

void ls_command(char** args);

void cat_command(char** args);

void load_command(char** args);

void print_fat12_info_command(char** args);

#endif
