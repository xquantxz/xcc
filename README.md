XCC
===

[![Action Status](https://github.com/tyfkda/xcc/workflows/AllTests/badge.svg)](https://github.com/tyfkda/xcc)
[![Open in Gitpod](https://gitpod.io/button/open-in-gitpod.svg)](https://gitpod.io/#https://github.com/tyfkda/xcc)

  * C compiler for [XV6 (64bit)](https://github.com/tyfkda/xv6)
    * Also work on Linux/MacOS
  * Output ELF64 (x86-64) file format
    * Also support Aarch64 (Arm64)


### Requirements

  * Linux (or MacOS)
  * C compiler (gcc or clang)
  * make


### Build

```sh
$ make
```

Generated files:

  * `xcc`: Compiler entry
  * `cpp`: Preprocessor
  * `cc1`: C compiler
  * `as`:  Assembler
  * `ld`:  Linker


### Usage

```sh
$ ./xcc -o hello examples/hello.c
$ ./hello
Hello, world!
```

#### Command line options

  * `-o <filename>`: Set output filename (default: `a.out`)
  * `-I <path>`:     Add include path
  * `-D <label>(=value)`:  Define macro
  * `-S`:            Output assembly code
  * `-E`:            Preprocess only
  * `-c`:            Output object file
  * `-nodefaultlibs`:  Ignore libc
  * `-nostdlib`:  Ignore libc and crt0


### TODO

  * Optimization
  * Archiver


### Reference

  * [低レイヤを知りたい人のためのCコンパイラ作成入門](https://www.sigbus.info/compilerbook)
  * [rui314/9cc: A Small C Compiler](https://github.com/rui314/9cc)


----

### WebAssembly

Compile C to WebAssembly.

[Online demo](https://tyfkda.github.io/xcc/)

#### Requirements

  * node.js (>=16), npm

Setup:

```sh
$ npm ci
```

#### Build

```sh
$ make wcc
```

Generated files:

  * `wcc`: C compiler (including preprocessor, and output .wasm directly)

#### Usage

Compile:

```sh
$ ./wcc -o hello.wasm examples/hello.c
```

Command line options:

  * `-o <filename>`: Set output filename (default: `a.wasm`)
  * `-I <path>`:     Add include path
  * `-D <label>(=value)`:  Define macro
  * `--entry-point=func_name`:  Specify entry point (default: `_start`)
  * `-e func_name,...`:  Export function names (comma separated)
  * `--stack-size=<size>`:  Set stack size (default: 8192)
  * `-nodefaultlibs`:  Ignore libc
  * `-nostdlib`:  Ignore libc and crt0
  * `--verbose`:  Output debug information

#### Run

```sh
$ node tool/runwasm.js hello.wasm
Hello, world!
```

#### Missing features

  * `goto` statement
  * WASI
