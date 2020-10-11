buffer.o: buffer.c u.h printf.h buffer.h winutil.h
luatk.o: luatk.c u.h printf.h winutil.h unicode.h
luatk_test.o: luatk_test.c u.h printf.h winutil.h unicode.h luatk.h
main.o: main.c u.h printf.h buffer.h tree.h unicode.h resource.h \
 monoedit.h treelistview.h winutil.h luatk.h
monoedit.o: monoedit.c u.h printf.h monoedit.h
printf.o: printf.c
tree.o: tree.c u.h printf.h tree.h
treelistview.o: treelistview.c u.h printf.h treelistview.h
treeviewtest.o: treeviewtest.c u.h printf.h treelistview.h
u.o: u.c u.h printf.h printf.c
unicode.o: unicode.c u.h printf.h unicode.h
whex_lua.o: whex_lua.c u.h printf.h buffer.h tree.h
winutil.o: winutil.c u.h printf.h winutil.h
