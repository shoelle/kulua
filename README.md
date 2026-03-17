# Kulua

Deterministic Lua for games. Fork of [Lua 5.5](https://www.lua.org/) (PUC-Rio).

Kulua takes standard Lua and adds determinism guarantees for networked and replayed game simulations. IEEE 754 doubles, 64-bit integers, closures, coroutines, the GC -- everything works like standard Lua, but math results are reproducible across platforms.

## Why deterministic?

Networked and replayed game simulations need identical results on every machine. Standard IEEE 754 doubles are well-defined in theory, but in practice compilers silently fuse multiply-add into FMA instructions, producing different rounding on different CPUs. A single bit of divergence compounds into a desync within seconds.

Kulua prevents this with `-ffp-contract=off`, which disables FMA contraction so that `a * b + c` always rounds the multiply before the add. This is the same approach used by [Box2D](https://box2d.org/) for cross-platform determinism. Combined with deterministic table iteration and object hashing, Kulua provides the reproducibility games need while keeping the full range and precision of standard doubles.

## Quick start

```bash
zig build              # build interpreter
zig build run          # launch REPL

echo 'print(math.sin(math.pi / 6))' > test.lua
zig build run -- test.lua
```

No dependencies beyond [Zig](https://ziglang.org/). No make, cmake, or readline.

## What's different from standard Lua

| | Standard Lua | Kulua |
|---|---|---|
| `lua_Integer` | `long long` (64-bit) | `long long` (64-bit) |
| `lua_Number` | `double` (64-bit IEEE 754) | `double` (64-bit IEEE 754) |
| FP contraction | compiler default (FMA allowed) | `-ffp-contract=off` (no FMA) |
| `pairs()` order | undefined | insertion order (deterministic) |
| Table hashing | pointer-based | object ID-based (deterministic) |
| Record types | N/A | first-class C-struct-like types |

## Determinism features

### `-ffp-contract=off`

FMA (fused multiply-add) instructions combine `a * b + c` into a single operation with only one rounding step instead of two. This produces more accurate results but different ones depending on whether the CPU and compiler choose to fuse. Kulua compiles all C source with `-ffp-contract=off` to ensure identical floating-point results across x86, ARM, and WASM.

### Deterministic `pairs()`

Standard Lua makes no guarantee about the order `pairs()` visits table keys. Kulua maintains an insertion-order linked list in every table, so `pairs()` always iterates in the order keys were first inserted. This eliminates a common source of non-determinism in game logic.

```lua
local t = {}
t.z = 1
t.a = 2
t.m = 3

for k, v in pairs(t) do
  print(k, v)
end
-- Always prints: z, a, m (insertion order)
```

### Object ID hashing

Standard Lua hashes GC objects (tables, strings, functions) by their memory address. Since allocators return different addresses across runs, hash table layouts differ, which affects iteration order for non-string keys. Kulua assigns each GC object a sequential ID at creation and hashes by that ID, making hash collisions and bucket placement deterministic.

### Sandbox libs

`os` and `io` functions that would break determinism (wall-clock time, file I/O) can be restricted for replay contexts. The standard library surface is otherwise unchanged.

## Record types

Kulua adds first-class C-struct-like types for sharing gameplay data between Lua and C engine code (physics, networking) via direct memory access -- no serialization, no getter/setter bindings.

Records are flat, packed byte buffers with named typed fields. Both Lua and C can read/write the same memory.

### Defining a record type

Pass a table of `name = type` pairs to the `record` constructor. Fields are laid out in declaration order with no alignment padding.

```lua
local Entity = record {
  x      = f32,    -- 32-bit float (4 bytes)
  y      = f32,
  health = i16,    -- signed 16-bit int (2 bytes)
  team   = u8,     -- unsigned 8-bit int (1 byte)
  alive  = bool,   -- boolean (1 byte)
}
-- Entity is 12 bytes packed: 4+4+2+1+1
```

Nine field types are available:

| Type | Bytes | Lua value | Range |
|------|-------|-----------|-------|
| `f32` | 4 | number | IEEE 754 single-precision |
| `i8` | 1 | integer | -128 to 127 |
| `u8` | 1 | integer | 0 to 255 |
| `i16` | 2 | integer | -32768 to 32767 |
| `u16` | 2 | integer | 0 to 65535 |
| `i32` | 4 | integer | -2^31 to 2^31-1 |
| `u32` | 4 | integer | 0 to 2^32-1 |
| `i64` | 8 | integer (`lua_Integer`) | full 64-bit range |
| `bool` | 1 | boolean | `true`/`false` |

### Creating instances

Call the record type like a function. Instances are zero-initialized.

```lua
local e = Entity()
e.x = 100.5
e.health = 50
e.alive = true

print(e.x)       -- 100.5
print(e.alive)   -- true
```

### Record arrays

Index the record type with a count to create a contiguous array. Elements are 1-based.

```lua
local enemies = Entity[100]
print(#enemies)  -- 100

enemies[1].x = 500.0
enemies[1].alive = true

for i = 1, #enemies do
  enemies[i].health = 100
end
```

Array access returns a lightweight view into the array's memory. The view keeps the parent array alive for GC purposes.

### Methods

Define methods on the record type. They're available on all instances via the metatable.

```lua
function Entity:is_alive()
  return self.alive and self.health > 0
end

function Entity:damage(amount)
  self.health = self.health - amount
  if self.health <= 0 then
    self.alive = false
  end
end

local e = Entity()
e.health = 100
e.alive = true
e:damage(30)
```

### Type checking

Each record variant has a distinct `type()` string:

```lua
type(Entity)       -- "recordtype"
type(Entity())     -- "record"
type(Entity[10])   -- "recordarray"
```

The `#` operator returns the byte size for types and instances, and the element count for arrays:

```lua
#Entity            -- 12 (byte size)
#Entity()          -- 12 (same)
#Entity[10]        -- 10 (element count)
```

### C API

Access record data from C for zero-copy interop:

```c
// Get raw pointer to a record's data buffer
void *data = lua_torecorddata(L, idx, &len);

// Get pointer to element i (0-indexed) of a record array
void *elem = lua_torecordarraydata(L, idx, i, &stride);

// Type checking
lua_isrecord(L, idx);
lua_isrecordarray(L, idx);
lua_isrecordtype(L, idx);
```

Fields are packed with no alignment padding, so C structs must use `#pragma pack(1)` or equivalent to match the layout.

### Error handling

- Assigning the wrong type to a field (e.g., a string to an integer field) raises an error
- Array access out of bounds (index < 1 or > count) raises an error
- An empty `record {}` raises an error
- Invalid field type constants raise an error

## Build options

```bash
zig build                        # debug build -> zig-out/bin/lua
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

All 34 upstream Lua test modules pass, plus `testes/record.lua` (record type tests). 35 modules total.

## Source layout

All source lives in the repo root.

| Area | Key files |
|------|-----------|
| Config | `luaconf.h`, `lua.h`, `llimits.h` |
| Records | `kulua_record.c`, `kulua_record.h` (record types, arrays, C API) |
| VM | `lvm.c`, `ldo.c`, `lopcodes.h` |
| Compiler | `lparser.c`, `lcode.c`, `llex.c` |
| Objects/tables | `lobject.c`, `ltable.c`, `lstring.c` |
| Std libs | `lmathlib.c`, `lstrlib.c`, `ltablib.c`, `lbaselib.c`, `liolib.c`, `loslib.c` |
| Interpreter | `lua.c` |

## References

- [Factorio Lua](https://lua-api.factorio.com/latest/auxiliary/libraries.html) -- deterministic multiplayer Lua reference
- [Box2D](https://box2d.org/) -- uses `-ffp-contract=off` for cross-platform determinism
- [Lua 5.5](https://www.lua.org/) -- upstream

## License

Same as Lua -- MIT license. See `lua.h` for the full notice.

## Caveat Machinam

This was written with Claude Code.
