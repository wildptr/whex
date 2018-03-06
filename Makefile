# configuration variables
include config.mk

CFLAGS += -std=c99 -Wall -I$(LUA_DIR)/include

whex.exe: monoedit.o lua_api.o tree.o main.o unicode.o arena.o
	$(CC) -o $@ -mwindows $^ -lgdi32 -lcomdlg32 -lcomctl32 -L$(LUA_DIR)/lib -llua

main.o: main.c mainwindow.h monoedit.h tree.h unicode.h
lua_api.o: lua_api.c mainwindow.h tree.h
tree.o: tree.c tree.h
unicode.o: unicode.c unicode.h
