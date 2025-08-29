/*------------------------------------------------------------------------------
 *  Kintsugi OS Drivers source code
 *  File: kernel/drivers/lowlevel_io.h
 *  Title: Заголовочный файл для drivers/lowlevel_io.c
 */

#include "../kklibc/ctypes.h"

/**
 * @brief Port byte in
 *
 * @param port порт
 **/
unsigned char port_byte_in(unsigned short port);

/**
 * @brief Port byte out
 *
 * @param port порт
 * @param data данные
 **/
void port_byte_out(unsigned short port, unsigned char data);

/**
 * @brief Poort word in
 *
 * @param port порт
 * @return unsigned char
 **/
u16 port_word_in(u16 port);

/**
 * @brief Port word out
 *
 * @param port порт
 * @param data данные
 **/
void port_word_out(unsigned short port, unsigned short data);

/**
 * @brief Чтение нескольких слов из порта
 * @param port порт
 * @param buffer буфер для данных
 * @param count количество слов
 */
void insw(u16 port, void* buffer, u32 count);

/**
 * @brief Запись нескольких слов в порт
 * @param port порт
 * @param buffer буфер с данными
 * @param count количество слов
 */
void outsw(u16 port, void* buffer, u32 count);
