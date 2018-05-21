#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <lua.h>
#include <lauxlib.h>
#include <tchar.h>

#include "types.h"
#include "region.h"
#include "buf.h"
#include "inst.h"
#include "dasm.h"

typedef struct {
	uchar len;
	uchar op;
	uchar var;
	uchar noperand;
	Operand operands[0];
} LuaInst;

static void
convert_operand(lua_State *L, Operand *o)
{
	lua_newtable(L);
	const char *kind;
	switch (o->kind) {
	case O_REG: kind = "reg"; break;
	case O_MEM: kind = "mem"; break;
	case O_IMM: kind = "imm"; break;
	case O_OFF: kind = "off"; break;
	case O_FAR: kind = "far"; break;
	default: return;
	}
	lua_pushlstring(L, kind, 3);
	lua_setfield(L, -2, "kind");
	lua_pushinteger(L, o->size);
	lua_setfield(L, -2, "size");
	switch (o->kind) {
	case O_REG:
		lua_pushinteger(L, o->reg);
		lua_setfield(L, -2, "reg");
		break;
	case O_MEM:
		lua_pushinteger(L, o->base);
		lua_setfield(L, -2, "base");
		lua_pushinteger(L, o->index);
		lua_setfield(L, -2, "index");
		lua_pushinteger(L, o->scale);
		lua_setfield(L, -2, "scale");
		lua_pushinteger(L, o->seg);
		lua_setfield(L, -2, "seg");
		lua_pushinteger(L, o->disp);
		lua_setfield(L, -2, "disp");
		break;
	case O_IMM:
		lua_pushinteger(L, o->imm);
		lua_setfield(L, -2, "imm");
		break;
	case O_OFF:
		lua_pushinteger(L, o->imm);
		lua_setfield(L, -2, "offset");
		break;
	case O_FAR:
		lua_pushinteger(L, o->imm16);
		lua_setfield(L, -2, "seg");
		lua_pushinteger(L, o->imm);
		lua_setfield(L, -2, "offset");
		break;
	default:
		assert(0);
	}
}

static void
convert_operands(lua_State *L, Inst *inst)
{
	lua_newtable(L);
	int n = inst->noperand;
	for (int i=0; i<n; i++) {
		convert_operand(L, &inst->operands[i]);
		lua_seti(L, -2, 1+i);
	}
}

/* places converted instruction on top of stack */
static void
convert_inst(lua_State *L, Inst *inst)
{
	/* bytes; op; var; operands */
	lua_newtable(L);
	int n = inst->noperand;
	LuaInst *li = lua_newuserdata(L, sizeof(*li) + n*sizeof(Operand));
	li->len = inst->len;
	li->op = inst->op;
	li->var = inst->var;
	li->noperand = inst->noperand;
	memcpy(&li->operands, inst->operands, n*sizeof(Operand));
	lua_setfield(L, -2, "userdata");
	lua_pushlstring(L, (char *) inst->bytes, inst->len);
	lua_setfield(L, -2, "bytes");
	lua_pushinteger(L, inst->op);
	lua_setfield(L, -2, "op");
	lua_pushinteger(L, inst->var);
	lua_setfield(L, -2, "var");
	convert_operands(L, inst);
	lua_setfield(L, -2, "operands");
}

__declspec(dllexport) int
api_disasm(lua_State *L)
{
	size_t len;
	const char *data = luaL_checklstring(L, 1, &len);
	long offset = (long) luaL_checkinteger(L, 2);
	Inst inst;
	DisasmConfig conf = {0};
	Region rgn = {0};
	conf.mode = MODE_32;
	conf.rgn = &rgn;
	disasm((uchar *)(data+offset), &inst, &conf);
	convert_inst(L, &inst);
	rfreeall(&rgn);
	return 1;
}

__declspec(dllexport) int
api_format_inst(lua_State *L)
{
	HeapBufA hb;
	if (init_heapbufA(&hb)) {
		return 0;
	}

	lua_getfield(L, 1, "userdata");
	LuaInst *li = lua_touserdata(L, -1);
	Inst inst = {0};
	inst.op = li->op;
	inst.var = li->var;
	inst.noperand = li->noperand;
	inst.operands = li->operands;
	format_inst(&hb.buf, &inst);
	lua_pop(L, 1); /* 'userdata' */
	lua_pushlstring(L, hb.start, hb.cur - hb.start);
	free(hb.start);
	return 1;
}
