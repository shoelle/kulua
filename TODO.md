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

- [ ] `LUA_FLOAT_TYPE`: new option `LUA_FLOAT_FIXED` that sets `lua_Number` = `int32_t`
- [ ] `l_floatatt`: fixed-point equivalents for `HUGE_VAL`, `MAX`, `MIN`, epsilon
- [ ] Arithmetic macros (`luai_numadd`, `luai_numsub`, `luai_nummul`, `luai_numdiv`, `luai_numpow`, `luai_numidiv`, `luai_nummod`): add/sub are plain integer ops; mul/div use 64-bit intermediates
- [ ] `luai_numisnan`: always returns 0
- [ ] `lua_str2number`: custom parser, decimal string to Q16.16
- [ ] `lua_getlnumfv2`: number-to-string formatting for fixed-point display

### VM changes (lvm.c, lobject.c)

- [ ] Int-to-float coercion: `value << 16`
- [ ] Float-to-int coercion: `value >> 16`
- [ ] Float literal parsing in compiler: `3.14` becomes the Q16.16 representation
- [ ] Verify `/` (float division), `//` (integer division), `%` (modulo) all behave correctly
- [ ] Verify comparison operators work (fixed-point values compare correctly as plain int32 since the encoding is monotonic)

### Math library (lmathlib.c)

- [ ] `math.sin`, `math.cos`: LUT-based (256+ entries per quadrant, linear interpolation)
- [ ] `math.sqrt`: Newton-Raphson in fixed-point (3-4 iterations for Q16.16 precision)
- [ ] `math.atan`: CORDIC or LUT
- [ ] `math.abs`, `math.floor`, `math.ceil`: bitwise ops on the fixed-point representation
- [ ] `math.min`, `math.max`: plain integer comparison (monotonic encoding)
- [ ] `math.pi`: Q16.16 representation of pi (205887)
- [ ] Remove or stub: `math.exp`, `math.log`, `math.pow`
- [ ] `math.random`: caller-provided PRNG hook (not `rand()`)
- [ ] `math.randomseed`: no-op or caller-provided

### Determinism patches

- [ ] `pairs()` iterates in insertion order (patch `ltable.c` `luaH_next`)
- [ ] Hash function for number keys: use the raw int32 value, not `frexp`-based float hashing
- [ ] `tostring()` on tables: deterministic output (not pointer-based)
- [ ] Sandbox mode: ability to strip `os`, `io`, `coroutine`, `loadfile`, `dofile`, `debug` from a Lua state
- [ ] GC: support explicit stepping at controlled points, no finalizers (`__gc`) in sandbox mode
- [ ] `luaL_makeseed`: deterministic (not time-based)

### Performance

- [ ] Audit PUC Lua VM hot paths for game-friendly optimizations (see Performance section below)
- [ ] Benchmark: Kulua vs PUC Lua 5.5 on representative game workloads

### Build and test

- [ ] Standalone repo with CMake or plain Makefile, produces a C static library
- [ ] Compile and pass Lua's own test suite (with expected failures for removed/changed modules)
- [ ] Fixed-point math tests: verify mul, div, trig, sqrt against known values
- [ ] Determinism test: two Lua states, same script, same inputs, `memcmp` output
- [ ] Cross-platform determinism: native build vs WASM (Emscripten) produce identical results

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
