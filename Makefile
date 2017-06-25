# configuration variables
include config.mk

CFLAGS := -std=c99 -Wall -I$(LUA_DIR)/include -DDEBUG

whex.exe: monoedit.o lua_api.o tree.o main.o
	$(CC) -o $@ $^ -lgdi32 -lcomdlg32 -lcomctl32 -L$(LUA_DIR)/lib -llua

main.o: main.c mainwindow.h tree.h
lua_api.o: lua_api.c mainwindow.h
tree.o: tree.c tree.h
