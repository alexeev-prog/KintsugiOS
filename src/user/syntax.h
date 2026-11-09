/* Small extension-driven lexer. State is advanced for off-screen text too. */
enum {
    SYN_TEXT,
    SYN_C,
    SYN_ASM,
    SYN_SCRIPT,
    SYN_CONFIG
};

typedef struct {
    int language, block, quote, escaped, comment;
    u32 word_end;
} syntax_state;

static int syntax_equal(const char* a, const char* b) {
    while (*a && *b) {
        char c = *a++, d = *b++;
        if (c >= 'A' && c <= 'Z') {
            c += 'a' - 'A';
        }
        if (d >= 'A' && d <= 'Z') {
            d += 'a' - 'A';
        }
        if (c != d) {
            return 0;
        }
    }
    return !*a && !*b;
}

static int syntax_language(const char* path) {
    const char* ext = "";
    for (const char* p = path; *p; ++p) {
        if (*p == '/') {
            ext = "";
        } else if (*p == '.') {
            ext = p + 1;
        }
    }
    if (syntax_equal(ext, "c") || syntax_equal(ext, "h") || syntax_equal(ext, "cpp")
        || syntax_equal(ext, "js") || syntax_equal(ext, "ts")) {
        return SYN_C;
    }
    if (syntax_equal(ext, "asm") || syntax_equal(ext, "s") || syntax_equal(ext, "inc")) {
        return SYN_ASM;
    }
    if (syntax_equal(ext, "sh") || syntax_equal(ext, "py")) {
        return SYN_SCRIPT;
    }
    if (syntax_equal(ext, "cfg") || syntax_equal(ext, "ini") || syntax_equal(ext, "thm")) {
        return SYN_CONFIG;
    }
    return SYN_TEXT;
}

static int syntax_word(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

static unsigned char syntax_color(syntax_state* s, const char* text, u32 n, u32 i, unsigned char normal) {
    char c = text[i], next = i + 1 < n ? text[i + 1] : 0;
    if (!s->language) {
        return normal;
    }
    if (c == '\n') {
        s->comment = s->quote = s->escaped = 0;
        return normal;
    }
    if (s->comment) {
        return TUI_COMMENT;
    }
    if (s->block) {
        if (c == '/' && i && text[i - 1] == '*') {
            s->block = 0;
        }
        return TUI_COMMENT;
    }
    if (s->quote) {
        if (c == s->quote && !s->escaped) {
            s->quote = 0;
        }
        s->escaped = c == '\\' && !s->escaped;
        return TUI_STRING;
    }
    if ((s->language == SYN_C && c == '/' && next == '/') || (s->language == SYN_ASM && c == ';')
        || ((s->language == SYN_SCRIPT || s->language == SYN_CONFIG) && c == '#')
        || (s->language == SYN_CONFIG && c == ';')) {
        s->comment = 1;
        return TUI_COMMENT;
    }
    if (s->language == SYN_C && c == '/' && next == '*') {
        s->block = 1;
        return TUI_COMMENT;
    }
    if (c == '"' || c == '\'') {
        s->quote = c;
        return TUI_STRING;
    }
    if (i < s->word_end) {
        return TUI_KEYWORD;
    }
    if (syntax_word(c) && (!i || !syntax_word(text[i - 1]))) {
        static const char* words[] = { "if",     "else",     "for",     "while",  "return", "static",
                                       "const",  "int",      "char",    "void",   "struct", "typedef",
                                       "enum",   "unsigned", "include", "define", "break",  "continue",
                                       "switch", "case",     "def",     "class",  "import", "from",
                                       "then",   "fi",       "do",      "done",   "export", "mov",
                                       "push",   "pop",      "call",    "ret",    "jmp",    "cmp",
                                       "db",     "dw",       "dd" };
        u32 end = i;
        while (end < n && syntax_word(text[end])) {
            ++end;
        }
        for (u32 w = 0; w < sizeof(words) / sizeof(words[0]); ++w) {
            u32 j = 0;
            while (i + j < end && words[w][j] == text[i + j]) {
                ++j;
            }
            if (i + j == end && !words[w][j]) {
                s->word_end = end;
                return TUI_KEYWORD;
            }
        }
    }
    if (c >= '0' && c <= '9') {
        return TUI_NUMBER;
    }
    return normal;
}
