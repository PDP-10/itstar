/*

  Read DIR.LIST to get file parameters.

  By John Wilson.

  08/09/93  JMBW  Created.
  07/14/98  JMBW  Separated from DUMP.C.

*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>

FILE *zopen();
void nomem();

static FILE *dir;

static int eat(char c), string(), number();
static void file(), punt(), numhuge(), subhuge();
static time_t valhuge();

extern char dev[7], ufd[7], fn1[7], fn2[7], author[7],
	lufd[7], lfn1[7], lfn2[7];
extern long islink;
extern struct tm cdate, rdate;

/* Date correction is done in decimal math to avoid overflow on 32-bit CPUs */
/* DIR.LIST has Common Lisp times which are seconds since midnight UTC on */
/* 01-Jan-1900.  WEENIX time began at midnight UTC on 01-Jan-1970. */
/* N.B. 1900 was not a leap year so only 17 leap years before 1970, so the */
/* offset is (70*365+17)*86400 = 2208988800 */
/* (appears backwards in array since lispoffset[0] = least significant digit) */
static char lispoffset[16] = { 0,0,8,8,8,9,8,0,2,2,0,0,0,0,0,0 };
#define LISPLEN sizeof(lispoffset)

int dirlist(int argc,char **argv,char *d)
{
	char *name=malloc(strlen(d)+1+8+1);  /* dir name, /, DIR.LIST, NUL */
	if(name==NULL) nomem();
	sprintf(name,"%s/DIR.LIST",d);	/* compose name */
	dir=zopen(name);		/* uncompress/open dir list */
	free(name);
	if(dir==NULL) return(-1);	/* no luck, do our own dir search */

	eat('(');

	/* get (DEV UFD) */
	eat('('), string(dev,6), string(ufd,6), eat(')');

	/* file entries until ')' */
	while(!eat(')')) file(d);	/* loop through all files */
	return(0);			/* success */
}

/* process next file */
static void file(char *dirname)
{
	static char link[50], f1[7], f2[7];
	char *fname;
 
	static char date[LISPLEN];
	char c, *p, *q;
	int len;
	long size, byte;
	time_t long date1, date2;
	struct tm *d;

	if(!eat('(')) punt();
	if(string(fn1,6)) {
		if(!string(fn2,6)) punt();
		size=number(), byte=number();

		/* file or link but not both */
		if(string(link,sizeof(link)-1)) {
			if(size>=0||byte>=0) punt();
		}
		else {
			if(size<0||byte<0) punt();
		}

		/* correct the dates */
		numhuge(date,LISPLEN);	/* creation date */
		subhuge(date,lispoffset,LISPLEN);  /* convert LISP => WEENIX */
		date1=valhuge(date,LISPLEN);  /* NOW it should fit in time_t */

		numhuge(date,LISPLEN);	/* again for ref date */
		subhuge(date,lispoffset,LISPLEN);
		date2=valhuge(date,LISPLEN);

		string(author,6);	/* file author's name */

		d=localtime(&date1); /* creation date */
		memcpy(&cdate,d,sizeof(struct tm));

		d=localtime(&date2); /* ref date */
		memcpy(&rdate,d,sizeof(struct tm));

		/* handle link or file */
		if(size<0) { /* link */
			p=index(link,';');
			*p=0, p+=2;
			q=index(p,' ');
			*q++=0;
			len=strlen(link);	/* copy/truncate link UFD */
			if(len>6) len=6;
			strncpy(lufd,link,len);
			lufd[len]='\0';
			len=strlen(p);		/* FN1 */
			if(len>6) len=6;
			strncpy(lfn1,p,len);
			lfn1[len]='\0';
			len=strlen(q);		/* FN2 */
			if(len>6) len=6;
			strncpy(lfn2,q,len);
			lfn2[len]='\0';
			islink=1;
		}
		else islink=0;

		/* copy ITS filename out of the way to WEENIXify it */
		strcpy(f1,fn1);
		strcpy(f2,fn2);
		weenixname(f1);
		weenixname(f2);

		/* compose filename */
		fname=malloc(strlen(dirname)+1+strlen(f1)+1+strlen(f2)+1);
			/* space for path / f1 . f2 <NUL> */
		if(fname==NULL) nomem();
		sprintf(fname,"%s/%s.%s",dirname,f1,f2);

		/* now save the file */
		save(fname);
		free(fname);
	}
	if(!eat(')')) punt();
}

/* if next char matches C, eat it and return 1, otherwise return 0 */
static int eat(char c)
{
	char q;
	while((q=getc(dir))==' ') ;
	if(q==c) return(1);
	ungetc(q,dir);
	return(0);
}

/* parse a string (up to N chars not including NUL) */
/* returns 1 if string read, or 0 if NIL */
static int string(char *buf,int n)
{
	char c;
	if(eat('"')) {	/* quoted string */
		while((c=getc(dir))!=EOF) switch(c) {
		case '"':
			/* end of string */
			*buf='\0'; 
			return(1);
		case '\\':
			/* only used in \" to quote '"' */
			if((c=getc(dir))!='"') punt();
			/* drop through to store it */
		default:
			/* anything else goes in buf if there's space */
			if(n) *buf++=c, n--;
		}
	} else {	/* must be NIL */
		if(eat('N')&&eat('I')&&eat('L')) {
			*buf='\0';
			return(0);
		}
	}
	punt();
}

/* parse a single precision number (NIL => -1) */
static int number()
{
	int i[1];
	char c;
	while((c=getc(dir))==' ') ;
	ungetc(c,dir);
	if(c>='0'&&c<='9') {
		fscanf(dir,"%d",i);
		if(!eat('.')) punt();
		return(i[0]);
	} else {	/* must be NIL */
		if(eat('N')&&eat('I')&&eat('L')) return(-1);
		else punt();
	}
}

/* parse a huge number into an array of decimal digits (NIL => 0) */
/* p[0] is LSD, n is # digits */
static void numhuge(char *p,int n)
{
	register char c;
	char *p1;
	int n1;

	for(p1=p,n1=n;n1--;) *p1++=0;	/* clear it out */

	while((c=getc(dir))==' ') ;	/* scan off leading spaces */
	if(c>='0'&&c<='9') {
		for(;c>='0'&&c<='9';c=getc(dir)) {
			for(p1=p+n-1,n1=n-1;n1--;p1--) *p1=*(p1-1);
			p[0]=c-'0';
		}
		if(c!='.') ungetc(c,dir);
	}
	else {	/* must be NIL */
		if(!(eat('N')&&eat('I')&&eat('L')))
			punt();		/* isn't */
		/* otherwise keep our giant 0 */
	}
}

/* subtract two huge numbers */
/* a=a-b, n=# digits, a[0]/b[0] is LSD */
static void subhuge(char *a,char *b,int n)
{
	register int digit, borrow;

	for(borrow=0;n--;a++,b++) {
		digit=borrow+(int)*a-(int)*b;  /* compute new digit */
		if(digit<0) {		/* borrowed */
			*a=(digit+10);	/* fix it */
			borrow=-1;	/* borrow from next column */
		}
		else {
			*a=digit;	/* no borrow, just save it */
			borrow=0;
		}
	}
}

/* convert huge to time_t */
static time_t valhuge(char *p,int n)
{
	register time_t val=0;
	for(p+=n;n--;) val=val*10L+(time_t)*--p;
	return(val);
}

/* error parsing DIR.LIST */
static void punt()
{
	int c, i;
	fflush(stdout);
	fprintf(stderr,"DIR.LIST format error -- exiting\n");
	for(i=100;i&&(c=getc(dir))>=0;i--) putc(c,stderr);
	putc('\n',stderr);
	exit(1);
}
