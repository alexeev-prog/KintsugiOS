#ifndef KERNEL_H
#define KERNEL_H

/**
 * @brief Обработка пользовательского ввода в шелле
 *
 * @param input
 **/
void user_input(char* input);

extern int shell_cursor_offset;
extern int shell_prompt_offset;

#endif
