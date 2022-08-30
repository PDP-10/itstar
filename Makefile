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

UNAME != uname
-include $(UNAME).conf

itstar: itstar.o dirlst.o pack.o tapeio.o tm03.o unpack.o zopen.o
	cc -g -o itstar itstar.o dirlst.o pack.o tapeio.o \
		tm03.o unpack.o zopen.o $(LIBS)
	#strip itstar

.c.o: itstar.h
	cc -g -O -c $<

tapeio.o: tapeio.c itstar.h tapsrv.h
	cc -g -O -c tapeio.c

clean:
	-rm *.o itstar
