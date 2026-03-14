-- $Id: record.lua $
-- Tests for the record type

global fx, i8, u8, i16, u16, i32, u32, i64, bool, record
global <const> *

print "testing records"

-- Field type constants should be defined
assert(type(fx) == "number")
assert(type(i16) == "number")
assert(type(u16) == "number")
assert(type(i8) == "number")
assert(type(u8) == "number")
assert(type(bool) == "number")
assert(type(i32) == "number")
assert(type(u32) == "number")
assert(type(i64) == "number")

-- All field types should be distinct
local types = {fx, i16, u16, i8, u8, bool, i32, u32, i64}
for i = 1, #types do
  for j = i + 1, #types do
    assert(types[i] ~= types[j], "field types must be distinct")
  end
end


-- ====================================================================
-- RecordType creation
-- ====================================================================

local Entity = record {
  x       = fx,
  y       = fx,
  health  = i16,
  team    = u8,
  alive   = bool,
}

assert(type(Entity) == "recordtype")

-- #Entity gives the byte size (4+4+2+1+1 = 12)
assert(#Entity == 12, "#Entity should be 12, got " .. #Entity)

-- tostring should contain "recordtype"
local s = tostring(Entity)
assert(s:find("recordtype"), "tostring should mention recordtype")


-- ====================================================================
-- Record instantiation
-- ====================================================================

local e = Entity()
assert(type(e) == "record")
assert(#e == 12)  -- same byte size as the type

-- Zero-initialized
assert(e.x == 0)
assert(e.y == 0)
assert(e.health == 0)
assert(e.team == 0)
assert(e.alive == false)


-- ====================================================================
-- Field read/write for all types
-- ====================================================================

-- fx field (Q16.16 / lua_Number)
e.x = 100
assert(e.x == 100)
e.y = -50
assert(e.y == -50)

-- i16 field
e.health = 1000
assert(e.health == 1000)
e.health = -500
assert(e.health == -500)

-- u8 field
e.team = 255
assert(e.team == 255)
e.team = 0
assert(e.team == 0)

-- bool field
e.alive = true
assert(e.alive == true)
e.alive = false
assert(e.alive == false)
e.alive = 1  -- truthy value
assert(e.alive == true)
e.alive = nil  -- falsy value
assert(e.alive == false)


-- Test a record with all field types
local AllTypes = record {
  a = fx,
  b = i16,
  c = u16,
  d = i8,
  e = u8,
  f = bool,
  g = i32,
  h = u32,
  k = i64,
}

local at = AllTypes()

-- i32
at.g = 2147483647   -- INT32_MAX
assert(at.g == 2147483647)
at.g = -2147483648  -- INT32_MIN
assert(at.g == -2147483648)

-- u32
at.h = 4294967295   -- UINT32_MAX
assert(at.h == 4294967295)

-- i64 (= lua_Integer)
at.k = 9007199254740992  -- 2^53
assert(at.k == 9007199254740992)
at.k = -9007199254740992
assert(at.k == -9007199254740992)

-- i8
at.d = 127
assert(at.d == 127)
at.d = -128
assert(at.d == -128)

-- u16
at.c = 65535
assert(at.c == 65535)
at.c = 0
assert(at.c == 0)

-- i16
at.b = 32767
assert(at.b == 32767)
at.b = -32768
assert(at.b == -32768)


-- ====================================================================
-- Record byte size with all types
-- ====================================================================

-- AllTypes: fx(4) + i16(2) + u16(2) + i8(1) + u8(1) + bool(1) + i32(4) + u32(4) + i64(8) = 27
assert(#AllTypes == 27, "#AllTypes should be 27, got " .. #AllTypes)


-- ====================================================================
-- RecordArray
-- ====================================================================

local entities = Entity[10]
assert(type(entities) == "recordarray")
assert(#entities == 10)

-- Array indexing (1-based)
entities[1].x = 500
entities[1].health = 100
entities[1].alive = true

assert(entities[1].x == 500)
assert(entities[1].health == 100)
assert(entities[1].alive == true)

-- Different elements are independent
entities[2].x = 999
assert(entities[1].x == 500)
assert(entities[2].x == 999)

-- Iterate and modify
for i = 1, 10 do
  local ent = entities[i]
  ent.x = i * 10
  ent.y = i * 20
  ent.health = i
  ent.team = i
  ent.alive = true
end

for i = 1, 10 do
  local ent = entities[i]
  assert(ent.x == i * 10)
  assert(ent.y == i * 20)
  assert(ent.health == i)
  assert(ent.team == i)
  assert(ent.alive == true)
end


-- ====================================================================
-- Out-of-bounds array access
-- ====================================================================

local ok, err = pcall(function() return entities[0] end)
assert(not ok)
assert(err:find("out of range"), err)

ok, err = pcall(function() return entities[11] end)
assert(not ok)
assert(err:find("out of range"), err)


-- ====================================================================
-- Error cases
-- ====================================================================

-- Invalid field name
ok, err = pcall(function() return e.nonexistent end)
-- Should return nil (falling through to metamethod, which returns nil)
-- Actually, for records without a method defined, this goes through
-- __index on the instance metatable. The metatable has __index = itself,
-- so it looks up "nonexistent" in the metatable. Not found -> nil.
-- This is fine behavior.

-- Wrong type for integer field
ok, err = pcall(function() e.health = "hello" end)
assert(not ok, "should error on string to integer field")

-- Wrong type for fx field
ok, err = pcall(function() e.x = "hello" end)
assert(not ok, "should error on string to fx field")

-- Record constructor errors
ok, err = pcall(function() record {} end)
assert(not ok, "empty record should error")

ok, err = pcall(function() record { x = 999 } end)
assert(not ok, "invalid field type should error")


-- ====================================================================
-- GC survival
-- ====================================================================

do
  local function make_records()
    local R = record { val = i32 }
    local arr = R[100]
    for i = 1, 100 do
      arr[i].val = i * 7
    end
    return arr
  end

  local arr = make_records()
  collectgarbage("collect")
  collectgarbage("collect")

  -- Data should survive GC
  for i = 1, 100 do
    assert(arr[i].val == i * 7, "GC corrupted record data at index " .. i)
  end
end


-- ====================================================================
-- View keeps parent alive
-- ====================================================================

do
  local R = record { val = i32 }
  local arr = R[5]
  arr[3].val = 42
  local view = arr[3]
  arr = nil  -- drop reference to parent
  collectgarbage("collect")
  collectgarbage("collect")
  -- View should still be valid because it holds a GC reference to parent
  assert(view.val == 42, "view should keep parent alive")
end


-- ====================================================================
-- Methods via metatable
-- ====================================================================

local Monster = record {
  hp = i16,
  alive = bool,
}

function Monster:is_dead()
  return not self.alive or self.hp <= 0
end

function Monster:damage(amount)
  self.hp = self.hp - amount
  if self.hp <= 0 then
    self.alive = false
  end
end

local m = Monster()
m.hp = 100
m.alive = true

assert(not m:is_dead())
m:damage(30)
assert(m.hp == 70)
assert(not m:is_dead())
m:damage(80)
assert(m.hp == -10)
assert(m:is_dead())
assert(m.alive == false)


-- ====================================================================
-- tostring
-- ====================================================================

local ts = tostring(e)
assert(type(ts) == "string")
-- Should contain a pointer
assert(ts:find("0x") or ts:find(":"), "tostring should show address")


-- ====================================================================
-- Record arrays tostring
-- ====================================================================

local ats = tostring(entities)
assert(type(ats) == "string")


-- ====================================================================
-- Multiple independent record types
-- ====================================================================

local Vec2 = record { x = fx, y = fx }
local Color = record { r = u8, g = u8, b = u8, a = u8 }

assert(#Vec2 == 8)
assert(#Color == 4)

local v = Vec2()
v.x = 10
v.y = 20
assert(v.x == 10)
assert(v.y == 20)

local c = Color()
c.r = 255
c.g = 128
c.b = 0
c.a = 255
assert(c.r == 255)
assert(c.g == 128)
assert(c.b == 0)
assert(c.a == 255)


-- ====================================================================
-- Userdata + records coexistence (GC integration test)
-- ====================================================================

-- io file handles are userdata with user values; this exercises the
-- GC traversal path that was previously broken by a fallthrough bug.
do
  local R = record { val = i32 }
  local arr = R[50]
  for i = 1, 50 do arr[i].val = i end

  -- Create and use userdata (io file handles) alongside records
  local f = io.tmpfile()
  f:write("hello")
  f:seek("set")
  local content = f:read("a")
  assert(content == "hello", "io userdata broken after record GC")
  f:close()

  -- Force GC while both records and userdata exist
  collectgarbage("collect")
  collectgarbage("collect")

  -- Records should survive
  for i = 1, 50 do
    assert(arr[i].val == i, "record data corrupted alongside userdata")
  end

  -- Create more userdata after GC
  f = io.tmpfile()
  f:write("world")
  f:seek("set")
  assert(f:read("a") == "world")
  f:close()
end


-- ====================================================================
-- Records in tables
-- ====================================================================

do
  local R = record { x = fx, y = fx }
  local t = {}
  for i = 1, 10 do
    local r = R()
    r.x = i
    r.y = i * 2
    t[i] = r
  end
  collectgarbage("collect")
  for i = 1, 10 do
    assert(t[i].x == i)
    assert(t[i].y == i * 2)
  end

  -- Records as table values survive replacement
  t[5] = R()
  t[5].x = 999
  assert(t[5].x == 999)
  assert(t[4].x == 4)  -- neighbors unaffected
end


-- ====================================================================
-- Weak tables with records
-- ====================================================================

do
  local R = record { v = i32 }
  local weak = setmetatable({}, { __mode = "v" })
  do
    local r = R()
    r.v = 42
    weak[1] = r
    assert(weak[1].v == 42)
  end
  -- r is now unreferenced; GC should collect it
  collectgarbage("collect")
  collectgarbage("collect")
  assert(weak[1] == nil, "record should be collected from weak table")
end


-- ====================================================================
-- Equality semantics
-- ====================================================================

do
  local R = record { v = i32 }
  local a = R()
  local b = R()
  a.v = 10
  b.v = 10

  -- Different instances are never equal (identity, not structural)
  assert(a ~= b, "different record instances should not be equal")
  assert(a == a, "record should be equal to itself")

  -- Array views: two accesses to the same index produce different view
  -- objects (not identical), since views are freshly allocated
  local arr = R[5]
  arr[1].v = 7
  local v1 = arr[1]
  local v2 = arr[1]
  assert(v1 ~= v2, "array views are distinct objects")
  assert(v1.v == v2.v, "but they see the same data")
  v1.v = 99
  -- v2 is a separate view pointing at the same memory
  local v3 = arr[1]
  assert(v3.v == 99, "writes through one view visible to others")
end


-- ====================================================================
-- Array view method inheritance
-- ====================================================================

do
  local Particle = record { life = i16, active = bool }

  function Particle:kill()
    self.life = 0
    self.active = false
  end

  local arr = Particle[3]
  arr[1].life = 100
  arr[1].active = true
  arr[1]:kill()
  assert(arr[1].life == 0)
  assert(arr[1].active == false)
end


-- ====================================================================
-- Method isolation between record types
-- ====================================================================

do
  local A = record { x = i32 }
  local B = record { x = i32 }

  function A:get() return self.x + 1 end
  function B:get() return self.x + 2 end

  local a = A()
  local b = B()
  a.x = 10
  b.x = 10

  assert(a:get() == 11, "A:get should return x+1")
  assert(b:get() == 12, "B:get should return x+2")
end


-- ====================================================================
-- Records in coroutines
-- ====================================================================

do
  local R = record { counter = i32 }
  local r = R()
  r.counter = 0

  local co = coroutine.create(function(rec)
    for i = 1, 5 do
      rec.counter = rec.counter + 1
      coroutine.yield(rec.counter)
    end
  end)

  for expected = 1, 5 do
    local ok, val = coroutine.resume(co, r)
    assert(ok)
    assert(val == expected, "coroutine record counter mismatch")
    assert(r.counter == expected, "record visible outside coroutine")
  end
end


-- ====================================================================
-- pcall with record errors
-- ====================================================================

do
  local R = record { hp = i16 }

  function R:checked_heal(amount)
    if amount < 0 then error("negative heal") end
    self.hp = self.hp + amount
  end

  local r = R()
  r.hp = 50
  local ok, err = pcall(R.checked_heal, r, -10)
  assert(not ok)
  assert(err:find("negative heal"))
  assert(r.hp == 50, "hp should be unchanged after error")

  -- Successful call
  r:checked_heal(20)
  assert(r.hp == 70)
end


-- ====================================================================
-- GC stress: many small allocations
-- ====================================================================

do
  local R = record { a = u8, b = u8 }
  local keep = {}
  for i = 1, 1000 do
    local r = R()
    r.a = i % 256
    r.b = (i * 7) % 256
    if i % 3 == 0 then keep[#keep + 1] = r end  -- keep every 3rd
    if i % 100 == 0 then collectgarbage("collect") end
  end
  collectgarbage("collect")
  collectgarbage("collect")
  -- Verify survivors
  for i, r in ipairs(keep) do
    local orig = i * 3
    assert(r.a == orig % 256)
    assert(r.b == (orig * 7) % 256)
  end
end


-- ====================================================================
-- Edge cases: array size 1, single-field record
-- ====================================================================

do
  local Single = record { flag = bool }
  assert(#Single == 1)

  local s = Single()
  assert(s.flag == false)
  s.flag = true
  assert(s.flag == true)

  local arr = Single[1]
  assert(#arr == 1)
  arr[1].flag = true
  assert(arr[1].flag == true)
end


-- ====================================================================
-- Negative and zero array sizes
-- ====================================================================

do
  local R = record { x = i32 }
  local ok, err = pcall(function() return R[0] end)
  assert(not ok, "array size 0 should error")

  ok, err = pcall(function() return R[-1] end)
  assert(not ok, "negative array size should error")
end


-- ====================================================================
-- type() returns correct strings in all contexts
-- ====================================================================

do
  local R = record { x = fx }
  local r = R()
  local arr = R[5]

  -- In concatenation
  assert("type is " .. type(R) == "type is recordtype")
  assert("type is " .. type(r) == "type is record")
  assert("type is " .. type(arr) == "type is recordarray")

  -- In table keys (type strings are interned)
  local counts = {}
  local things = { R, r, arr }
  for _, v in ipairs(things) do
    local t = type(v)
    counts[t] = (counts[t] or 0) + 1
  end
  assert(counts["recordtype"] == 1)
  assert(counts["record"] == 1)
  assert(counts["recordarray"] == 1)
end


print "OK"
