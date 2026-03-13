# Kulua

Deterministic fixed-point Lua for games. Fork of [Lua 5.5](https://www.lua.org/) (PUC-Rio).

Kulua replaces Lua's `double` float type with **Q16.16 fixed-point** (`int32_t`) for bit-exact arithmetic across platforms. Integer table keys, closures, coroutines, the GC — everything else works like standard Lua.

## Why

Networked and replayed game simulations need deterministic math. IEEE 754 floats aren't — different compilers, platforms, and optimization levels produce different results. Fixed-point arithmetic is identical everywhere because it's just integer math.

## Quick start

```bash
zig build              # build interpreter
zig build run          # launch REPL

echo 'print(math.sin(math.pi / 6))' > test.lua
zig build run -- test.lua
```

No dependencies beyond [Zig](https://ziglang.org/). No make, cmake, or readline.

## How it works

Lua 5.4+ has two number subtypes: `lua_Integer` (64-bit int) and `lua_Number` (double). Kulua keeps the integer and replaces the float:

| | Standard Lua | Kulua |
|---|---|---|
| `lua_Integer` | `long long` (64-bit) | `long long` (64-bit) |
| `lua_Number` | `double` (64-bit IEEE 754) | `int32_t` (Q16.16 fixed-point) |

The upper 16 bits hold the integer part (signed), the lower 16 bits hold the fraction (65,536 steps per unit). This is the same format used by Doom, PICO-8, and GBA games.

Because integers remain 64-bit, `t[1]` is still the integer `1` — not `65536`. This is Kulua's advantage over [z8lua](https://github.com/samhocevar/z8lua) (PICO-8's Lua), which uses a single number type where every table index is a Q16.16 value.

## Differences from standard Lua

If you're porting Lua code to Kulua, here's what to watch for.

### Float range is ±32767.99998

Q16.16 can only represent numbers from about -32768 to +32767. Standard Lua handles numbers up to ~1.8×10³⁰⁸.

```lua
-- Standard Lua: fine
local x = 1000000.5

-- Kulua: saturates to 32767.99998 (math.huge)
local x = 1000000.5  -- silently becomes 32767.99998
```

Integer-to-float conversion saturates when the integer exceeds ±32767:
```lua
local big = 100000     -- integer, fine
local f = big + 0.0    -- coerces to float: saturates to math.huge
```

### No NaN or infinity

Q16.16 has no bit patterns for NaN or infinity. Division by zero returns `math.huge` (positive) or its negation instead of `inf`. The NaN check pattern `x ~= x` always returns `false`.

```lua
-- Standard Lua
print(0/0)        -- nan
print(1/0)        -- inf
print(0/0 == 0/0) -- false (NaN)

-- Kulua
print(0/0)        -- 32767.99998 (clamped)
print(1/0)        -- 32767.99998 (math.huge)
print(0/0 == 0/0) -- true (no NaN exists)
```

`math.huge` is a finite value (~32768) rather than infinity.

### ~4.6 decimal digits of precision

Q16.16 has 16 fractional bits, giving about 4.6 significant decimal digits. Values that differ only in the 5th+ decimal place may compare equal.

```lua
-- Standard Lua: different values
print(0.123456 > 0.123455)  -- true

-- Kulua: same Q16.16 representation
print(0.123456 > 0.123455)  -- false (indistinguishable)
```

Tostring round-trips may lose precision:
```lua
local x = 3.14159
print(x)              -- 3.14159 (fine, within precision)
-- but very precise values won't survive parse → print → parse
```

### Arithmetic saturates on overflow

Add, sub, and mul use 64-bit intermediates and **saturate** to `math.huge` / `-math.huge` on overflow instead of wrapping:

```lua
local x = 32000.0 + 32000.0  -- saturates to math.huge (32767.99998)
local y = 200.0 * 200.0      -- saturates to math.huge (40000 > Q16.16 max)
```

This is safer for game simulations than wrapping (a position that clamps at the world edge is better than one that teleports to the opposite side).

### Math library is approximate

Trig functions use lookup tables (256 entries per quadrant) with linear interpolation. `exp`/`log` use Taylor series. Results are close but not bit-exact with standard `libm`:

```lua
math.sin(math.pi / 2)  -- 1.0 (exact)
math.sin(1.0)           -- ~0.84147 (within ~0.01 of true value)
math.sqrt(2.0)          -- ~1.41421 (good to ~4 digits)
```

`math.atan`, `math.asin`, `math.acos` use CORDIC. `math.exp` and `math.log` are rough approximations suitable for game use, not scientific computing.

### `os.difftime` and `os.clock` saturate

These return Q16.16 floats. Time differences larger than ~32768 seconds (~9 hours) saturate.

### `string.format` works via double conversion

`%f`, `%g`, and `%e` format specifiers convert the Q16.16 value to `double` for display. This is a display-only path and doesn't affect determinism.

### `string.pack`/`unpack` with floats

Packing Q16.16 values into `f` (float) or `d` (double) format converts appropriately. Unpacking saturates if the stored float exceeds Q16.16 range.

## Build options

```bash
zig build                        # debug build → zig-out/bin/lua
zig build -Doptimize=ReleaseFast # optimized build
zig build run                    # build and launch REPL
zig build run -- script.lua      # run a script
zig build -Dtests=true           # enable ltests.h instrumentation
```

The original `makefile` is still present for reference but isn't maintained.

## Test suite

```bash
cd testes && ../zig-out/bin/lua -W -e "_U=true" all.lua
```

All 34 upstream Lua test modules pass, plus `testes/fixed.lua` (Q16.16-specific assertions). Tests that assume 64-bit float range are guarded behind a `_fixedpoint` flag (auto-detected via `math.huge < 100000`).

## Source layout

All source lives in the repo root.

| Area | Key files |
|------|-----------|
| Config | `luaconf.h`, `lua.h`, `llimits.h` |
| Fixed-point | `kulua_fixed.c` (Q16.16 math, parsing, formatting) |
| VM | `lvm.c`, `ldo.c`, `lopcodes.h` |
| Compiler | `lparser.c`, `lcode.c`, `llex.c` |
| Objects/tables | `lobject.c`, `ltable.c`, `lstring.c` |
| Std libs | `lmathlib.c`, `lstrlib.c`, `ltablib.c`, `lbaselib.c`, `liolib.c`, `loslib.c` |
| Interpreter | `lua.c` |

## Merging upstream Lua updates

Kulua is designed to sit on top of upstream Lua with minimal friction. Most changes are behind `#if LUA_FLOAT_TYPE == LUA_FLOAT_FIXED` guards or use macros that expand to no-ops on standard builds. Here's what to watch for when merging a new Lua release.

### Change categories

**Zero conflict** — new standalone files, never touch upstream:
- `kulua_fixed.c` (all Q16.16 math, parsing, trig LUTs)
- `build.zig` (parallel to upstream makefile)

**Low conflict** — guarded `#if` blocks appended to upstream code:
- `luaconf.h` — Q16.16 config section at the end of each upstream block
- `lbaselib.c`, `lcode.c`, `lundump.h`, `lmathlib.c`, `lstrlib.c`, `loslib.c` — isolated `#if` blocks
- `lstate.h`, `lstate.c`, `lapi.c` — new fields/functions, guarded
- `lobject.c` — alternate `tostringbuffFloat` implementation behind `#if`

**Medium conflict** — struct field additions (guarded, but interact with layout):
- `lobject.h` — `kulua_objid` in `CommonHeader`, `insert_next` in `Node`, `insert_head`/`insert_tail` in `Table`

**High conflict** — algorithmic additions to table internals:
- `ltable.c` — ~200 lines of insertion-order linked list logic for deterministic `pairs()`. Touches rehash, insert, delete, and iteration paths. If upstream restructures table internals, this needs careful re-verification.

### `cast_num` vs `luai_int2num`

The most common per-site patch is replacing `cast_num(intvalue)` with `luai_int2num(intvalue)` (~20 sites in `lvm.c`, `ltm.c`, `loslib.c`, `lobject.h`, `lvm.h`). These are semantically different and cannot be unified:

- `cast_num(x)` = plain C cast to `lua_Number` (the raw Q16.16 `int32_t`)
- `luai_int2num(i)` = encode integer `i` as Q16.16 (`i << 16` with saturation)

In standard Lua both are `(double)(i)`, so they're identical. In Q16.16 they differ: `cast_num(42)` gives raw value `42` (fraction-only), while `luai_int2num(42)` gives `2752512` (the encoding of `42.0`).

After merging upstream, grep for new `cast_num(intvalue)` sites in the VM and coercion paths — any place that converts a logical integer to a float needs `luai_int2num` instead.

### Merge checklist

1. Apply upstream diff. Conflicts will mostly be in `luaconf.h` and `ltable.c`.
2. `grep cast_num lvm.c ltm.c loslib.c lobject.h lvm.h` — check for new int-to-float coercion sites, replace with `luai_int2num`.
3. Check `KULUA_ONE` — anywhere upstream assumes float `1.0` for ceiling/floor/rounding logic may need the constant.
4. Rebuild: `zig build`
5. Run tests: `cd testes && ../zig-out/bin/lua -W -e "_U=true" all.lua`
6. Run determinism test: `zig build test-determinism-golden`
7. If upstream changed table internals (`ltable.c`), manually verify insertion-order iteration survives rehash with `testes/fixed.lua`.

## References

- [z8lua](https://github.com/samhocevar/z8lua) — Q16.16 Lua 5.2 fork (PICO-8 engine)
- [Factorio Lua](https://lua-api.factorio.com/latest/auxiliary/libraries.html) — deterministic multiplayer Lua reference
- [Lua 5.5](https://www.lua.org/) — upstream

## License

Same as Lua — MIT license. See `lua.h` for the full notice.

## Caveat Machinam

This was written with Claude Code. 

