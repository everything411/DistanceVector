CC=/usr/bin/gcc
main: main.c
	${CC} -O2 -g -o main.exe main.c error.c wrapstdio.c wrapsock.c wrapunix.c wraplib.c wrappthread.c -lpthread
clean:
	rm main.exe
