enum {
	MODE_16,
	MODE_32,
};

typedef struct {
	uchar mode;
	Region *rgn;
} DisasmConfig;

int disasm(const uchar *p, Inst *inst, DisasmConfig *conf);
