/*------------------------------------------------------------------------------
 *  Kintsugi OS Drivers source code
 *  File: kernel/drivers/lowlevel_io.h
 *  Title: Заголовочный файл для drivers/lowlevel_io.c
 */

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
unsigned char port_word_in(unsigned short port);

/**
 * @brief Port word out
 *
 * @param port порт
 * @param data данные
 **/
void port_word_out(unsigned short port, unsigned short data);
