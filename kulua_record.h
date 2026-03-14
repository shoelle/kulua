/*
** kulua_record.h
** Record type support for Kulua
** See Copyright Notice in lua.h
*/

#ifndef kulua_record_h
#define kulua_record_h

#include "lobject.h"


/* Field size lookup */
LUAI_FUNC int kulua_fieldsize (int type);

/* Field access */
LUAI_FUNC void kulua_record_readfield (TValue *res, const uint8_t *data,
                                       const RecordField *f);
LUAI_FUNC int kulua_record_writefield (lua_State *L, const TValue *val,
                                       uint8_t *data, const RecordField *f);

/* Field lookup: returns field index or -1 */
LUAI_FUNC int kulua_recordtype_findfield (RecordType *rt, TString *key);

/* Allocation */
LUAI_FUNC RecordType *kulua_recordtype_new (lua_State *L, int nfields);
LUAI_FUNC Record *kulua_record_new (lua_State *L, RecordType *rt);
LUAI_FUNC Record *kulua_record_newview (lua_State *L, RecordType *rt,
                                        RecordArray *arr, uint32_t index);
LUAI_FUNC RecordArray *kulua_recordarray_new (lua_State *L, RecordType *rt,
                                              uint32_t count);

/* Constructor (Lua C function) */
LUAI_FUNC int kulua_record_constructor (lua_State *L);

/* Library initialization: sets up G(L)->mt[LUA_TRECORD] */
LUAI_FUNC void kulua_record_init (lua_State *L);


#endif
