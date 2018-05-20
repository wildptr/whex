#include <assert.h>
#include <tchar.h>

#include "types.h"
#include "buf.h"
#include "inst.h"

#define DEF(x) #x

const char *opname[] = {
	"<INVALID>",
#include "inst.in"
};

const char *regname[] = {
#include "reg.in"
};

#undef DEF

static int
format_index(Buf *buf, Operand *o)
{
	int ret = bprintf(buf, "%s", regname[o->index]);
	if (o->scale > 1) ret += bprintf(buf, "*%d", o->scale);
	return ret;
}

static int
format_mem(Buf *buf, Operand *o)
{
	int ret = 0;
	if (o->base == NOREG) {
		if (o->index == NOREG) {
			ret += bprintf(buf, "[%d]", o->disp);
		} else {
			ret += bprintf(buf, "[%a", format_index, o);
			if (o->disp) ret += bprintf(buf, "%+d", o->disp);
			ret += bprintf(buf, "]");
		}
	} else {
		if (o->index == NOREG) {
			ret += bprintf(buf, "[%s", regname[o->base]);
			if (o->disp) ret += bprintf(buf, "%+d", o->disp);
			ret += bprintf(buf, "]");
		} else {
			ret += bprintf(buf, "[%s+%a", regname[o->base],
				       format_index, o);
			if (o->disp) ret += bprintf(buf, "%+d", o->disp);
			ret += bprintf(buf, "]");
		}
	}
	return ret;
}

static int
format_operand(Buf *buf, Operand *o)
{
	int ret;
	switch (o->kind) {
	case O_REG:
		return bprintf(buf, "%s", regname[o->reg]);
	case O_MEM:
		return format_mem(buf, o);
	case O_IMM:
		return bprintf(buf, "%d", o->imm);
	case O_OFF:
		ret = bprintf(buf, "$");
		ret += bprintf(buf, "%+d", o->imm);
		return ret;
	case O_FAR:
		return bprintf(buf, "0x%x:0x%x", o->imm16, o->imm);
	}
	assert(0);
	return 0;
}

int
format_inst(Buf *buf, Inst *inst)
{
	int ret;
	ret = bprintf(buf, "%s", opname[inst->op]);
	if (inst->noperand) {
		ret += bprintf(buf, " %a", format_operand, &inst->operands[0]);
		for (int i=1; i<inst->noperand; i++) {
			ret += bprintf(buf, ",%a",
				       format_operand, &inst->operands[i]);
		}
	}
	return ret;
}
