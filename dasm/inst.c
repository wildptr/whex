#include <assert.h>
#include <tchar.h>

#include "types.h"
#include "buf.h"
#include "printf.h"
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
format_index(BufA *buf, Operand *o)
{
	int ret = bprintfA(buf, "%s", regname[o->index]);
	if (o->scale > 1) ret += bprintfA(buf, "*%d", o->scale);
	return ret;
}

static int
format_mem(BufA *buf, Operand *o)
{
	int ret = 0;
	if (o->base == NOREG) {
		if (o->index == NOREG) {
			ret += bprintfA(buf, "[%d]", o->disp);
		} else {
			ret += bprintfA(buf, "[%a", format_index, o);
			if (o->disp) ret += bprintfA(buf, "%+d", o->disp);
			ret += bprintfA(buf, "]");
		}
	} else {
		if (o->index == NOREG) {
			ret += bprintfA(buf, "[%s", regname[o->base]);
			if (o->disp) ret += bprintfA(buf, "%+d", o->disp);
			ret += bprintfA(buf, "]");
		} else {
			ret += bprintfA(buf, "[%s+%a", regname[o->base],
				       format_index, o);
			if (o->disp) ret += bprintfA(buf, "%+d", o->disp);
			ret += bprintfA(buf, "]");
		}
	}
	return ret;
}

static int
format_operand(BufA *buf, Operand *o)
{
	int ret;
	switch (o->kind) {
	case O_INV:
		return buf->puts(buf, "<INVALID>", 9);
	case O_REG:
		return bprintfA(buf, "%s", regname[o->reg]);
	case O_MEM:
		return format_mem(buf, o);
	case O_IMM:
		return bprintfA(buf, "%d", o->imm);
	case O_OFF:
		ret = bprintfA(buf, "$");
		ret += bprintfA(buf, "%+d", o->imm);
		return ret;
	case O_FAR:
		return bprintfA(buf, "0x%x:0x%x", o->imm16, o->imm);
	}
	assert(0);
	return 0;
}

int
format_inst(BufA *buf, Inst *inst)
{
	int ret;
	ret = bprintfA(buf, "%s", opname[inst->op]);
	if (inst->noperand) {
		ret += bprintfA(buf, " %a", format_operand, &inst->operands[0]);
		for (int i=1; i<inst->noperand; i++) {
			ret += bprintfA(buf, ",%a",
				       format_operand, &inst->operands[i]);
		}
	}
	return ret;
}
