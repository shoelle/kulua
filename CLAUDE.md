# Kulua

Deterministic fixed-point Lua for games. Fork of Lua 5.5-work (PUC-Rio).

## Project overview

Kulua keeps Lua 5.5's dual number subtype system (`lua_Integer` + `lua_Number`) but replaces the float subtype with Q16.16 fixed-point (`int32_t`). This gives deterministic arithmetic for networked/replayed game simulations while preserving correct integer table indexing (unlike z8lua's single-type approach).

Current state: **pre-implementation**. The source tree is vanilla Lua 5.5. See `TODO.md` for the Milestone 1 checklist.

## Build (Zig)

```bash
zig build                        # debug build → zig-out/bin/lua
zig build -Doptimize=ReleaseFast # optimized build
zig build run                    # build and launch interpreter
zig build run -- script.lua      # run a script
zig build -Dtests=true           # enable ltests.h instrumentation
```

- `build.zig` compiles all C99 source with Zig's bundled clang
- Produces `liblua.a` (static lib) + `lua` (interpreter)
- No external deps — Zig provides libc, no readline/make/cmake needed
- Original `makefile` still present for reference (Linux/GCC)

## Test

```bash
cd testes && ../zig-out/bin/lua -W all.lua   # run full test suite
```

- Test runner: `testes/all.lua` (34 test modules, ~740K lines)
- C library tests in `testes/libs/` (need separate compilation, not yet in build.zig)

## Source layout

All source lives in the repo root (no `src/` directory).

| Area | Key files |
|------|-----------|
| Config | `luaconf.h`, `lua.h`, `llimits.h` |
| VM | `lvm.c`, `ldo.c`, `lopcodes.h`, `ljumptab.h` |
| Compiler | `lparser.c`, `lcode.c`, `llex.c` |
| Objects/tables | `lobject.c`, `ltable.c`, `lstring.c` |
| GC | `lgc.c`, `lmem.c` |
| C API | `lapi.c`, `lauxlib.c` |
| Std libs | `lmathlib.c`, `lstrlib.c`, `ltablib.c`, `lbaselib.c`, `liolib.c`, `loslib.c` |
| Interpreter | `lua.c` |
| Single-file | `onelua.c` |

## Key implementation targets (Milestone 1)

1. **`luaconf.h`**: Add `LUA_FLOAT_FIXED` option, Q16.16 arithmetic macros, `lua_str2number`/`lua_getlnumfv2` for fixed-point
2. **`lvm.c` / `lobject.c`**: Int-to-float coercion (`<< 16`), float-to-int (`>> 16`), literal parsing
3. **`lmathlib.c`**: LUT-based trig, Newton-Raphson sqrt, stub `exp`/`log`/`pow`
4. **`ltable.c`**: Insertion-order `pairs()`, raw int32 number hashing
5. **Determinism**: Deterministic `tostring` for tables, `luaL_makeseed`, sandbox mode, controlled GC

## References

- [z8lua](https://github.com/samhocevar/z8lua) - Q16.16 Lua 5.2 fork (PICO-8). Math lib and parser reference.
- [Factorio Lua](https://lua-api.factorio.com/latest/auxiliary/libraries.html) - Determinism checklist reference.

## Conventions

- C99 standard, extensive warnings (`-Wall -Wextra` etc.)
- Follow existing PUC Lua coding style (2-space indent in C, `luaX_` prefix naming)
- Platform: primarily Linux, with Windows and WASM (Emscripten) as targets
- Keep changes minimal and isolated behind `#if` guards where possible so upstream merges stay clean
