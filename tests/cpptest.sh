#!/bin/bash

set -o pipefail

source ./test_sub.sh

AOUT=${AOUT:-$(basename `mktemp -u`)}
CPP=${CPP:-../cpp}
XCC=${XCC:-../xcc}
RUN_AOUT=${RUN_AOUT:-./"$AOUT"}

try() {
  local title="$1"
  local expected
  expected=$(echo -e "$2")
  local input="$3"

  begin_test "$title"

  local actual
  actual=$(echo -e "$input" | $CPP 2>/dev/null | tr -d '\n')
  local exitcode=$?
  if [ $exitcode -ne 0 ]; then
    end_test "CPP failed: exitcode=${exitcode}"
    return
  fi

  local err=''; [[ "$actual" == "$expected" ]] || err="${expected} expected, but ${actual}"
  end_test "$err"
}

try_run() {
  local title="$1"
  local expected
  expected=$(echo -e "$2")
  local input="$3"

  begin_test "$title"

  echo -e "$input" | $XCC -o "$AOUT" -Werror -xc - || exit 1

  $RUN_AOUT
  local actual="$?"

  local err=''; [[ "$actual" == "$expected" ]] || err="${expected} expected, but ${actual}"
  end_test "$err"
}

pp_error() {
  local title="$1"
  local input="$2"

  begin_test "$title"

  echo -e "$input" | $CPP > /dev/null 2>&1 | tr -d '\n'
  local result="$?"

  local err=''; [[ "$result" -ne 0 ]] || err="Compile error expected, but succeeded"
  end_test "$err"
}

test_misc() {
  begin_test_suite "Misc"

  try '#define' '0' "#define NULL 0\nNULL"
  try 'empty' '' "#define EMPTY\nEMPTY"
  try '#undef' 'undefined' "#define FOO\n#undef FOO\n#ifdef FOO\ndefined\n#else\nundefined\n#endif"
  try 'Param' '((1) + (2))' "#define ADD(x,y)  ((x) + (y))\nADD(1, 2)"
  try 'No arguments keeps as is' 'int MAX=123;' "#define MAX(a,b) ((a)>=(b)?(a):(b))\nint MAX=123;"
  try 'Newline in macro' '1+2' "#define ADD(x,y) x+y\nADD(1,\n2)"
  try 'Newline in macro2' '(1 + 2)' "#define FOO(x) (x)\nFOO( 1  \n + 2 )"
  try 'Newline in macro3' '(123)' "#define FOO(x) (x)\nFOO\n(123)"
  try 'Macro w/ str str' '"a" "b"' "#define M(x) x\nM(\"a\" \"b\")"
  try 'Block comment after #define' '88' '#define X 88/*block\ncomment*/\nX'
  try 'Nothing' 'ABC ' '#define NOTHING /*nothing*/\nABC NOTHING'
  try '#ifdef' 'x' "#define X\n#ifdef X\nx\n#else\ny\n#endif"
  try '#ifdef else' 'y' "#ifdef X\nx\n#else\ny\n#endif"
  try '#if 1' '2' "#if 1\n2\n#else\n3\n#endif"
  try '#if 0' '3' "#if 0\n3\n#else\n3\n#endif"
  try '#elif-1' '2' "#if 1\n2\n#elif 1\n3\n#elif 1\n4#else\n5\n#endif"
  try '#elif-01' '3' "#if 0\n2\n#elif 1\n3\n#elif 1\n4\n#else\n5\n#endif"
  try '#elif-001' '4' "#if 0\n2\n#elif 0\n3\n#elif 1\n4\n#else\n5\n#endif"
  try 'non-defined-ident is zero' 'false' "#if UNDEF\ntrue\n#else\nfalse\n#endif"
  try '#if defined' '3' "#if defined(XXX)\n2\n#else\n3\n#endif"
  try '#if defined w/o ()' '3' "#if defined XXX\n2\n#else\n3\n#endif"
  try '#if !defined' '2' "#if !defined(XXX)\n2\n#else\n3\n#endif"
  try '#if defined reserved' 'NOT DEFINED' "#if !defined(int)\nNOT DEFINED\n#endif"
  try '#if &&' '3' "#if 1 && 2\n3\n#else\n4\n#endif"
  try '#if &&-2' '4' "#if 1 && 0\n3\n#else\n4\n#endif"
  try '#if ||' '3' "#if 0 || 1\n3\n#else\n4\n#endif"
  try '#if ||-2' '4' "#if 0 || 0\n3\n#else\n4\n#endif"
  try '#if exp' 'abc' "#if 4 + 5 > 6\nabc\n#else\nxyz\n#endif"
  try '#if VAR expr' 'FOUR' "#define X  -2 + 2\n#if X * 3 == 0\nZERO\n#elif X * 3 == 4\nFOUR\n#endif"
  try 'Macro in #if' '111' "#define FOO(x) x/2\n#if FOO(2)==1\n111\n#else\n222\n#endif"
  try 'Direct comment' '/*comment*///comment' "#if 0\n#else/*comment*/\n#endif//comment"
  try '#if w/ block comment' 'AAA' '#if 1==2/*\n*/-1\nAAA\n#else\nBBB\n#endif'
  try '#else w/ block comment' '/**/BBB' '#if 0\nAAA\n#else/*\n*/\nBBB\n#endif'
  try '#endif w/ block comment' '/**/CCC' '#if 0\nAAA\n#endif/*\n*/CCC'
  try '__FILE__' '"*stdin*"' "__FILE__"
  try '__LINE__' "3" "\n\n__LINE__"
  try '#line' "# 123 \"foobar.p\" 1\"foobar.p\":123" "#line  123\t\"foobar.p\"\n__FILE__:__LINE__"
  try '#line number only' "# 123 \"*stdin*\" 1\"dummy\"\"*stdin*\":124" "#line  123\n\"dummy\"\n__FILE__:__LINE__"
  try 'Block comment' '/*block comment*/' "/*\nblock comment\n*/"
  try 'Quote in comment' "/*I'm fine*/" "/*\nI'm fine\n*/"

  try 'recursive macro' 'SELF(123-1)' "#define SELF(n) SELF(n-1)\nSELF(123)"
  try 'recursive macro in expr' 'false' "#define SELF SELF\n#if SELF\ntrue\n#else\nfalse\n#endif"
  try 'Nested' 'H(987)' "#define F(x) C(G(x))\n#define G(x) C(H(x))\n#define C(x) x\nF(987)"
  try 'recursive in arg' 'SELF' "#define I(v)  v\n#define SELF  I(SELF)\nSELF"
  try 'Empty arg' '"" ""' "#define F(x, y) #x #y\nF(  ,  )"
  try 'vaarg' '1 2 (3, 4, 5)' "#define VAARG(x, y, ...)  x y (__VA_ARGS__)\nVAARG(1, 2, 3, 4, 5)"
  try 'no vaarg' '1 2 ()' "#define VAARG(x, y, ...)  x y (__VA_ARGS__)\nVAARG(1, 2)"
  try 'all vaarg' '{x, y, z};' "#define ALL(...)  {__VA_ARGS__};\nALL(x, y, z)"
  try 'named vaarg' '{3, 4, 5}' "#define NAMED(x, y, rest...)  {rest}\nNAMED(1, 2, 3, 4, 5)"

  end_test_suite
}

test_cat() {
  begin_test_suite "Concat"

  local CATDEFS=`cat <<EOS
#define CAT(x, y) x ## y
#define INDIRECT(x, y) CAT(x, y)
#define N 1
#define FOO1 MATCHED
#define X Y
#define _ /* empty */
EOS
`
  try 'simple'   'ABCDEF'   "$CATDEFS\nCAT(ABC, DEF)"
  try 'non-sym'  '123UL'    "$CATDEFS\nCAT(123, UL)"
  try 'non-sym2' '123.f'    "$CATDEFS\nCAT(123, .f)"
  try 'match'    'MATCHED'  "$CATDEFS\nCAT(FOO, 1)"
  try 'unmatch'  'FOON'     "$CATDEFS\nCAT(FOO, N)"
  try 'indirect' 'MATCHED'  "$CATDEFS\nINDIRECT(FOO, N)"
  try 'unmatch2' 'XN'       "$CATDEFS\nCAT(X, N)"
  try 'unmatch3' 'X_'       "$CATDEFS\nCAT(X, _)"
  try 'unmatch4' '_X'       "$CATDEFS\nCAT(_, X)"

  try 'with non-param' 'x y z_' "#define POST(x) x ## _\nPOST(x y z)"
  try 'num with postfix' '123U' "# define UINT32_C(c) c ## U\nUINT32_C(123)"
  try 'empty l' 'R' "#define CAT(x, y) x ## y\nCAT(, R)"
  try 'empty r' 'L' "#define CAT(x, y) x ## y\nCAT(L, )"
  try 'empty both' '' "#define CAT(x, y) x ## y\nCAT(, )"

  end_test_suite
}

test_stringify() {
  begin_test_suite "Stringify"

  try 'basic' '"1 + 2"' '#define S(x)  #x\nS(1 + 2)'
  try 'escaped' '"\"abc\""' '#define S(x)  # x\nS("abc")'

  end_test_suite
}

test_error() {
  begin_test_suite "Error"

  pp_error '#error' '#error !!!\nvoid main(){}'
  pp_error '#if not closed' '#if 1'
  pp_error '#elif not closed' '#if 0\n#elif 1'
  pp_error 'Duplicate #else' '#if 0\n#else\n#else\n#endif'
  pp_error 'less params' '#define FOO(x, y) x+y\nFOO(1)'
  pp_error 'more params' '#define FOO(x, y) x+y\nFOO(1, 2, 3)'

  end_test_suite
}

test_run() {
  begin_test_suite "Run"

  # Include with macro
  echo "#define FOO (37)" > tmp.h
  try_run 'Include with macro' 37 "#define FILE  \"tmp.h\"\n#include FILE\nint main(){return FOO;}"

  # Block comment after include
  echo "#define FOO (73)" > tmp.h
  try_run 'Comment after include' 73 "#include \"tmp.h\" /*block\n*/ // line\nint main(){return FOO;}"
  pp_error 'Token after include comment' "#include \"tmp.h\" /*block\n*/ illegal-token\nint main(){return FOO;}"

  end_test_suite
}

test_misc
test_cat
test_stringify
test_error
test_run

if [[ $FAILED_SUITE_COUNT -ne 0 ]]; then
  exit $FAILED_SUITE_COUNT
fi
