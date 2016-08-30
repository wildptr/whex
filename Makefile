CC = gcc

whex.exe: monoedit.o main.o
	$(CC) -o $@ $^ -lgdi32
