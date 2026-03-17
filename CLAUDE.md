# Kulua

Deterministic Lua for games. Fork of Lua 5.5-work (PUC-Rio).

## Project overview

Kulua builds on Lua 5.5's standard IEEE 754 double-precision floats but adds determinism infrastructure for networked/replayed game simulations: insertion-order table iteration (via `kulua_objid`), `-ffp-contract=off` to prevent fused multiply-add reordering, and sandboxed standard libraries.

Current state: **determinism infrastructure + record types implemented, test suite passing**. The interpreter builds and runs with first-class record types and deterministic table traversal. All 34 upstream Lua test modules pass. See `TODO.md` for remaining work (determinism patches). See `README.md` for user-facing differences and gotchas vs standard Lua.

## Build (Zig)

```bash
zig build                        # debug build → zig-out/bin/lua
zig build -Doptimize=ReleaseFast # optimized build
zig build run                    # build and launch interpreter
zig build run -- script.lua      # run a script
zig build -Dtests=true           # enable ltests.h instrumentation
```

- `build.zig` compiles all C99 source with Zig's bundled clang + `-ffp-contract=off`
- Produces `liblua.a` (static lib) + `lua` (interpreter)
- No external deps — Zig provides libc, no readline/make/cmake needed
- Original `makefile` still present for reference (Linux/GCC)

## Test

```bash
cd testes && ../zig-out/bin/lua -W -e "_U=true" all.lua   # run test suite (portable mode)
```

- Test runner: `testes/all.lua` (35 test modules: 34 upstream + `record.lua`)
- `-e "_U=true"` sets `_soft`, `_port`, `_nomsg` to skip non-portable and long-running tests
- All 35 test modules pass
- `testes/record.lua` contains record type tests (all field types, arrays, views, GC, methods, coroutines)

## Source layout

All source lives in the repo root (no `src/` directory).

| Area | Key files |
|------|-----------|
| Config | `luaconf.h`, `lua.h`, `llimits.h` |
| Records | `kulua_record.c`, `kulua_record.h` (record types, arrays, views, C API) |
| VM | `lvm.c`, `ldo.c`, `lopcodes.h`, `ljumptab.h` |
| Compiler | `lparser.c`, `lcode.c`, `llex.c` |
| Objects/tables | `lobject.c`, `ltable.c`, `lstring.c` |
| GC | `lgc.c`, `lmem.c` |
| C API | `lapi.c`, `lauxlib.c` |
| Std libs | `lmathlib.c`, `lstrlib.c`, `ltablib.c`, `lbaselib.c`, `liolib.c`, `loslib.c` |
| Interpreter | `lua.c` |
| Single-file | `onelua.c` |

## Files modified from upstream Lua 5.5

- `lua.h` — `LUA_TRECORD` type constant (value 9), `LUA_NUMTYPES` bump, record C API declarations
- `lobject.h` — `RecordType`, `Record`, `RecordArray` structs; variant tags; accessor/setter macros; size computation macros
- `lstate.h` — `gco2rtype`/`gco2rec`/`gco2recarr` cast macros (direct casts, not via GCUnion)
- `lvm.c` — record field fast-paths in `OP_GETTABLE`/`OP_SETTABLE`/`OP_SELF`; record array indexing in `OP_GETI`; `#` operator for record variants
- `ltm.c` — `LUA_TRECORD` typename; variant-aware metatable lookup in `luaT_gettmbyobj`; variant type names in `luaT_objtypename`
- `lgc.c` — `objsize`/`getgclist`/`reallymarkobject`/`propagatemark`/`freeobj` cases for all three record variants; `traverserecordtype` and `traverserecordarray` functions
- `lapi.c` — `LUA_TRECORD` case in `lua_getmetatable`
- `lbaselib.c` — `record` constructor and field type globals registration; `kulua_record_init` call; variant-aware `luaB_type`
- `ltable.c` — GC object hashing uses `kulua_objid` for determinism, insertion-order linked list
- `onelua.c` — includes `kulua_record.c` for single-file builds

## Record type architecture

Records are a Kulua-only feature (not in upstream Lua). They provide first-class C-struct-like types for zero-copy Lua/C data sharing.

### Key design decisions

- **`LUA_TRECORD`** (type tag 9) with three variants: `LUA_VRECORDTYPE` (schema), `LUA_VRECORD` (instance or view), `LUA_VRECORDARRAY` (contiguous array)
- **`kulua_record.c`/`kulua_record.h`** contain all record-specific code (allocation, field access, constructor, C API). Keeps upstream files clean.
- **Nine field types**: `i8`, `u8`, `i16`, `u16`, `i32`, `u32`, `i64`, `bool`, `f32`. No pointers, strings, or nested records.
- **Packed layout** with no alignment padding. Fields laid out in declaration order. `memcpy` for unaligned access safety (compilers optimize to single loads).
- **Array views**: `RecordArray[i]` returns a `Record` whose `data` pointer points into the parent array's memory. The view holds a GC reference (`rec->parent`) to prevent use-after-free.
- **Not in GCUnion**: Record types use direct casts in `gco2*` macros (not union member access) because `RecordType` has a flexible array member and including it would inflate `GCUnion` size.
- **VM fast-paths**: `OP_GETFIELD`/`OP_SETFIELD` check for records before the table path. Field lookup uses interned `TString*` pointer comparison (linear scan, fine for <30 fields).
- **GC-friendly**: Record data contains only primitive bytes (no Lua object references), so traversal only marks the `rtype`, `parent`, and field name strings.
- **Per-type metatables**: Each `RecordType` has its own instance metatable for methods. `RecordType` itself uses `G(L)->mt[LUA_TRECORD]` for `__call`/`__index`/`__newindex`/`__len`/`__tostring`.

## References

- [Factorio Lua](https://lua-api.factorio.com/latest/auxiliary/libraries.html) - Determinism checklist reference.

## Conventions

- C99 standard, extensive warnings (`-Wall -Wextra` etc.)
- Follow existing PUC Lua coding style (2-space indent in C, `luaX_` prefix naming)
- Platform: primarily Windows (Zig build), with Linux and WASM (Emscripten) as targets
- Keep changes minimal and isolated behind `#if` guards where possible so upstream merges stay clean
