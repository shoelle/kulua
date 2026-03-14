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


print "OK"
