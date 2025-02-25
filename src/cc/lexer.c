#include "../config.h"
#include "lexer.h"

#include <assert.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdlib.h>  // malloc, strtoul
#include <string.h>
#include <sys/types.h>  // ssize_t

#include "table.h"
#include "util.h"

Token *alloc_token(enum TokenKind kind, Line *line, const char *begin, const char *end) {
  if (end == NULL) {
    assert(begin != NULL);
    end = begin + strlen(begin);
  }
  Token *token = malloc(sizeof(*token));
  token->kind = kind;
  token->line = line;
  token->begin = begin;
  token->end = end;
  return token;
}

bool auto_concat_string_literal;

static const struct {
  const char *str;
  enum TokenKind kind;
} kReservedWords[] = {
  {"if", TK_IF},
  {"else", TK_ELSE},
  {"switch", TK_SWITCH},
  {"case", TK_CASE},
  {"default", TK_DEFAULT},
  {"do", TK_DO},
  {"while", TK_WHILE},
  {"for", TK_FOR},
  {"break", TK_BREAK},
  {"continue", TK_CONTINUE},
  {"goto", TK_GOTO},
  {"return", TK_RETURN},
  {"void", TK_VOID},
  {"char", TK_CHAR},
  {"short", TK_SHORT},
  {"int", TK_INT},
  {"long", TK_LONG},
  {"const", TK_CONST},
  {"unsigned", TK_UNSIGNED},
  {"signed", TK_SIGNED},
  {"static", TK_STATIC},
  {"inline", TK_INLINE},
  {"extern", TK_EXTERN},
  {"volatile", TK_VOLATILE},
  {"auto", TK_AUTO},
  {"register", TK_REGISTER},
  {"struct", TK_STRUCT},
  {"union", TK_UNION},
  {"enum", TK_ENUM},
  {"sizeof", TK_SIZEOF},
  {"_Alignof", TK_ALIGNOF},
  {"typedef", TK_TYPEDEF},
  {"__asm", TK_ASM},
#ifndef __NO_FLONUM
  {"float", TK_FLOAT},
  {"double", TK_DOUBLE},
#endif
};

static const struct {
  const char ident[4];
  enum TokenKind kind;
} kMultiOperators[] = {
  {"<<=", TK_LSHIFT_ASSIGN},
  {">>=", TK_RSHIFT_ASSIGN},
  {"...", TK_ELLIPSIS},
  {"==", TK_EQ},
  {"!=", TK_NE},
  {"<=", TK_LE},
  {">=", TK_GE},
  {"+=", TK_ADD_ASSIGN},
  {"-=", TK_SUB_ASSIGN},
  {"*=", TK_MUL_ASSIGN},
  {"/=", TK_DIV_ASSIGN},
  {"%=", TK_MOD_ASSIGN},
  {"&=", TK_AND_ASSIGN},
  {"|=", TK_OR_ASSIGN},
  {"^=", TK_HAT_ASSIGN},
  {"++", TK_INC},
  {"--", TK_DEC},
  {"->", TK_ARROW},
  {"&&", TK_LOGAND},
  {"||", TK_LOGIOR},
  {"<<", TK_LSHIFT},
  {">>", TK_RSHIFT},

  {"##", PPTK_CONCAT},
};

static const char kSingleOperatorTypeMap[] = {  // enum TokenKind
  ['+'] = TK_ADD,
  ['-'] = TK_SUB,
  ['*'] = TK_MUL,
  ['/'] = TK_DIV,
  ['%'] = TK_MOD,
  ['&'] = TK_AND,
  ['|'] = TK_OR,
  ['^'] = TK_HAT,
  ['<'] = TK_LT,
  ['>'] = TK_GT,
  ['!'] = TK_NOT,
  ['('] = TK_LPAR,
  [')'] = TK_RPAR,
  ['{'] = TK_LBRACE,
  ['}'] = TK_RBRACE,
  ['['] = TK_LBRACKET,
  [']'] = TK_RBRACKET,
  ['='] = TK_ASSIGN,
  [':'] = TK_COLON,
  [';'] = TK_SEMICOL,
  [','] = TK_COMMA,
  ['.'] = TK_DOT,
  ['?'] = TK_QUESTION,
  ['~'] = TK_TILDA,

  ['#'] = PPTK_STRINGIFY,
};

Lexer lexer;
static Table reserved_word_table;
static LexEofCallback lex_eof_callback;

void lex_error(const char *p, const char *fmt, ...) {
  fprintf(stderr, "%s(%d): ", lexer.filename, lexer.lineno);

  va_list ap;
  va_start(ap, fmt);
  vfprintf(stderr, fmt, ap);
  va_end(ap);
  fprintf(stderr, "\n");

  if (lexer.line != NULL)
    show_error_line(lexer.line->buf, p, 1);

  exit(1);
}

static Token *alloc_ident(const Name *name, Line *line, const char *begin, const char *end) {
  Token *tok = alloc_token(TK_IDENT, line, begin, end);
  tok->ident = name;
  return tok;
}

Token *alloc_dummy_ident(void) {
  const Name *label = alloc_label();
  return alloc_ident(label, NULL, label->chars, label->chars + label->bytes);
}

static void init_reserved_word_table(void) {
  table_init(&reserved_word_table);

  // Reserved words.
  for (int i = 0, n = (int)(sizeof(kReservedWords) / sizeof(*kReservedWords)); i < n; ++i) {
    const Name *key = alloc_name(kReservedWords[i].str, NULL, false);
    table_put(&reserved_word_table, key, (void*)(intptr_t)kReservedWords[i].kind);
  }

  // Multi-char operators.
  for (int i = 0, n = (int)(sizeof(kMultiOperators) / sizeof(*kMultiOperators)); i < n; ++i) {
    const Name *key = alloc_name(kMultiOperators[i].ident, NULL, false);
    table_put(&reserved_word_table, key, (void*)(intptr_t)kMultiOperators[i].kind);
  }
}

static enum TokenKind reserved_word(const Name *name) {
  void *kind = table_get(&reserved_word_table, name);
  return kind != NULL ? (enum TokenKind)(intptr_t)kind : TK_EOF;
}

static int backslash(int c, const char **pp) {
  switch (c) {
  case '0':
    if (!isoctal((*pp)[1]))
      return '\0';
    // Fallthrough
  case '1': case '2': case '3': case '4': case '5': case '6': case '7':
    {
      const char *p = *pp + 1;
      int v = c - '0';
      for (int i = 0; i < 2; ++i, ++p) {
        char c2 = *p;
        if (!isoctal(c2))
          break;
        v = (v << 3) | (c2 - '0');
      }
      *pp = p - 1;
      return v;
    }
  case 'x':
    {
      const char *p = *pp + 1;
      c = 0;
      for (int i = 0; i < 2; ++i, ++p) {
        int v = xvalue(*p);
        if (v < 0)
          break;  // TODO: Error
        c = (c << 4) | v;
      }
      *pp = p - 1;
      return c;
    }
  case 'a':  return '\a';
  case 'b':  return '\b';
  case 'f':  return '\f';
  case 'n':  return '\n';
  case 'r':  return '\r';
  case 't':  return '\t';
  case 'v':  return '\v';

  default:
    lex_error(*pp, "Illegal escape");
    // Fallthrough
  case '\'': case '"': case '\\':
    return c;
  }
}

LexEofCallback set_lex_eof_callback(LexEofCallback callback) {
  LexEofCallback old = lex_eof_callback;
  lex_eof_callback = callback;
  return old;
}

bool lex_eof_continue(void) {
  return (lex_eof_callback != NULL &&
          (*lex_eof_callback)());
}

void init_lexer(void) {
  init_reserved_word_table();
  auto_concat_string_literal = false;
}

void set_source_file(FILE *fp, const char *filename) {
  lexer.fp = fp;
  lexer.filename = filename;
  lexer.line = NULL;
  lexer.p = "";
  lexer.idx = -1;
  lexer.lineno = 0;
}

void set_source_string(const char *line, const char *filename, int lineno) {
  Line *p = malloc(sizeof(*p));
  p->filename = lexer.filename;
  p->buf = line;
  p->lineno = lineno;

  lexer.fp = NULL;
  lexer.filename = filename;
  lexer.line = p;
  lexer.p = line;
  lexer.idx = -1;
  lexer.lineno = lineno;
}

const char *get_lex_p(void) {
  if (lexer.idx < 0)
    return lexer.p;
  else
    return lexer.fetched[lexer.idx]->begin;
}

static int scan_linemarker(const char *line, long *pnum, char **pfn, int *pflag) {
  const char *p = line;
  if (p[0] != '#' || !isspace(p[1]))
    return 0;
  p = skip_whitespaces(p + 2);

  int n = 0;
  const char *next = p;
  unsigned long num = strtoul(next, (char**)&next, 10);
  if (next > p) {
    ++n;
    *pnum = num;
    if (isspace(*next) && (p = skip_whitespaces(next), *p == '"')) {
      p += 1;
      const char *q = strchr(p, '"');
      if (q != NULL) {
        ++n;
        *pfn = strndup(p, q - p);
        p = q + 1;

        if (isspace(*p)) {
          p = skip_whitespaces(p);
          next = p;
          int flag = strtol(next, (char**)&next, 10);
          if (next > p) {
            ++n;
            *pflag = flag;
          }
        }
      }
    }
  }
  return n;
}

static void read_next_line(void) {
  if (lexer.fp == NULL) {
    if (!lex_eof_continue()) {
      lexer.p = NULL;
      lexer.line = NULL;
    }
    return;
  }

  char *line = NULL;
  size_t capa = 0;
  for (;;) {
    ssize_t len = getline_cont(&line, &capa, lexer.fp, &lexer.lineno);
    if (len == -1) {
      if (lex_eof_continue())
        continue;

      lexer.p = NULL;
      lexer.line = NULL;
      return;
    }

    if (line[0] != '#')
      break;

    // linemarkers: # linenum filename flags
    long num = -1;
    char *fn;
    int flag = -1;
    int n = scan_linemarker(line, &num, &fn, &flag);
    if (n >= 2) {
      lexer.lineno = num - 1;
      lexer.filename = fn;
    }
  }

  Line *p = malloc(sizeof(*p));
  p->filename = lexer.filename;
  p->buf = line;
  p->lineno = lexer.lineno;
  lexer.line = p;
  lexer.p = lexer.line->buf;
}

const char *block_comment_start(const char *p) {
  const char *q = skip_whitespaces(p);
  return (*q == '/' && q[1] == '*') ? q : NULL;
}

const char *block_comment_end(const char *p) {
  for (;;) {
    p = strchr(p, '*');
    if (p == NULL)
      return NULL;
    if (*(++p) == '/')
      return p + 1;
  }
}

static const char *skip_block_comment(const char *p) {
  for (;;) {
    p = block_comment_end(p);
    if (p != NULL)
      break;

    read_next_line();
    p = lexer.p;
    if (p == NULL)
      break;
  }
  return p;
}

static const char *skip_line_comment(void) {
  read_next_line();
  return lexer.p;
}

static const char *skip_whitespace_or_comment(const char *p) {
  for (;;) {
    p = skip_whitespaces(p);
    switch (*p) {
    case '\0':
      read_next_line();
      p = lexer.p;
      if (p == NULL)
        return NULL;
      continue;
    case '/':
      switch (p[1]) {
      case '*':
        {
          const char *q = skip_block_comment(p + 2);
          if (q == NULL)
            lex_error(p, "Block comment not closed");
          p = q;
        }
        continue;
      case '/':
        p = skip_line_comment();
        if (p == NULL)
          return NULL;
        continue;
      default:  break;
      }
      break;
    default:  break;
    }
    break;
  }
  return p;
}

#ifndef __NO_FLONUM
static Token *read_flonum(const char **pp) {
  const char *start = *pp;
  char *next;
  double val = strtod(start, &next);
  enum TokenKind tk = TK_DOUBLELIT;
  if (tolower(*next) == 'f') {
    tk = TK_FLOATLIT;
    ++next;
  }
  Token *tok = alloc_token(tk, lexer.line, start, next);
  tok->flonum = val;
  *pp = next;
  return tok;
}
#endif

static Token *read_num(const char **pp) {
  const char *start = *pp, *p = start;
  int base = 10;
  bool is_unsigned = false;
  if (*p == '0') {
    char c = tolower(p[1]);
    if (c == 'x') {
      base = 16;
      is_unsigned = true;
      p += 2;
      c = tolower(*p);
      if (!isxdigit(c))
        lex_error(p, "Hexadecimal expected");
    } else if (isdigit(c)) {
      if (c >= '8')
        lex_error(p, "Octal expected");
      base = 8;
      is_unsigned = true;
    }
  }
  const char *q = p;
  unsigned long long val = strtoull(p, (char**)&p, base);
  if (p == q)
    lex_error(p, "Illegal literal");

#ifndef __NO_FLONUM
  if (*p == '.' || tolower(*p) == 'e') {
    if (base != 10)
      lex_error(p, "Illegal literal");
    return read_flonum(pp);
  }
#endif
  enum TokenKind tt = TK_INTLIT;
  if (tolower(*p) == 'u') {
    is_unsigned = true;
    ++p;
  }
  if (tolower(*p) == 'l') {
    tt = TK_LONGLIT;
    ++p;
    if (tolower(*p) == 'l') {
      tt = TK_LLONGLIT;
      ++p;
    }
  } else {
    const int INT_BYTES = 4;  // TODO: Detect.
    int bits = INT_BYTES * TARGET_CHAR_BIT;
    unsigned long long threshold = 1UL << (bits - (is_unsigned ? 0 : 1));
    if (val >= threshold)
      tt = TK_LONGLIT;
  }
  Token *tok = alloc_token(tt + (is_unsigned ? (TK_UINTLIT - TK_INTLIT) : 0), lexer.line, start, p);
  tok->fixnum = val;
  *pp = p;
  return tok;
}

const char *read_ident(const char *p_) {
  const unsigned char *p = (const unsigned char *)p_;
  unsigned char uc = *p;
  int ucc = isutf8first(uc) - 1;
  if (!(ucc > 0 || isalpha(uc) || uc == '_'))
    return NULL;

  for (;;) {
    uc = *++p;
    if (ucc > 0) {
      if (!isutf8follow(uc)) {
        lex_error(p_, "Illegal byte sequence");
        break;
      }
      --ucc;
      continue;
    }
    if ((ucc = isutf8first(uc) - 1) > 0)
      continue;
    if (!isalnum_(uc))
      break;
  }
  return (const char*)p;
}

static Token *read_char(const char **pp) {
  const char *p = *pp;
  const char *begin = p++;
  int c = *(unsigned char*)p;
  if (c == '\'')
    lex_error(p, "Empty character");
  if (c == '\\') {
    c = *(unsigned char*)(++p);
    if (c == '\0')
      --p;
    else
      c = backslash(c, &p);
  }
  if (*(++p) != '\'')
    lex_error(p, "Character not closed");

  ++p;
  Token *tok = alloc_token(TK_CHARLIT, lexer.line, begin, p);
  tok->fixnum = c;
  *pp = p;
  return tok;
}

static Token *read_string(const char **pp) {
  const int ADD = 16;
  const char *p = *pp;
  const char *begin, *end;
  size_t capa = 16, size = 0;
  char *str = malloc(capa);
  for (;;) {
    begin = p++;  // Skip first '"'
    for (int c; (c = *(unsigned char*)p++) != '"'; ) {
      if (c == '\0')
        lex_error(p - 1, "String not closed");
      if (size + 1 >= capa) {
        capa += ADD;
        str = realloc(str, capa);
        if (str == NULL)
          lex_error(p, "Out of memory");
      }

      if (c == '\\') {
        c = *(unsigned char*)p;
        if (c == '\0')
          lex_error(p, "String not closed");
        c = backslash(c, &p);
        ++p;
      }
      assert(size < capa);
      str[size++] = c;
    }
    end = p;
    if (auto_concat_string_literal)
      break;

    // Continue string literal when next character is '"'
    const char *q = skip_whitespace_or_comment(p);
    if (q == NULL || (p = q, *q != '"'))
      break;
  }
  assert(size < capa);
  str[size++] = '\0';
  Token *tok = alloc_token(TK_STR, lexer.line, begin, end);
  tok->str.buf = str;
  tok->str.size = size;
  *pp = p;
  return tok;
}

static Token *get_op_token(const char **pp) {
  const char *p = *pp;
  unsigned char c = *(unsigned char*)p;
  if (c < sizeof(kSingleOperatorTypeMap)) {
    enum TokenKind single = kSingleOperatorTypeMap[c];
    if (single != 0) {
      int n;
      for (n = 1; n < 3; ++n) {
        unsigned char c = *(unsigned char*)(p + n);
        if (c >= sizeof(kSingleOperatorTypeMap) || kSingleOperatorTypeMap[c] == 0)
          break;
      }

      for (int len = n; len > 1; --len) {
        const Name *op = alloc_name(p, p + len, false);
        enum TokenKind kind = reserved_word(op);
        if (kind != TK_EOF) {
          const char *q = p + len;
          *pp = q;
          return alloc_token(kind, lexer.line, p, q);
        }
      }

      const char *q = p + 1;
      *pp = q;
      return alloc_token(single, lexer.line, p, q);
    }
  }
  return NULL;
}

static Token *get_token(void) {
  static Line kEofLine = {.buf = ""};
  static Token kEofToken = {.kind = TK_EOF, .line = &kEofLine};

  const char *p = lexer.p;
  if (p == NULL || (p = skip_whitespace_or_comment(p)) == NULL) {
    kEofLine.filename = lexer.filename;
    kEofLine.lineno = lexer.lineno;
    return &kEofToken;
  }

  Token *tok = NULL;
  const char *begin = p;
  const char *ident_end = read_ident(p);
  if (ident_end != NULL) {
    const Name *name = alloc_name(begin, ident_end, false);
    enum TokenKind kind = reserved_word(name);
    tok = kind != TK_EOF ? alloc_token(kind, lexer.line, begin, ident_end)
                         : alloc_ident(name, lexer.line, begin, ident_end);
    p = ident_end;
  } else if (isdigit(*p)) {
    tok = read_num(&p);
#ifndef __NO_FLONUM
  } else if (*p == '.' && isdigit(p[1])) {
    tok = read_flonum(&p);
#endif
  } else if ((tok = get_op_token(&p)) != NULL) {
    // Ok.
  } else if (*p == '\'') {
    tok = read_char(&p);
  } else if (*p == '"') {
    tok = read_string(&p);
  } else {
    lex_error(p, "Unexpected character `%c'(%d)", *p, *p);
    return NULL;
  }

  assert(tok != NULL);
  lexer.p = p;
  return tok;
}

Token *fetch_token(void) {
  if (lexer.idx < 0) {
    Token *tok = get_token();
    lexer.idx = lexer.idx < 0 ? 0 : lexer.idx + 1;
    lexer.fetched[lexer.idx] = tok;
  }
  return lexer.fetched[lexer.idx];
}

Token *match(enum TokenKind kind) {
  Token *tok = fetch_token();
  if (tok->kind != kind && (int)kind != -1)
    return NULL;
  if (tok->kind != TK_EOF)
    --lexer.idx;
  return tok;
}

void unget_token(Token *token) {
  if (token->kind == TK_EOF)
    return;
  ++lexer.idx;
  assert(lexer.idx < MAX_LEX_LOOKAHEAD);
  lexer.fetched[lexer.idx] = token;
}
