#define _CRT_SECURE_NO_WARNINGS

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define USE_FILEBUF
#include "util.h"
#include "inst.h"
#include "dasm.h"
#include "buf.h"

int main(int argc, char **argv)
{
	FILE *fp = fopen(argv[1], "rb");
	if (!fp) {
		fprintf(stderr, "cannot open file\n");
		return 1;
	}
	fseek(fp, 0, SEEK_END);
	unsigned long len = ftell(fp);
	fseek(fp, 0, SEEK_SET);
	uchar *data = malloc(len+16);
	if (fread(data, 1, len, fp) < len) {
		fprintf(stderr, "error reading file\n");
		return 1;
	}
	uchar *end = data + len;
	memset(end, 0, 16);
	uchar *p = data;
	Region rgn;
	DisasmConfig conf = {0};
	conf.rgn = &rgn;
	Inst inst;
	FileBuf buf;
	init_filebuf(&buf, stdout);
	while (p < end) {
		p += disasm(p, &inst, &conf);
		bprintf(&buf.buf, "%a\n", format_inst, &inst);
	}
	return 0;
}
