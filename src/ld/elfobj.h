#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#if defined(__XV6)

#else
// *nix

#if defined(__linux__)
#include <elf.h>
#else
#include "../../include/elf.h"
#endif

#endif

typedef struct Name Name;
typedef struct Table Table;
typedef struct Vector Vector;

typedef struct {
  struct ElfObj *elfobj;
  Elf64_Shdr *shdr;
  union {
    struct {
      uintptr_t address;
      unsigned char *content;
    } progbits;
    struct {
      const char *buf;
    } strtab;
    struct {
      Elf64_Sym *symtabs;
      Table *names;  // <Elf64_Sym*>
    } symtab;
  };
} ElfSectionInfo;

typedef struct ElfObj {
  FILE *fp;
  size_t start_offset;
  Elf64_Ehdr ehdr;
  Elf64_Shdr *shdrs;
  char *shstrtab;

  ElfSectionInfo *section_infos;
} ElfObj;

void elfobj_init(ElfObj *elfobj);
bool open_elf(const char *fn, ElfObj *elfobj);
bool read_elf(ElfObj *elfobj, FILE *fp, const char *fn);
void close_elf(ElfObj *elfobj);
Elf64_Sym *elfobj_find_symbol(ElfObj *elfobj, const Name *name);

void *malloc_or_die(size_t size);
void *read_from(FILE *fp, unsigned long offset, size_t size);
