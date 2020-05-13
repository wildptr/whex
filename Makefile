# Makefile for MinGW toolchain

LUA_DIR := E:/lua

CC := gcc
CFLAGS := -std=c99 -Wall -Wno-parentheses -I$(LUA_DIR)/include -D_WIN32_IE=0x0400 -DUNICODE

.PHONY: all
all: whex.exe

depend.mk:
	gcc -MM *.c > $@

include depend.mk

OBJS := buffer.o luatk.o main.o monoedit.o tree.o u.o unicode.o whex_lua.o winutil.o res.o treelistview.o

res.o: res.rc resource.h
	windres -o $@ $<

whex.exe: $(OBJS)
	gcc -static-libgcc -o $@ $(OBJS) -lgdi32 -luser32 -lkernel32 -lcomctl32 -lcomdlg32 -L$(LUA_DIR)/lib -llua

treeviewtest.exe: treeviewtest.o u.o treelistview.o
	gcc -static-libgcc -o $@ $^ -lgdi32 -luser32 -lkernel32 -lcomctl32

luatk_test.exe: luatk_test.o u.o unicode.o luatk.o
	gcc -static-libgcc -o $@ $^ -luser32 -lkernel32 -L$(LUA_DIR)/lib -llua
