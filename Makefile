itstar: itstar.o dirlst.o pack.o tapeio.o tm03.o unpack.o zopen.o
	cc -o itstar itstar.o dirlst.o pack.o tapeio.o tm03.o unpack.o zopen.o
	strip itstar

itstar.o: itstar.c
	cc -O -c itstar.c

dirlst.o: dirlst.c
	cc -O -c dirlst.c

pack.o: pack.c
	cc -O -c pack.c

tapeio.o: tapeio.c tapsrv.h
	cc -O -c tapeio.c

tm03.o: tm03.c
	cc -O -c tm03.c

unpack.o: unpack.c
	cc -O -c unpack.c

zopen.o: zopen.c
	cc -O -c zopen.c

clean:
	rm *.o itstar
