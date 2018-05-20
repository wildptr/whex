#define DEF(x) R_##x
enum {
#include "reg.in"
};
#undef DEF

#define NOREG 0xff

enum {
	O_INV,
	O_REG,
	O_MEM,
	O_IMM,
	O_OFF,
	O_FAR,
};

typedef struct {
	uchar kind;
	uchar size;
	uchar opcode_ext;
	uchar fmt;
	union {
		uchar reg;
		struct {
			uchar base;
			uchar index;
			uchar scale;
			uchar seg;
		};
		ushort imm16;
	};
	union { int32 imm, disp; };
} Operand;

#define DEF(x) I_##x
enum {
	INVALID_INSTRUCTION,
#include "inst.in"
};
#undef DEF

typedef struct {
	uchar len;
	uchar op;
	uchar var;
	uchar noperand;
	uchar *bytes;
	Operand *operands;
} Inst;

extern const char *opname[];
extern const char *regname[];

uchar regsize(uchar reg); /* in bytes */
int format_inst(BufA *, Inst *);
