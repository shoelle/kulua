/*
** kulua_record.c
** Record type support for Kulua — flat, packed, C-struct-like data types
** See Copyright Notice in lua.h
*/

#define LUA_CORE

#include "lprefix.h"

#include <string.h>

#include "lua.h"
#include "lobject.h"
#include "lstate.h"
#include "lgc.h"
#include "lmem.h"
#include "lstring.h"
#include "ltable.h"
#include "ltm.h"
#include "ldebug.h"
#include "ldo.h"
#include "lapi.h"
#include "lauxlib.h"

#include "kulua_record.h"


/*
** Get TValue at stack index (reproduces lapi.c's index2value for
** positive indices and pseudo-indices we don't use).
*/
static const TValue *rec_index2value (lua_State *L, int idx) {
  if (idx > 0) {
    StkId o = L->ci->func.p + idx;
    api_check(L, idx <= L->ci->top.p - (L->ci->func.p + 1), "unacceptable index");
    return s2v(o);
  }
  else {  /* negative index */
    api_check(L, -idx <= L->top.p - (L->ci->func.p + 1), "invalid index");
    return s2v(L->top.p + idx);
  }
}


/* ====================================================================
** Field utilities
** ==================================================================== */

int kulua_fieldsize (int type) {
  switch (type) {
    case KULUA_FIELD_FX:   return 4;
    case KULUA_FIELD_I32:  return 4;
    case KULUA_FIELD_U32:  return 4;
    case KULUA_FIELD_I16:  return 2;
    case KULUA_FIELD_U16:  return 2;
    case KULUA_FIELD_I8:   return 1;
    case KULUA_FIELD_U8:   return 1;
    case KULUA_FIELD_BOOL: return 1;
    case KULUA_FIELD_I64:  return 8;
    default: return 0;
  }
}


void kulua_record_readfield (TValue *res, const uint8_t *data,
                             const RecordField *f) {
  const uint8_t *p = data + f->offset;
  switch (f->type) {
    case KULUA_FIELD_FX: {
      int32_t v;
      memcpy(&v, p, 4);
      setfltvalue(res, (lua_Number)v);
      break;
    }
    case KULUA_FIELD_I32: {
      int32_t v;
      memcpy(&v, p, 4);
      setivalue(res, (lua_Integer)v);
      break;
    }
    case KULUA_FIELD_U32: {
      uint32_t v;
      memcpy(&v, p, 4);
      setivalue(res, (lua_Integer)v);
      break;
    }
    case KULUA_FIELD_I16: {
      int16_t v;
      memcpy(&v, p, 2);
      setivalue(res, (lua_Integer)v);
      break;
    }
    case KULUA_FIELD_U16: {
      uint16_t v;
      memcpy(&v, p, 2);
      setivalue(res, (lua_Integer)v);
      break;
    }
    case KULUA_FIELD_I8: {
      int8_t v = (int8_t)*p;
      setivalue(res, (lua_Integer)v);
      break;
    }
    case KULUA_FIELD_U8: {
      uint8_t v = *p;
      setivalue(res, (lua_Integer)v);
      break;
    }
    case KULUA_FIELD_BOOL: {
      if (*p)
        setbtvalue(res);
      else
        setbfvalue(res);
      break;
    }
    case KULUA_FIELD_I64: {
      int64_t v;
      memcpy(&v, p, 8);
      setivalue(res, (lua_Integer)v);
      break;
    }
    default:
      setnilvalue(res);
      break;
  }
}


int kulua_record_writefield (lua_State *L, const TValue *val,
                             uint8_t *data, const RecordField *f) {
  uint8_t *p = data + f->offset;
  switch (f->type) {
    case KULUA_FIELD_FX: {
      int32_t v;
      if (ttisfloat(val))
        v = (int32_t)fltvalue(val);
      else if (ttisinteger(val))
        v = (int32_t)luai_int2num(ivalue(val));
      else
        luaG_runerror(L, "fx field requires number");
      memcpy(p, &v, 4);
      break;
    }
    case KULUA_FIELD_I32: {
      if (!ttisinteger(val))
        luaG_runerror(L, "i32 field requires integer");
      int32_t v = (int32_t)ivalue(val);
      memcpy(p, &v, 4);
      break;
    }
    case KULUA_FIELD_U32: {
      if (!ttisinteger(val))
        luaG_runerror(L, "u32 field requires integer");
      uint32_t v = (uint32_t)ivalue(val);
      memcpy(p, &v, 4);
      break;
    }
    case KULUA_FIELD_I16: {
      if (!ttisinteger(val))
        luaG_runerror(L, "i16 field requires integer");
      int16_t v = (int16_t)ivalue(val);
      memcpy(p, &v, 2);
      break;
    }
    case KULUA_FIELD_U16: {
      if (!ttisinteger(val))
        luaG_runerror(L, "u16 field requires integer");
      uint16_t v = (uint16_t)ivalue(val);
      memcpy(p, &v, 2);
      break;
    }
    case KULUA_FIELD_I8: {
      if (!ttisinteger(val))
        luaG_runerror(L, "i8 field requires integer");
      *p = (uint8_t)(int8_t)ivalue(val);
      break;
    }
    case KULUA_FIELD_U8: {
      if (!ttisinteger(val))
        luaG_runerror(L, "u8 field requires integer");
      *p = (uint8_t)ivalue(val);
      break;
    }
    case KULUA_FIELD_BOOL: {
      *p = !l_isfalse(val) ? 1 : 0;
      break;
    }
    case KULUA_FIELD_I64: {
      if (!ttisinteger(val))
        luaG_runerror(L, "i64 field requires integer");
      int64_t v = (int64_t)ivalue(val);
      memcpy(p, &v, 8);
      break;
    }
    default:
      return 0;
  }
  return 1;
}


int kulua_recordtype_findfield (RecordType *rt, TString *key) {
  int i;
  for (i = 0; i < rt->nfields; i++) {
    if (rt->fields[i].name == key)  /* interned string pointer comparison */
      return i;
  }
  return -1;
}


/* ====================================================================
** Allocation
** ==================================================================== */

RecordType *kulua_recordtype_new (lua_State *L, int nfields) {
  GCObject *o;
  RecordType *rt;
  size_t sz = sizerecordtype(nfields);
  o = luaC_newobj(L, LUA_VRECORDTYPE, sz);
  rt = gco2rtype(o);
  rt->nfields = cast(uint16_t, nfields);
  rt->record_size = 0;
  rt->name = NULL;
  rt->metatable = NULL;
  rt->gclist = NULL;
  return rt;
}


Record *kulua_record_new (lua_State *L, RecordType *rt) {
  GCObject *o;
  Record *rec;
  size_t sz = sizerecordstandalone(rt->record_size);
  o = luaC_newobj(L, LUA_VRECORD, sz);
  rec = gco2rec(o);
  rec->rtype = rt;
  rec->parent = NULL;
  rec->data = (uint8_t *)(rec + 1);  /* data follows struct */
  memset(rec->data, 0, rt->record_size);
  return rec;
}


Record *kulua_record_newview (lua_State *L, RecordType *rt,
                              RecordArray *arr, uint32_t index) {
  GCObject *o;
  Record *rec;
  size_t sz = sizerecordview();
  o = luaC_newobj(L, LUA_VRECORD, sz);
  rec = gco2rec(o);
  rec->rtype = rt;
  rec->parent = obj2gco(arr);
  rec->data = arr->data + (size_t)index * rt->record_size;
  return rec;
}


RecordArray *kulua_recordarray_new (lua_State *L, RecordType *rt,
                                    uint32_t count) {
  GCObject *o;
  RecordArray *ra;
  size_t sz = sizerecordarray(count, rt->record_size);
  o = luaC_newobj(L, LUA_VRECORDARRAY, sz);
  ra = gco2recarr(o);
  ra->rtype = rt;
  ra->count = count;
  ra->gclist = NULL;
  memset(ra->data, 0, (size_t)count * rt->record_size);
  return ra;
}


/* ====================================================================
** Helper: check if stack value at idx is a specific record variant
** ==================================================================== */

static RecordType *check_recordtype (lua_State *L, int idx) {
  const TValue *o = rec_index2value(L, idx);
  if (!ttisrecordtype(o))
    luaL_typeerror(L, idx, "recordtype");
  return rtypevalue(o);
}

static Record *check_record (lua_State *L, int idx) {
  const TValue *o = rec_index2value(L, idx);
  if (!ttisrecord(o))
    luaL_typeerror(L, idx, "record");
  return recvalue(o);
}

static RecordArray *check_recordarray (lua_State *L, int idx) {
  const TValue *o = rec_index2value(L, idx);
  if (!ttisrecordarray(o))
    luaL_typeerror(L, idx, "recordarray");
  return recarrvalue(o);
}


/* ====================================================================
** RecordType metamethods (called via G(L)->mt[LUA_TRECORD])
** ==================================================================== */

/*
** __call: Entity() -> new Record
** After tryfuncTM: stack = [metamethod, RecordType, args...]
** So self = arg 1
*/
static int recordtype_call (lua_State *L) {
  RecordType *rt = check_recordtype(L, 1);
  Record *rec = kulua_record_new(L, rt);
  setrecvalue2s(L, L->top.p, rec);
  api_incr_top(L);
  return 1;
}


/*
** __index: Entity[N] -> RecordArray, Entity.name -> method lookup
*/
static int recordtype_index (lua_State *L) {
  RecordType *rt = check_recordtype(L, 1);
  const TValue *key = rec_index2value(L, 2);
  if (ttisinteger(key)) {
    /* Entity[N] -> create array */
    lua_Integer count = ivalue(key);
    RecordArray *ra;
    if (count <= 0)
      luaG_runerror(L, "record array size must be positive");
    if (count > (lua_Integer)UINT32_MAX)
      luaG_runerror(L, "record array size too large");
    ra = kulua_recordarray_new(L, rt, (uint32_t)count);
    setrecarrvalue2s(L, L->top.p, ra);
    api_incr_top(L);
    return 1;
  }
  /* String key: look in instance metatable (methods) */
  if (ttisstring(key) && rt->metatable != NULL) {
    const TValue *res = luaH_Hgetshortstr(rt->metatable, tsvalue(key));
    if (!isabstkey(res)) {
      setobj2s(L, L->top.p, res);
      api_incr_top(L);
      return 1;
    }
  }
  lua_pushnil(L);
  return 1;
}


/*
** __newindex: Entity.method_name = func
*/
static int recordtype_newindex (lua_State *L) {
  RecordType *rt = check_recordtype(L, 1);
  const TValue *kv = rec_index2value(L, 2);
  StkId valslot = L->ci->func.p + 3;  /* writable slot for value */
  if (!ttisstring(kv))
    luaG_runerror(L, "recordtype index must be a string");
  if (rt->metatable == NULL)
    luaG_runerror(L, "recordtype has no metatable");
  luaH_set(L, rt->metatable, kv, s2v(valslot));
  luaC_barrierback(L, obj2gco(rt->metatable), s2v(valslot));
  return 0;
}


/*
** __len: #Entity -> byte size per instance
*/
static int recordtype_len (lua_State *L) {
  RecordType *rt = check_recordtype(L, 1);
  lua_pushinteger(L, rt->record_size);
  return 1;
}


/*
** __tostring: tostring(Entity) -> "recordtype: EntityName"
*/
static int recordtype_tostring (lua_State *L) {
  RecordType *rt = check_recordtype(L, 1);
  if (rt->name)
    lua_pushfstring(L, "recordtype: %s", getstr(rt->name));
  else
    lua_pushfstring(L, "recordtype: %p", (void *)rt);
  return 1;
}


/* ====================================================================
** Record/RecordArray instance metamethods (set on rtype->metatable)
** ==================================================================== */

/*
** __len: #e -> byte size, #arr -> count
*/
static int record_len (lua_State *L) {
  const TValue *self = rec_index2value(L, 1);
  if (ttisrecord(self))
    lua_pushinteger(L, recvalue(self)->rtype->record_size);
  else if (ttisrecordarray(self))
    lua_pushinteger(L, recarrvalue(self)->count);
  else
    lua_pushinteger(L, 0);
  return 1;
}


/*
** __tostring: tostring(e) -> "TypeName: 0x..."
*/
static int record_tostring (lua_State *L) {
  const TValue *self = rec_index2value(L, 1);
  if (ttisrecord(self)) {
    Record *rec = recvalue(self);
    if (rec->rtype->name)
      lua_pushfstring(L, "%s: %p", getstr(rec->rtype->name), (void *)rec);
    else
      lua_pushfstring(L, "record: %p", (void *)rec);
  }
  else if (ttisrecordarray(self)) {
    RecordArray *ra = recarrvalue(self);
    if (ra->rtype->name)
      lua_pushfstring(L, "%s[%d]: %p", getstr(ra->rtype->name),
                      (int)ra->count, (void *)ra);
    else
      lua_pushfstring(L, "recordarray[%d]: %p", (int)ra->count, (void *)ra);
  }
  else
    lua_pushstring(L, "record: ?");
  return 1;
}


/* ====================================================================
** Constructor: record { x = fx, y = fx, health = i16, ... }
** ==================================================================== */

int kulua_record_constructor (lua_State *L) {
  Table *arg;
  RecordType *rt;
  Table *inst_mt;
  int nfields = 0;
  uint16_t offset = 0;
  int i;
  StkId kvslot;  /* stack slot for luaH_next key; value at kvslot+1 */

  luaL_checktype(L, 1, LUA_TTABLE);
  arg = hvalue(rec_index2value(L, 1));

  /* Reserve two stack slots for luaH_next iteration (key + value) */
  luaD_checkstack(L, 2);
  kvslot = L->top.p;
  setnilvalue(s2v(kvslot));      /* key = nil */
  setnilvalue(s2v(kvslot + 1));  /* value slot */
  L->top.p += 2;  /* stack: [arg, key, val] */

  /* First pass: count fields */
  setnilvalue(s2v(kvslot));
  while (luaH_next(L, arg, kvslot)) {
    if (!ttisstring(s2v(kvslot)))
      luaG_runerror(L, "record field name must be a string");
    if (!ttisinteger(s2v(kvslot + 1)))
      luaG_runerror(L, "record field type must be a field type constant");
    nfields++;
  }

  if (nfields == 0)
    luaG_runerror(L, "record must have at least one field");

  /* Pop iteration slots, allocate RecordType and anchor on stack */
  L->top.p -= 2;
  rt = kulua_recordtype_new(L, nfields);
  setrtypevalue2s(L, L->top.p, rt);
  api_incr_top(L);  /* stack: [arg, rt] */

  /* Reserve iteration slots again for second pass */
  luaD_checkstack(L, 2);
  kvslot = L->top.p;
  setnilvalue(s2v(kvslot));
  setnilvalue(s2v(kvslot + 1));
  L->top.p += 2;  /* stack: [arg, rt, key, val] */

  /* Second pass: fill fields (insertion order via luaH_next) */
  i = 0;
  offset = 0;
  setnilvalue(s2v(kvslot));
  while (luaH_next(L, arg, kvslot)) {
    lua_Integer ftype = ivalue(s2v(kvslot + 1));
    int fsz;
    if (ftype < 1 || ftype > KULUA_FIELD_MAX)
      luaG_runerror(L, "invalid field type %d", (int)ftype);
    fsz = kulua_fieldsize((int)ftype);
    rt->fields[i].name = tsvalue(s2v(kvslot));
    rt->fields[i].type = cast(lu_byte, ftype);
    rt->fields[i].offset = offset;
    rt->fields[i].size = cast(uint16_t, fsz);
    offset += cast(uint16_t, fsz);
    i++;
  }
  /* Pop iteration slots */
  L->top.p -= 2;
  rt->record_size = offset;

  /* Create instance metatable */
  inst_mt = luaH_new(L);
  sethvalue2s(L, L->top.p, inst_mt);
  api_incr_top(L);  /* stack: [arg, rt, inst_mt] */

  /* inst_mt.__index = inst_mt (class pattern: methods live here) */
  {
    TString *s = luaS_newliteral(L, "__index");
    TValue tkey, tval;
    setsvalue(L, &tkey, s);
    sethvalue(L, &tval, inst_mt);
    luaH_set(L, inst_mt, &tkey, &tval);
    luaC_barrierback(L, obj2gco(inst_mt), &tval);
  }

  /* inst_mt.__len = record_len */
  {
    TString *s = luaS_newliteral(L, "__len");
    TValue tkey, tval;
    setsvalue(L, &tkey, s);
    setfvalue(&tval, record_len);
    luaH_set(L, inst_mt, &tkey, &tval);
  }

  /* inst_mt.__tostring = record_tostring */
  {
    TString *s = luaS_newliteral(L, "__tostring");
    TValue tkey, tval;
    setsvalue(L, &tkey, s);
    setfvalue(&tval, record_tostring);
    luaH_set(L, inst_mt, &tkey, &tval);
  }

  rt->metatable = inst_mt;

  /* Pop inst_mt, leave RecordType on top */
  L->top.p--;

  return 1;
}


/* ====================================================================
** Library initialization: register record globals and shared metatable
** ==================================================================== */

/*
** Sets up G(L)->mt[LUA_TRECORD] with the shared RecordType metatable
** (__call, __index, __newindex, __len, __tostring).
** Called from luaopen_base or a dedicated init function.
*/
void kulua_record_init (lua_State *L) {
  Table *mt;
  TValue tkey, tval;

  mt = luaH_new(L);
  /* Anchor on stack */
  sethvalue2s(L, L->top.p, mt);
  api_incr_top(L);

  /* __call */
  {
    TString *s = luaS_newliteral(L, "__call");
    setsvalue(L, &tkey, s);
    setfvalue(&tval, recordtype_call);
    luaH_set(L, mt, &tkey, &tval);
  }

  /* __index */
  {
    TString *s = luaS_newliteral(L, "__index");
    setsvalue(L, &tkey, s);
    setfvalue(&tval, recordtype_index);
    luaH_set(L, mt, &tkey, &tval);
  }

  /* __newindex */
  {
    TString *s = luaS_newliteral(L, "__newindex");
    setsvalue(L, &tkey, s);
    setfvalue(&tval, recordtype_newindex);
    luaH_set(L, mt, &tkey, &tval);
  }

  /* __len */
  {
    TString *s = luaS_newliteral(L, "__len");
    setsvalue(L, &tkey, s);
    setfvalue(&tval, recordtype_len);
    luaH_set(L, mt, &tkey, &tval);
  }

  /* __tostring */
  {
    TString *s = luaS_newliteral(L, "__tostring");
    setsvalue(L, &tkey, s);
    setfvalue(&tval, recordtype_tostring);
    luaH_set(L, mt, &tkey, &tval);
  }

  /* Set as the global metatable for LUA_TRECORD */
  G(L)->mt[LUA_TRECORD] = mt;

  L->top.p--;  /* pop mt */
}


/* ====================================================================
** C API
** ==================================================================== */

LUA_API void *lua_torecorddata (lua_State *L, int idx, size_t *len) {
  const TValue *o = rec_index2value(L, idx);
  if (ttisrecord(o)) {
    Record *rec = recvalue(o);
    if (len) *len = rec->rtype->record_size;
    return rec->data;
  }
  return NULL;
}


LUA_API void *lua_torecordarraydata (lua_State *L, int idx,
                                     int element, size_t *stride) {
  const TValue *o = rec_index2value(L, idx);
  if (ttisrecordarray(o)) {
    RecordArray *arr = recarrvalue(o);
    if (stride) *stride = arr->rtype->record_size;
    if (element >= 0 && (uint32_t)element < arr->count)
      return arr->data + (size_t)element * arr->rtype->record_size;
  }
  return NULL;
}


LUA_API int lua_isrecord (lua_State *L, int idx) {
  const TValue *o = rec_index2value(L, idx);
  return ttisrecord(o);
}

LUA_API int lua_isrecordarray (lua_State *L, int idx) {
  const TValue *o = rec_index2value(L, idx);
  return ttisrecordarray(o);
}

LUA_API int lua_isrecordtype (lua_State *L, int idx) {
  const TValue *o = rec_index2value(L, idx);
  return ttisrecordtype(o);
}
