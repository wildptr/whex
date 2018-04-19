# configuration variables
include config.mk

CFLAGS += -std=gnu99 -Wall -I$(LUA_DIR)/include

whex.exe: monoedit.o lua_api.o tree.o main.o unicode.o region.o list.o buffer.o res.o printf.o luatk.o
	$(CC) -o $@ -mwindows $^ -lgdi32 -lcomdlg32 -lcomctl32 -L$(LUA_DIR)/lib -llua

list.o: list.c util.h
lua_api.o: lua_api.c buffer.h tree.h
main.o: main.c buffer.h ui.h monoedit.h tree.h unicode.h resource.h printf.h
region.o: region.c util.h
tree.o: tree.c util.h tree.h
unicode.o: unicode.c util.h unicode.h
buffer.o: buffer.c util.h buffer.h
res.o: res.rc resource.h
	windres -O coff $< $@

.PHONY: clean
clean:
	rm -f whex.exe *.o
