#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>

#include <windows.h>

#include "util.h"
#include "whex.h"
#include "tree.h"
#include "ui.h"

static UI *
getui(lua_State *L)
{
	lua_pushinteger(L, 0);
	lua_gettable(L, LUA_REGISTRYINDEX);
	UI *ui = lua_touserdata(L, -1);
	lua_pop(L, 1);
	return ui;
}

int
lapi_peek(lua_State *L)
{
	long long addr = luaL_checkinteger(L, 1);
	UI *ui = getui(L);
	Whex *w = ui->whex;
	if (addr < 0 || addr >= w->file_size) {
		return luaL_error(L, "address out of bounds");
	}
	lua_pushinteger(L, whex_getbyte(w, addr));
	return 1;
}

int
lapi_peekstr(lua_State *L)
{
	long long addr = luaL_checkinteger(L, 1);
	int n = luaL_checkinteger(L, 2);
	UI *ui = getui(L);
	Whex *w = ui->whex;
	if (addr < 0 || addr+n > w->file_size) {
		return luaL_error(L, "address out of bounds");
	}
	char *s = malloc(n+1);
	for (int i=0; i<n; i++) {
		s[i] = whex_getbyte(w, addr+i);
	}
	s[n] = 0;
	lua_pushstring(L, s);
	free(s);
	return 1;
}

Tree *
convert_tree(Region *r, lua_State *L)
{
	Tree *tree = ralloc(r, sizeof *tree);
	tree->parent = 0;

	lua_getfield(L, -1, "_start");
	if (!lua_isinteger(L, -1)) {
		lua_pop(L, 1);
		return 0;
	}
	tree->start = lua_tointeger(L, -1);
	lua_pop(L, 1);

	lua_getfield(L, -1, "_size");
	if (!lua_isinteger(L, -1)) {
		lua_pop(L, 1);
		return 0;
	}
	tree->len = lua_tointeger(L, -1);
	lua_pop(L, 1);

	lua_getfield(L, -1, "_name");
	if (!lua_isstring(L, -1)) {
		lua_pop(L, 1);
		return 0;
	}
	const char *name = lua_tostring(L, -1);
	int name_len1 = strlen(name)+1;
	tree->name = ralloc(r, name_len1);
	memcpy(tree->name, name, name_len1);
	lua_pop(L, 1);

	lua_getfield(L, -1, "_value");
	switch (lua_type(L, -1)) {
	case LUA_TNUMBER:
		tree->type = F_UINT;
		break;
	case LUA_TSTRING:
		tree->type = F_ASCII;
		break;
	default:
		tree->type = F_RAW;
	}
	lua_pop(L, 1);

	lua_getfield(L, -1, "_children");
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
lapi_msgbox(lua_State *L)
{
	const char *msg = luaL_checkstring(L, 1);
	UI *ui = getui(L);
	MessageBoxA(ui->hwnd, msg, "WHEX", MB_OK);
	return 0;
}

void
register_lua_globals(lua_State *L)
{
	lua_pushinteger(L, 1); // index in Lua registry
	lua_newtable(L);
	lua_pushcfunction(L, lapi_msgbox);
	// at index -1 is the C function
	lua_setfield(L, -2, "msgbox");
	lua_pushcfunction(L, lapi_peek);
	lua_setfield(L, -2, "peek");
	lua_pushcfunction(L, lapi_peekstr);
	lua_setfield(L, -2, "peekstr");
	lua_settable(L, LUA_REGISTRYINDEX);
}
