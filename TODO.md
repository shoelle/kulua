# Kulua

Deterministic Lua for games. Fork of Lua 5.5.

## Core Idea

Lua 5.5 with determinism features for networked and replayed game simulations. Standard IEEE 754 doubles with compiler and runtime patches to guarantee cross-platform reproducibility.

- `lua_Integer` stays as `long long` (64-bit). Array indices, loop counters, bitwise ops all work unchanged.
- `lua_Number` stays as `double` (IEEE 754). Determinism achieved through `-ffp-contract=off`, consistent rounding, and avoiding platform-dependent math.
- First-class record types for zero-copy Lua/C data sharing.

## References

- [Factorio's Lua](https://lua-api.factorio.com/latest/auxiliary/libraries.html): Deterministic multiplayer Lua 5.2 fork. Deterministic `pairs()`, seeded RNG, `frexp`-based hashing, sandbox stripping. The checklist to replicate.
- [Lua 5.5 source](https://www.lua.org/ftp/): Base to fork from.

## Why not other runtimes

- **LuaJIT**: No WASM support (assembly interpreter, no C fallback). NaN-boxing makes number type non-configurable.
- **Luau**: `double` hardcoded with no config macro. ~100K lines of C++. Number surgery is 5x the work of PUC Lua for marginal interpreter speed gains (no codegen on WASM anyway).

---

## Milestone 1: Deterministic Runtime

Patch the runtime for cross-platform determinism.

### Determinism patches

- [x] `pairs()` iterates in insertion order (insertion-order linked list in hash nodes, survives rehash)
- [x] `tostring()` on tables/functions/etc: deterministic output (monotonic counter-based `kulua_objid`, not pointer-based)
- [x] Table hashing for GC objects as keys: uses `kulua_objid` instead of pointer address for deterministic bucket placement
- [x] Sandbox mode: `kulua_opensandboxlibs()` C API loads safe subset (base, math, string, table, utf8, coroutine), strips `loadfile`/`dofile`/`load`
- [x] GC: `__gc` finalizers disabled in sandbox mode (`kulua_no_gc_metamethod` flag)
- [x] `luaL_makeseed`: deterministic (returns 0)
- [ ] Floating-point determinism: compile with `-ffp-contract=off` and audit for platform-dependent math (e.g., `pow`, `exp`, `log` variance across libm implementations)
- [ ] Deterministic `math.random`: ensure identical sequences across platforms with same seed
- [ ] Audit `string.format` for platform-dependent float formatting

### Build and test

- [x] Zig build system: `build.zig` compiles, produces static lib + interpreter
- [x] Compile and pass basic Lua functionality (arithmetic, tables, strings, closures, coroutines)
- [x] Determinism test: two lua_States, same scripts, `memcmp` output
- [x] Cross-platform determinism: golden file verified identical between native x86_64 and WASM (Emscripten) builds

### Performance (future work)

- [ ] Computed goto dispatch: Lua 5.5 already ships `ljumptab.h` with computed goto infrastructure — investigate whether it's already active with GCC/Clang
- [ ] Audit PUC Lua VM hot paths for game-friendly optimizations
- [ ] Benchmark: Kulua vs PUC Lua 5.5 on representative game workloads

---

## Performance

PUC Lua's interpreter is straightforward and leaves performance on the table that LuaJIT and Luau picked up. Some of their wins can be ported back to PUC Lua without the complexity of a JIT or full rewrite.

### Feasible wins (interpreter-level, pure C)

**Computed goto dispatch.** PUC Lua uses a `switch` statement for opcode dispatch. LuaJIT and Luau both use computed goto (`&&label` / `goto *dispatch[op]`). This eliminates the branch predictor bottleneck of a central switch. GCC/Clang support this via labels-as-values. Emscripten (Clang) supports it too. MSVC does not, so keep the switch as a fallback. Estimated gain: 10-20% on opcode-heavy workloads. **Note:** Lua 5.5 already ships `ljumptab.h` with computed goto infrastructure — this may already be partially done upstream. Investigate before implementing.

**Inline caching for table field access.** Luau caches the slot offset for `t.field` accesses. On repeat access to the same field on tables with the same "shape", the lookup skips the hash entirely. LuaJIT does something similar. This is significant for game code that does `entity.x`, `entity.y`, `entity.health` thousands of times per frame. Implementation: add a small cache (key + slot offset) to `GETTABLE`/`SETTABLE` instructions. Moderate complexity.

**NaN-tagging (or tagged-pointer) value representation.** PUC Lua stores values as a struct with a type tag + union (16 bytes on 64-bit). LuaJIT NaN-boxes everything into 8 bytes. Luau uses a tagged union but packs it tighter. Halving TValue size improves cache utilization across the board. Tagged pointers might work but the complexity is high. Worth investigating but not a first-pass change.

**String interning with better hashing.** PUC Lua hashes strings on creation for interning. The hash function is adequate but not SIMD-friendly. A faster hash (e.g., wyhash) speeds up string-heavy workloads. Low complexity, easy win.

**Precomputed method lookup.** Game scripts do `entity:method()` constantly. Each call resolves the method through `__index` metamethod lookup. Caching the resolved function pointer (invalidated when the metatable changes) avoids repeated hash lookups. Luau does this. Moderate complexity.

### Not worth porting

**JIT compilation.** LuaJIT's tracing JIT and Luau's method JIT both produce native code. Neither works on WASM. The complexity is enormous. Not worth it for a ~25K line C codebase.

**Register allocation optimizations.** Luau's compiler does smarter register allocation than PUC Lua's single-pass compiler. Meaningful but requires a multi-pass compiler rewrite. Too invasive for the payoff.

### Approach

Start with computed goto (low risk, easy to measure). Benchmark before and after on a representative game script (e.g., 200 entities running AI + ability logic per tick). Add inline caching if table field access shows up as a bottleneck in profiling. Leave NaN-tagging and compiler rewrites for later if ever.
