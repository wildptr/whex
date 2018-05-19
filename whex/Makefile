LUA_DIR := ../lua/lua-5.3.4/src
CC := gcc
CFLAGS := -std=gnu99 -Wall -I$(LUA_DIR)

whex.exe: monoedit.o lua_api.o tree.o main.o unicode.o region.o list.o buffer.o res.o printf.o luatk.o winutil.o
	$(CC) -mwindows -o $@ $^ -lgdi32 -lcomdlg32 -lcomctl32 -L$(LUA_DIR) -llua

luatk.exe: luatk.o luatk_main.o winutil.o
	$(CC) -mwindows -o $@ $^ -lgdi32 -lcomdlg32 -lcomctl32 -L$(LUA_DIR) -llua

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
	rm -f whex.exe luatk.exe *.o
