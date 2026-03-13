# Kulua

Deterministic fixed-point Lua for games. Fork of Lua 5.5-work (PUC-Rio).

## Project overview

Kulua keeps Lua 5.5's dual number subtype system (`lua_Integer` + `lua_Number`) but replaces the float subtype with Q16.16 fixed-point (`int32_t`). This gives deterministic arithmetic for networked/replayed game simulations while preserving correct integer table indexing (unlike z8lua's single-type approach).

Current state: **Q16.16 core implemented**. The interpreter builds and runs with fixed-point arithmetic. See `TODO.md` for remaining work (determinism patches, test suite adaptation).

## Build (Zig)

```bash
zig build                        # debug build → zig-out/bin/lua
zig build -Doptimize=ReleaseFast # optimized build
zig build run                    # build and launch interpreter
zig build run -- script.lua      # run a script
zig build -Dtests=true           # enable ltests.h instrumentation
```

- `build.zig` compiles all C99 source with Zig's bundled clang + `-DLUA_FIXED_POINT`
- Produces `liblua.a` (static lib) + `lua` (interpreter)
- No external deps — Zig provides libc, no readline/make/cmake needed
- Original `makefile` still present for reference (Linux/GCC)

## Test

```bash
cd testes && ../zig-out/bin/lua -W -e "_U=true" all.lua   # run test suite (portable mode)
```

- Test runner: `testes/all.lua` (34 test modules)
- `-e "_U=true"` sets `_soft`, `_port`, `_nomsg` to skip non-portable and long-running tests
- Some tests assume 64-bit float range and will fail with Q16.16 (expected)

## Source layout

All source lives in the repo root (no `src/` directory).

| Area | Key files |
|------|-----------|
| Config | `luaconf.h`, `lua.h`, `llimits.h` |
| Fixed-point | `kulua_fixed.c` (Q16.16 math, parsing, formatting) |
| VM | `lvm.c`, `ldo.c`, `lopcodes.h`, `ljumptab.h` |
| Compiler | `lparser.c`, `lcode.c`, `llex.c` |
| Objects/tables | `lobject.c`, `ltable.c`, `lstring.c` |
| GC | `lgc.c`, `lmem.c` |
| C API | `lapi.c`, `lauxlib.c` |
| Std libs | `lmathlib.c`, `lstrlib.c`, `ltablib.c`, `lbaselib.c`, `liolib.c`, `loslib.c` |
| Interpreter | `lua.c` |
| Single-file | `onelua.c` |

## Q16.16 architecture

The fixed-point system is activated by `-DLUA_FIXED_POINT` which sets `LUA_FLOAT_TYPE = LUA_FLOAT_FIXED`.

### Key design decisions

- **`luai_int2num(i)`** macro converts integers to Q16.16 (`i << 16` with saturation). Replaces `cast_num(i)` at all int→float coercion sites. For non-fixed builds, expands to plain `cast_num`.
- **`KULUA_ONE`** = `1 << 16` in fixed-point, `1` in float. Used where code assumes float `1.0`.
- **Arithmetic macros** (`luai_nummul`, `luai_numdiv`) use 64-bit intermediates to avoid overflow.
- **`kulua_fixed.c`** contains all Q16.16-specific C functions (str2number, number2str, trig LUT, sqrt, pow, exp, log). Keeps upstream files clean.
- **`l_mathop(op)`** expands to `kulua_##op`, routing to fixed-point implementations.
- **`l_hashfloat`** uses raw int32 value (no `frexp`).
- **No NaN, no inf.** `luai_numisnan` = 0. `math.huge` = `INT32_MAX`.
- **Integer range:** Q16.16 represents ±32767.99998. Values outside this range saturate.

### Files modified from upstream Lua 5.5

- `luaconf.h` — `LUA_FLOAT_FIXED` type, arithmetic macros, `luai_int2num`, `l_hashfloat`
- `llimits.h` — `#if !defined` guards on `cast_num`, `lua_numbertointeger`
- `lobject.h` — `cast_num` → `luai_int2num` in `nvalue()`
- `lvm.h` — `cast_num` → `luai_int2num` in `tonumberns()`
- `lvm.c` — `cast_num` → `luai_int2num` (9 sites), `KULUA_ONE` ceiling fix
- `ltm.c` — `cast_num` → `luai_int2num`
- `lobject.c` — Q16.16 `tostringbuffFloat`
- `lcode.c` — `luaK_numberK` simplified for fixed-point
- `lundump.h` — Q16.16 `LUAC_NUM` constant
- `lmathlib.c` — PI, huge, deg/rad constants, Q16.16 I2d for PRNG
- `lstrlib.c` — `%f/%g/%e` formatting via double conversion, `quotefloat` override
- `lbaselib.c` — `collectgarbage("count")` Q16.16 formatting
- `loslib.c` — `os.clock()`, `os.difftime()` Q16.16 conversion

## References

- [z8lua](https://github.com/samhocevar/z8lua) - Q16.16 Lua 5.2 fork (PICO-8). Math lib and parser reference.
- [Factorio Lua](https://lua-api.factorio.com/latest/auxiliary/libraries.html) - Determinism checklist reference.

## Conventions

- C99 standard, extensive warnings (`-Wall -Wextra` etc.)
- Follow existing PUC Lua coding style (2-space indent in C, `luaX_` prefix naming)
- Platform: primarily Windows (Zig build), with Linux and WASM (Emscripten) as targets
- Keep changes minimal and isolated behind `#if` guards where possible so upstream merges stay clean
- Fixed-point-specific code goes in `kulua_fixed.c` rather than scattering large blocks in upstream files
