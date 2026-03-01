# Guia de Desenvolvimento - Terminal Improvements

## Sobre Esta Branch

Esta branch (`feature/terminal-improvements`) foi criada para implementar melhorias no sistema de terminal do KintsugiOS, incluindo:

- ✅ Histórico de comandos (navegação com setas)
- ✅ Múltiplos terminais virtuais
- ✅ Temas e paletas de cores
- ✅ Suporte a mouse em modo texto

## Configuração do Ambiente

### Pré-requisitos

Antes de começar, certifique-se de ter instalado:

```bash
# Verificar ambiente
./check-env.sh
```

### Dependências Necessárias

- [Kross-compiler i386 ELF GCC 15](http://newos.org/toolchains/i386-elf-15.1.0-Linux-x86_64.tar.xz)
- FASM
- NASM
- GDB (para debug)
- MTools e Xorriso
- QEMU ou Bochs
- Make
- Python 3.x

### Clone do Repositório

```bash
git clone https://github.com/ElioNeto/KintsugiOS.git
cd KintsugiOS
git checkout feature/terminal-improvements
```

## Workflow de Desenvolvimento

### 1. Compilação e Testes Locais

```bash
# Compilação rápida
make quick

# Criar imagem e rodar no QEMU
make run

# Rodar com FAT12
make run_fat12

# Debug
make debug
```

### 2. Formatação de Código

O projeto utiliza clang-format e clang-tidy para manter consistência:

```bash
# Formatar código automaticamente
python format-code.py
```

### 3. Testes

Antes de fazer commit, sempre teste:

- ✅ Compilação sem erros
- ✅ Boot no QEMU
- ✅ Funcionalidades básicas do shell
- ✅ Novas features implementadas

### 4. Commits

Siga o padrão de commits convencionais:

```bash
git add .
git commit -m "feat(terminal): adicionar histórico de comandos"
git push origin feature/terminal-improvements
```

Prefixos de commit:
- `feat`: Nova funcionalidade
- `fix`: Correção de bug
- `docs`: Documentação
- `style`: Formatação
- `refactor`: Refatoração
- `test`: Testes
- `chore`: Manutenção

## Estrutura do Código do Terminal

```
src/kernel/
├── drivers/
│   ├── screen.c/h      # Driver de tela VGA
│   └── keyboard.c/h    # Driver de teclado PS/2
├── terminal/
│   ├── terminal.c/h    # Camada de abstração do terminal
│   └── shell.c/h       # Shell Keramika
└── kklibc/
    └── stdio.c/h       # Funções de I/O
```

## Funcionalidades a Implementar

### 1. Histórico de Comandos

**Arquivo**: `src/kernel/terminal/terminal.c`

```c
// Estrutura para histórico
#define HISTORY_SIZE 50
char command_history[HISTORY_SIZE][TERMINAL_BUFFER_WIDTH];
int history_index = 0;
int history_current = 0;

// Funções a implementar:
void terminal_add_to_history(const char* command);
void terminal_history_up(void);
void terminal_history_down(void);
```

### 2. Múltiplos Terminais Virtuais

**Arquivo**: `src/kernel/terminal/terminal.c`

```c
#define MAX_VIRTUAL_TERMINALS 4

typedef struct {
    char buffer[TERMINAL_BUFFER_HEIGHT][TERMINAL_BUFFER_WIDTH];
    uint8_t colors[TERMINAL_BUFFER_HEIGHT][TERMINAL_BUFFER_WIDTH];
    int cursor_x;
    int cursor_y;
    int scroll_offset;
} virtual_terminal_t;

virtual_terminal_t terminals[MAX_VIRTUAL_TERMINALS];
int active_terminal = 0;

// Funções a implementar:
void switch_terminal(int terminal_id);
void save_terminal_state(int terminal_id);
void restore_terminal_state(int terminal_id);
```

### 3. Temas e Paletas

**Arquivo**: `src/kernel/terminal/themes.c/h` (novo)

```c
typedef struct {
    uint8_t foreground;
    uint8_t background;
    uint8_t accent;
    const char* name;
} terminal_theme_t;

// Temas pré-definidos
terminal_theme_t themes[] = {
    {VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK, VGA_COLOR_CYAN, "Default"},
    {VGA_COLOR_GREEN, VGA_COLOR_BLACK, VGA_COLOR_LIGHT_GREEN, "Matrix"},
    {VGA_COLOR_CYAN, VGA_COLOR_BLUE, VGA_COLOR_WHITE, "Ocean"},
    {VGA_COLOR_YELLOW, VGA_COLOR_BROWN, VGA_COLOR_WHITE, "Sepia"}
};

void apply_theme(int theme_id);
```

### 4. Suporte a Mouse

**Arquivo**: `src/kernel/drivers/mouse.c/h` (novo)

```c
// Driver PS/2 Mouse
void mouse_init(void);
void mouse_handler(void);
int mouse_get_x(void);
int mouse_get_y(void);
bool mouse_button_left(void);
bool mouse_button_right(void);
```

## Diretrizes de Código

### Comentários

**Todos os comentários devem ser em russo**, conforme o padrão do projeto:

```c
// Инициализация истории команд
void terminal_init_history(void) {
    // Очистка буфера истории
    for (int i = 0; i < HISTORY_SIZE; i++) {
        memset(command_history[i], 0, TERMINAL_BUFFER_WIDTH);
    }
}
```

### Estilo de Código

- Indentação: 4 espaços (tabs)
- Chaves: estilo K&R
- Nomes de funções: snake_case
- Constantes: UPPER_CASE
- Use clang-format antes de fazer commit

## Testando GitHub Actions Localmente

Para testar o workflow de documentação antes de enviar:

```bash
# Instalar doxygen
sudo apt install doxygen

# Gerar documentação
doxygen

# Verificar saída
ls -la docs/html/
```

## Configuração do GitHub Pages

O workflow `static.yml` automaticamente faz deploy da documentação Doxygen para GitHub Pages quando você faz push na branch `main`.

Para testar antes de mergear:

1. Certifique-se que GitHub Pages está habilitado no repositório
2. Configure para usar GitHub Actions como source
3. O workflow rodará automaticamente em cada push na main

## Checklist Antes de Abrir PR

- [ ] Código compila sem warnings
- [ ] Testes no QEMU executados com sucesso
- [ ] Código formatado com clang-format
- [ ] Comentários em russo adicionados
- [ ] Documentação atualizada
- [ ] CHANGELOG.md atualizado
- [ ] Commits seguem padrão convencional
- [ ] Workflow do GitHub Actions testado

## Recursos Úteis

- [Documentação do Projeto](https://elioneto.github.io/KintsugiOS/)
- [OSDev Wiki](http://osdev.org)
- [Repositório Original](https://github.com/alexeev-prog/KintsugiOS)

## Contato

Dúvidas ou sugestões?
- GitHub: [@ElioNeto](https://github.com/ElioNeto)
- Email: netoo.elio@hotmail.com

---

**Boa sorte com o desenvolvimento! 🚀**
