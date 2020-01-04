#include "parse_asm.h"

#include <ctype.h>
#include <stddef.h>
#include <stdlib.h>  // strtol
#include <string.h>
#include <strings.h>

#include "gen.h"
#include "ir_asm.h"
#include "table.h"
#include "util.h"

// Align with Opcode.
static const char *kOpTable[] = {
  "mov",
  "movsx",
  "movzx",
  "lea",

  "add",
  "addq",
  "sub",
  "subq",
  "mul",
  "div",
  "idiv",
  "neg",
  "not",
  "inc",
  "incl",
  "incq",
  "dec",
  "decl",
  "decq",
  "and",
  "or",
  "xor",
  "shl",
  "shr",
  "cmp",
  "test",
  "cltd",
  "cqto",

  "seto",
  "setno",
  "setb",
  "setae",
  "sete",
  "setne",
  "setbe",
  "seta",
  "sets",
  "setns",
  "setp",
  "setnp",
  "setl",
  "setge",
  "setle",
  "setg",

  "jmp",
  "jo",
  "jno",
  "jb",
  "jae",
  "je",
  "jne",
  "jbe",
  "ja",
  "js",
  "jns",
  "jp",
  "jnp",
  "jl",
  "jge",
  "jle",
  "jg",
  "call",
  "ret",
  "push",
  "pop",

  "int",
  "syscall",
};

static const struct {
  const char *name;
  enum RegType reg;
} kRegisters[] = {
  {"al", AL},
  {"cl", CL},
  {"dl", DL},
  {"bl", BL},
  {"spl", SPL},
  {"bpl", BPL},
  {"sil", SIL},
  {"dil", DIL},

  {"r8b", R8B},
  {"r9b", R9B},
  {"r10b", R10B},
  {"r11b", R11B},
  {"r12b", R12B},
  {"r13b", R13B},
  {"r14b", R14B},
  {"r15b", R15B},

  {"ax", AX},
  {"cx", CX},
  {"dx", DX},
  {"bx", BX},
  {"sp", SP},
  {"bp", BP},
  {"si", SI},
  {"di", DI},

  {"r8w", R8W},
  {"r9w", R9W},
  {"r10w", R10W},
  {"r11w", R11W},
  {"r12w", R12W},
  {"r13w", R13W},
  {"r14w", R14W},
  {"r15w", R15W},

  {"eax", EAX},
  {"ecx", ECX},
  {"edx", EDX},
  {"ebx", EBX},
  {"esp", ESP},
  {"ebp", EBP},
  {"esi", ESI},
  {"edi", EDI},

  {"r8d", R8D},
  {"r9d", R9D},
  {"r10d", R10D},
  {"r11d", R11D},
  {"r12d", R12D},
  {"r13d", R13D},
  {"r14d", R14D},
  {"r15d", R15D},

  {"rax", RAX},
  {"rcx", RCX},
  {"rdx", RDX},
  {"rbx", RBX},
  {"rsp", RSP},
  {"rbp", RBP},
  {"rsi", RSI},
  {"rdi", RDI},

  {"r8", R8},
  {"r9", R9},
  {"r10", R10},
  {"r11", R11},
  {"r12", R12},
  {"r13", R13},
  {"r14", R14},
  {"r15", R15},

  {"rip", RIP},
};

static const char *kDirectiveTable[] = {
  "ascii",
  "section",
  "text",
  "data",
  "align",
  "byte",
  "word",
  "long",
  "quad",
  "comm",
  "globl",
  "extern",
};

bool err;

void parse_error(const ParseInfo *info, const char *message) {
  fprintf(stderr, "%s(%d): %s\n", info->filename, info->lineno, message);
  fprintf(stderr, "%s\n", info->rawline);
  err = true;
}

static bool is_reg8(enum RegType reg) {
  return reg >= AL && reg <= R15B;
}

static bool is_reg16(enum RegType reg) {
  return reg >= AX && reg <= R15W;
}

static bool is_reg32(enum RegType reg) {
  return reg >= EAX && reg <= R15D;
}

static bool is_reg64(enum RegType reg) {
  return reg >= RAX && reg <= R15;
}

const char *skip_whitespace(const char *p) {
  while (isspace(*p))
    ++p;
  return p;
}

static int find_match_index(const char **pp, const char **table, size_t count) {
  const char *p = *pp;
  const char *start = p;

  while (isalpha(*p))
    ++p;
  if (*p == '\0' || isspace(*p)) {
    size_t n = p - start;
    for (size_t i = 0; i < count; ++i) {
      const char *name = table[i];
      size_t len = strlen(name);
      if (n == len && strncasecmp(start, name, n) == 0) {
        *pp = skip_whitespace(p);
        return i;
      }
    }
  }
  return -1;
}

static enum Opcode find_opcode(ParseInfo *info) {
  return find_match_index(&info->p, kOpTable, sizeof(kOpTable) / sizeof(*kOpTable)) + 1;
}

static enum DirectiveType find_directive(ParseInfo *info) {
  return find_match_index(&info->p, kDirectiveTable, sizeof(kDirectiveTable) / sizeof(*kDirectiveTable)) + 1;
}

static enum RegType find_register(const char **pp) {
  const char *p = *pp;
  for (int i = 0, len = sizeof(kRegisters) / sizeof(*kRegisters); i < len; ++i) {
    const char *name = kRegisters[i].name;
    size_t n = strlen(name);
    if (strncmp(p, name, n) == 0) {
      *pp = p + n;
      return kRegisters[i].reg;
    }
  }
  return NOREG;
}

static bool immediate(const char **pp, long *value) {
  const char *p = *pp;
  bool negative = false;
  if (*p == '-') {
    negative = true;
    ++p;
  }
  if (!isdigit(*p))
    return false;
  long v = strtol(p, (char**)pp, 10);
  *value = negative ? -v : v;
  return true;
}

static bool is_label_first_chr(char c) {
  return isalpha(c) || c == '_' || c == '.';
}

static bool is_label_chr(char c) {
  return is_label_first_chr(c) || isdigit(c);
}

static const Name *parse_label(ParseInfo *info) {
  const char *p = info->p;
  const char *start = p;
  if (!is_label_first_chr(*p))
    return NULL;

  do {
    ++p;
  } while (is_label_chr(*p));
  info->p = p;
  return alloc_name(start, p, false);
}

static enum RegType parse_direct_register(ParseInfo *info, Operand *operand) {
  enum RegType reg = find_register(&info->p);
  enum RegSize size;
  int no;
  if (is_reg8(reg)) {
    size = REG8;
    no = reg - AL;
  } else if (is_reg16(reg)) {
    size = REG16;
    no = reg - AX;
  } else if (is_reg32(reg)) {
    size = REG32;
    no = reg - EAX;
  } else if (is_reg64(reg)) {
    size = REG64;
    no = reg - RAX;
  } else {
    parse_error(info, "Illegal register");
    return false;
  }

  operand->type = REG;
  operand->reg.size = size;
  operand->reg.no = no & 7;
  operand->reg.x = (no & 8) >> 3;
  return true;
}

static bool parse_indirect_register(ParseInfo *info, const Name *label, long offset, Operand *operand) {
  // Already read "(%".
  enum RegType reg = find_register(&info->p);
  if (!(is_reg64(reg) || reg == RIP))
    parse_error(info, "Register expected");
  info->p = skip_whitespace(info->p);
  if (*info->p != ')')
    parse_error(info, "`)' expected");
  ++info->p;

  char no = reg - RAX;
  operand->type = INDIRECT;
  operand->indirect.reg.size = REG64;
  operand->indirect.reg.no = reg != RIP ? no & 7 : RIP;
  operand->indirect.reg.x = (no & 8) >> 3;
  operand->indirect.label = label;
  operand->indirect.offset = offset;
  return true;
}

static enum RegType parse_deref_register(ParseInfo *info, Operand *operand) {
  enum RegType reg = find_register(&info->p);
  if (!is_reg64(reg))
    parse_error(info, "Illegal register");

  char no = reg - RAX;
  operand->type = DEREF_REG;
  operand->deref_reg.size = REG64;
  operand->deref_reg.no = no & 7;
  operand->deref_reg.x = (no & 8) >> 3;
  return true;
}

static bool parse_operand(ParseInfo *info, Operand *operand) {
  const char *p = info->p;
  if (*p == '%') {
    info->p = p + 1;
    return parse_direct_register(info, operand);
  }

  if (*p == '*' && p[1] == '%') {
    info->p = p + 2;
    return parse_deref_register(info, operand);
  }

  if (*p == '$') {
    info->p = p + 1;
    if (!immediate(&info->p, &operand->immediate))
      parse_error(info, "Syntax error");
    operand->type = IMMEDIATE;
    return true;
  }

  bool has_offset = false;
  long offset = 0;
  const Name *label = parse_label(info);
  if (label == NULL) {
    if (immediate(&info->p, &offset))
      has_offset = true;
  }
  info->p = skip_whitespace(info->p);
  if (*info->p != '(') {
    if (label != NULL) {
      operand->type = LABEL;
      operand->label = label;
      return true;
    }
    if (has_offset)
      parse_error(info, "direct number not implemented");
  } else {
    if (info->p[1] == '%') {
      info->p += 2;
      return parse_indirect_register(info, label, offset, operand);
    }
    parse_error(info, "Illegal `('");
  }

  return false;
}

static void parse_inst(ParseInfo *info, Inst *inst) {
  enum Opcode op = find_opcode(info);
  inst->op = op;
  if (op != NOOP) {
    if (parse_operand(info, &inst->src)) {
      info->p = skip_whitespace(info->p);
      if (*info->p == ',') {
        info->p = skip_whitespace(info->p + 1);
        parse_operand(info, &inst->dst);
        info->p = skip_whitespace(info->p);
      }
    }
  }
}

int current_section = SEC_CODE;

Line *parse_line(ParseInfo *info) {
  Line *line = malloc(sizeof(*line));
  line->label = NULL;
  line->inst.op = NOOP;
  line->inst.src.type = line->inst.dst.type = NOOPERAND;
  line->dir = NODIRECTIVE;

  info->p = info->rawline;
  line->label = parse_label(info);
  if (line->label != NULL) {
    if (*info->p != ':') {
      parse_error(info, "`:' rqeuired after label");
      return NULL;
    }
    ++info->p;
  }

  info->p = skip_whitespace(info->p);
  if (*info->p == '.') {
    ++info->p;
    enum DirectiveType dir = find_directive(info);
    if (dir == NODIRECTIVE) {
      parse_error(info, "Unknown directive");
      return NULL;
    }
    line->dir = dir;
  } else if (*info->p != '\0') {
    parse_inst(info, &line->inst);
    if (*info->p != '\0' && !(*info->p == '/' && info->p[1] == '/')) {
      parse_error(info, "Syntax error");
      err = true;
    }
  }
  return line;
}

static char unescape_char(char c) {
  switch (c) {
  case '0':  return '\0';
  case 'n':  return '\n';
  case 't':  return '\t';
  case 'r':  return '\r';
  case '"':  return '"';
  case '\'':  return '\'';
  default:
    return c;
  }
}

static size_t unescape_string(ParseInfo *info, const char *p, char *dst) {
  size_t len = 0;
  for (; *p != '"'; ++p, ++len) {
    char c = *p;
    if (c == '\0')
      parse_error(info, "string not closed");
    if (c == '\\') {
      // TODO: Handle \x...
      c = unescape_char(*(++p));
    }
    if (dst != NULL)
      *dst++ = c;
  }
  return len;
}

void handle_directive(ParseInfo *info, enum DirectiveType dir, Vector **section_irs) {
  Vector *irs = section_irs[current_section];

  switch (dir) {
  case DT_ASCII:
    {
      if (*info->p != '"')
        parse_error(info, "`\"' expected");
      ++info->p;
      size_t len = unescape_string(info, info->p, NULL);
      char *str = malloc(len);
      unescape_string(info, info->p, str);

      vec_push(irs, new_ir_data(str, len));
    }
    break;

  case DT_COMM:
    {
      const Name *label = parse_label(info);
      if (label == NULL)
        parse_error(info, ".comm: label expected");
      info->p = skip_whitespace(info->p);
      if (*info->p != ',')
        parse_error(info, ".comm: `,' expected");
      info->p = skip_whitespace(info->p + 1);
      long count;
      if (!immediate(&info->p, &count))
        parse_error(info, ".comm: count expected");
      current_section = SEC_BSS;
      irs = section_irs[current_section];
      vec_push(irs, new_ir_label(label));
      vec_push(irs, new_ir_bss(count));
    }
    break;

  case DT_TEXT:
    current_section = SEC_CODE;
    break;

  case DT_DATA:
    current_section = SEC_DATA;
    break;

  case DT_ALIGN:
    {
      long align;
      if (!immediate(&info->p, &align))
        parse_error(info, ".align: number expected");
      vec_push(irs, new_ir_align(align));
    }
    break;

  case DT_BYTE:
  case DT_WORD:
  case DT_LONG:
  case DT_QUAD:
    {
      long value;
      if (immediate(&info->p, &value)) {
        // TODO: Target endian.
        int size = 1 << (dir - DT_BYTE);
        unsigned char *buf = malloc(size);
        for (int i = 0; i < size; ++i)
          buf[i] = value >> (8 * i);
        vec_push(irs, new_ir_data(buf, size));
      } else {
        const Name *label = parse_label(info);
        if (label != NULL) {
          if (dir == DT_QUAD) {
            vec_push(irs, new_ir_abs_quad(label));
          } else {
            parse_error(info, "label can use only in .quad");
          }
        } else {
          parse_error(info, ".quad: number or label expected");
        }
      }
    }
    break;

  case DT_SECTION:
  case DT_GLOBL:
  case DT_EXTERN:
    break;

  default:
    parse_error(info, "Unhandled directive");
    break;
  }
}
