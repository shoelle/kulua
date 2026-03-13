# Kulua

Deterministic fixed-point Lua for games. Fork of Lua 5.5.

## Core Idea

Lua 5.4+ has two number subtypes: `lua_Integer` and `lua_Number` (float). The VM tracks which is which. Kulua keeps the integer subtype and replaces only the float subtype with Q16.16 fixed-point.

- `lua_Integer` stays as `int32_t`. Array indices, loop counters, bitwise ops all work unchanged.
- `lua_Number` becomes `int32_t` (Q16.16). Fractional values use this.
- Int-to-float coercion becomes `value << 16`. Float-to-int becomes `value >> 16`.
- `t[2]` uses the integer `2` directly. No table indexing problem (unlike z8lua/Lua 5.2 where everything was one type).

## References

- [z8lua](https://github.com/samhocevar/z8lua): C++ Q16.16 Lua 5.2 fork (ZEPTO-8/PICO-8). Proves fixed-point Lua works. Different approach (single number type), but useful for math library and parser reference.
- [Factorio's Lua](https://lua-api.factorio.com/latest/auxiliary/libraries.html): Deterministic multiplayer Lua 5.2 fork. Deterministic `pairs()`, seeded RNG, `frexp`-based hashing, sandbox stripping. The checklist to replicate.
- [Lua 5.5 source](https://www.lua.org/ftp/): Base to fork from.

## Why not other runtimes

- **LuaJIT**: No WASM support (assembly interpreter, no C fallback). NaN-boxing makes number type non-configurable.
- **Luau**: `double` hardcoded with no config macro. ~100K lines of C++. Number surgery is 5x the work of PUC Lua for marginal interpreter speed gains (no codegen on WASM anyway).
- **Lua 5.2**: z8lua proves it works, but the single number type means `t[2]` becomes `t[131072]`, requiring table indexing hacks. 5.5's dual type system avoids this entirely.

## Why Q16.16

- 65,536 fractional steps gives smooth sub-pixel movement and accurate trig.
- Doom, PICO-8, and GBA best practices all validate Q16.16.
- Q24.8 (256 fractional steps) causes visible stutter at slow speeds and degrades trig LUTs from 65K values to 256.
- +-32K integer range covers most game world coordinate systems.

---

## Milestone 1: Deterministic Runtime

Replace the float subtype with Q16.16 fixed-point. Patch for determinism.

### luaconf.h changes

- [x] `LUA_FLOAT_TYPE`: new option `LUA_FLOAT_FIXED` that sets `lua_Number` = `int32_t`
- [x] `l_floatatt`: fixed-point equivalents for `HUGE_VAL`, `MAX`, `MIN`, epsilon
- [x] Arithmetic macros (`luai_numadd`, `luai_numsub`, `luai_nummul`, `luai_numdiv`, `luai_numpow`, `luai_numidiv`, `luai_nummod`): add/sub are plain integer ops; mul/div use 64-bit intermediates
- [x] `luai_numisnan`: always returns 0
- [x] `lua_str2number`: custom parser, decimal string to Q16.16
- [x] Number-to-string formatting: custom `tostringbuffFloat` for Q16.16 display

### VM changes (lvm.c, lobject.c)

- [x] Int-to-float coercion: `luai_int2num` macro (`value << 16` with saturation)
- [x] Float-to-int coercion: `value >> 16` via `lua_numbertointeger`
- [x] Float literal parsing in compiler: `3.14` becomes the Q16.16 representation
- [x] Verify `/` (float division), `//` (integer division), `%` (modulo) all behave correctly
- [x] Verify comparison operators work (fixed-point values compare correctly as plain int32 since the encoding is monotonic)
- [x] `luaV_flttointeger` ceiling fix: `KULUA_ONE` (1<<16) instead of literal `1`
- [x] `luaK_numberK` simplified for fixed-point (no `ldexp` perturbation needed)

### Math library (lmathlib.c)

- [x] `math.sin`, `math.cos`: LUT-based (256 entries per quadrant, linear interpolation)
- [x] `math.sqrt`: Newton-Raphson in fixed-point (6 iterations for Q16.16 precision)
- [x] `math.atan`: CORDIC-based `atan2`
- [x] `math.abs`, `math.floor`, `math.ceil`: bitwise ops on the fixed-point representation
- [x] `math.min`, `math.max`: plain integer comparison (monotonic encoding) — unchanged
- [x] `math.pi`: Q16.16 representation of pi (205887)
- [x] `math.exp`, `math.log`: Taylor series approximations (stub quality)
- [x] `math.random`: Q16.16-native output from xoshiro256** (top 16 bits as fraction)
- [x] `math.randomseed`: unchanged (uses `luaL_makeseed`)

### Table hashing

- [x] Hash function for number keys: use the raw int32 value, not `frexp`-based float hashing

### Determinism patches

- [x] `pairs()` iterates in insertion order (insertion-order linked list in hash nodes, survives rehash)
- [x] `tostring()` on tables/functions/etc: deterministic output (monotonic counter-based `kulua_objid`, not pointer-based)
- [x] Sandbox mode: `kulua_opensandboxlibs()` C API loads safe subset (base, math, string, table, utf8, coroutine), strips `loadfile`/`dofile`/`load`
- [x] GC: `__gc` finalizers disabled in sandbox mode (`kulua_no_gc_metamethod` flag)
- [x] `luaL_makeseed`: deterministic (returns 0 for fixed-point builds)

### Performance (future work)

- [ ] Computed goto dispatch: Lua 5.5 already ships `ljumptab.h` with computed goto infrastructure — investigate whether it's already active with GCC/Clang
- [ ] Audit PUC Lua VM hot paths for game-friendly optimizations
- [ ] Benchmark: Kulua vs PUC Lua 5.5 on representative game workloads

### Build and test

- [x] Zig build system: `build.zig` compiles with `-DLUA_FIXED_POINT`, produces static lib + interpreter
- [x] Compile and pass basic Lua functionality (arithmetic, tables, strings, closures, coroutines)
- [x] Adapt Lua test suite for Q16.16 (tests assuming 64-bit float range need adjustment)
- [x] Fixed-point math tests: verify mul, div, trig, sqrt against known values
- [x] Determinism test: `kulua_test_determinism.c` — two lua_States, same scripts, `memcmp` output (`zig build test-determinism`)
- [x] Cross-platform determinism: golden file approach (`zig build test-determinism-golden`); actual WASM build deferred

---

## Performance

PUC Lua's interpreter is straightforward and leaves performance on the table that LuaJIT and Luau picked up. Some of their wins can be ported back to PUC Lua without the complexity of a JIT or full rewrite.

### Feasible wins (interpreter-level, pure C)

**Computed goto dispatch.** PUC Lua uses a `switch` statement for opcode dispatch. LuaJIT and Luau both use computed goto (`&&label` / `goto *dispatch[op]`). This eliminates the branch predictor bottleneck of a central switch. GCC/Clang support this via labels-as-values. Emscripten (Clang) supports it too. MSVC does not, so keep the switch as a fallback. Estimated gain: 10-20% on opcode-heavy workloads. **Note:** Lua 5.5 already ships `ljumptab.h` with computed goto infrastructure — this may already be partially done upstream. Investigate before implementing.

**Inline caching for table field access.** Luau caches the slot offset for `t.field` accesses. On repeat access to the same field on tables with the same "shape", the lookup skips the hash entirely. LuaJIT does something similar. This is significant for game code that does `entity.x`, `entity.y`, `entity.health` thousands of times per frame. Implementation: add a small cache (key + slot offset) to `GETTABLE`/`SETTABLE` instructions. Moderate complexity.

**NaN-tagging (or tagged-pointer) value representation.** PUC Lua stores values as a struct with a type tag + union (16 bytes on 64-bit). LuaJIT NaN-boxes everything into 8 bytes. Luau uses a tagged union but packs it tighter. Halving TValue size improves cache utilization across the board. However, NaN-boxing is incompatible with our fixed-point number type (it relies on IEEE 754 NaN bit patterns). Tagged pointers might work but the complexity is high. Worth investigating but not a first-pass change.

**String interning with better hashing.** PUC Lua hashes strings on creation for interning. The hash function is adequate but not SIMD-friendly. A faster hash (e.g., wyhash) speeds up string-heavy workloads. Low complexity, easy win.

**Precomputed method lookup.** Game scripts do `entity:method()` constantly. Each call resolves the method through `__index` metamethod lookup. Caching the resolved function pointer (invalidated when the metatable changes) avoids repeated hash lookups. Luau does this. Moderate complexity.

### Not worth porting

**JIT compilation.** LuaJIT's tracing JIT and Luau's method JIT both produce native code. Neither works on WASM. The complexity is enormous. Not worth it for a ~25K line C codebase.

**Register allocation optimizations.** Luau's compiler does smarter register allocation than PUC Lua's single-pass compiler. Meaningful but requires a multi-pass compiler rewrite. Too invasive for the payoff.

### Approach

Start with computed goto (low risk, easy to measure). Benchmark before and after on a representative game script (e.g., 200 entities running AI + ability logic per tick). Add inline caching if table field access shows up as a bottleneck in profiling. Leave NaN-tagging and compiler rewrites for later if ever.
