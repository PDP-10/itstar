/*

  Pack an ITS file into a WEENIX file using Alan Bawden's evacuated file format.

  By John Wilson.

  07/15/1998  JMBW  Created.

*/

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <string.h>

/* macro to send one byte to output file */
#define outbyte(c) outbuf[outcnt++]=c
/* macro to flush byte stored in PREV after we discover sequence won't work */
/* prev char (015 or 177) turned out to not be part of a sequence after all */
#define flushprev() if(prev==015) outbyte(0356);\
        else if(prev==0177) outbyte(0357);\
	prev=0;

static FILE *out;
static void outword();
static int outcnt;	/* # chars saved in OUTBUF */
static char outbuf[5+1];  /* chars written since last word boundary */
			/* +1 is for char from PREV */

/*
 
Message: 2881200, 91 lines
Posted: 4:07pm EDT, Thu May 14/92, imported: 4:05pm EDT, Thu May 14/92
Subject: ITS filesystems
To: John Wilson, bruce@think.com
From: alan%ai.mit.edu@life.ai.mit.edu

   Date: Wed, 13 May 92 21:56:02 EDT
   From: John_Wilson@mts.rpi.edu
   ...
   I have a question -- how are the 36-bit words packed into 8-bit bytes?
   ...

   From: Bruce Walker <bruce@think.com>
   Date: Wed, 13 May 92 18:17:20 EDT
   ...
   What is the representation of binary files?
   ...

-------

			   Storing 36-Bit Words
                              In 8-Bit Bytes

 Here are the details of the encoding used to store 36-bit PDP-10 words in
 8-bit byte file systems.  It is easiest to explain the encoding by
 describing how to reconstruct the 36-bit words from the 8-bit bytes.  Going
 the other direction is harder (you will see why). 

   Algorithm to decode a sequence of 8-bit bytes into a sequence of 36-bit
   words:

     Each 8-bit byte between 0 and 357 (octal) is decoded into one or two
     7-bit bytes (see table below).  These 7-bit bytes are then assembled
     into 36-bit words in the usual PDP-10 byte ordering.  The lowest order
     bit of each such word is always set to 0.

     Each 8-bit byte between 360 and 377 (octal) is combined with the next 4
     8-bit bytes to form a complete 36-bit word (see figure below).  It is
     an error to encounter such a byte when there is a partially assembled
     output word.

     If there is a partially assembled output word at the end of the
     sequence of 8-bit bytes, it is padded out with 7-bit bytes that contain
     the value 3 (control-C in ASCII).

     Here is the table for decoding bytes between 0 and 357 (all values are
     in octal):

               input byte   1st output   2nd output
               ----------   ----------   ----------
                 0 --  11     0 --  11   none
                12           15           12
                13 --  14    13 --  14   none
                15           12          none
                16 -- 176    16 -- 176   none
               177          177            7
               200 -- 206   177            0 --   6
               207          177          177
               210 -- 211   177           10 --  11
               212          177           15
               213 -- 214   177           13 --  14
               215          177           12
               216 -- 355   177           16 -- 155
               356           15          none
               357          177          none

     For bytes between 360 and 377 (octal) the 36-bit word is reconstructed
     as follows:

                byte: 000011111111222222 223333333344444444
                 bit: 321076543210765432 107654321076543210

     Where byte 0 is the current byte (the one between 360 and 377), byte 1
     is the next in sequence, and so forth.

 Going in the other direction, from 36-bit words to 8-bit bytes, is harder
 only because there are choices to be made.  For example, you can encode
 every 36-bit word using 5 bytes where the first is between 360 and 377 --
 but if you did this, files that were stored as ASCII packed in 36-bit words
 in the usual way wouldn't be readable.  A -good- encoder will produce a
 sequence of 8-bit bytes that can be read as an ordinary text file under
 Unix whenever the input words contain only PDP-10 ASCII.

 (You may be puzzled by all two byte sequences starting with 177 in the
 table above.  The explanation is that this is done to preserve not just
 ASCII files, but also the files written by the Lisp Machine system using
 the Lisp Machine character set.  But you don't really need to worry about
 this, as long as you have the above table, you know all you need to know
 about it.)

 I have a library of C routines that know how to encode and decode files in
 this format.  I'm not willing to make a widely public release of it, but if
 you really need to use it let me know.  (It is best to keep the number of
 different programs that understand the format small in order to minimize
 the chances of introducing incompatibilities.)

-------
*/

/* lookup table to convert basic ASCII characters 0-177 */
#define SPEC (unsigned char)0200
static unsigned char base[128] = {
	0000,0001,0002,0003,0004,0005,0006,0007,	/* 000 */
	0010,0011,0015,0013,0014,SPEC,0016,0017,	/* 010 */
	0020,0021,0022,0023,0024,0025,0026,0027,	/* 020 */
	0030,0031,0032,0033,0034,0035,0036,0037,	/* 030 */
	0040,0041,0042,0043,0044,0045,0046,0047,	/* 040 */
	0050,0051,0052,0053,0054,0055,0056,0057,	/* 050 */
	0060,0061,0062,0063,0064,0065,0066,0067,	/* 060 */
	0070,0071,0072,0073,0074,0075,0076,0077,	/* 070 */
	0100,0101,0102,0103,0104,0105,0106,0107,	/* 100 */
	0110,0111,0112,0113,0114,0115,0116,0117,	/* 110 */
	0120,0121,0122,0123,0124,0125,0126,0127,	/* 120 */
	0130,0131,0132,0133,0134,0135,0136,0137,	/* 130 */
	0140,0141,0142,0143,0144,0145,0146,0147,	/* 140 */
	0150,0151,0152,0153,0154,0155,0156,0157,	/* 150 */
	0160,0161,0162,0163,0164,0165,0166,0167,	/* 160 */
	0170,0171,0172,0173,0174,0175,0176,SPEC		/* 170 */
};

/* table to map 2-byte sequences prefixed with 177, to just one 8-bit byte. */
/* 0 entry means no single byte code is possible so use 357 for the 177 and */
/* use some other means for the 2nd byte. */
static unsigned char pfx177[128] = {
	0200,0201,0202,0203,0204,0205,0206,0177,	/* 000 */
	0210,0211,0215,0213,0214,0212,0216,0217,	/* 010 */
	0220,0221,0222,0223,0224,0225,0226,0227,	/* 020 */
	0230,0231,0232,0233,0234,0235,0236,0237,	/* 030 */
	0240,0241,0242,0243,0244,0245,0246,0247,	/* 040 */
	0250,0251,0252,0253,0254,0255,0256,0257,	/* 050 */
	0260,0261,0262,0263,0264,0265,0266,0267,	/* 060 */
	0270,0271,0272,0273,0274,0275,0276,0277,	/* 070 */
	0300,0301,0302,0303,0304,0305,0306,0307,	/* 100 */
	0310,0311,0312,0313,0314,0315,0316,0317,	/* 110 */
	0320,0321,0322,0323,0324,0325,0326,0327,	/* 120 */
	0330,0331,0332,0333,0334,0335,0336,0337,	/* 130 */
	0340,0341,0342,0343,0344,0345,0346,0347,	/* 140 */
	0350,0351,0352,0353,0354,0355,0000,0000,	/* 150 */
	0000,0000,0000,0000,0000,0000,0000,0000,	/* 160 */
	0000,0000,0000,0000,0000,0000,0000,0207		/* 170 */
};

/* pack tape data into WEENIX form, creating a file named FILE */
void pack(char *file)
{
	register unsigned char c, d, prev;
	register int i;
	static char inbuf[5];
	unsigned long l, r;
	char *p;

	out=fopen(file,"wb");	/* create output file */
	if(out==NULL) {
		perror(file);
		exit(1);
	}

	if((remaining()==0)&&(taperead()<0)) {
				/* read first rec for nextword() */
		fclose(out);	/* null file, we're done */
		return;
	}

	outcnt=0;		/* nothing has gone out yet */
	for(prev=0;;) {
		if(nextword(&l,&r)<0) {
			flushprev();
			break;
		}
		outword();	/* starting a new word, flush previous */
		if(r&1) {	/* b35 set => can't be ASCII */
			flushprev();
			/* pack up a quoted word */
			outbyte(0360L|((l>>14L)&0017L));
			outbyte((l>>6L)&0377L);
			outbyte(((l<<2L)&0374L)|((r>>16L)&0003L));
			outbyte((r>>8L)&0377L);
			outbyte(r&0377L);
			continue;
		}

		/* unpack word into five ASCII bytes */
		inbuf[0]=(l>>11L)&0177L;
		inbuf[1]=(l>>4L)&0177L;
		inbuf[2]=((l<<3L)&0170L)|((r>>15L)&0007L);
		inbuf[3]=(r>>8L)&0177L;
		inbuf[4]=(r>>1L)&0177L;

		/* process each byte */
		for(p=inbuf,i=sizeof(inbuf);i--;) {
			c=(*p++);
			if(prev) {	/* prev char was special */
				if(prev==015) {	/* CRLF => '\n' */
					if(c==012) {
						outbyte('\n');
						prev=0;
						continue;
					}
				}
				else {		/* 177 quoting */
					if(d=pfx177[c]) {
						outbyte(d);
						prev=0;
						continue;
					}
				}
				/* guess the sequence didn't work out */
				flushprev();
			}
			/* see if we're starting a new sequence */
			if((d=base[c])==SPEC) {	/* possible special sequence */
				prev=c;		/* check next char */
				continue;
			}
			outbyte(d);
		}
	}
	/* trim off trailing ^Cs from last word */
	/* note that there may be a PREV character inherited from the prev */
	/* word, but it can't be ^C (since PREV is only for 015 and 177) so */
	/* we won't screw up the previous word if the file ends with 6 ^Cs */
	while(outcnt&&outbuf[outcnt-1]==003) outcnt--;
	outword();		/* flush bytes from final word, if any */

	fclose(out);
	return;
}

/* flush all bytes saved in OUTBUF to the output file, and set OUTCNT=0 */
static void outword()
{
	char *p;
	for(p=outbuf;outcnt;outcnt--) {	/* write out all stored bytes */
		if(putc(*p++,out)==EOF) {
			perror("?File write error");
			exit(1);
		}
	}
}
