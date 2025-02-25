#include "alloca.h"
#include "stdarg.h"
#include "stddef.h"  // offsetof
#include "stdint.h"
#include "stdio.h"
#include "stdlib.h"  // exit

#include "./xtest.h"

#if defined(__LP64__)
#define LONG_SIZE  (8)
#elif defined(__ILP32__)
#define LONG_SIZE  (4)
#else
#error Environment unknonwn
#endif

#define EXPECT(title, expected, actual)  expecti64(title, expected, actual)

int g_zero, g_work;
int g_init = (8 * 7 + 100 / 4 - 50 % 3) & 0x5a | 0x100;

void *null = (void*)0;

struct {int x; int *p;} g_struct = { 42, &g_zero };

static int s_val = 456;

extern int e_val;

short protodecl(short);

int foo(void) {
  return 123;
}
int (*foop)(void) = foo;

int sq(int x) {
  return x * x;
}

int sqsub(int x, int y) {
  int xx = x * x;
  int yy = y * y;
  return xx - yy;
}

int sub(int x, int y) {
  return x - y;
}

int apply(int (*f)(int, int), int x, int y) {
  return f(x, y);
}

int apply2(int f(int, int), int x, int y) {
  return f(x, y);
}

int array_from_ptr1(int a[]) {
  return a[0];
}
int array_from_ptr2(int a[][2]) {
  return a[1][1];
}
int array_from_ptr3(int a[][3]) {
  return a[1][1];
}
int ptr_from_array(int *p) {
  return *(p + 1);
}

int vaargs(int n, ...) {
  int acc = 0;
  va_list ap;
  va_start(ap, n);
  acc += va_arg(ap, int);
  if (n >= 2) {
    char v = va_arg(ap, int);  // char is promoted to int.
    acc += v;
  }
  if (n >= 3)
    acc += va_arg(ap, long);
  va_end(ap);
  return acc;
}

int static_local(void) {
  static int x = 42;
  return ++x;
}

#if !defined(__WASM)
int oldfuncptr(int(*f)(), int x) {
  return f(f(x));
}
#endif

TEST(all) {
  int x, y;
#if defined(__LP64__)
  EXPECT("big literal", 4, sizeof(2147483647));  // (1<<31) - 1
  EXPECT("big literal", 8, sizeof(2147483648));  // 1<<31
  EXPECT("big literal", 4, sizeof(4294967295U));  // (1<<32) - 1
  EXPECT("big literal", 8, sizeof(4294967296U));  // 1<<32
#endif
  EXPECT("zero", 0, 0);
  EXPECT("decimal", 42, 42);
  EXPECT("hex", 18, 0x12);
  EXPECT("octal", 83, 0123);
  EXPECT("negative", -42, (x=42, -x));
  EXPECT("long", 123, 123L);
  { long long x = 9876543LL; EXPECT("long long", 9876543, x); }
  EXPECT("escape sequence octal", 28, '\034');
  EXPECT("escape sequence hex", 27, '\x1b');
  EXPECT("escape char in str", 19, "\023"[0]);
  EXPECT("+-", 21, (x=5, x+20-4));
  EXPECT("*+", 47, (x=6, 5+x*7));
  EXPECT("()", 15, (x=9, 5*(x-6)));
  EXPECT("/", 4, (x=3, (x+5)/2));
  EXPECT("%", 3, (x=123, x%10));
  EXPECT("<<", 32, (x=4, 1 << x + 1));
  EXPECT(">>", 0x0a, (x=0xa5, x >> 4));
  EXPECT(">>", -6, (x=-0x5b, x >> 4));
  EXPECT("&", 0xa0, (x=0xa5, x & 0xf0));
  EXPECT("|", 0xbc, (x=0x88, x | 0x3c));
  EXPECT("^", 0x66, (x=0xc3, x ^ 0xa5));

  {
    unsigned char x = 0xff;
    unsigned char y = 0x13;
    unsigned char z;
    EXPECT("unsigned char +", 0x12, z = x + y);
    EXPECT("unsigned char -", 0xec, z = x - y);
    EXPECT("unsigned char -", 0x14, z = y - x);
    EXPECT("unsigned char *", 0xed, z = x * y);
    EXPECT("unsigned char /", 0x0d, z = x / y);
    EXPECT("unsigned char %", 0x08, z = x % y);
  }

  {
    short x = 3;
    EXPECT("short", 1, x == 3 && sizeof(x) == 2);
  }
  {
    long x = 3;
    EXPECT("long arithmetic", 3, 5L + 4L - x * 2L / 1L);
  }
  {
    unsigned char c = 255;
    EXPECT("unsigned char", 255, c);
  }
  {
    char c = -0x43;
    EXPECT("cast to unsigned", 0xbd, (unsigned char)c);
  }
  {
    unsigned char c = 0xbd;
    EXPECT("cast to signed", -0x43, (char)c);
  }
  {
    unsigned int x = 0x80000000U;
    EXPECT("unsigned division", 173, x / 12345678);
  }
  {
    unsigned int x = 0x80000000U;
    EXPECT("unsigned modulo", 80, x % 123);
  }
  {
    int a = 3;
    int b = 5 * 6 - 8;
    EXPECT("variable", 14, a + b / 2);
  }
  {
    int foo = 3;
    int bar = 5 * 6 - 8;
    EXPECT("variable2", 14, foo + bar / 2);
  }
  EXPECT("positive var", 42, (x=42, +x));
  EXPECT("negative var", -42, (x=42, -x));
  {
    int a, b, c;
    a = b = (c = 1) + 2;
    EXPECT("assign", 1, a == b);
  }
  EXPECT("!=", 1, (x=123, x != 456));
  EXPECT("not true", 0, (x=1, !(x == 1)));
  EXPECT("not false", 1, (x=1, !(x == 0)));
  EXPECT("not str", 0, !"abc");
  EXPECT("bit not", 0x12345678, (x=0xedcba987, ~x));
  {
    int x = 1;
    int y = ++x;
    EXPECT("preinc", 4, x + y);
  }
  {
    int x = 1;
    int y = --x;
    EXPECT("preinc", 0, x + y);
  }
  {
    int x = 1;
    int y = x++;
    EXPECT("postinc", 3, x + y);
  }
  {
    int x = 1;
    int y = x--;
    EXPECT("postinc", 1, x + y);
  }
  {
    int x = 10;
    x += 3;
    EXPECT("+=", 13, x);
  }
  {
    char x = 0x25;
    x += (short)0x1234;
    EXPECT("char += short", 0x59, x);
  }
  {
    int x = 10;
    x -= 3;
    EXPECT("-=", 7, x);
  }
  {
    int x = 10;
    x *= 3;
    EXPECT("*=", 30, x);
  }
  {
    int x = 10;
    x /= 3;
    EXPECT("/=", 3, x);
  }
  {
    int x = 10;
    x %= 3;
    EXPECT("%=", 1, x);
  }
  {
    int x = 0x12;
    x <<= 4;
    EXPECT("<<=", 0x120, x);
  }
  {
    int x = 0x123;
    x >>= 8;
    EXPECT(">>=", 0x1, x);
  }
  {
    int x = 0x123;
    x &= 0xa5;
    EXPECT("&=", 0x21, x);
  }
  {
    int x = 0x123;
    x |= 0xa5;
    EXPECT("|=", 0x1a7, x);
  }
  {
    int x = 0x123;
    x ^= 0xa5;
    EXPECT("^=", 0x186, x);
  }
  EXPECT("var < num", 0, (x=1, x < 0));
  EXPECT("var <= num", 0, (x=1, x <= 0));
  EXPECT("var > num", 1, (x=1, x > 0));
  EXPECT("var >= num", 1, (x=1, x >= 0));

  EXPECT("num > var", 0, (x=1, 0 > x));
  EXPECT("num >= var", 0, (x=1, 0 >= x));
  EXPECT("num < var", 1, (x=1, 0 < x));
  EXPECT("num <= var", 1, (x=1, 0 <= x));

  {
    unsigned char x = 255;
    EXPECT("unsigned cmp", 1, (x > (unsigned char)0));
    if (x <= (unsigned char)0)
      fail("unsigned cmp jmp");
  }
  {
    // C implicitly cast to unsigned type if it handles mixed signed values.
    int minus = -1;
    unsigned int uone = 1U;
    EXPECT("compare with different sign1", 1, minus > uone);  // !!!
    EXPECT("compare with different sign2", 1, uone < minus);  // !!!
  }

  EXPECT("t && t", 1, (x=1, y=1, x && y));
  EXPECT("f && t", 0, (x=0, y=1, x && y));
  EXPECT("t && f", 0, (x=1, y=0, x && y));
  EXPECT("f && f", 0, (x=0, y=0, x && y));
  EXPECT("t || t", 1, (x=1, y=1, x || y));
  EXPECT("f || t", 1, (x=0, y=1, x || y));
  EXPECT("t || f", 1, (x=1, y=0, x || y));
  EXPECT("f || f", 0, (x=0, y=0, x || y));

  EXPECT("funcall", 23, foo() - 100);
  EXPECT("func var", 9, sqsub(5, 4));
  {
    int x = 0;
    if (1)
      x = 2;
    EXPECT("if", 2, x);
  }
  {
    int x = 0;
    if (0)
      x = 2;
    EXPECT("if-false", 0, x);
  }
  {
    int x = 0;
    if (1 == 1)
      x = 2;
    else
      x = 3;
    EXPECT("if else", 2, x);
  }
  {
    int x = 0;
    if (1 == 0)
      x = 2;
    else
      x = 3;
    EXPECT("if else-false", 3, x);
  }
  {
    int a = 0, b = 0;
    if (1) {
      a = 1;
      b = 2;
    }
    EXPECT("block statement", 3, a + b);
  }
  {
    int x = 0;
    if (1)
      ;
    else
      x = 1;
    EXPECT("empty statement", 0, x);
  }
  {
    int i = 0, acc = 0;
    while (i <= 10) {
      acc += i;
      ++i;
    }
    EXPECT("while", 55, acc);
  }
  {
    int i, acc;
    for (i = acc = 0; i <= 10; i++)
      acc += i;
    EXPECT("for", 55, acc);
  }
  {
    int x = 0;
    switch (1) {
    case 1:
      x = 11;
      break;
    default:
      x = 22;
      break;
    }
    EXPECT("switch", 11, x);
  }
  {
    int x = 0;
    switch (2) {
    case 1:
      x = 11;
      break;
    default:
      x = 22;
      break;
    }
    EXPECT("switch default", 22, x);
  }
  {
    int x = 0;
    switch (3) {
    case 1:
      x = 11;
      break;
    }
    EXPECT("switch no-default", 0, x);
  }
  {
    int x = 0;
    switch (1) {
    case 1:
      x += 1;
      // Fallthrough
    default:
      x += 10;
    }
    EXPECT("switch fallthrough", 11, x);
  }
  {
    int x = 10, *p = &x;
    ++(*p);
    EXPECT("pointer", 11, x);
  }
  {
    int a[3], *p = a;
    ++p;
    *p = 123;
    EXPECT("array", 123, *(a + 1));
  }
  {
    int a[2];
    *a = 1;
    a[1] = 10;
    EXPECT("array access", 11, a[0] + 1[a]);
  }
  {
    int a[2], *p;
    a[0] = 10;
    a[1] = 20;
    p = a;
    EXPECT("preinc pointer", 20, *(++p));
  }
  {
    int a[2], *p;
    a[0] = 10;
    a[1] = 20;
    p = a;
    EXPECT("postinc pointer", 10, *p++);
  }
  {
    int a[2], *p;
    a[0] = 98;
    a[1] = 76;
    p = a;
    p += 1;
    EXPECT("pointer +=", 76, *p);
  }
  {
    int a[2], *p;
    a[0] = 54;
    a[1] = 32;
    p = &a[1];
    p -= 1;
    EXPECT("pointer -=", 54, *p);
  }
  {
    int a[2], *p;
    a[0] = 11;
    a[1] = 22;
    p = a;
    *(p++) += 100;
    EXPECT("postinc +=, 1", 111, a[0]);
    EXPECT("postinc +=, 2", 22, a[1]);
  }
  {
    int x = 0;
    EXPECT("cast pointer", 0, *(int*)&x);
  }
  {
    char x, *p = &x;
    EXPECT("cast pointer", 1, (void*)&x == (void(*)())p);
  }
  {
    void *p = (void*)1234;
    EXPECT("cast pointer", 1234L, (long)p);
  }
  EXPECT("global cleared", 0, g_zero);
  EXPECT("global initializer", 330, g_init);
  EXPECT("global struct initializer: int", 42, g_struct.x);
  EXPECT("global struct initializer: ptr", (long)&g_zero, (long)g_struct.p);
  {
    g_work = 1;
    EXPECT("global access", 11, g_work + 10);
  }
  {
    struct {char x; int y;} foo;
    foo.x = 1;
    foo.y = 2;
    EXPECT("struct", 3, foo.x + foo.y);
  }
  {
    struct {char x; int y;} foo, *p = &foo;
    p->x = 1;
    p->y = 2;
    EXPECT("struct pointer", 3, foo.x + foo.y);
  }
  {
    union {char x; int y;} foo;
    EXPECT("union", 1, sizeof(foo) == sizeof(int) && (void*)&foo.x == (void*)&foo.y);
  }
  {
    struct{
      union{
        int x;
      };
    } a;
    a.x = 596;
    EXPECT("anonymous", 596, a.x);
    EXPECT("anonymous adr", (long)&a, (long)&a.x);
  }
  EXPECT("func pointer", 9, apply(&sub, 15, 6));
  EXPECT("func pointer w/o &", 9, apply(sub, 15, 6));
  EXPECT("func", 2469, apply2(sub, 12345, 9876));
  EXPECT("block comment", 123, /* comment */ 123);
  EXPECT("line comment", 123, // comment
         123);
  EXPECT("proto decl", 12321, protodecl(111));
  {
    long proto_in_func(short);
    EXPECT("proto in func", 78, proto_in_func(77));
  }

  {
    int i, acc;
    for (i = acc = 0; i <= 10; i++) {
      if (i == 5)
        break;
      acc += i;
    }
    EXPECT("for-break", 10, acc);
  }
  {
    int i, acc;
    for (i = acc = 0; i <= 10; i++) {
      if (i == 5)
        continue;
      acc += i;
    }
    EXPECT("for-continue", 50, acc);
  }
  {
    int i = 0, acc = 0;
    while (++i <= 10) {
      if (i == 5)
        break;
      acc += i;
    }
    EXPECT("while-break", 10, acc);
  }
  {
    int i = 0, acc = 0;
    while (++i <= 10) {
      if (i == 5)
        continue;
      acc += i;
    }
    EXPECT("while-continue", 50, acc);
  }
  {
    int i = 0, acc = 0;
    do {
      if (i == 5)
        break;
      acc += i;
    } while (++i <= 10);
    EXPECT("do-while-break", 10, acc);
  }
  {
    int i = 0, acc = 0;
    do {
      if (i == 5)
        continue;
      acc += i;
    } while (++i <= 10);
    EXPECT("do-while-continue", 50, acc);
  }
  EXPECT("t && t", 1, 1 && 2);
  {
    int x = 1;
    0 && (x = 0);
    EXPECT("&& shortcut", 1, x);
  }
  EXPECT("f || t", 1, 0 || 2);
  {
    int x = 1;
    1 || (x = 0);
    EXPECT("|| shortcut", 1, x);
  }
  {
    int x = 0;
    if (!(1 && 0))
      x = 1;
    EXPECT("conditional !(t && t)", 1, x);
  }
  {
    int x = 0;
    if (0 || 1)
      x = 1;
    EXPECT("conditional (f || t)", 1, x);
  }
  {
    int x = 1;
    { int x = 2; }
    EXPECT("block scope", 1, x);
  }
  {
    char a[2][3];
    a[1][0] = 1;
    EXPECT("nested-array", 1, ((char*)a)[3]);
  }
  {
    int a[2];
    a[1] = 45;
    EXPECT("array <- ptr", 45, array_from_ptr1(&a[1]));
  }
  {
    int a[3][2];
    a[1][1] = 39;
    EXPECT("array <- ptr:2", 39, array_from_ptr2(a));
  }
  {
    int a[3][2];
    a[2][0] = 987;
    EXPECT("array <- ptr:3", 987, array_from_ptr3((int(*)[3])a));
  }
  {
    int a[6] = {11, 22, 33, 44, 55, 66};
    int (*b)[3] = (int(*)[3])a;
    EXPECT("array ptr", 55, b[1][1]);
  }
  {
    int a[2];
    a[1] = 55;
    EXPECT("ptr <- array", 55, ptr_from_array(a));
  }

  EXPECT("sizeof(int)", 4, sizeof(int));
  EXPECT("sizeof(long)", LONG_SIZE, sizeof(long));
  EXPECT("int8_t",  1, sizeof(int8_t));
  EXPECT("int16_t", 2, sizeof(int16_t));
  EXPECT("int32_t", 4, sizeof(int32_t));
  EXPECT("int64_t", 8, sizeof(int64_t));
#if defined(__LP64__)
  EXPECT("intptr_t", 8, sizeof(intptr_t));
#elif defined(__ILP32__)
  EXPECT("intptr_t", 4, sizeof(intptr_t));
#endif
  EXPECT("sizeof(void)", 1, sizeof(void));
  EXPECT("sizeof(array)", 3, sizeof(char [3]));
  {
    int x;
    EXPECT("sizeof var", 4, sizeof(x));
  }
  EXPECT("sizeof(expr)", 4, sizeof(5 * 9));
  {
    int a[5];
    EXPECT("sizeof(array len)", 5, sizeof(a) / sizeof(*a));
  }
  EXPECT("sizeof(struct)", 8, sizeof(struct {int a; char b;}));
  EXPECT("sizeof(empty struct)", 0, sizeof(struct {}));
  {
    struct {} a, b;
    EXPECT("empty struct occupy", 1, &a != &b);
  }
  EXPECT("sizeof(str) include nul", 12, sizeof("hello\0world"));
  EXPECT("sizeof(struct ptr)", sizeof(void*), sizeof(struct Undefined*));
  EXPECT("sizeof(func ptr)", sizeof(void*), sizeof(int (*)()));
  {
    int a[3] = {1, 2, 3};
    EXPECT("array initializer", 1, a[0] == 1 && a[1] == 2 && a[2] == 3);
  }
  {
    int a[] = {1, 2};
    EXPECT("array without size", 1, sizeof(a) == 2 * sizeof(int) && a[0] == 1 && a[1] == 2);
  }
  {
    int a[] = {1, 2, 3,};
    EXPECT("array with last comma", 3 * sizeof(int), sizeof(a));
  }
  {
    struct {int x; int y;} s = {3};
    EXPECT("struct initializer", 3, s.x + s.y);
  }
  {
    struct {int x; int y;} s = {3, 4,};
    EXPECT("struct initializer with last comma", 7, s.x + s.y);
  }
  {
    struct {int x; int y;} s = {.y = 9};
    EXPECT("struct initializer with member", 9, s.x + s.y);
  }
  {
    union {char x; int y;} u = {0x1234};
    EXPECT("union initializer", 0x34, u.x);
  }
  {
    union {int y; char x;} u = {0x5678};
    EXPECT("union initializer2", 0x5678, u.y);
  }
  {
    union {char x; int y;} u = {.y=0xabcd};
    EXPECT("union initializer with member", 0xabcd, u.y);
  }
  {
    const int x = 123;
    EXPECT("const", 123, x);
  }
  EXPECT("file static", 456, s_val);
  EXPECT("extern", 789, e_val);

  {
    static static_only = 11;
    EXPECT("static_only", 11, static_only);
    EXPECT("sizeof static_only", sizeof(int), sizeof(static_only));
  }
  {
    extern extern_only;
    EXPECT("extern_only", 22, extern_only);
    EXPECT("sizeof extern_only", sizeof(int), sizeof(extern_only));
  }
  {
    const const_only = 33;
    EXPECT("const_only", 33, const_only);
    EXPECT("sizeof const_only", sizeof(int), sizeof(const_only));
  }
  {
    volatile volatile_only = 44;
    EXPECT("volatile_only", 44, volatile_only);
    EXPECT("sizeof volatile_only", sizeof(int), sizeof(volatile_only));
  }
  {
    static const struct {
      int x;
    } sc_struct = {
      55
    };
    EXPECT("sc_struct", 55, sc_struct.x);
  }
  {
    static const struct {
      int a;
      struct {
        int b;
        struct {
          int z;
        } y;
      } x;
    } t = {
      1, { 2, { 3 } }
    };
    static const int *p = &t.x.y.z;
    EXPECT("&member initializer", 3, *p);
  }
  {
    static const struct {
      int a;
      struct {
        int b[2];
      } x[2];
    } t = {
      11, { {{22, 23}}, {{33, 34}} }
    };
    static const int *p = t.x[1].b;
    EXPECT("member[].member initializer", 33, *p);
  }
  {
    struct S {
      int a;
      int b;
    };
    struct S x = {11, 22};
    struct S a[] = {x};
    EXPECT("struct initializer with variable", 33, a[0].a + a[0].b);
  }

  {
    int desig[] = {[2] = 100, [1] 200};
    EXPECT("desig[0]",   0, desig[0]);
    EXPECT("desig[1]", 200, desig[1]);
    EXPECT("desig[2]", 100, desig[2]);
    EXPECT("sizeof(desig)", 3, sizeof(desig) / sizeof(*desig));
  }
  {
    static int desig[] = {[2] = 1000, 2000};
    EXPECT("static desig[0]",    0, desig[0]);
    EXPECT("static desig[1]",    0, desig[1]);
    EXPECT("static desig[2]", 1000, desig[2]);
    EXPECT("static desig[3]", 2000, desig[3]);
    EXPECT("sizeof(static desig)", 4, sizeof(desig) / sizeof(*desig));
  }
  EXPECT("?:", 2, 1 ? 2 : 3);
  EXPECT("comma", 3333, (11, 222, 3333));
  EXPECT("vaargs 1", 1, vaargs(1, (int)1, (char)20, 300L));
  EXPECT("vaargs 2", 21, vaargs(2, (int)1, (char)20, 300L));
  EXPECT("vaargs 3", 321, vaargs(3, (int)1, (char)20, 300L));
  {
    char c = 'A';
    EXPECT("vaargs char", 65, vaargs(1, c));
  }
  EXPECT("static local var", 44, (static_local(), static_local()));
  EXPECT("null initializer", 0L, (long)null);
  {
    int buf[16], *p = buf + 13;
    EXPECT("diff ptr and array", 13, p - buf);
    EXPECT("diff ptr and array2", -13, buf - p);
  }
  {
    int buf[16];
    const int *p = &buf[9];
    EXPECT("diff ptr and const ptr", 9, p - buf);
  }
  {
    int array1[] = {1}, array2[] = {2};
    EXPECT("compare arrays", 0, array1 == array2);
    EXPECT("compare arrays2", 1, array1 == array1);
  }

  EXPECT("block expr", 169, ({int x = 13; x * x;}));

  {
    int64_t x = 0x123456789abcdef0LL;
    EXPECT("64bit literal 1", 0xdef0,  x        & 0xffff);
    EXPECT("64bit literal 2", 0x9abc, (x >> 16) & 0xffff);
    EXPECT("64bit literal 3", 0x5678, (x >> 32) & 0xffff);
    EXPECT("64bit literal 4", 0x1234, (x >> 48) & 0xffff);
  }
} END_TEST()

int e_val = 789;

int extern_only = 22;

short protodecl(short x) { return x * x; }
long proto_in_func(short x) { return x + 1; }

//

typedef int SameTypedefAllowed;
typedef int SameTypedefAllowed;

int f27(void){return 27;}
int f53(void){return 53;}
void mul2p(int *p) {*p *= 2;}
const char *retstr(void){ return "foo"; }

TEST(basic) {
  {
    int array[0];
    EXPECT("zero sized array", 0, sizeof(array));
  }

  {
    static char s[4]="abcd";
    EXPECT("non nul-terminated str static", 4, sizeof(s));
    char l[6]="efghij";
    EXPECT("non nul-terminated str local", 6, sizeof(l));

    struct S {char str[7];} static ss = {"klmnopq"};
    EXPECT("non nul-terminated str in struct", 7, sizeof(ss));
  }

  {
    char *l = (char*)"x";
    EXPECT("cast string", 120, l[0]);

    static char *s = (char*)"x";
    EXPECT("cast string static", 120, s[0]);
  }

  {
    static uintptr_t x = (uintptr_t)"str";
    char *p = (char*)x;
    EXPECT("cast str to int", 116, p[1]);
  }

  {
    int x;
    (x) = 98;
    EXPECT("paren =", 98, x);
  }

  {
    enum Num { Zero, One, Two };
    EXPECT("enum", 11, One + 10);

    enum Num2 { Ten = 10, Eleven, };
    EXPECT("enum with assign and trailing comma", 11, Eleven);

    int x = 0;
    switch (1) {
    case One: EXPECT_TRUE("enum can use in case"); break;
    default: fail("enum can use in case"); break;
    }

    enum Num num = Zero;
    EXPECT_FALSE(num == One);
  }

  // empty block
  ; {} {;;;}

  {
    typedef char T1, T2[29];
    EXPECT("multi typedef", 29, sizeof(T2));
  }

  {
    typedef void VOID;
    extern VOID voidfunc(VOID);
    EXPECT_TRUE("typedef void");

    typedef char T[];
    T t1 = {1, 2};
    T t2 = {3, 4, 5, 6, 7};
    EXPECT("typedef[]", 2, sizeof(t1));
    EXPECT("typedef[]", 5, sizeof(t2));
  }

#if !defined(__WASM)
  {
    int oldstylefunc();
    EXPECT("old-style func", 93, oldstylefunc(31));

    EXPECT("old-func-ptr", 81, oldfuncptr(sq, 3));
  }
#endif

  {
    EXPECT("global-func-var", 123, foop());

    static int (*f)(void) = foo;
    EXPECT("static-func-var", 123, f());
  }

  {
    int acc;
    acc = 0;
    for (int i = 1, len = 10; i <= len; ++i)
      acc += i;
    EXPECT("for-var", 55, acc);

    const char *p = "abc";
    int len = 0;
    for (char c; (c = *p) != '\0'; ++p)
      ++len;
    EXPECT("for-no-initial-val", 3, len);

    acc = 0;
    for (int i = 1; i <= 10; ++i) {
      if (i > 5)
        break;
      acc += i;
    }
    EXPECT("break", 15, acc);

    acc = 0;
    for (int i = 1; i <= 10; ++i) {
      if (i <= 5)
        continue;
      acc += i;
    }
    EXPECT("continue", 40, acc);
  }

  {
    int x = 123;
    (void)x;
  }

  EXPECT_STREQ("strings", "hello world", "hello " "world");
  {
    const char* const ccc = "foobar";
    EXPECT_STREQ("const char* const", "foobar", ccc);
  }

#if !defined(__WASM)
  {
#define SUPPRESS_LABEL(label)  do { if (false) goto label; } while (0)
    int x = 1;
    SUPPRESS_LABEL(dummy1);
    goto label;
dummy1:
    x = 2;
label:
    EXPECT("goto", 1, x);

//j3:
    goto j1;
dummy2:
    goto j2;
j2:
    goto dummy2;
j1:;
  }
#endif

  {
    int x;
    x = 0;
    switch (0) {
    default: x = 1; break;
    }
    EXPECT("switch w/o case", 1, x);

    x = 0;
    switch (0) {
      x = 1;
    }
    EXPECT("switch w/o case & default", 0, x);

#if !defined(__WASM)
    x = 94;
    switch (0) {
      if (0) {
        default: x = 49;
      }
    }
    EXPECT("switch-if-default", 49, x);
#endif

    x = 2;
    switch (x) {
    case 0: x = 0; break;
    case 1: x = 11; break;
    case 2: x = 22; break;
    case 3: x = 33; break;
    }
    EXPECT("switch table", 22, x);

    short y = 1;
    switch (y) {
    case 2: y = 22; break;
    case 3: y = 33; break;
    case 4: y = 44; break;
    case 5: y = 55; break;
    default: y = 99; break;
    }
    EXPECT("switch table less", 99, y);
  }

  {  // "post inc pointer"
    char *p = (char*)(-1L);
    p++;
    EXPECT_NULL(p);
  }

  {
    int x = 1;
    {
      x = 10;
      int x = 100;
    }
    EXPECT("shadow var", 10, x);
  }

  {
    typedef int Foo;
    {
      int Foo = 61;
      EXPECT("typedef name can use in local", 61, Foo);
    }
  }

  {
    unsigned x = 92;
    EXPECT("implicit int", 92, x);
  }

  {
    int x = 1;
    const char *p = x ? "true" : "false";
    EXPECT("ternary string", 114, p[1]);

    const char *q = "abc";
    q = q != 0 ? q + 1 : 0;
    EXPECT("ternary ptr:0", 98, *q);

    int selector = 0;
    EXPECT("ternary w/ func", 53, (selector?f27:f53)());

    char buf[16] = "";
    selector ? (void)strcpy(buf, "true") : (void)strcpy(buf, "false");
    EXPECT_STREQ("ternary void", "false", buf);
  }

  {
    int x;
    x = 43;
    mul2p(&(x));
    EXPECT("&()", 86, x);
    x = 33;
    EXPECT("pre-inc ()", 34, ++(x));
    x = 44;
    EXPECT("post-dec ()", 44, (x)--);
  }

  {
    const char *p = "foo";
    p = "bar";
    EXPECT("can assign const ptr", 97, p[1]);

    "use strict";

    p = (1, "use strict", "dummy");
    EXPECT("str in comma", 117, p[1]);

    int x = 1;
    x != 0 && (x = 0, 1);
    EXPECT("condition with comma expr", 0, x);

    EXPECT("return str", 111, retstr()[2]);
    EXPECT("deref str", 48, *"0");
  }
} END_TEST()

int oldstylefunc(int x) {
  return x * 3;
}

//

typedef struct {int x, y;} FooStruct;

int struct_arg(FooStruct foo, int k) { return foo.x * k + foo.y; }
FooStruct return_struct(void) { FooStruct s = {.x = 12, .y = 34}; return s; }

typedef struct {long x; long y;} LongStruct;
LongStruct return_lstruct(void) { LongStruct s = {111, 222}; return s; }

TEST(struct) {
  FooStruct foo;
  foo.x = 123;
  EXPECT("typedef", 123, foo.x);

  {
    typedef struct FILE FILE;
    EXPECT("Undeclared struct typedef", sizeof(void*), sizeof(FILE*));
  }

  {
    struct Foo *p;
    struct Foo {int x;};
    struct Foo foo;
    p = &foo;
    p->x = 42;
    EXPECT("late declare struct", 42, p->x);
  }

  {
    int size;
    struct S {int x;};
    {
      struct S {char y;};
      size = sizeof(struct S);
    }
    EXPECT("scoped struct", 5, size + sizeof(struct S));
  }

  {
    int size;
    typedef struct {int x;} S;
    {
      typedef struct {char y;} S;
      size = sizeof(S);
    }
    EXPECT("scoped typedef", 5, size + sizeof(S));
  }

  {
    struct S {struct S *p;} *s = 0;
    EXPECT("self referential struct", sizeof(void*), sizeof(s->p[0]));
  }

  {
    struct Foo { int x; };
    struct Foo foo, bar;
    foo.x = 33;
    bar = foo;
    EXPECT("struct assign", 33, bar.x);
  }

  {
    struct Foo { int x; };
    struct Foo foo = {55}, bar = foo;
    EXPECT("struct initial assign", 55, bar.x);
  }

  {
    struct Foo { long x; };
    struct Foo foo, bar, *baz = &bar;
    baz->x = 44;
    foo = *baz;
    EXPECT("struct deref", 44, foo.x);
  }

  {
    typedef struct {int x;} S;
    S s = {51}, x;
    S *e1 = &x;
    S *e2 = &s;
    *e1 = *e2;
    EXPECT("struct copy", 51, x.x);
  }

  {
    struct empty {};
    EXPECT("empty struct size", 0, sizeof(struct empty));

    struct empty a = {}, b;
    b = a;
    EXPECT("empty struct copy", 0, sizeof(b));
  }

  {
    struct S {int x;};
    const struct S s = {123};
    struct S a;
    a = s;
    EXPECT("copy struct from const", 123, a.x);

    struct S b = s;
    EXPECT("init struct with variable", 123, b.x);
  }

  {
    FooStruct foo = {12, 34};
    EXPECT("struct args", 82, struct_arg(foo, 4));

    const FooStruct bar = {56, 78};
    EXPECT("implicit cast to non-const", 246, struct_arg(bar, 3));
  }

  {
    typedef struct { int x, y, z; } S;
    S a = {1, 2, 3}, b = {10, 20, 30};
    int x = 1;
    S c = x == 0 ? a : b;
    EXPECT("ternary struct", 60, c.x + c.y + c.z);
  }

  {
    #define NULL  ((void*)0)
    #define offsetof(S, m)  ((long)(&((S*)NULL)->m))
    struct X {char x[5]; char z;};
    char a[offsetof(struct X, z)];
    EXPECT("offsetof is const", 5, sizeof(a));
  }

  {
    FooStruct s = return_struct();
    EXPECT("return struct", 46, s.x + s.y);

    EXPECT("return struct member", 12, return_struct().x);
  }
  {
    int dummy[1];
    LongStruct s; s = return_lstruct();
    EXPECT("return struct not broken", 222, s.y);
  }
} END_TEST()

TEST(bitfield) {
  {
    union {
      int16_t _;
      struct {
        int16_t x : 5;
        int16_t y : 5;
        int16_t z : 6;
      };
    } u;

    // EXPECT("sizeof", sizeof(int16_t), sizeof(u));

    u.x = 5;
    u.y = 23;
    u.z = 27;
    EXPECT("value 1", 5, u.x);
    EXPECT("value 2", -9, u.y);
    EXPECT("value 3", 27, u.z);
    EXPECT("total", (27 << 10) | (23 << 5) | 5, u._);
  }

  {
    union {
      uint16_t _;
      struct {
        uint16_t x : 5;
        uint16_t y : 5;
        uint16_t z : 6;
      };
    } u;

    u.x = -1;
    u.y = -2;
    u.z = -3;
    EXPECT("unsigned 1", 31, u.x);
    EXPECT("unsigned 2", 30, u.y);
    EXPECT("unsigned 3", 61, u.z);
    EXPECT("total2", (61 << 10) | (30 << 5) | 31, u._);
  }

  // {
  //   typedef struct {
  //     char a;
  //     long x : 5;
  //     long y : 4;
  //     long z : 3;
  //     short b;
  //   } M;
  //   EXPECT("mix size", sizeof(long) * 3, sizeof(M));
  //   EXPECT("mix layout", sizeof(long) * 2, offsetof(M, b));
  // }

  {
    struct {
      int _1 : 5;
      int x  : 5;
      int _2 : 6;
    } s = {};

    s.x = 5;
    EXPECT("assign-op +=", 12, s.x += 7);
    EXPECT("assign-op /=", 4, s.x /= 3);

    EXPECT("assign-op overflow", -13, s.x += 15);
    EXPECT("assign-op underflow", 7, s.x -= 12);
    EXPECT("assign-op overflow 2", -11, s.x *= 3);

    EXPECT("assign-op pad 1", 0, s._1);
    EXPECT("assign-op pad 2", 0, s._2);
  }

  {
    struct {
      int x : 3;
      int y : 4;
      int z : 5;
      int w : 6;
    } s;

    s.x = 0;
    s.y = 5;
    s.z = 10;
    s.w = 15;

    EXPECT("pre-dec", -1, --s.x);
    EXPECT("pre-dec'", -1, s.x);
    EXPECT("pre-inc", 6, ++s.y);
    EXPECT("pre-inc'", 6, s.y);
    EXPECT("post-inc", 10, s.z++);
    EXPECT("post-inc'", 11, s.z);
    EXPECT("post-dec", 15, s.w--);
    EXPECT("post-dec'", 14, s.w);

    s.y = (1 << 3) - 1;
    EXPECT("pre-overflow", -(1 << 3), ++s.y);
    EXPECT("pre-overflow'", -(1 << 3), s.y);
    s.z = -(1 << 4);
    EXPECT("post-underflow", -(1 << 4), s.z--);
    EXPECT("post-underflow'", (1 << 4) - 1, s.z);
  }

  {
    union {
      int _;
      int x: 5;
      int y: 9;
    } s;

    s._ = 0xa5a5;
    EXPECT("union 1", 0x5, s.x);
    EXPECT("union 2", -0x5b, s.y);
  }

  {
    struct S {
      int x : 4;
      int y : 6;
      int z : 10;
    };
    struct S s = {
      1,
      22,
      333,
    };

    EXPECT("value 1", 1, s.x);
    EXPECT("value 2", 22, s.y);
    EXPECT("value 3", 333, s.z);

    struct S s2 = s;
    EXPECT("value 1'", 1, s2.x);
    EXPECT("value 2'", 22, s2.y);
    EXPECT("value 3'", 333, s2.z);
  }
} END_TEST()

//

char g_strarray[] = "StrArray";
char *g_strptr = "StrPtr";
char *g_strptrarray[] = {"StrPtrArray"};
struct {char *s;} g_str_struct_array[] = {{"StrStructArray"}};
struct {char s[4];} g_chararray_struct[] = {{"abc"}};
char nums[] = "0123456789";
char *g_ptr_ref1 = nums + 4;
char *g_ptr_ref2 = &nums[2];
int g_array[] = {10,20,30};
FooStruct g_comp_deficit = (FooStruct){};
int g_comp_array[] = (int[]){11, 22, 33, 0};
int *g_comp_ptr = (int[]){45, 56, 67, 78, 0};
union { int x; struct { char a; short b; } y; } g_union = {.y={.b=77}};
struct {union {int x;};} g_anonymous = {.x = 99};
FooStruct *g_comp_p = &(FooStruct){88};

TEST(initializer) {
  {
    char s[] = "abc";
    s[1] = 'B';
    EXPECT_STREQ("string initializer", "aBc", s);
  }

  {
    enum Num { Zero } num = Zero;
    EXPECT("enum initializer", 0, num);

    enum Num num2 = 67;
    EXPECT("enum initializer2", 67, num2);
  }

  { int x = {34}; EXPECT("brace initializer", 34, x); }

  EXPECT_STREQ("global str-array init", "StrArray", g_strarray);
  EXPECT_STREQ("global str-ptr init", "StrPtr", g_strptr);
  EXPECT_STREQ("global str-ptr-array init", "StrPtrArray", g_strptrarray[0]);
  EXPECT_STREQ("global str-in-struct init", "StrStructArray", g_str_struct_array[0].s);
  EXPECT_STREQ("global char-array-in-struct init", "abc", g_chararray_struct[0].s);
  EXPECT_STREQ("global ptr-ref1", "456789", g_ptr_ref1);
  EXPECT_STREQ("global ptr-ref2", "23456789", g_ptr_ref2);
  EXPECT("global array", 42, sizeof(g_array) + g_array[2]);

  EXPECT("global compound literal init (deficit)", 0, g_comp_deficit.x);
  {
    int *p, sum;

    p = g_comp_array;
    sum = 0;
    while (*p != 0)
      sum += *p++;
    EXPECT("global compound literal array", 66, sum);

    p = g_comp_ptr;
    sum = 0;
    while (*p != 0)
      sum += *p++;
    EXPECT("global compound literal ptr", 246, sum);

    int l_comp_array[] = (int[]){111, 222, 333, 0};
    p = l_comp_array;
    sum = 0;
    while (*p != 0)
      sum += *p++;
    EXPECT("local compound literal array", 666, sum);

    int *l_comp_ptr = (int[]){1, 11, 111, 1111, 0};
    p = l_comp_ptr;
    sum = 0;
    while (*p != 0)
      sum += *p++;
    EXPECT("local compound literal ptr", 1234, sum);
  }

  {
    static const int array[] = {11,22,33};
    static const int *p = array;
    EXPECT("static ref", 22, p[1]);
  }
  {
    static int array[] = {10,20,30};
    EXPECT("local static array", 42, sizeof(array) + array[2]);
  }
  {
    int static const a = 34;
    EXPECT("int static const", 34, a);
  }
  {
    struct {int x;} static const a[] = {{67}};
    EXPECT("struct static const", 67, a[0].x);
  }
  {
    struct { union { long a; char b; } x; int y; } static s = {.x={.b=88}, .y=99};
    EXPECT("init struct contain union", 99, s.y);
  }
  {
    int x = sizeof(x);
    EXPECT("sizeof(self) in initializer", 4, x);
  }
  EXPECT("init union", 77, g_union.y.b);
  EXPECT("anonymous union init", 99, g_anonymous.x);

  {
    int *foo = (int[]){1, 2, 3};
    EXPECT("compound literal:array", 2, foo[1]);
  }
  {
    struct Foo {int x;};
    struct Foo foo = (struct Foo){66};
    EXPECT("compound literal:struct", 66, foo.x);

    struct Foo *foop = &(struct Foo){77};
    EXPECT("compound literal:struct ptr", 77, foop->x);
  }
  {
    int i = ++(int){55};
    EXPECT("inc compound literal", 56, i);
  }
  EXPECT("compound literal in global", 88, g_comp_p->x);
  {
    struct S {int x;};
    struct S s = (struct S){44};
    EXPECT("Initializer with compound literal", 44, s.x);
  }
} END_TEST()

//

void empty_function(void){}
int more_params(int a, int b, int c, int d, int e, int f, char g, int h) { return a + b + c + d + e + f + g + h; }
typedef struct {int x;} MoreParamsReturnsStruct;
MoreParamsReturnsStruct more_params_returns_struct(int a, int b, int c, int d, int e, int f, int g) { return (MoreParamsReturnsStruct){f + g}; }
int array_arg_wo_size(int arg[]) { return arg[1]; }
long long long_immediate(unsigned long long x) { return x / 11; }
static inline int inline_func(void) { return 93; }

int mul2(int x) {return x * 2;}
int div2(int x) {return x / 2;}
int (*func_ptr_array[])(int) = {mul2, div2};

typedef unsigned char u8;
u8 const_typedefed(const u8 x);
u8 const_typedefed(const unsigned char x) { return x - 1;}

int vaarg_and_array(int n, ...) {
  int a[14 * 2];
  for (int i = 0; i < 14 * 2; ++i)
    a[i] = 100 + i;
  va_list ap;
  va_start(ap, n);
  int sum = 0;
  for (int i = 0; i < n; ++i)
    sum += va_arg(ap, int);
  va_end(ap);
  return sum;
}

int use_alloca(int index) {
  int *a = alloca(10 * sizeof(*a));
  for (int i = 0; i < 10; ++i)
    a[i] = i;
  return a[index];
}

int 漢字(int χ) { return χ * χ; }

TEST(function) {
  empty_function();
  EXPECT("more params", 36, more_params(1, 2, 3, 4, 5, 6, 7, 8));
  {
    MoreParamsReturnsStruct s = more_params_returns_struct(11, 22, 33, 44, 55, 66, 77);
    EXPECT("more params w/ struct", 143, s.x);
  }

  // EXPECT("proto in func' 78 'int main(){ int sub(int); return sub(77); } int sub(int x) { return x + 1; }'
  {
    extern long extern_in_func;
    extern_in_func = 45;
    EXPECT("extern in func", 45, extern_in_func);
  }
  {
    extern int extern_array_wo_size[];
    EXPECT("array arg w/o size", 22, array_arg_wo_size(extern_array_wo_size));
  }
  EXPECT_PTREQ(empty_function, &empty_function);  // "func ref"
  EXPECT_PTREQ((void*)empty_function, (void*)*empty_function);  // "func deref"
  {
    int acc = 0;
    for (int i = 0; i < 2; ++i)
      acc += func_ptr_array[i](12);
    EXPECT("func-ptr-array", 30, acc);

    int (*funcs[])(int) = {div2, sq, mul2};
    int value = 18;
    for (int i = 0; i < 3; ++i)
      value = funcs[i](value);
    EXPECT("func-ptr-array in local", 162, value);
  }
  {
    int w = 0, x = 2, y = 5;
    int z = sub(++x, y += 10);
    EXPECT("modify arg", 6, x + y + z + w);
  }

  EXPECT("long immediate", 119251678860344574LL, long_immediate(0x123456789abcdef0));
  EXPECT("inline", 93, inline_func());
  EXPECT("const typedef-ed type", 65, const_typedefed(66));

  EXPECT("stdarg", 55, vaarg_and_array(10, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10));

  {
    int x = 77;
    int y = use_alloca(5);
    EXPECT("alloca", 82, x + y);
  }

  EXPECT("unicode", 121, 漢字(11));
} END_TEST()

long extern_in_func;
int extern_array_wo_size[] = {11, 22, 33};

//

int main(void) {
  return RUN_ALL_TESTS(
    test_all,
    test_basic,
    test_struct,
    test_bitfield,
    test_initializer,
    test_function,
  );
}
