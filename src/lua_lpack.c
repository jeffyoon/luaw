/*
* Copyright (c) 2015 raksoras
*
* Permission is hereby granted, free of charge, to any person obtaining a copy
* of this software and associated documentation files (the "Software"), to deal
* in the Software without restriction, including without limitation the rights
* to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
* copies of the Software, and to permit persons to whom the Software is
* furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in all
* copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
* AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
* OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
* SOFTWARE.
*/

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <float.h>
#include <lua.h>
#include <lauxlib.h>

#include "uv.h"
#include "luaw_common.h"
#include "lua_lpack.h"


static void be16(const char* in, char * out) {
    if (is_bigendian()) {
        out[0] = in[0];
        out[1] = in[1];
    } else {
        out[0] = in[1];
        out[1] = in[0];
    }
}

static void be32(const char* in, char* out) {
    if (is_bigendian()) {
        out[0] = in[0];
        out[1] = in[1];
        out[2] = in[2];
        out[3] = in[3];
    } else {
        out[0] = in[3];
        out[3] = in[0];
        out[1] = in[2];
        out[2] = in[1];
    }
}

static void be64(const char* in, char* out) {
    if (is_bigendian()) {
        out[0] = in[0];
        out[1] = in[1];

        out[2] = in[2];
        out[3] = in[3];

        out[4] = in[4];
        out[5] = in[5];

        out[6] = in[6];
        out[7] = in[7];
    } else {
        out[0] = in[7];
        out[7] = in[0];

        out[1] = in[6];
        out[6] = in[1];

        out[2] = in[5];
        out[5] = in[2];

        out[3] = in[4];
        out[4] = in[3];
    }
}

static int number_to_lua(lua_State* L, int read_len, double value) {
    lua_pushinteger(L, read_len);
    lua_pushnumber(L, value);
    return 2;
}

LUA_LIB_METHOD int read_number(lua_State* L) {
    int num_type = luaL_checkinteger(L, 1);
    size_t len;
    const char* buff = luaL_checklstring(L, 2, &len);
    size_t offset = luaL_checkinteger(L, 3);

    int remaining = len - offset;
    if (remaining < 0) {
        luaL_error(L, "Buffer underflow while reading number");
    }

    buff = buff + offset;
    size_t sz;
    switch(num_type) {
        case TYPE_MARKER:
        case UINT_8:
        case STRING:
        case DICT_ENTRY:
            if (remaining < 1) return number_to_lua(L, 0, 0);
            uint8_t u = buff[0];
            return number_to_lua(L, 1, u);

        case UINT_16:
        case BIG_STRING:
        case BIG_DICT_ENTRY:
            if (remaining < 2) return number_to_lua(L, 0, 0);
            uint16_t u16;
            be16(buff, (char *)&u16);
            return number_to_lua(L, 2, u16);

        case UINT_32:
        case HUGE_STRING:
            if (remaining < 4) return number_to_lua(L, 0, 0);
            uint32_t u32;
            be32(buff, (char *)&u32);
            return number_to_lua(L, 4, u32);

        case INT_8:
            if (remaining < 1) return number_to_lua(L, 0, 0);
            int8_t i = buff[0];
            return number_to_lua(L, 1, i);

        case INT_16:
            if (remaining < 2) return number_to_lua(L, 0, 0);
            int16_t i16;
            be16(buff, (char *)&i16);
            return number_to_lua(L, 2, i16);

        case INT_32:
            if (remaining < 4) return number_to_lua(L, 0, 0);
            int32_t i32;
            be32(buff, (char *)&i32);
            return number_to_lua(L, 4, i32);

        case INT_64:
            if (remaining < 8) return number_to_lua(L, 0, 0);
            int64_t i64;
            be64(buff, (char *)&i64);
            return number_to_lua(L, 8, i64);

        case FLOAT:
            sz = sizeof(float);
            if (remaining < sz) return number_to_lua(L, 0, 0);
            float f;
            be32(buff, (char *)&f);
            return number_to_lua(L, sz, f);

        case DOUBLE:
            sz = sizeof(double);
            if (remaining < sz) return number_to_lua(L, 0, 0);
            double d;
            be64(buff, (char *)&d);
            return number_to_lua(L, sz, d);

        default:
            return luaL_error(L, "Invalid marker %d specified", num_type);
    }
}

static int string_to_lua(lua_State* L, const char* buff, size_t len) {
    lua_pushinteger(L, len);
    lua_pushlstring(L, buff, len);
    return 2;
}

LUA_LIB_METHOD int read_string(lua_State* L) {
    int desired = luaL_checkinteger(L, 1);
    size_t len;
    const char* buff = luaL_checklstring(L, 2, &len);
    size_t offset = luaL_checkinteger(L, 3);

    int remaining = len - offset;
    if (remaining < 0) {
        luaL_error(L, "Buffer underflow while reading string");
    }

    buff = buff + offset;
    if (remaining >= desired) {
        return string_to_lua(L, buff, desired);
    } else {
        return string_to_lua(L, buff, remaining);
    }
}

static double fetch_next_as_integer(lua_State* L, int* idx, const char* s) {
    lua_rawgeti(L, 1, *idx);
    if (!lua_isnumber(L, -1)) {
        const char* dbg_val = lua_tostring(L, -1);
        luaL_error(L, "Invalid value %s found where %s was expected",dbg_val, s);
    }
    int i = lua_tointeger(L, -1);
    *idx = *idx + 1;
    lua_pop(L, 1);
    return i;
}

static double fetch_next_as_double(lua_State* L, int* idx, const char* s) {
    lua_rawgeti(L, 1, *idx);
    if (!lua_tonumber(L, -1)) {
        const char* dbg_val = lua_tostring(L, -1);
        luaL_error(L, "Invalid value %s found where %s was expected", dbg_val, s);
    }
    double d = lua_tonumber(L, -1);
    *idx = *idx + 1;
    lua_pop(L, 1);
    return d;
}

static const char* fetch_next_as_string(lua_State* L, size_t *len, int* idx, const char* s) {
    lua_rawgeti(L, 1, *idx);
    const char* val = lua_tolstring(L, -1, len);
    if (val == NULL) {
        luaL_error(L, "Invalid value found where %s was expected", s);
    }
    *idx = *idx + 1;
    lua_pop(L, 1);
    return val;
}

#define CHECK_ROOM(L, pos, need, size)  \
    if ((pos+need) > size) {            \
       return luaL_error(L, "Run out of buffer in serialize_write_Q, wrong expected size supplied"); \
    }

LUA_LIB_METHOD int serialize_write_Q(lua_State* L) {
    if (lua_istable(L, 1) == 0) {
        return luaL_error(L, "Invalid WriteQ, not a table");
    }

    size_t len = lua_tointeger(L, 2);
    if (len == 0) {
        return luaL_error(L, "Invalid write buffer length specified");
    }

    const int buffsize = len + 64;
    char* buff = (char*) malloc(buffsize);
    if (buff == NULL) {
        return luaL_error(L, "Could not allocate memory for serialize_write_Q");
    }
    int pos = 0;

    int ival;
    double dval;
    uint16_t u16;
    uint32_t u32;
    int16_t i16;
    int32_t i32;
    int64_t i64;
    const char *sval;
    int qLen = lua_rawlen(L, 1);
    int idx = 1;
    int datasize = 0;

    while (idx <= qLen) {
        // write marker
        int marker = fetch_next_as_integer(L, &idx, "marker");
        CHECK_ROOM(L, pos, 1, buffsize)
        buff[pos] = (char)marker;
        pos++;

        switch(marker) {
            case MAP_START:
            case ARRAY_START:
            case DICT_START:
            case RECORD_END:
            case NIL:
            case BOOL_TRUE:
            case BOOL_FALSE:
                break; //No value, single byte markers

            case UINT_8:
            case DICT_ENTRY:
                datasize = sizeof(uint8_t);
                CHECK_ROOM(L, pos, datasize, buffsize)
                ival = fetch_next_as_integer(L, &idx, "UINT_8");
                buff[pos] = (uint8_t)ival;
                pos += datasize;
                break;

            case UINT_16:
            case BIG_DICT_ENTRY:
                datasize = sizeof(uint16_t);
                CHECK_ROOM(L, pos, datasize, buffsize)
                ival = fetch_next_as_integer(L, &idx, "UINT_16 or BIG_DICT_ENTRY");
                u16 = (uint16_t)ival;
                be16((char *)&u16, buff+pos);
                pos += datasize;
                break;

            case UINT_32:
                datasize = sizeof(uint32_t);
                ival = fetch_next_as_integer(L, &idx, "UINT_32");
                u32 = (uint32_t)ival;
                be32((char *)&u32, buff+pos);
                pos += datasize;
                break;

            case INT_8:
                datasize = sizeof(int8_t);
                CHECK_ROOM(L, pos, datasize, buffsize)
                ival = fetch_next_as_integer(L, &idx, "INT_8");
                buff[pos] = (int8_t)ival;
                pos += datasize;
                break;

            case INT_16:
                datasize = sizeof(int16_t);
                CHECK_ROOM(L, pos, datasize, buffsize)
                ival = fetch_next_as_integer(L, &idx, "INT_16");
                i16 = (int16_t)ival;
                be16((char *)&i16, buff+pos);
                pos += datasize;
                break;

            case INT_32:
                datasize = sizeof(int32_t);
                CHECK_ROOM(L, pos, datasize, buffsize)
                ival = fetch_next_as_integer(L, &idx, "INT_32");
                i32 = (int32_t)ival;
                be32((char *)&i32, buff+pos);
                pos += datasize;
                break;

            case INT_64:
                datasize = sizeof(int64_t);
                CHECK_ROOM(L, pos, datasize, buffsize)
                dval = fetch_next_as_double(L, &idx, "INT_64");
                i64 = (int64_t)dval;
                be64((char *)&i64, buff+pos);
                pos += datasize;
                break;

            case FLOAT:
                datasize = sizeof(float);
                CHECK_ROOM(L, pos, datasize, buffsize)
                dval = fetch_next_as_double(L, &idx, "FLOAT");
                float f = (float)dval;
                be32((char *)&f, buff+pos);
                pos += datasize;
                break;

            case DOUBLE:
                datasize = sizeof(double);
                CHECK_ROOM(L, pos, datasize, buffsize)
                dval = fetch_next_as_double(L, &idx, "DOUBLE");
                be64((char *)&dval, buff+pos);
                pos += datasize;
                break;

            case STRING:
                datasize = sizeof(uint8_t);
                sval = fetch_next_as_string(L, &len, &idx, "STRING");
                CHECK_ROOM(L, pos, datasize+len, buffsize)
                buff[pos] = (uint8_t)len;
                pos += datasize;
                memcpy(buff+pos, sval, len);
                pos += len;
                break;

            case BIG_STRING:
                datasize = sizeof(uint16_t);
                sval = fetch_next_as_string(L, &len, &idx, "BIG_STRING");
                CHECK_ROOM(L, pos, datasize+len, buffsize)
                u16 = (uint16_t)len;
                be16((char *)&u16, buff+pos);
                pos += datasize;
                memcpy(buff+pos, sval, len);
                pos += len;
                break;

            case HUGE_STRING:
                datasize = sizeof(uint32_t);
                sval = fetch_next_as_string(L, &len, &idx, "HUGE_STRING");
                CHECK_ROOM(L, pos, datasize+len, buffsize)
                u32 = (uint32_t)len;
                be32((char *)&u32, buff+pos);
                pos += datasize;
                memcpy(buff+pos, sval, len);
                pos += len;
                break;

            default:
                return luaL_error(L, "Invalid marker %d encountered", marker);
        }
    }

    lua_pushlstring(L, buff, pos);
    free(buff);
    return 1;
}

/* Type enum definition in Lua: lpack.INT_8 = {"INT_8", 14, 1, -128, 127} */
static void define_lpack_enum(lua_State* L, const char* name, type_tag tval, size_t size, double min, double max) {
    lua_pushstring(L, name);
    lua_createtable(L, 5, 0);

    lua_pushstring(L, name);
    lua_rawseti(L, -2, 1);

    lua_pushinteger(L, tval);
    lua_rawseti(L, -2, 2);

    lua_pushinteger(L, size);
    lua_rawseti(L, -2, 3);

    lua_pushnumber(L, min);
    lua_rawseti(L, -2, 4);

    lua_pushnumber(L, max);
    lua_rawseti(L, -2, 5);

    lua_rawset(L, -3);
}

#define LPACK_ENUM_DEF(L, e, size, min, max) define_lpack_enum(L, #e, e, size, min, max)

static void define_lapck_types(lua_State* L) {
    LPACK_ENUM_DEF(L, TYPE_MARKER, 0, -1, -1);

    LPACK_ENUM_DEF(L, MAP_START, sizeof(int8_t), MAP_START, MAP_START);
    LPACK_ENUM_DEF(L, ARRAY_START, sizeof(int8_t), ARRAY_START, ARRAY_START);
    LPACK_ENUM_DEF(L, DICT_START, sizeof(int8_t), DICT_START, DICT_START);
    LPACK_ENUM_DEF(L, RECORD_END, sizeof(int8_t), RECORD_END, RECORD_END);

    LPACK_ENUM_DEF(L, NIL, sizeof(int8_t), NIL, NIL);
    LPACK_ENUM_DEF(L, BOOL_TRUE, sizeof(int8_t), BOOL_TRUE, BOOL_TRUE);
    LPACK_ENUM_DEF(L, BOOL_FALSE, sizeof(int8_t), BOOL_FALSE, BOOL_FALSE);

    LPACK_ENUM_DEF(L, UINT_8, sizeof(uint8_t), 0, UINT8_MAX);
    LPACK_ENUM_DEF(L, DICT_ENTRY, sizeof(uint8_t), 0, UINT8_MAX);
    LPACK_ENUM_DEF(L, UINT_16, sizeof(uint16_t), 0, UINT16_MAX);
    LPACK_ENUM_DEF(L, BIG_DICT_ENTRY, sizeof(uint16_t), 0, UINT16_MAX);
    LPACK_ENUM_DEF(L, UINT_32, sizeof(uint32_t), 0, UINT32_MAX);
    LPACK_ENUM_DEF(L, INT_8, sizeof(int8_t), INT8_MIN, INT8_MAX);
    LPACK_ENUM_DEF(L, INT_16, sizeof(int16_t), INT16_MIN, INT16_MAX);
    LPACK_ENUM_DEF(L, INT_32, sizeof(int32_t), INT32_MIN, INT32_MAX);
    LPACK_ENUM_DEF(L, INT_64, sizeof(int64_t), INT64_MIN, INT64_MAX);
    LPACK_ENUM_DEF(L, FLOAT, sizeof(float), -FLT_MAX, FLT_MAX);
    LPACK_ENUM_DEF(L, DOUBLE, sizeof(double), -DBL_MAX, DBL_MAX);

    LPACK_ENUM_DEF(L, STRING, sizeof(uint8_t), 0, UINT8_MAX);
    LPACK_ENUM_DEF(L, BIG_STRING, sizeof(uint16_t), 0, UINT16_MAX);
    LPACK_ENUM_DEF(L, HUGE_STRING, sizeof(uint32_t), 0, UINT32_MAX);
}

LUA_LIB_METHOD static int new_lpack_parser(lua_State* L) {
    lua_createtable(L, 0, 16);
    luaL_setmetatable(L, LUA_LPACK_META_TABLE);
    return 1;
}

LUA_LIB_METHOD static int create_dict(lua_State* L) {
    int narr = luaL_checkinteger(L, 1);
    int nrec = luaL_checkinteger(L, 2);
    lua_createtable(L, narr, nrec);
    return 1;
}

LUA_LIB_METHOD static int to_hex(lua_State* L) {
    int num = luaL_checkinteger(L, -1);
    if (num > 65536) {
        raise_lua_error(L, "toHex called with input %d, which is larger than acceptable limit", num);
    }

    char hex[5];
    sprintf(hex, "%x", num);
    lua_pushstring(L, hex);
    return 1;
}

static const struct luaL_Reg luaw_lpack_methods[] = {
	{"read_number", read_number},
	{"read_string", read_string},
	{"serialize_write_Q", serialize_write_Q},
    {NULL, NULL}  /* sentinel */
};

static const struct luaL_Reg luaw_lpack_lib[] = {
	{"newLPackParser", new_lpack_parser},
    {"createDict", create_dict},
    {"toHex", to_hex},
    {NULL, NULL}  /* sentinel */
};

void luaw_init_lpack_lib (lua_State *L) {
	make_metatable(L, LUA_LPACK_META_TABLE, luaw_lpack_methods);
    define_lapck_types(L);
    luaL_newlib(L, luaw_lpack_lib);
    lua_setglobal(L, "luaw_lpack_lib");
}


