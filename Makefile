# Makefile for MinGW toolchain

CC := gcc
CFLAGS := -g -std=c99 -Wall -Wno-parentheses -Ilua -D_WIN32_IE=0x0400 -DUNICODE -D_WIN32_WINNT=0x0500

.PHONY: all
all: whex.exe

depend.mk:
	gcc -MM *.c > $@

include depend.mk

OBJS := buffer.o luatk.o main.o monoedit.o tree.o u.o unicode.o whex_lua.o winutil.o res.o treelistview.o

res.o: res.rc resource.h
	windres -o $@ $<

whex.exe: $(OBJS)
	gcc -o $@ $(OBJS) -lgdi32 -luser32 -lkernel32 -lcomctl32 -lcomdlg32 -Llua -llua

treeviewtest.exe: treeviewtest.o u.o treelistview.o
	gcc -o $@ $^ -lgdi32 -luser32 -lkernel32 -lcomctl32

luatk_test.exe: luatk_test.o u.o unicode.o luatk.o
	gcc -o $@ $^ -luser32 -lkernel32 -Llua -llua

EDIT_OBJS := newedit.o u.o winutil.o

edit.exe: $(EDIT_OBJS)
	gcc -o $@ $(EDIT_OBJS) -luser32 -lkernel32 -lgdi32 -lcomdlg32
