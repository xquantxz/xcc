#include <stdio.h>

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>  // intptr_t
#include <string.h>

#ifndef __NO_FLONUM
#include <math.h>
#endif

#include "./xtest.h"

#define SSIZE  (64)
#define MARKER  (0xbd)

void expect_vsnprintf(const char *expected, int expected_len, const char *fmt, ...) {
  begin_test(expected);

  char out[SSIZE + 1];
  out[SSIZE] = MARKER;

  va_list ap;
  va_start(ap, fmt);
  int len = vsnprintf(out, SSIZE, fmt, ap);
  va_end(ap);
  int written = len < SSIZE - 1 ? len : SSIZE - 1;

  if (len != expected_len || memcmp(expected, out, written) != 0) {
    fail("actal [%s], len=%d/%d", out, len, expected_len);
  } else if ((unsigned char)out[SSIZE] != MARKER) {
    fail("marker broken");
  } else if (out[written] != '\0') {
    fail("not nul terminated, %d, [%s]\n", len, out);
  }
}

TEST(open_memstream) {
  char *ptr = NULL;
  size_t size = 0;
  FILE *fp = open_memstream(&ptr, &size);
  EXPECT_NOT_NULL(fp);
  if (fp != NULL) {
    EXPECT_EQ(12, fprintf(fp, "Hello world\n"));
    EXPECT_EQ(14, fprintf(fp, "Number: %d\n", 12345));
    fclose(fp);
    EXPECT_NOT_NULL(ptr);
    EXPECT_EQ(12 + 14, size);
  }
} END_TEST()

TEST(sprintf) {
  char buf[16];
  memset(buf, 0x7f, sizeof(buf));
  EXPECT_EQ(5, sprintf(buf, "%d", 12345));
  EXPECT_EQ('\0', buf[5]);
  EXPECT_EQ(0x7f, buf[6]);
} END_TEST()

TEST(vsnprintf) {
#define EXPECT(expected, fmt, ...)  expect_vsnprintf(expected, sizeof(expected)-1, fmt, __VA_ARGS__)
  EXPECT("Number:123", "Number:%d", 123);
  EXPECT("Negative:-456", "Negative:%d", -456);
  EXPECT("Flag:+789", "Flag:%+d", 789);
  EXPECT("FlagNeg:-987", "FlagNeg:%+d", -987);
  EXPECT("Padding:  654", "Padding:%5d", 654);
  EXPECT("ZeroPadding:00321", "ZeroPadding:%05d", 321);
  EXPECT("PaddingOver:12345678", "PaddingOver:%5d", 12345678);
  EXPECT("EndPadding:234  ", "EndPadding:%-5d", 234);
  EXPECT("Hex:89ab", "Hex:%x", 0x89ab);
  if (sizeof(int) == 4) {
    EXPECT("Hex-:ffff7655", "Hex-:%x", -0x89ab);
    EXPECT("Unsigned:4294932053", "Unsigned:%u", -0x89ab);
  }
  if (sizeof(long) == 8) {
    EXPECT("ULong:18446744073709551615", "ULong:%lu", -1L);
  }
  if (sizeof(long long) == 8) {
    EXPECT("ULLong:18446744073709551615", "ULLong:%llu", -1LL);
  }

  EXPECT("String:Foo.", "String:%s.", "Foo");
  EXPECT("BeginPadding:  Bar", "BeginPadding:%5s", "Bar");
  EXPECT("EndPadding:Baz  ", "EndPadding:%-5s", "Baz");
  EXPECT("SubstringRemain:   Fo", "SubstringRemain:%5.5s", "Fo");
  EXPECT("SubstringCut:FooBa", "SubstringCut:%5.5s", "FooBarBaz");
  EXPECT("NullString:(null)", "NullString:%s", NULL);

  EXPECT("Param:  Foo", "Param:%*s", 5, "Foo");
  EXPECT("Param2:FooBa", "Param2:%.*s", 5, "FooBarBaz");

  EXPECT("Character", "Char%ccter", 'a');
  EXPECT("Nul\0Inserted", "Nul%cInserted", '\0');
  EXPECT("%", "%%", 666);

  EXPECT("MoreThanBufferSize:12345678901234567890123456789012345678901234567890", "MoreThanBufferSize:%s", "12345678901234567890123456789012345678901234567890");

  EXPECT("Pointer:0x1234", "Pointer:%p", (void*)(intptr_t)0x1234);
  EXPECT("Pointer:  0x1234", "Pointer:%8p", (void*)(intptr_t)0x1234);
  EXPECT("NullPointer:0x0", "NullPointer:%p", NULL);

#ifndef __NO_FLONUM
  EXPECT("Float:1.234000", "Float:%f", 1.234);
  EXPECT("Float-:-1.234000", "Float-:%f", -1.234);
  EXPECT("Float+:+1.234000", "Float+:%+f", 1.234);
  EXPECT("FloatSub:1.23", "FloatSub:%.2f", 1.234);
  EXPECT("inf:inf", "inf:%f", HUGE_VAL);
  EXPECT("nan:nan", "nan:%f", NAN);
#endif
#undef EXPECT
} END_TEST()

int main() {
  return RUN_ALL_TESTS(
    test_open_memstream,
    test_sprintf,
    test_vsnprintf,
  );
}
