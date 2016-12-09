# This file is part of itstar.
#
# itstar is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# itstar is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with itstar.  If not, see <http://www.gnu.org/licenses/>.

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
