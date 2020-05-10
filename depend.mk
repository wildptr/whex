buffer.o: buffer.c u.h printf.h buffer.h winutil.h
luatk.o: luatk.c u.h printf.h unicode.h monoedit.h
main.o: main.c u.h printf.h buffer.h tree.h unicode.h \
 resource.h monoedit.h winutil.h
monoedit.o: monoedit.c u.h printf.h monoedit.h
tree.o: tree.c u.h printf.h tree.h
u.o: u.c u.h printf.h printf.c
unicode.o: unicode.c u.h printf.h unicode.h
whex_lua.o: whex_lua.c u.h printf.h buffer.h tree.h
winutil.o: winutil.c u.h printf.h winutil.h
