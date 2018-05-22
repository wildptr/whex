#include <stdio.h>
#include <stdlib.h>

#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>

#include <windows.h>

#include "types.h"
#include "region.h"
#include "buffer.h"
#include "tree.h"
#include "ui.h"

/* TODO: fix address size issues */

static int
checkaddr(lua_State *L, int index, uint64 *addr)
{
	lua_Integer n = luaL_checkinteger(L, index);
	if (n < 0) return -1;
	*addr = n;
	return 0;
}

int
api_buffer_peek(lua_State *L)
{
	Buffer *b = luaL_checkudata(L, 1, "buffer");
	uint64 addr;
	if (checkaddr(L, 2, &addr)) return 0;
	if (addr >= b->file_size) return 0;
	lua_pushinteger(L, buf_getbyte(b, addr));
	return 1;
}

int
api_buffer_peeku16(lua_State *L)
{
	Buffer *b = luaL_checkudata(L, 1, "buffer");
	uint64 addr;
	if (checkaddr(L, 2, &addr)) return 0;
	if (addr+2 > b->file_size) return 0;
	union {
		uint16 i;
		uchar b[2];
	} u;
	buf_read(b, u.b, addr, 2);
	lua_pushinteger(L, u.i);
	return 1;
}

int
api_buffer_peeku32(lua_State *L)
{
	Buffer *b = luaL_checkudata(L, 1, "buffer");
	uint64 addr;
	if (checkaddr(L, 2, &addr)) return 0;
	if (addr+4 > b->file_size) return 0;
	union {
		uint32 i;
		uchar b[4];
	} u;
	buf_read(b, u.b, addr, 4);
	lua_pushinteger(L, u.i);
	return 1;
}

int
api_buffer_peeku64(lua_State *L)
{
	Buffer *b = luaL_checkudata(L, 1, "buffer");
	uint64 addr;
	if (checkaddr(L, 2, &addr)) return 0;
	if (addr+8 > b->file_size) return 0;
	union {
		uint64 i;
		uchar b[8];
	} u;
	buf_read(b, u.b, addr, 8);
	lua_pushinteger(L, u.i);
	return 1;
}

int
api_buffer_read(lua_State *L)
{
	Buffer *b = luaL_checkudata(L, 1, "buffer");
	uint64 addr;
	if (checkaddr(L, 2, &addr)) return 0;
	long n = (long) luaL_checkinteger(L, 3);
	uchar *s;

	if (addr + n > b->file_size) return 0;
	s = malloc(n+1);
	buf_read(b, s, addr, n);
	s[n] = 0;
	lua_pushlstring(L, (char *) s, n);
	free(s);
	return 1;
}

Tree *
convert_tree(Region *r, lua_State *L)
{
	Tree *tree = ralloc(r, sizeof *tree);
	tree->parent = 0;

	lua_getfield(L, -1, "name");
	if (lua_isstring(L, -1)) {
		const char *name = lua_tostring(L, -1);
		int name_len1 = strlen(name)+1;
		tree->name = ralloc(r, name_len1);
		memcpy(tree->name, name, name_len1);
	} else {
		tree->name = "<anonymous>";
	}
	lua_pop(L, 1);

	lua_getfield(L, -1, "start");
	if (!lua_isinteger(L, -1)) {
		lua_pop(L, 1);
		return 0;
	}
	tree->start = lua_tointeger(L, -1);
	lua_pop(L, 1);

	lua_getfield(L, -1, "size");
	if (!lua_isinteger(L, -1)) {
		lua_pop(L, 1);
		return 0;
	}
	tree->len = lua_tointeger(L, -1);
	lua_pop(L, 1);

	tree->intvalue = 0;
	lua_getfield(L, -1, "value");
	switch (lua_type(L, -1)) {
	case LUA_TNUMBER:
		tree->type = F_UINT;
		tree->intvalue = (long) lua_tointeger(L, -1);
		break;
	case LUA_TSTRING:
		tree->type = F_ASCII;
		break;
	default:
		tree->type = F_RAW;
	}
	lua_pop(L, 1);

	lua_getfield(L, -1, "type");
	if (!lua_isnil(L, -1)) {
		const char *typename = luaL_checkstring(L, -1);
		tree->custom_type_name = _strdup(typename);
		tree->type = F_CUSTOM;
	} else {
		tree->custom_type_name = 0;
	}
	lua_pop(L, 1);

	lua_getfield(L, -1, "children");
	// top of stack is array of children
	int n = lua_rawlen(L, -1); // get number of children

	tree->n_child = n;
	tree->children = ralloc(r, n * sizeof *tree->children);
	for (int i=0; i<n; i++) {
		// beware that indices start at 1 in Lua
		lua_rawgeti(L, -1, 1+i); // push child
		Tree *child = convert_tree(r, L);
		if (!child) {
			lua_pop(L, 2);
			return 0;
		}
		child->parent = tree;
		tree->children[i] = child;
		lua_pop(L, 1); // pop child
	}

	// pop array of children
	lua_pop(L, 1);

	return tree;
}

int
api_buffer_tree(lua_State *L)
{
	Buffer *b = luaL_checkudata(L, 1, "buffer");
	if (!b->tree) {
		return 0;
	}
	lua_getuservalue(L, 1);
	lua_getfield(L, -1, "value");
	lua_remove(L, -2);
	return 1;
}

int
api_buffer_size(lua_State *L)
{
	Buffer *b = luaL_checkudata(L, 1, "buffer");
	lua_pushinteger(L, b->file_size);
	return 1;
}

int
api_buffer_replace(lua_State *L)
{
	Buffer *b = luaL_checkudata(L, 1, "buffer");
	uint64 addr;
	size_t len;
	const uchar *data;

	if (checkaddr(L, 2, &addr)) return 0;
	data = (const uchar *) lua_tolstring(L, 3, &len);
	if (addr + len > b->buffer_size) return 0;
	buf_replace(b, addr, data, len);
	return 0;
}

int
api_buffer_insert(lua_State *L)
{
	Buffer *b = luaL_checkudata(L, 1, "buffer");
	uint64 addr;
	size_t len;
	const uchar *data;

	if (checkaddr(L, 2, &addr)) return 0;
	data = (const uchar *) lua_tolstring(L, 3, &len);
	if (addr > b->buffer_size) return 0;
	buf_insert(b, addr, data, len);
	return 0;
}
