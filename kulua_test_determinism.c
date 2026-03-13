/*
** kulua_test_determinism.c
** Determinism verification test harness for Kulua.
**
** Three modes:
**   (default)                  Two independent lua_States, same scripts,
**                              memcmp captured output.
**   --golden-generate          Print captured output to stdout (pipe to file).
**   --golden-verify <path>     Compare captured output against golden file.
**
** Uses kulua_opensandboxlibs() for library setup (no os/io/debug).
** See Copyright Notice in lua.h
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#endif

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"


#define OUTPUT_BUF_SIZE  (64 * 1024)

typedef struct {
  char data[OUTPUT_BUF_SIZE];
  size_t len;
} OutputBuffer;


/*
** Custom 'print' replacement that appends to an OutputBuffer.
** Buffer pointer is stored as upvalue 1 (light userdata).
*/
static int capture_print (lua_State *L) {
  OutputBuffer *buf = (OutputBuffer *)lua_touserdata(L, lua_upvalueindex(1));
  int n = lua_gettop(L);
  int i;
  for (i = 1; i <= n; i++) {
    size_t slen;
    const char *s;
    if (i > 1) {
      if (buf->len >= OUTPUT_BUF_SIZE) goto overflow;
      buf->data[buf->len++] = '\t';
    }
    s = luaL_tolstring(L, i, &slen);
    if (buf->len + slen > OUTPUT_BUF_SIZE) goto overflow;
    memcpy(buf->data + buf->len, s, slen);
    buf->len += slen;
    lua_pop(L, 1);  /* pop tolstring result */
  }
  if (buf->len >= OUTPUT_BUF_SIZE) goto overflow;
  buf->data[buf->len++] = '\n';
  return 0;
overflow:
  return luaL_error(L, "output buffer overflow (%d bytes)", OUTPUT_BUF_SIZE);
}


static void install_capture_print (lua_State *L, OutputBuffer *buf) {
  lua_pushlightuserdata(L, buf);
  lua_pushcclosure(L, capture_print, 1);
  lua_setglobal(L, "print");
}


/* ===== Test scripts ===== */

static const char *test_arithmetic =
  "print('=== arithmetic ===')\n"
  "print(1.5 + 2.5)\n"
  "print(10.0 - 3.0)\n"
  "print(2.0 * 3.0)\n"
  "print(10.0 / 4.0)\n"
  "print(100.0 * 100.0)\n"
  "print(200.0 * 100.0)\n"
  "print(1.0 / 3.0)\n"
  "print(7.0 / 2.0)\n"
  "print(-1.5 + 2.5)\n"
  "print(-(3.5))\n"
  "print(math.floor(3.7))\n"
  "print(math.ceil(3.2))\n"
  "print(string.format('%.4f', 3.14))\n"
  "print(string.format('%g', 0.5))\n"
  "print(string.format('%.2f', 100.0 * 100.0))\n"
  "print(tostring(1.5))\n"
  "print(tostring(-0.25))\n"
  "print(tostring(0.0))\n"
;

static const char *test_pairs_order =
  "print('=== pairs order ===')\n"
  "local t = {}\n"
  "local keys = {'z','a','m','b','x','c','w','d','v','e'}\n"
  "for _, k in ipairs(keys) do t[k] = true end\n"
  "for k, v in pairs(t) do print(k, v) end\n"
;

static const char *test_pairs_delete_reinsert =
  "print('=== pairs delete/reinsert ===')\n"
  "local t = {}\n"
  "for i = 1, 20 do t['k'..i] = i end\n"
  "for i = 1, 10 do t['k'..i] = nil end\n"
  "for i = 21, 30 do t['k'..i] = i end\n"
  "local count = 0\n"
  "for k, v in pairs(t) do\n"
  "  print(k, v)\n"
  "  count = count + 1\n"
  "end\n"
  "print('count', count)\n"
;

static const char *test_tostring_ids =
  "print('=== tostring IDs ===')\n"
  "local t1 = {}\n"
  "local t2 = {}\n"
  "local t3 = {}\n"
  "print(tostring(t1))\n"
  "print(tostring(t2))\n"
  "print(tostring(t3))\n"
  "local f1 = function() end\n"
  "local f2 = function() end\n"
  "print(tostring(f1))\n"
  "print(tostring(f2))\n"
  "local co = coroutine.create(function() end)\n"
  "print(tostring(co))\n"
;

static const char *test_math =
  "print('=== math ===')\n"
  "print(math.sin(0.0))\n"
  "print(math.cos(0.0))\n"
  "print(math.sin(math.pi / 2))\n"
  "print(math.cos(math.pi))\n"
  "print(math.atan(1.0))\n"
  "print(math.atan(1.0, 0.0))\n"
  "print(math.sqrt(4.0))\n"
  "print(math.sqrt(2.0))\n"
  "print(math.exp(0.0))\n"
  "print(math.exp(1.0))\n"
  "print(math.log(1.0))\n"
  "print(math.abs(-5.5))\n"
  "print(math.floor(3.7))\n"
  "print(math.ceil(-3.2))\n"
  "print(math.fmod(7.0, 3.0))\n"
  "print(math.pi)\n"
  "print(math.huge)\n"
  "print(math.maxinteger)\n"
  "print(math.mininteger)\n"
;

static const char *test_random =
  "print('=== random ===')\n"
  "math.randomseed(42)\n"
  "for i = 1, 20 do print(math.random()) end\n"
  "math.randomseed(42)\n"
  "for i = 1, 10 do print(math.random(1, 100)) end\n"
;

static const char *test_strings =
  "print('=== strings ===')\n"
  "print(string.rep('ab', 5))\n"
  "print(string.sub('hello world', 1, 5))\n"
  "print(string.find('hello world', 'world'))\n"
  "print(string.format('%d %s %.2f', 42, 'test', 1.5))\n"
  "print(string.upper('hello'))\n"
  "print(string.lower('HELLO'))\n"
  "print(string.reverse('abcdef'))\n"
  "print(string.byte('A'))\n"
  "print(string.char(65))\n"
  "print(#'hello world')\n"
;

static const char *test_gc_interaction =
  "print('=== gc interaction ===')\n"
  "local t = {}\n"
  "for i = 1, 10 do t['key'..i] = i end\n"
  "collectgarbage('collect')\n"
  "for k, v in pairs(t) do print(k, v) end\n"
  "collectgarbage('collect')\n"
  "-- create more objects after GC, verify iteration still works\n"
  "local t2 = {}\n"
  "t2.a = 1; t2.b = 2; t2.c = 3\n"
  "for k, v in pairs(t2) do print(k, v) end\n"
  "-- verify tostring is deterministic format (absolute ID varies\n"
  "-- cross-platform due to GC timing differences)\n"
  "local s = tostring(t2)\n"
  "print(string.find(s, '^table: %d+$') and 'tostring_ok' or 'tostring_fail')\n"
;


static int run_all_tests (lua_State *L) {
  const char *all_tests[] = {
    test_arithmetic,
    test_pairs_order,
    test_pairs_delete_reinsert,
    test_tostring_ids,
    test_math,
    test_random,
    test_strings,
    test_gc_interaction,
    NULL
  };
  int i;
  for (i = 0; all_tests[i] != NULL; i++) {
    if (luaL_dostring(L, all_tests[i]) != LUA_OK) {
      fprintf(stderr, "ERROR in test %d: %s\n", i,
              lua_tostring(L, -1));
      return 0;
    }
  }
  return 1;
}


static lua_State *create_test_state (OutputBuffer *buf) {
  lua_State *L = luaL_newstate();
  if (L == NULL) {
    fprintf(stderr, "FATAL: cannot create Lua state\n");
    exit(2);
  }
  kulua_opensandboxlibs(L);
  install_capture_print(L, buf);
  /* Reset object ID counter after all setup so user scripts start from
     a known baseline regardless of platform-specific init allocations */
  lua_resetobjidcounter(L);
  return L;
}


/* ===== Diff diagnostic ===== */

static void print_diff (const OutputBuffer *a, const OutputBuffer *b) {
  size_t minlen = a->len < b->len ? a->len : b->len;
  size_t pos;
  int line = 1;
  for (pos = 0; pos < minlen; pos++) {
    if (a->data[pos] != b->data[pos]) break;
    if (a->data[pos] == '\n') line++;
  }
  if (pos < minlen) {
    fprintf(stderr, "First difference at byte %zu (line %d):\n", pos, line);
    fprintf(stderr, "  State 1: 0x%02x '%c'\n",
            (unsigned char)a->data[pos],
            a->data[pos] >= 32 ? a->data[pos] : '?');
    fprintf(stderr, "  State 2: 0x%02x '%c'\n",
            (unsigned char)b->data[pos],
            b->data[pos] >= 32 ? b->data[pos] : '?');
  } else {
    fprintf(stderr, "Outputs differ in length: %zu vs %zu\n",
            a->len, b->len);
  }
}


/* ===== Modes ===== */

static int mode_dual_state (void) {
  OutputBuffer buf1 = {{""},0}, buf2 = {{""},0};
  lua_State *L1, *L2;
  int ok;

  L1 = create_test_state(&buf1);
  ok = run_all_tests(L1);
  lua_close(L1);
  if (!ok) { fprintf(stderr, "FAIL: State 1 script error\n"); return 1; }

  L2 = create_test_state(&buf2);
  ok = run_all_tests(L2);
  lua_close(L2);
  if (!ok) { fprintf(stderr, "FAIL: State 2 script error\n"); return 1; }

  if (buf1.len != buf2.len ||
      memcmp(buf1.data, buf2.data, buf1.len) != 0) {
    fprintf(stderr, "FAIL: outputs differ (%zu vs %zu bytes)\n",
            buf1.len, buf2.len);
    print_diff(&buf1, &buf2);
    return 1;
  }

  printf("PASS: dual-state determinism verified (%zu bytes identical)\n",
         buf1.len);
  return 0;
}


static int mode_golden_generate (void) {
  OutputBuffer buf = {{""},0};
  lua_State *L = create_test_state(&buf);
  int ok = run_all_tests(L);
  lua_close(L);
  if (!ok) { fprintf(stderr, "FAIL: script error during generation\n"); return 1; }

  /* Write to stdout in binary mode */
#ifdef _WIN32
  _setmode(_fileno(stdout), _O_BINARY);
#endif
  fwrite(buf.data, 1, buf.len, stdout);
  fflush(stdout);
  fprintf(stderr, "Generated %zu bytes of golden output\n", buf.len);
  return 0;
}


static int mode_golden_verify (const char *path) {
  OutputBuffer buf = {{""},0};
  FILE *f;
  char *golden;
  long fsize;
  size_t nread;
  lua_State *L;
  int ok;

  /* Read golden file */
  f = fopen(path, "rb");
  if (f == NULL) {
    fprintf(stderr, "FAIL: cannot open golden file: %s\n", path);
    return 1;
  }
  fseek(f, 0, SEEK_END);
  fsize = ftell(f);
  fseek(f, 0, SEEK_SET);
  golden = (char *)malloc(fsize);
  if (golden == NULL) {
    fprintf(stderr, "FAIL: cannot allocate %ld bytes\n", fsize);
    fclose(f);
    return 1;
  }
  nread = fread(golden, 1, fsize, f);
  fclose(f);
  if ((long)nread != fsize) {
    fprintf(stderr, "FAIL: short read on golden file (%zu of %ld)\n",
            nread, fsize);
    free(golden);
    return 1;
  }

  /* Run tests */
  L = create_test_state(&buf);
  ok = run_all_tests(L);
  lua_close(L);
  if (!ok) { free(golden); return 1; }

  /* Compare */
  if (buf.len != (size_t)fsize ||
      memcmp(buf.data, golden, buf.len) != 0) {
    OutputBuffer goldbuf;
    fprintf(stderr, "FAIL: output does not match golden file\n");
    fprintf(stderr, "  Current: %zu bytes, Golden: %ld bytes\n",
            buf.len, fsize);
    /* Use print_diff for diagnostics */
    goldbuf.len = (size_t)fsize < OUTPUT_BUF_SIZE ? (size_t)fsize : OUTPUT_BUF_SIZE;
    memcpy(goldbuf.data, golden, goldbuf.len);
    print_diff(&buf, &goldbuf);
    free(golden);
    return 1;
  }

  free(golden);
  printf("PASS: output matches golden file (%zu bytes)\n", buf.len);
  return 0;
}


int main (int argc, char *argv[]) {
  if (argc >= 2 && strcmp(argv[1], "--golden-generate") == 0)
    return mode_golden_generate();
  if (argc >= 3 && strcmp(argv[1], "--golden-verify") == 0)
    return mode_golden_verify(argv[2]);
  if (argc >= 2) {
    fprintf(stderr, "Usage: %s [--golden-generate | --golden-verify <file>]\n",
            argv[0]);
    return 1;
  }
  return mode_dual_state();
}
