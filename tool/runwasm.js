#! /usr/bin/env node

'use strict'

const assert = require('assert')
const fs = require('fs')
const os = require('os')
const path = require('path')

async function createWasm(wasmFile, imports) {
  const buffer = fs.readFileSync(wasmFile)
  const module = await WebAssembly.compile(buffer)
  return new WebAssembly.Instance(module, imports)
}

// Decode string in linear memory to JS.
function decodeString(buffer, ptr) {
  const memoryImage = new Uint8Array(buffer, ptr)
  let len
  for (len = 0; len < memoryImage.length && memoryImage[len] !== 0x00; ++len)
    ;
  const arr = new Uint8Array(buffer, ptr, len)
  return new TextDecoder('utf-8').decode(arr)
}

function tmppath() {
  const CHARS = '0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz'
  const len = 8
  return [...Array(len)].map(_ => CHARS[(Math.random() * CHARS.length) | 0]).join('')
}

function tmpfileSync(len) {
  const filePath = path.join(os.tmpdir(), tmppath())
  return fs.openSync(filePath, 'w+', 0o600)
}

;(async () => {
  const program = require('commander')
  program
    .option('--entry-point <func-name>', 'Entry point', '_start')
    .parse(process.argv)
  const encoder = new TextEncoder()
  const encodedArgs = program.args.map(arg => encoder.encode(arg))
  const totalArgsBytes = encodedArgs.reduce((acc, arg) => acc + arg.length + 1, 0)

  let memory

  const O_RDONLY  = 0x00
  const O_WRONLY  = 0x01
  const O_RDWR    = 0x02
  const O_CREAT   = 0x040  //  0100
  const O_EXCL    = 0x080  //  0200
  const O_TRUNC   = 0x200  // 01000
  const O_APPEND  = 0x400  // 02000

  const SEEK_SET = 0
  const SEEK_CUR = 1
  const SEEK_END = 2

  const kOpenFlags = {}
  kOpenFlags[O_RDONLY] = 'r'
  kOpenFlags[O_WRONLY] = 'w'
  kOpenFlags[O_RDWR] = 'w+'
  kOpenFlags[O_WRONLY | O_CREAT | O_TRUNC] = 'w'

  const ERANGE = 34

  const files = new Map()

  function getImports() {
    const imports = {
      c: {
        args_sizes_get: (pargc, plen) => {
          const argc = new Uint32Array(memory.buffer, pargc, 1)
          argc[0] = encodedArgs.length

          const len = new Uint32Array(memory.buffer, plen, 1)
          len[0] = totalArgsBytes
        },
        args_get: (pargv, pstr) => {
          const argv = new Uint32Array(memory.buffer, pargv, encodedArgs.length)
          const str = new Uint8Array(memory.buffer, pstr, totalArgsBytes)
          let offset = 0
          for (let i = 0; i < encodedArgs.length; ++i) {
            argv[i] = pstr + offset
            const encoded = encodedArgs[i]
            const len = encoded.length
            for (let j = 0; j < len; ++j)
              str[j + offset] = encoded[j]
            str[len + offset] = 0
            offset += len + 1
          }
        },

        read: (fd, buf, size) => {
          const memoryImage = new Uint8Array(memory.buffer, buf, size)
          if (fd < 3) {
            return fs.readSync(fd, memoryImage)
          } else {
            const bytes = fs.readSync(fd, memoryImage, files[fd])
            files[fd].position += bytes
            return bytes
          }
        },
        write: (fd, buf, size) => {
          const memoryImage = new Uint8Array(memory.buffer, buf, size)
          if (fd < 3) {
            return fs.writeSync(fd, memoryImage)
          } else {
            const bytes = fs.writeSync(fd, memoryImage)
            files[fd].position += bytes
            return bytes
          }
        },
        open: (filename, flag, mode) => {
          if (filename === 0)
            return -1
          const fn = decodeString(memory.buffer, filename)
          if (fn == null || fn === '')
            return -1

          const flagStr = kOpenFlags[flag]
          if (flagStr == null) {
            console.error(`Unsupported open flag: ${flag}`)
            return -1
          }

          try {
            const fd = fs.openSync(fn, flagStr)
            files[fd] = {
              position: 0,
            }
            return fd
          } catch (e) {
            if (e.code !== 'ENOENT')
              console.error(e)
            return -1
          }
        },
        close: (fd) => {
          fs.closeSync(fd)
          files.delete(fd)
          return 0
        },
        lseek: (fd, offset, where) => {
          let position
          switch (where) {
          default:
          case SEEK_SET:
            position = offset
            break
          case SEEK_CUR:
            position = files[fd].position + offset
            break
          case SEEK_END:
            //position = files[fd].position + offset
            assert(false, 'TODO: Implement')
            break
          }
          files[fd].position = position
          return position
        },
        unlink: (fn) => {
          fs.delete(fn)
          return 0
        },
        _tmpfile: () => {
          const fd = tmpfileSync()
          if (fd >= 0) {
            files[fd] = {
              position: 0,
            }
          }
          return fd
        },

        _getcwd: (buffer, size) => {
          const cwd = process.cwd()
          const encoded = new TextEncoder('utf-8').encode(cwd)
          const len = encoded.length
          if (len + 1 > size)
            return -ERANGE
          const memoryImage = new Uint8Array(memory.buffer, buffer, len + 1)
          for (let i = 0; i < len; ++i)
            memoryImage[i] = encoded[i]
          memoryImage[len] = 0
          return len + 1
        },

        proc_exit: (x) => {
          process.exit(x)
        },

        clock_gettime: (clkId, tp) => {
          // TODO: Check clkId
          const ts = new Uint32Array(memory.buffer, tp, 2)
          const t = new Date().getTime()
          ts[0] = (t / 1000) | 0
          ts[1] = (t % 1000) * 1000000
          return 0
        },
      },
    }
    return imports
  }

  async function loadWasm(wasmFile) {
    const imports = getImports()
    const instance = await createWasm(wasmFile, imports)
    if (instance.exports.memory)
      memory = instance.exports.memory
    return instance
  }

  async function main() {
    if (program.args < 1) {
      program.help()
    }

    const args = program.args
    const opts = program.opts()
    const wasmFile = args[0]
    const instance = await loadWasm(wasmFile)
    try {
      instance.exports[opts.entryPoint]()
    } catch (e) {
      console.error(e.toString())
      process.exit(1)
    }
  }

  main()
})()
