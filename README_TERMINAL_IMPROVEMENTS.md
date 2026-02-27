# Terminal Improvements Branch - KintsugiOS

![Build Status](https://github.com/ElioNeto/KintsugiOS/actions/workflows/ci-build-test.yml/badge.svg?branch=feature/terminal-improvements)

## 🚧 Branch de Desenvolvimento Ativa

Esta é uma branch de desenvolvimento do fork do [KintsugiOS](https://github.com/alexeev-prog/KintsugiOS) focada em melhorias do sistema de terminal.

### 🎯 Objetivos

Implementar as seguintes funcionalidades no terminal do KintsugiOS:

1. **Histórico de Comandos** – Navegação com setas para cima/baixo
2. **Múltiplos Terminais Virtuais** – Suporte para várias sessões (Alt+F1, F2, etc.)
3. **Temas e Paletas de Cores** – Sistema configurável de cores
4. **Suporte a Mouse** – Driver PS/2 mouse para modo texto

## 🛠️ Setup do Ambiente

### Clone e Preparação

```bash
# Clone o fork
git clone https://github.com/ElioNeto/KintsugiOS.git
cd KintsugiOS

# Checkout da branch de desenvolvimento
git checkout feature/terminal-improvements

# Verificar ambiente
./check-env.sh
```

### Dependências

Você precisa ter instalado:

- **i386-elf-gcc 15** - [Download aqui](http://newos.org/toolchains/i386-elf-15.1.0-Linux-x86_64.tar.xz)
- **NASM/FASM** - Assemblers
- **QEMU** - Emulador
- **Python 3** - Scripts de build
- **Make** - Sistema de build
- **Doxygen** - Documentação

#### Instalação no Ubuntu/Debian

```bash
sudo apt update
sudo apt install -y nasm fasm qemu-system-x86 mtools xorriso gdb python3 doxygen

# Download e extração do cross-compiler
wget http://newos.org/toolchains/i386-elf-15.1.0-Linux-x86_64.tar.xz
tar xf i386-elf-15.1.0-Linux-x86_64.tar.xz
echo 'export PATH="$HOME/i386-elf-15.1.0-Linux-x86_64/bin:$PATH"' >> ~/.bashrc
source ~/.bashrc
```

## 💻 Workflow de Desenvolvimento

### Compilação

```bash
# Compilação rápida (bootloader + kernel)
make quick

# Build completo com imagem de disco
make run

# Com sistema de arquivos FAT12
make run_fat12
```

### Testing

```bash
# Rodar no QEMU
make run

# Debug com GDB
make debug
# Em outro terminal:
gdb build/kernel.bin
(gdb) target remote localhost:1234
(gdb) continue
```

### Formatação

```bash
# Formatar todo o código C/C++
python format-code.py
```

## 📝 Estrutura de Arquivos Relevantes

```
KintsugiOS/
├── src/
│   ├── kernel/
│   │   ├── drivers/
│   │   │   ├── screen.c/h        # ⭐ Driver VGA
│   │   │   ├── keyboard.c/h      # ⭐ Driver teclado
│   │   │   └── mouse.c/h         # 🆕 NOVO: Driver mouse
│   │   ├── terminal/
│   │   │   ├── terminal.c/h      # ⭐ Abstração do terminal
│   │   │   ├── shell.c/h         # ⭐ Keramika Shell
│   │   │   ├── history.c/h       # 🆕 NOVO: Histórico
│   │   │   └── themes.c/h        # 🆕 NOVO: Temas
│   │   └── kklibc/
│   │       └── stdio.c/h         # Funções I/O
│   └── bootloader/
├── .github/
│   └── workflows/
│       ├── static.yml            # Deploy docs
│       └── ci-build-test.yml     # ✅ CI/CD testes
├── DEVELOPMENT.md                # 📖 Guia detalhado
└── README_TERMINAL_IMPROVEMENTS.md  # Este arquivo
```

## ✅ CI/CD e Testes Automatizados

Esta branch possui um **workflow de CI/CD completo** que roda automaticamente:

### O que é testado

1. **Build Test** – Compilação do bootloader e kernel
2. **QEMU Test** – Boot rápido no emulador
3. **Code Quality** – Verificação de formatação
4. **Documentation** – Geração da documentação Doxygen

### Status dos Workflows

Você pode acompanhar o status em:
- [GitHub Actions](https://github.com/ElioNeto/KintsugiOS/actions)

### Como garantir que seu código passará no CI

```bash
# 1. Formatar código
python format-code.py

# 2. Compilar localmente
make clean && make quick

# 3. Testar no QEMU
make run

# 4. Gerar documentação
doxygen

# 5. Verificar que tudo está OK
ls -la build/ docs/html/
```

Se todos esses passos funcionarem localmente, seu PR passará no CI! ✅

## 📚 Documentação

- **Guia Completo de Desenvolvimento**: Veja [DEVELOPMENT.md](./DEVELOPMENT.md)
- **Documentação Doxygen**: Gerada automaticamente em `docs/html/`
- **README Original**: [README.md](./README.md)

## 🚀 Roadmap de Implementação

### Fase 1: Histórico de Comandos (🔵 Em Andamento)
- [ ] Estrutura de dados para histórico
- [ ] Integração com keyboard driver (setas)
- [ ] Navegação up/down no histórico
- [ ] Persistência de histórico
- [ ] Comando `history` no shell

### Fase 2: Temas de Cores (⚪ Pendente)
- [ ] Criar arquivo `themes.c/h`
- [ ] Definir estrutura de temas
- [ ] Implementar 4-5 temas predefinidos
- [ ] Comando `theme` no shell
- [ ] Salvar preferência de tema

### Fase 3: Terminais Virtuais (⚪ Pendente)
- [ ] Estrutura para múltiplos terminais
- [ ] Funções de switch entre terminais
- [ ] Atalhos de teclado (Alt+F1, F2, etc.)
- [ ] Indicador visual de terminal ativo
- [ ] Estado independente por terminal

### Fase 4: Suporte a Mouse (⚪ Pendente)
- [ ] Driver PS/2 Mouse
- [ ] IRQ12 handler
- [ ] Detecção de movimento
- [ ] Botões esquerdo/direito
- [ ] Cursor visual em modo texto
- [ ] Seleção de texto com mouse

## 📋 Diretrizes de Contribuição

### Padrão de Commits

```
feat(terminal): adicionar histórico de comandos
fix(keyboard): corrigir detecção de seta para cima
docs(readme): atualizar instruções de setup
style(terminal): formatar código com clang-format
test(shell): adicionar testes de histórico
```

### Comentários no Código

**IMPORTANTE**: Todos os comentários devem ser em **russo** para manter consistência com o projeto original:

```c
// Инициализация истории команд
void terminal_init_history(void) {
    // Очистка буфера
    memset(command_history, 0, sizeof(command_history));
}
```

### Checklist do PR

Antes de abrir um Pull Request para o repositório original:

- [ ] Código compila sem warnings
- [ ] Testado no QEMU (boot + funcionalidade)
- [ ] Código formatado (`python format-code.py`)
- [ ] Comentários em russo
- [ ] CI/CD passing (todos workflows verdes ✅)
- [ ] Documentação atualizada
- [ ] CHANGELOG.md atualizado
- [ ] README atualizado se necessário

## 🔗 Links Úteis

- **Fork Original**: [alexeev-prog/KintsugiOS](https://github.com/alexeev-prog/KintsugiOS)
- **Este Fork**: [ElioNeto/KintsugiOS](https://github.com/ElioNeto/KintsugiOS)
- **Documentação**: [GitHub Pages](https://elioneto.github.io/KintsugiOS/)
- **OSDev Wiki**: [osdev.org](http://osdev.org)
- **Actions**: [CI/CD Status](https://github.com/ElioNeto/KintsugiOS/actions)

## 💬 Contato

**Elio Neto**
- GitHub: [@ElioNeto](https://github.com/ElioNeto)
- Email: netoo.elio@hotmail.com
- LinkedIn: [Elio Neto](https://www.linkedin.com/in/elio-neto/)

## 📝 Licença

Este projeto mantém a licença MIT do projeto original. Veja [LICENSE](./LICENSE).

---

**Status**: 🚧 Em Desenvolvimento Ativo | **Última Atualização**: 27/02/2026
