#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <tchar.h>

#include "types.h"
#include "buf.h"
#include "inst.h"
#include "region.h"
#include "util.h"
#include "dasm.h"

#define HAS_REGMEM 8

typedef struct {
	uchar prefix;
	uchar opcode[3];
} PrefixedOpcode;

typedef struct {
	/* index and base are raw codes rather than R_XX enum */
	uchar scale, index, base;
} SIB;

static const uchar inst_format_table[256] = {
	010,010,010,010,001,002,000,000,010,010,010,010,001,002,000,000,
	010,010,010,010,001,002,000,000,010,010,010,010,001,002,000,000,
	010,010,010,010,001,002,000,000,010,010,010,010,001,002,000,000,
	010,010,010,010,001,002,000,000,010,010,010,010,001,002,000,000,
	000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,
	000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,
	000,000,010,010,000,000,000,000,002,012,001,011,000,000,000,000,
	001,001,001,001,001,001,001,001,001,001,001,001,001,001,001,001,
	011,012,011,011,010,010,010,010,010,010,010,010,010,010,010,010,
	000,000,000,000,000,000,000,000,000,000,005,000,000,000,000,000,
	006,006,006,006,000,000,000,000,001,002,000,000,000,000,000,000,
	001,001,001,001,001,001,001,001,002,002,002,002,002,002,002,002,
	011,011,003,000,010,010,011,012,004,000,003,000,000,001,000,000,
	010,010,010,010,001,001,000,000,010,010,010,010,010,010,010,010,
	001,001,001,001,001,001,001,001,002,002,005,001,000,000,000,000,
	000,000,000,000,000,000,010,010,000,000,000,000,000,000,010,010,
};

static const uchar inst_format_table_0f[256] = {
	000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,
	010,010,000,000,000,000,000,000,000,000,000,000,000,000,000,000,
	000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,
	000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,
	000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,
	000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,
	000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,
	000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,
	002,002,002,002,002,002,002,002,002,002,002,002,002,002,002,002,
	010,010,010,010,010,010,010,010,010,010,010,010,010,010,010,010,
	000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,010,
	000,000,000,000,000,000,010,010,000,000,000,000,000,000,010,010,
	000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,
	000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,
	000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,
	000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,
};

enum {
	G_80, G_8F, G_C0, G_C6, G_F6, G_FE, G_FF
};
static const short optable[256] = {
	I_ADD,	I_ADD,	I_ADD,	I_ADD,	I_ADD,	I_ADD,	I_PUSH,	I_POP,
	I_OR,	I_OR,	I_OR,	I_OR,	I_OR,	I_OR,	I_PUSH,	0,
	I_ADC,	I_ADC,	I_ADC,	I_ADC,	I_ADC,	I_ADC,	I_PUSH,	I_POP,
	I_SBB,	I_SBB,	I_SBB,	I_SBB,	I_SBB,	I_SBB,	I_PUSH,	I_POP,
	I_AND,	I_AND,	I_AND,	I_AND,	I_AND,	I_AND,	0,	I_DAA,
	I_SUB,	I_SUB,	I_SUB,	I_SUB,	I_SUB,	I_SUB,	0,	I_DAS,
	I_XOR,	I_XOR,	I_XOR,	I_XOR,	I_XOR,	I_XOR,	0,	I_AAA,
	I_CMP,	I_CMP,	I_CMP,	I_CMP,	I_CMP,	I_CMP,	0,	I_AAS,
	I_INC,	I_INC,	I_INC,	I_INC,	I_INC,	I_INC,	I_INC,	I_INC,
	I_DEC,	I_DEC,	I_DEC,	I_DEC,	I_DEC,	I_DEC,	I_DEC,	I_DEC,
	I_PUSH,	I_PUSH,	I_PUSH,	I_PUSH,	I_PUSH,	I_PUSH,	I_PUSH,	I_PUSH,
	I_POP,	I_POP,	I_POP,	I_POP,	I_POP,	I_POP,	I_POP,	I_POP,
	I_PUSHA,I_POPA,	I_BOUND,I_ARPL,	0,	0,	0,	0,
	I_PUSH,	I_IMUL,	I_PUSH,	I_IMUL,	I_INS,	I_INS,	I_OUTS,	I_OUTS,
	I_CJMP,	I_CJMP,	I_CJMP,	I_CJMP,	I_CJMP,	I_CJMP,	I_CJMP,	I_CJMP,
	I_CJMP,	I_CJMP,	I_CJMP,	I_CJMP,	I_CJMP,	I_CJMP,	I_CJMP,	I_CJMP,
	~G_80,	~G_80,	~G_80,	~G_80,	I_TEST,	I_TEST,	I_XCHG,	I_XCHG,
	I_MOV,	I_MOV,	I_MOV,	I_MOV,	I_MOV,	I_LEA,	I_MOV,	~G_8F,
	I_XCHG,	I_XCHG,	I_XCHG,	I_XCHG,	I_XCHG,	I_XCHG,	I_XCHG,	I_XCHG,
	I_CBW,	I_CWD,	I_CALLF,I_WAIT,	I_PUSHF,I_POPF,	I_SAHF,	I_LAHF,
	I_MOV,	I_MOV,	I_MOV,	I_MOV,	I_MOVS,	I_MOVS,	I_CMPS,	I_CMPS,
	I_TEST,	I_TEST,	I_STOS,	I_STOS,	I_LODS,	I_LODS,	I_SCAS,	I_SCAS,
	I_MOV,	I_MOV,	I_MOV,	I_MOV,	I_MOV,	I_MOV,	I_MOV,	I_MOV,
	I_MOV,	I_MOV,	I_MOV,	I_MOV,	I_MOV,	I_MOV,	I_MOV,	I_MOV,
	~G_C0,	~G_C0,	I_RETN,	I_RET,	I_LES,	I_LDS,	~G_C6,	~G_C6,
	I_ENTER,I_LEAVE,I_RETNF,I_RETF,	I_INT3,	I_INT,	I_INTO,	I_IRET,
	~G_C0,	~G_C0,	~G_C0,	~G_C0,	I_AAM,	I_AAD,	0,	I_XLAT,
	0,	0,	0,	0,	0,	0,	0,	0,
	I_LOOPNZ,I_LOOPZ,I_LOOP,I_JCXZ,	I_IN,	I_IN,	I_OUT,	I_OUT,
	I_CALL,	I_JMP,	I_JMPF,	I_JMP,	I_IN,	I_IN,	I_OUT,	I_OUT,
	0,	I_INT1,	0,	0,	I_HLT,	I_CMC,	~G_F6,	~G_F6,
	I_CLC,	I_STC,	I_CLI,	I_STI,	I_CLD,	I_STD,	~G_FE,	~G_FF,
};

static const short optable_ext[] = {
	I_ADD,	I_OR,	I_ADC,	I_SBB,	I_AND,	I_SUB,	I_XOR,	I_CMP,
	I_POP,	0,	0,	0,	0,	0,	0,	0,
	I_ROL,	I_ROR,	I_RCL,	I_RCR,	I_SHL,	I_SHR,	I_SHL,	I_SAR,
	I_MOV,	0,	0,	0,	0,	0,	0,	0,
	I_TEST,	0,	I_NOT,	I_NEG,	I_MUL,	I_IMUL,	I_DIV,	I_IDIV,
	I_INC,	I_DEC,	0,	0,	0,	0,	0,	0,
	I_INC,	I_DEC,	I_CALL,	I_CALLF,I_JMP,	I_JMPF,	I_PUSH,	0,
};

static const uchar seg_reg_table[6] = {
	R_ES, R_CS, R_SS, R_DS, R_FS, R_GS
};

static const char vartable[256] = {
	 0,-1, 0,-1, 0,-1,-1,-1, 0,-1, 0,-1, 0,-1,-1,-1,
	 0,-1, 0,-1, 0,-1,-1,-1, 0,-1, 0,-1, 0,-1,-1,-1,
	 0,-1, 0,-1, 0,-1,-1,-1, 0,-1, 0,-1, 0,-1,-1,-1,
	 0,-1, 0,-1, 0,-1,-1,-1, 0,-1, 0,-1, 0,-1,-1,-1,
	-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
	-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
	-1,-1,-1,-1, 0, 0, 0, 0,-1,-1,-1,-1, 0,-1, 0,-1,
	 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,15,
	 0,-1, 0,-1, 0,-1, 0,-1, 0,-1, 0,-1,-1,-1,-1,-1,
	-1,-1,-1,-1,-1,-1,-1,-1, 0,-1,-1,-1,-1,-1, 0, 0,
	 0,-1, 0,-1, 0,-1, 0,-1, 0,-1, 0,-1, 0,-1, 0,-1,
	 0, 0, 0, 0, 0, 0, 0, 0,-1,-1,-1,-1,-1,-1,-1,-1,
	 0,-1,-1,-1,-1,-1, 0,-1, 0,-1,-1,-1, 0, 0, 0,-1,
	 0,-1, 0,-1, 0, 0, 0,-1, 0, 0, 0, 0, 0, 0, 0, 0,
	 0, 0, 0,-1, 0,-1, 0,-1,-1,-1, 0,-1, 0,-1, 0,-1,
	 0, 0, 0, 0, 0, 0, 0,-1, 0, 0, 0, 0, 0, 0, 0,-1,
};

enum { X=1, I, I2, O, R, R0, RO, RM, M };

static const char operand_table[256][2] = {
	{RM,R},	{RM,R},	{R,RM},	{R,RM},	{R0,I},	{R0,I},	{X},	{X},
	{RM,R},	{RM,R},	{R,RM},	{R,RM},	{R0,I},	{R0,I},	{X},	{0},
	{RM,R},	{RM,R},	{R,RM},	{R,RM},	{R0,I},	{R0,I},	{X},	{X},
	{RM,R},	{RM,R},	{R,RM},	{R,RM},	{R0,I},	{R0,I},	{X},	{X},
	{RM,R},	{RM,R},	{R,RM},	{R,RM},	{R0,I},	{R0,I},	{X},	{X},
	{RM,R},	{RM,R},	{R,RM},	{R,RM},	{R0,I},	{R0,I},	{X},	{X},
	{RM,R},	{RM,R},	{R,RM},	{R,RM},	{R0,I},	{R0,I},	{X},	{X},
	{RM,R},	{RM,R},	{R,RM},	{R,RM},	{R0,I},	{R0,I},	{X},	{X},
	{RO},	{RO},	{RO},	{RO},	{RO},	{RO},	{RO},	{RO},
	{RO},	{RO},	{RO},	{RO},	{RO},	{RO},	{RO},	{RO},
	{RO},	{RO},	{RO},	{RO},	{RO},	{RO},	{RO},	{RO},
	{RO},	{RO},	{RO},	{RO},	{RO},	{RO},	{RO},	{RO},
	{0},	{0},	{R,M},	{RM,R},	{0},	{0},	{0},	{0},
	{I},	{R,RM},	{I},	{R,RM},	{0},	{0},	{0},	{0},
	{O},	{O},	{O},	{O},	{O},	{O},	{O},	{O},
	{O},	{O},	{O},	{O},	{O},	{O},	{O},	{O},
	{RM,I},	{RM,I},	{RM,I},	{RM,I},	{RM,I},	{RM,I},	{RM,I},	{RM,I},
	{RM,R},	{RM,R},	{R,RM},	{R,RM},	{RM,X},	{R,M},	{X,RM},	{RM},
	{R0,RO},{R0,RO},{R0,RO},{R0,RO},{R0,RO},{R0,RO},{R0,RO},{R0,RO},
	{0},	{0},	{I,I2},	{0},	{0},	{0},	{0},	{0},
	{R0,RM},{R0,RM},{RM,R0},{RM,R0},{0},	{0},	{0},	{0},
	{R0,I},	{R0,I},	{0},	{0},	{0},	{0},	{0},	{0},
	{RO,I},	{RO,I},	{RO,I},	{RO,I},	{RO,I},	{RO,I},	{RO,I},	{RO,I},
	{RO,I},	{RO,I},	{RO,I},	{RO,I},	{RO,I},	{RO,I},	{RO,I},	{RO,I},
	{RM,I},	{RM,I},	{I},	{0},	{R,M},	{R,M},	{RM,I},	{RM,I},
	{0},	{0},	{I},	{0},	{0},	{I},	{0},	{0},
	{RM,X},	{RM,X},	{RM,X},	{RM,X},	{I},	{I},	{0},	{0},
	{0},	{0},	{0},	{0},	{0},	{0},	{0},	{0},
	{O},	{O},	{O},	{O},	{R0,I},	{R0,I},	{R0,I},	{R0,I},
	{O},	{O},	{I,I2},	{O},	{R0,X},	{R0,X},	{R0,X},	{R0,X},
	{0},	{0},	{0},	{0},	{0},	{0},	{-1},	{-1},
	{0},	{0},	{0},	{0},	{0},	{0},	{-2},	{-2},
};

static const char operand_table_ext[][2] = {
	{RM},	{0},	{RM,I},	{RM,I},	{RM,I},	{RM,I},	{RM,I},	{RM,I},
	{RM},	{RM},	{RM},	{M},	{RM},	{M},	{RM},	{0},
};

int read_prefixed_opcode(const uchar *p, PrefixedOpcode *po);
int read_regmem32(const uchar *p, Operand *o);
int read_imm1(const uchar *p, int32 *);
int read_imm2(const uchar *p, int32 *);
int read_imm4(const uchar *p, int32 *);
int read_sib(const uchar *p, SIB *);
uchar addr32reg(uchar r);
uchar defaultseg(uchar r);
void convert_inst(PrefixedOpcode *po, Operand *o, int32 *imm, Inst *inst,
		  DisasmConfig *conf);
uint instfmt1(uchar opcode, uchar ext, uchar lw, ushort fmt[3]);
uchar logwordsize(uchar mode, uchar prefix);
uchar getreg(uchar r, uchar size);

uchar
prefix_code(uchar b)
{
	switch (b) {
	case 0x26: return 4;
	case 0x2e: return 8;
	case 0x36: return 12;
	case 0x3e: return 16;
	case 0x64: return 20;
	case 0x65: return 24;
	case 0x66: return 32;
	case 0x67: return 64;
	case 0xf0: return 1;
	case 0xf2: return 2;
	case 0xf3: return 3;
	}
	return 0;
}

#define HAS_PREFIX_66(x) ((x)&32)
#define HAS_PREFIX_67(x) ((x)&64)

int
read_prefixed_opcode(const uchar *p, PrefixedOpcode *po)
{
	const uchar *start = p;
	po->prefix = 0;
	for (;;) {
		uchar pfx = prefix_code(*p);
		if (!pfx) break;
		po->prefix |= pfx;
		p++;
	}
	uchar b;
	po->opcode[0] = b = *p++;
	if (b == 0x0f) {
		po->opcode[1] = *p++;
	}
	return p-start;
}

int
disasm(const uchar *p, Inst *inst, DisasmConfig *conf)
{
	const uchar *start = p;
	PrefixedOpcode po;
	p += read_prefixed_opcode(p, &po);
	uchar fmt;
	uchar opcode1 = po.opcode[0];
	if (opcode1 == 0x0f) {
		fmt = inst_format_table[po.opcode[1]];
	} else {
		fmt = inst_format_table[opcode1];
	}

	Operand o = {0};
	int32 imm[2] = {0};

	/* read regmem operand */
	if (fmt & HAS_REGMEM) {
		p += read_regmem32(p, &o);
		o.fmt = HAS_REGMEM;
	}

	uchar lw = logwordsize(conf->mode, po.prefix);
	int (*read_imm)(const uchar *, int32 *);
	switch (lw) {
	case 1: read_imm = read_imm2; break;
	case 2: read_imm = read_imm4; break;
	default: assert(0);
	}

	/* read immediates */
	switch (opcode1) {
	case 0xd8: case 0xd9: case 0xda: case 0xdb:
	case 0xdc: case 0xdd: case 0xde: case 0xdf:
		TODO();
		break;
	case 0xf6: case 0xf7:
		if (o.opcode_ext == 0) {
			o.fmt |= 1;
			if (opcode1&1) {
				p += read_imm(p, imm);
			} else {
				p += read_imm1(p, imm);
			}
		}
		break;
	default:
		switch (fmt&7) {
		case 0:
			break;
		case 1:
			o.fmt |= 1;
			p += read_imm1(p, imm);
			break;
		case 2:
			o.fmt |= 1;
			p += read_imm(p, imm);
			break;
		case 3:
			o.fmt |= 1;
			p += read_imm2(p, imm);
			break;
		case 4:
			o.fmt |= 2;
			p += read_imm2(p, imm);
			p += read_imm1(p, imm+1);
			break;
		case 5:
			o.fmt |= 2;
			p += read_imm2(p, imm);
			p += read_imm(p, imm+1);
			break;
		case 6:
			o.fmt = HAS_REGMEM;
			o.kind = O_MEM;
			o.size = 0;
			o.base = NOREG;
			o.index = NOREG;
			o.scale = 0;
			o.seg = R_DS;
			p += read_imm4(p, &o.imm);
			break;
		default:
			assert(0);
		}
	}

	int len = p-start;
	assert(len < 256);
	uchar *bytes = ralloc(conf->rgn, len);
	memcpy(bytes, start, len);

	inst->len = len;
	inst->bytes = bytes;

	convert_inst(&po, &o, imm, inst, conf);

	return p-start;
}

int
read_regmem32(const uchar *p, Operand *o)
{
	const uchar *start = p;
	uchar modrm = *p++;
	uchar r = modrm & 7;
	uchar m = modrm >> 6; /* ModRM[7:6] -- mode field */
	o->opcode_ext = (modrm >> 3) & 7;
	switch (m) {
	case 0:
		o->kind = O_MEM;
		o->size = 0;
		switch (r) {
			SIB sib;
			int32 disp;
		case 4: /* SIB follows */
			p += read_sib(p, &sib);
			if (sib.base == 5) {
				/* 4-byte disp, no base reg */
				p += read_imm4(p, &disp);
				o->base = NOREG;
				o->index = addr32reg(sib.index);
				o->scale = sib.scale;
				o->seg = R_DS;
				o->disp = disp;
			} else {
				o->base = addr32reg(sib.base);
				o->index = addr32reg(sib.index);
				o->scale = sib.scale;
				o->seg = defaultseg(sib.base);
				o->disp = 0;
			}
			break;
		case 5: /* 4-byte disp, no base reg, no index */
			p += read_imm4(p, &disp);
			o->base = NOREG;
			o->index = NOREG;
			o->scale = 0;
			o->seg = R_DS;
			o->disp = disp;
			break;
		default:
			o->base = addr32reg(r);
			o->index = NOREG;
			o->scale = 0;
			o->seg = defaultseg(r);
			o->disp = 0;
		}
		break;
	case 1:
	case 2:
		o->kind = O_MEM;
		o->size = 0;
		int32 disp = 0;
		int (*read_disp)(const uchar *, int32 *) =
			m == 1 ? read_imm1 : read_imm2;
		if (r == 4) {
			/* SIB follows */
			SIB sib;
			p += read_sib(p, &sib);
			p += read_disp(p, &disp);
			o->base = addr32reg(sib.base);
			o->index = addr32reg(sib.index);
			o->scale = sib.scale;
			o->seg = defaultseg(sib.base);
			o->disp = disp;
		} else {
			p += read_disp(p, &disp);
			o->base = addr32reg(r);
			o->index = NOREG;
			o->scale = 0;
			o->seg = defaultseg(r);
			o->disp = disp;
		}
		break;
	case 3:
		o->kind = O_REG;
		o->reg = addr32reg(r);
		o->size = regsize(o->reg);
		break;
	default:
		assert(0);
	}
	return p-start;
}

int
read_sib(const uchar *p, SIB *o)
{
	uchar sib = *p++;
	uchar s = sib >> 6;
	uchar i = (sib >> 3) & 7;
	uchar b = sib & 7;
	o->base = b;
	if (i == 4) {
		o->index = NOREG;
		o->scale = 0;
	} else {
		o->index = i;
		o->scale = 1 << s;
	}
	return 1;
}

uchar
addr32reg(uchar r)
{
	static const uchar tab[8] = {
		R_EAX, R_ECX, R_EDX, R_EBX, R_ESP, R_EBP, R_ESI, R_EDI
	};
	if (r < 8) return tab[r];
	return NOREG;
}

uchar
defaultseg(uchar r)
{
	switch (r) {
	case 0: case 1: case 2: case 3: case 6: case 7:
		return R_DS;
	case 4: case 5:
		return R_SS;
	}
	assert(0);
	return 0;
}

enum {
	F_I1=1,
	F_I2,
	F_I,
	F_R,
	F_RM,
	F_O,
};

void
convert_inst(PrefixedOpcode *po, Operand *rm, int32 *imm, Inst *inst,
	     DisasmConfig *conf)
{
	uchar lw = logwordsize(conf->mode, po->prefix);

	ushort fmt[3];
	int op_var = instfmt1(po->opcode[0], rm->opcode_ext, lw, fmt);
	if (!op_var) {
		/* invalid instruction */
		inst->op = 0;
		inst->var = 0;
		inst->noperand = 0;
		inst->operands = 0;
		return;
	}

	int no=0;
	while (fmt[no]) no++;
	Operand *ov = ralloc0(conf->rgn, no * sizeof *ov);
	Operand *o = ov;
	for (int i=0; i<no; i++) {
		uchar tag = fmt[i]>>8;
		uchar data = fmt[i]&0xff;
		switch (tag) {
		case F_I1:
			o->kind = O_IMM;
			o->size = (uchar) op_var;
			o->imm = imm[0];
			break;
		case F_I2:
			o->kind = O_IMM;
			o->size = (uchar) op_var;
			o->imm = imm[1];
			break;
		case F_I:
			o->kind = O_IMM;
			o->size = (uchar) op_var;
			o->imm = data;
			break;
		case F_R:
			o->kind = O_REG;
			o->size = regsize(data);
			o->reg = data;
			break;
		case F_RM:
			*o = *rm;
			break;
		case F_O:
			o->kind = O_OFF;
			o->size = (uchar) op_var;
			o->imm = imm[0];
			break;
		default:
			assert(0);
		}
		o++;
	}

	inst->op = op_var >> 8;
	inst->var = (uchar) op_var;
	inst->noperand = no;
	inst->operands = ov;
}

#define FMT(f,x) (((f)<<8)|x)

uint
instfmt1(uchar opcode, uchar ext, uchar lw, ushort fmt[3])
{
	short op = optable[opcode];
	uchar s;

	if (op < 0) {
		op = optable_ext[(~op<<3)|ext];
	}
	if (!op) return 0;

	char var = vartable[opcode];
	if (var < 0) var = lw;
	s = 1<<var;

	char argspec[3];
	const char *p = &operand_table[opcode][0];
	argspec[0] = p[0];
	if (argspec[0] < 0) {
		int i = ~argspec[0];
		p = &operand_table_ext[(i<<3)|ext][0];
		argspec[0] = p[0];
	}
	argspec[1] = p[1];
	argspec[2] = 0;

	ushort xfmt = 0;

	switch (opcode) {
	case 0x06: case 0x07: case 0x0e:
	case 0x16: case 0x17: case 0x1e: case 0x1f:
		xfmt = FMT(F_R, seg_reg_table[opcode>>3]);
		break;
	case 0x69: case 0x6b:
		fmt[2] = FMT(F_I1, s);
		break;
	case 0x8c: case 0x8e:
		if (ext >= 6) return 0;
		xfmt = FMT(F_R, seg_reg_table[ext]);
		if (ext >= 6) return 0;
		xfmt = FMT(F_R, seg_reg_table[ext]);
		break;
	case 0xd0: case 0xd1:
		xfmt = FMT(F_I, 1);
		break;
	case 0xd2: case 0xd3:
		xfmt = FMT(F_R, R_CL);
		break;
	case 0xd8: case 0xd9: case 0xda: case 0xdb:
	case 0xdc: case 0xdd: case 0xde: case 0xdf:
		TODO();
		break;
	case 0xec: case 0xed: case 0xee: case 0xef:
		xfmt = FMT(F_R, R_DX);
		break;
	}

	for (int i=0; i<3; i++) {
		switch (argspec[i]) {
		case 0:
			fmt[i] = 0;
			break;
		case X:
			assert(xfmt);
			fmt[i] = xfmt;
			break;
		case I:
			fmt[i] = FMT(F_I1, 0);
			break;
		case O:
			fmt[i] = FMT(F_O, 0);
			break;
		case I2:
			fmt[i] = FMT(F_I2, 0);
			break;
		case R:
			fmt[i] = FMT(F_R, getreg(ext, s));
			break;
		case R0:
			fmt[i] = FMT(F_R, getreg(0, s));
			break;
		case RO:
			fmt[i] = FMT(F_R, getreg(opcode&7, s));
			break;
		case RM:
			fmt[i] = FMT(F_RM, s);
			break;
		case M:
			fmt[i] = FMT(F_RM, 0);
			break;
		}
	}

	return (((int) op) << 8) | (uchar) var;
}

uchar
logwordsize(uchar mode, uchar prefix)
{
	switch (mode) {
	case MODE_16:
		return HAS_PREFIX_66(prefix) ? 2 : 1;
	case MODE_32:
		return HAS_PREFIX_66(prefix) ? 1 : 2;
	default:
		assert(0);
	}
	return 0;
}

uchar
getreg(uchar r, uchar size)
{
	switch (size) {
	case 1: return R_AL+r;
	case 2: return R_AX+r;
	case 4: return R_EAX+r;
	default: assert(0);
	}
	return 0;
}

uchar
regsize(uchar reg)
{
	static const uchar tab[] = {
		1,1,1,1,1,1,1,1,
		2,2,2,2,2,2,2,2,
		4,4,4,4,4,4,4,4,
		10,10,10,10,10,10,10,10,
		16,16,16,16,16,16,16,16,
		2,2,2,2,2,2,
		1,1,1,1,1,1,1
	};
	assert(reg < NELEM(tab));
	return tab[reg];
}

int
read_imm1(const uchar *p, int32 *imm)
{
	*imm = *(char *) p;
	return 1;
}

int
read_imm2(const uchar *p, int32 *imm)
{
	*imm = *(short *) p;
	return 2;
}

int
read_imm4(const uchar *p, int32 *imm)
{
	*imm = *(int *) p;
	return 4;
}
