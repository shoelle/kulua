-- $Id: testes/fixed.lua $
-- See Copyright Notice in file lua.h
-- Q16.16 fixed-point specific tests for Kulua

global <const> *

print "testing Q16.16 fixed-point arithmetic"


-- Q16.16 range: ±32767.99998 (integer part 16 bits, fraction 16 bits)
local FIXED_MAX = 32767
local FIXED_FRAC_BITS = 16
local FIXED_ONE_UNIT = 1 / 2^FIXED_FRAC_BITS  -- smallest representable step


-- basic type system
assert(math.type(1) == "integer")
assert(math.type(1.0) == "float")
assert(math.type(0.5) == "float")


-- integer-to-float coercion (luai_int2num)
assert(1 + 0.0 == 1.0)
assert(100 + 0.0 == 100.0)
assert(-100 + 0.0 == -100.0)
assert(0 + 0.0 == 0.0)

-- saturation: integers beyond Q16.16 range saturate
assert(100000 + 0.0 == math.huge)  -- saturates to KULUA_HUGE_VAL
assert(-100000 + 0.0 < 0)  -- saturates to KULUA_NHUGE_VAL
assert(-100000 + 0.0 == -math.huge)  -- symmetric sentinels


-- basic arithmetic
assert(1.5 + 2.5 == 4.0)
assert(10.0 - 3.0 == 7.0)
assert(2.0 * 3.0 == 6.0)
assert(10.0 / 4.0 == 2.5)
assert(-1.5 + 2.5 == 1.0)
assert(0.0 - 0.0 == 0.0)


-- multiplication uses 64-bit intermediate
assert(100.0 * 100.0 == 10000.0)
assert(200.0 * 100.0 == 20000.0)


-- division
assert(1.0 / 3.0 ~= 0)  -- not exactly representable but nonzero
assert(7.0 / 2.0 == 3.5)
do
  local third = 1.0 / 3.0
  assert(third > 0.333 and third < 0.334)
end


-- division by zero returns max/min (no inf in Q16.16)
do
  local posmax = 1.0 / 0.0
  local negmax = -1.0 / 0.0
  assert(posmax > 0)
  assert(negmax < 0)
  assert(posmax == math.huge)
end


-- no NaN in Q16.16
assert(not (0.0 ~= 0.0))  -- NaN check pattern returns false
assert(0.0 == 0.0)


-- math.huge is finite in Q16.16
assert(math.huge > 0)
assert(math.huge < 100000)  -- used as the _fixedpoint detection flag
assert(-math.huge < 0)


-- unary minus
assert(-(3.5) == -3.5)
assert(-(0.0) == 0.0)
assert(-(-1.0) == 1.0)


-- floor and ceil
assert(math.floor(3.7) == 3.0)
assert(math.floor(-3.7) == -4.0)
assert(math.floor(3.0) == 3.0)
assert(math.ceil(3.2) == 4.0)
assert(math.ceil(-3.2) == -3.0)
assert(math.ceil(3.0) == 3.0)


-- modf (truncation toward zero)
do
  local i, f = math.modf(3.75)
  assert(i == 3.0 and f == 0.75)
  i, f = math.modf(-3.75)
  assert(i == -3.0 and f == -0.75)
  i, f = math.modf(0.5)
  assert(i == 0.0 and f == 0.5)
end


-- fmod (truncation toward zero)
assert(math.fmod(7.0, 3.0) == 1.0)
assert(math.fmod(-7.0, 3.0) == -1.0)
assert(math.fmod(7.0, -3.0) == 1.0)
assert(math.fmod(-7.0, -3.0) == -1.0)


-- abs
assert(math.abs(-5.5) == 5.5)
assert(math.abs(5.5) == 5.5)
assert(math.abs(0.0) == 0.0)


-- sqrt
do
  local s = math.sqrt(4.0)
  assert(s == 2.0)
  s = math.sqrt(9.0)
  assert(s == 3.0)
  s = math.sqrt(2.0)
  assert(s > 1.414 and s < 1.415)
end


-- trig functions (LUT-based, approximate)
do
  local function near(a, b, tol)
    tol = tol or 0.01
    return math.abs(a - b) < tol
  end

  assert(near(math.sin(0.0), 0.0, 0.001))
  assert(near(math.cos(0.0), 1.0, 0.001))
  assert(near(math.sin(math.pi / 2), 1.0))
  assert(near(math.cos(math.pi / 2), 0.0))
  assert(near(math.sin(math.pi), 0.0))
  assert(near(math.cos(math.pi), -1.0))

  -- sin^2 + cos^2 ≈ 1
  for _, angle in ipairs{0.0, 0.5, 1.0, 2.0, 3.0, -1.5} do
    local s, c = math.sin(angle), math.cos(angle)
    assert(near(s*s + c*c, 1.0, 0.05))
  end

  -- atan
  assert(near(math.atan(1.0), math.pi / 4))
  assert(near(math.atan(0.0), 0.0, 0.001))
  assert(near(math.atan(1.0, 0.0), math.pi / 2))
end


-- exp and log (rough approximations)
do
  local function near(a, b, tol)
    return math.abs(a - b) < (tol or 0.1)
  end

  assert(near(math.exp(0.0), 1.0, 0.01))
  assert(near(math.exp(1.0), 2.718, 0.1))
  assert(near(math.log(1.0), 0.0, 0.01))

  -- log base conversion
  assert(near(math.log(8, 2), 3.0, 0.1))
  assert(near(math.log(100, 10), 2.0, 0.1))
end


-- integer table keys work correctly (Kulua's key advantage over z8lua)
do
  local t = {}
  t[1] = "a"
  t[2] = "b"
  t[3] = "c"
  assert(t[1] == "a")
  assert(t[2] == "b")
  assert(t[3] == "c")
  assert(#t == 3)

  -- integer and float keys are distinct when they should be
  t[1] = "int"
  t[1.5] = "float"
  assert(t[1] == "int")
  assert(t[1.5] == "float")

  -- integer key via float (1.0 coerces to integer key 1)
  t[1.0] = "one-point-zero"
  assert(t[1] == "one-point-zero")
end


-- string.format with floats
do
  assert(string.format("%.1f", 3.14) == "3.1")
  assert(string.format("%.2f", 3.14) == "3.14")
  assert(string.format("%g", 1.0) == "1")
  assert(string.format("%g", 0.5) == "0.5")
end


-- parsing edge cases
assert(tonumber("0") == 0)
assert(tonumber("1.0") == 1.0)
assert(tonumber("-1.0") == -1.0)
assert(tonumber("0.5") == 0.5)
assert(tonumber("3.14") == 3.14)
assert(tonumber("1e2") == 100.0)
assert(tonumber(".5e1") == 5.0)
assert(tonumber("2.E-1") == 0.2)
assert(tonumber("0e0") == 0.0)
-- reject malformed exponents
assert(tonumber("1e") == nil)
assert(tonumber("1e+") == nil)
assert(tonumber("1.0e-") == nil)

-- hex floats
assert(tonumber("0x1p0") == 1.0)
assert(tonumber("0x1p4") == 16.0)
assert(tonumber("0x1.8p0") == 1.5)
-- reject malformed hex exponents
assert(tonumber("0x1p") == nil)
assert(tonumber("0x1p+") == nil)


-- saturating arithmetic (add/sub/mul clamp on overflow instead of wrapping)
do
  -- very large positive + positive saturates to max
  local big = 32000.0
  local sum = big + big
  -- result saturates (64000 doesn't fit in Q16.16)
  assert(sum > 0)  -- saturates to positive max, not negative wrap
  assert(sum == math.huge)

  -- very large negative + negative saturates to min
  local negsum = (-big) + (-big)
  assert(negsum < 0)

  -- subtraction saturation
  assert(big - (-big) == math.huge)
  assert((-big) - big < 0)

  -- multiplication saturation
  assert(200.0 * 200.0 == math.huge)  -- 40000 > Q16.16 max
  assert(200.0 * (-200.0) < 0)        -- negative saturation
end


-- pi constant
do
  assert(math.pi > 3.14 and math.pi < 3.15)
end


-- os.clock returns Q16.16 float
do
  local c = os.clock()
  assert(math.type(c) == "float")
  assert(c >= 0.0)
end


-- ===== Determinism patches =====

print "testing deterministic pairs() (insertion-order iteration)"

-- insertion order is preserved
do
  local t = {}
  local keys = {"z", "a", "m", "b", "x", "c", "w"}
  for _, k in ipairs(keys) do t[k] = true end
  local result = {}
  for k in pairs(t) do result[#result+1] = k end
  for i, k in ipairs(keys) do
    assert(result[i] == k, "order mismatch at " .. i)
  end
end

-- two tables with same insertion order iterate identically
do
  local t1, t2 = {}, {}
  local keys = {"alpha", "beta", "gamma", "delta", "epsilon"}
  for _, k in ipairs(keys) do t1[k] = true end
  for _, k in ipairs(keys) do t2[k] = true end
  local r1, r2 = {}, {}
  for k in pairs(t1) do r1[#r1+1] = k end
  for k in pairs(t2) do r2[#r2+1] = k end
  assert(#r1 == #r2)
  for i = 1, #r1 do assert(r1[i] == r2[i]) end
end

-- deleted keys are skipped, order of remaining keys preserved
do
  local t = {}
  for i = 1, 10 do t["k"..i] = i end
  t["k3"] = nil
  t["k7"] = nil
  local result = {}
  for k in pairs(t) do result[#result+1] = k end
  -- should be k1,k2,k4,k5,k6,k8,k9,k10 in insertion order
  assert(#result == 8)
  local expected = {"k1","k2","k4","k5","k6","k8","k9","k10"}
  for i, k in ipairs(expected) do
    assert(result[i] == k, "delete-skip mismatch at " .. i ..
           ": expected " .. k .. ", got " .. tostring(result[i]))
  end
end

-- insert-delete-insert cycle preserves order correctly
do
  local t = {}
  for i = 1, 20 do t["k"..i] = i end
  for i = 1, 10 do t["k"..i] = nil end
  for i = 21, 30 do t["k"..i] = i end
  local count = 0
  for k, v in pairs(t) do count = count + 1 end
  assert(count == 20, "insert-delete-insert: expected 20, got " .. count)
end

-- large table stress test
do
  local t = {}
  for i = 1, 100 do t["key"..i] = i end
  for i = 1, 50 do t["key"..i] = nil end
  for i = 101, 150 do t["key"..i] = i end
  local count = 0
  for _ in pairs(t) do count = count + 1 end
  assert(count == 100, "large stress: expected 100, got " .. count)
end

-- re-setting an existing key keeps original position
do
  local t = {}
  t.a = 1; t.b = 2; t.c = 3
  t.b = 99  -- update value, not a new key
  local result = {}
  for k in pairs(t) do result[#result+1] = k end
  assert(result[1] == "a" and result[2] == "b" and result[3] == "c")
end

-- mixed array + hash: array part iterates first, then hash in insertion order
do
  local t = {}
  t[1] = "one"
  t["x"] = "ex"
  t[2] = "two"
  t["y"] = "why"
  local result = {}
  for k in pairs(t) do result[#result+1] = tostring(k) end
  -- array keys 1,2 first, then hash keys x,y in insertion order
  assert(result[1] == "1" and result[2] == "2")
  assert(result[3] == "x" and result[4] == "y")
end


print "testing deterministic tostring()"

-- tostring(table) uses counter-based IDs, not pointers
do
  local t1 = {}
  local t2 = {}
  local s1 = tostring(t1)
  local s2 = tostring(t2)
  -- format: "table: <number>"
  assert(string.find(s1, "^table: %d+$"), "unexpected format: " .. s1)
  assert(string.find(s2, "^table: %d+$"), "unexpected format: " .. s2)
  -- IDs are sequential
  local id1 = tonumber(string.match(s1, "%d+"))
  local id2 = tonumber(string.match(s2, "%d+"))
  assert(id2 > id1, "IDs should be sequential")
end

-- tostring is deterministic: same program produces same IDs each run
-- (this is implicitly tested by the test suite running identically)


print "OK"
