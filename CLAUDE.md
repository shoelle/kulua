# Kulua

Deterministic fixed-point Lua for games. Fork of Lua 5.5-work (PUC-Rio).

## Project overview

Kulua keeps Lua 5.5's dual number subtype system (`lua_Integer` + `lua_Number`) but replaces the float subtype with Q16.16 fixed-point (`int32_t`). This gives deterministic arithmetic for networked/replayed game simulations while preserving correct integer table indexing (unlike z8lua's single-type approach).

Current state: **Q16.16 core + record types implemented, test suite passing**. The interpreter builds and runs with fixed-point arithmetic and first-class record types. All 34 upstream Lua test modules pass. See `TODO.md` for remaining work (determinism patches). See `README.md` for user-facing differences and gotchas vs standard Lua.

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

- Test runner: `testes/all.lua` (36 test modules: 34 upstream + `fixed.lua` + `record.lua`)
- `-e "_U=true"` sets `_soft`, `_port`, `_nomsg` to skip non-portable and long-running tests
- All 36 test modules pass; float-range-dependent tests are guarded behind `_fixedpoint`
- `testes/fixed.lua` contains Q16.16-specific assertions (saturation, trig, parsing, determinism)
- `testes/record.lua` contains record type tests (all field types, arrays, views, GC, methods, coroutines)

## Source layout

All source lives in the repo root (no `src/` directory).

| Area | Key files |
|------|-----------|
| Config | `luaconf.h`, `lua.h`, `llimits.h` |
| Fixed-point | `kulua_fixed.c` (Q16.16 math, parsing, formatting) |
| Records | `kulua_record.c`, `kulua_record.h` (record types, arrays, views, C API) |
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
- **`KULUA_HUGE_VAL`** / **`KULUA_NHUGE_VAL`** = symmetric ±32767.99998 sentinel constants (substitutes for ±inf). `INT32_MIN` (-32768.0) is a valid value, not a sentinel.
- **Arithmetic macros** (`luai_numadd`, `luai_numsub`, `luai_nummul`, `luai_numdiv`) use 64-bit intermediates and **saturate** on overflow to `KULUA_HUGE_VAL`/`KULUA_NHUGE_VAL`.
- **`kulua_fixed.c`** contains all Q16.16-specific C functions (str2number, number2str, trig LUT, sqrt, pow, exp, log). Keeps upstream files clean.
- **`l_mathop(op)`** expands to `kulua_##op`, routing to fixed-point implementations.
- **`l_hashfloat`** uses raw int32 value (no `frexp`).
- **No NaN, no inf.** `luai_numisnan` = 0. `math.huge` = `INT32_MAX`.
- **Integer range:** Q16.16 represents ±32767.99998. Values outside this range saturate.

### Files modified from upstream Lua 5.5

- `luaconf.h` — `LUA_FLOAT_FIXED` type, saturating arithmetic, `luai_int2num`, `l_hashfloat`, sentinel constants
- `llimits.h` — `#if !defined` guards on `cast_num`, `lua_numbertointeger`
- `lua.h` — `LUA_TRECORD` type constant (value 9), `LUA_NUMTYPES` bump, record C API declarations
- `lobject.h` — `cast_num` → `luai_int2num` in `nvalue()`; `RecordType`, `Record`, `RecordArray` structs; variant tags; accessor/setter macros; size computation macros
- `lstate.h` — `gco2rtype`/`gco2rec`/`gco2recarr` cast macros (direct casts, not via GCUnion)
- `lvm.h` — `cast_num` → `luai_int2num` in `tonumberns()`
- `lvm.c` — `cast_num` → `luai_int2num` (9 sites), `KULUA_ONE` ceiling fix; record field fast-paths in `OP_GETTABLE`/`OP_SETTABLE`/`OP_SELF`; record array indexing in `OP_GETI`; `#` operator for record variants
- `ltm.c` — `cast_num` → `luai_int2num`; `LUA_TRECORD` typename; variant-aware metatable lookup in `luaT_gettmbyobj`; variant type names in `luaT_objtypename`
- `lgc.c` — `objsize`/`getgclist`/`reallymarkobject`/`propagatemark`/`freeobj` cases for all three record variants; `traverserecordtype` and `traverserecordarray` functions
- `lapi.c` — `LUA_TRECORD` case in `lua_getmetatable`
- `lobject.c` — Q16.16 `tostringbuffFloat`
- `lcode.c` — `luaK_numberK` simplified for fixed-point
- `lundump.h` — Q16.16 `LUAC_NUM` constant
- `lmathlib.c` — PI, huge, deg/rad constants, Q16.16 I2d for PRNG
- `lstrlib.c` — `%f/%g/%e` formatting via double conversion, `quotefloat` override
- `lbaselib.c` — `collectgarbage("count")` Q16.16 formatting; `record` constructor and field type globals registration; `kulua_record_init` call; variant-aware `luaB_type`
- `ltable.c` — GC object hashing uses `kulua_objid` for determinism, insertion-order linked list
- `loslib.c` — `os.clock()`, `os.difftime()` Q16.16 conversion
- `onelua.c` — includes `kulua_fixed.c` and `kulua_record.c` for single-file builds

## Record type architecture

Records are a Kulua-only feature (not in upstream Lua). They provide first-class C-struct-like types for zero-copy Lua/C data sharing.

### Key design decisions

- **`LUA_TRECORD`** (type tag 9) with three variants: `LUA_VRECORDTYPE` (schema), `LUA_VRECORD` (instance or view), `LUA_VRECORDARRAY` (contiguous array)
- **`kulua_record.c`/`kulua_record.h`** contain all record-specific code (allocation, field access, constructor, C API). Keeps upstream files clean.
- **Nine field types**: `fx` (Q16.16), `i8`, `u8`, `i16`, `u16`, `i32`, `u32`, `i64`, `bool`. No floats, pointers, strings, or nested records.
- **Packed layout** with no alignment padding. Fields laid out in declaration order. `memcpy` for unaligned access safety (compilers optimize to single loads).
- **Array views**: `RecordArray[i]` returns a `Record` whose `data` pointer points into the parent array's memory. The view holds a GC reference (`rec->parent`) to prevent use-after-free.
- **Not in GCUnion**: Record types use direct casts in `gco2*` macros (not union member access) because `RecordType` has a flexible array member and including it would inflate `GCUnion` size.
- **VM fast-paths**: `OP_GETFIELD`/`OP_SETFIELD` check for records before the table path. Field lookup uses interned `TString*` pointer comparison (linear scan, fine for <30 fields).
- **GC-friendly**: Record data contains only primitive bytes (no Lua object references), so traversal only marks the `rtype`, `parent`, and field name strings.
- **Per-type metatables**: Each `RecordType` has its own instance metatable for methods. `RecordType` itself uses `G(L)->mt[LUA_TRECORD]` for `__call`/`__index`/`__newindex`/`__len`/`__tostring`.

## References

- [z8lua](https://github.com/samhocevar/z8lua) - Q16.16 Lua 5.2 fork (PICO-8). Math lib and parser reference.
- [Factorio Lua](https://lua-api.factorio.com/latest/auxiliary/libraries.html) - Determinism checklist reference.

## Conventions

- C99 standard, extensive warnings (`-Wall -Wextra` etc.)
- Follow existing PUC Lua coding style (2-space indent in C, `luaX_` prefix naming)
- Platform: primarily Windows (Zig build), with Linux and WASM (Emscripten) as targets
- Keep changes minimal and isolated behind `#if` guards where possible so upstream merges stay clean
- Fixed-point-specific code goes in `kulua_fixed.c` rather than scattering large blocks in upstream files
