#include "mainwindow.h"

int lapi_getbyte(lua_State *L)
{
	lua_Integer l_address = luaL_checkinteger(L, 1);
	if (l_address < 0) {
		luaL_error(L, "negative address: %d", l_address);
		return 1;
	}
	uint32_t address = (uint32_t) l_address;
	lua_pushinteger(L, 0);
	lua_gettable(L, LUA_REGISTRYINDEX);
	struct mainwindow *w = lua_touserdata(L, -1);
	lua_pushinteger(L, mainwindow_getbyte(w, address));
	return 1;
}
