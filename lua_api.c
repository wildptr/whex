#include "mainwindow.h"
#include "tree.h"

char *strdup(const char *);

static struct mainwindow *get_mainwindow(lua_State *L)
{
	lua_pushinteger(L, 0);
	lua_gettable(L, LUA_REGISTRYINDEX);
	return lua_touserdata(L, -1);
}

int lapi_peek(lua_State *L)
{
	long long addr = luaL_checkinteger(L, 1);
	struct mainwindow *w = get_mainwindow(L);
	if (addr < 0 || addr >= w->file_size) {
		return luaL_error(L, "address out of bounds");
	}
	lua_pushinteger(L, mainwindow_getbyte(w, addr));
	return 1;
}

static struct tree *convert_tree(lua_State *L)
{
	struct tree *tree = malloc(sizeof *tree);
	tree->parent = 0;

	lua_getfield(L, -1, "start");
	if (!lua_isinteger(L, -1)) goto error;
	tree->start = lua_tointeger(L, -1);
	lua_pop(L, 1);

	lua_getfield(L, -1, "size");
	if (!lua_isinteger(L, -1)) goto error;
	tree->len = lua_tointeger(L, -1);
	lua_pop(L, 1);

	lua_getfield(L, -1, "name");
	if (!lua_isstring(L, -1)) goto error;
	tree->name = strdup(lua_tostring(L, -1));
	lua_pop(L, 1);

	lua_getfield(L, -1, "value");
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

	lua_getfield(L, -1, "children");
	// top of stack is array of children
	int n = lua_rawlen(L, -1); // get number of children

	tree->n_child = n;
	tree->children = malloc(n * sizeof *tree->children);
	for (int i=0; i<n; i++) {
		// beware that indices start at 1 in Lua
		lua_rawgeti(L, -1, 1+i); // push child
		tree->children[i] = convert_tree(L);
		if (!tree->children[i]) {
			free(tree->children);
			goto error;
		}
		tree->children[i]->parent = tree;
		lua_pop(L, 1);
	}

	// pop array of children
	lua_pop(L, 1);

	return tree;
error:
	free(tree);
	return 0;
}

int lapi_set_tree(lua_State *L)
{
	struct tree *tree = convert_tree(L);
	if (!tree) {
		return luaL_error(L, "invalid argument");
	}
	struct mainwindow *w = get_mainwindow(L);
	mainwindow_set_tree(w, tree);
	return 0;
}

int lapi_filepath(lua_State *L)
{
	struct mainwindow *w = get_mainwindow(L);
	lua_pushstring(L, w->filepath);
	return 1;
}

int lapi_goto(lua_State *L)
{
	struct mainwindow *w = get_mainwindow(L);
	long long addr = luaL_checkinteger(L, 1);
	if (addr < 0 || addr >= w->file_size) {
		luaL_error(L, "address out of bounds");
	}
	mainwindow_goto_address(w, addr);
	return 0;
}

void register_lua_globals(lua_State *L)
{
	lua_newtable(L);
	lua_pushcfunction(L, lapi_peek);
	lua_setfield(L, -2, "peek");
	lua_pushcfunction(L, lapi_filepath);
	lua_setfield(L, -2, "filepath");
	lua_pushcfunction(L, lapi_set_tree);
	lua_setfield(L, -2, "set_tree");
	lua_pushcfunction(L, lapi_goto);
	lua_setfield(L, -2, "goto_address");
	lua_setglobal(L, "whex");
}
