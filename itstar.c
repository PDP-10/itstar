/*

  Tape archive program to handle ITS DUMP tapes.

  By John Wilson <wilson@dbit.com>, JOHNW.

  04/02/93  JMBW  Created.
  08/10/93  JMBW  Mangled to write to TCP socket (using MTS tape drives).
  07/08/94  JMBW  Finished local magtape code but didn't test it.
  07/15/98  JMBW  -c, -r, -t functions finished.
  07/18/98  JMBW  -x function finished.

*/

#include <dirent.h>
#include <errno.h>
#include <netdb.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <time.h>
#include <unistd.h>
#include <utime.h>

/* variables for estimating length of tape used: */
extern unsigned long bpi;	/* tape density in bits per inch */
extern unsigned long count;	/* count of tape frames written */

static void usage(), itsname(), extitsname();
static void addfiles(), addfile(), listfiles(), listfile(),
	extfiles(), extfile();
static void scantape(int argc,char **argv,void (*process)());
void save(), weenixname(), nomem();

int append=0;	/* func=append */
int create=0;	/* func=create */
int type=0;	/* func=type filenames */
int extract=0;	/* func=extract files */
int verify=0;	/* NZ => print names of all files processed on stdout */

unsigned long tapeno=1, reelno=0;  /* DUMP tape, reel number */

unsigned long islink; /* NZ => file is a link, 0 => it's a file */

char date6[7];	/* today's date in SIXBIT */

char ufd[7], fn1[7], fn2[7];  /* buffers for UFD and filename 1/2 */
char lufd[7], lfn1[7], lfn2[7];  /* same as above, for target of link */
char dev[7], author[7];	 /* not currently used, but available in DIR.LIST */
struct tm cdate, rdate;  /* creation, ref dates (none if tm_year=0) */

static char sbuf[256];	/* scratch buffer, for readlink() */

static char *tape=NULL;  /* name of tape drive or file */

main(int argc,char **argv)
{
	int i;
	time_t t0;
	struct tm *now;

	argc--, argv++;	/* skip our name */

	for(i=0;argc;i++,argc--,argv++) { /* scan through JCL */
		char *p=*argv;

		if(*p=='-'||i==0) {	/* 1st parm is flag even w/o '-' */
			if(*p=='-') p++;  /* skip the '-' if it's there */
			while(*p) {	/* scan next byte of option */
				switch(*p++) {
				case 'c':	/* create archive */
					create=1;
					break;
				case 'f':	/* specify archive filename */
					if(*p) tape=p;  /* -ffilename */
					else {	/* -f filename */
						if((--argc)==0) goto msgarg;
						tape=*++argv;
					}
					goto nxtwrd;
				case 'h':	/* help */
					usage(0);
				case 'r':	/* append to archive */
					append=1;
					break;
				case 't':	/* type filenames */
					type=1;
					break;
				case 'v':	/* verify filenames */
					verify=1;
					break;
				case 'x':	/* extract files */
					extract=1;
					break;
				default:
					fprintf(stderr,"?Invalid option: %c\n",
						*(p-1));
					usage(1);
				msgarg:	/* missing argument */
					fprintf(stderr,"?Missing argument\n");
					usage(1);
				}
			}
		}
		else break;		/* rest of JCL is filenames */
	nxtwrd:	;
	}

	/* check switches */
	if((append+create+type+extract)==0) {
		fprintf(stderr,"?Must specify one of:  -c -t -r -x\n");
		exit(1);
	}

	if((append+create+type+extract)>1) {
		fprintf(stderr,"?Switch conflict\n");
		exit(1);
	}

	/* get local time for tape creation info */
	t0=time((time_t *)0);		/* secs since midnight 01-Jan-1970 */
	now=localtime(&t0);		/* unpack into local time */
	sprintf(date6,"%2.2d%2.2d%2.2d",  /* ASCII YYMMDD */
		now->tm_year%100,now->tm_mon+1,now->tm_mday);

	setenv("TZ","EST5EDT",1);	/* ITS dates are all Cambridge, MA */
/*	tzset(); */

	if(append) {			/* append to existing tape */
		opentape(tape,0,1);	/* open tape */
		posneot(1);		/* space to EOT */
		addfiles(argc,argv);	/* add files onto end */
	}
	else if(create) {		/* initialize and write tape */
		opentape(tape,1,1);	/* open tape */
		posnbot();		/* rewind */
		writevolhdr();		/* write volume header */
		addfiles(argc,argv);	/* add files onto end */
	}
	else if(type) {			/* list files on tape */
		if(argc!=0) usage(1);	/* should we handle filenames?  no. */
		opentape(tape,0,0);	/* open tape */
		posnbot();		/* rewind */
		listfiles();		/* list files */
	}
	else /*if(extract)*/ {		/* extract files from tape */
		if(argc!=0) usage(1);	/* should we handle filenames?  soon */
		opentape(tape,0,0);	/* open tape */
		posnbot();		/* rewind */
		extfiles(argc,argv);	/* extract files */
	}
	closetape();
	exit(0);
}

/* write DUMP volume header to tape */
writevolhdr()
{
	resetbuf();		/* a record all to itself */
	outword(-4L,0L);	/* 1: AOBJN pointer giving length */
	outword(tapeno,reelno);	/* 2: tape,,reel */
	if(verify) printf("Tape %ld, reel %ld\n",tapeno,reelno);
	outsix(date6);		/* 3: today's date */
	outword(0L,0L);		/* 4: random dump (not full/incremental) */
	tapeflush();		/* write it out */
	/* N.B. no tape mark between vol. header and first file label */
}

/* add files to a DUMP tape */
static void addfiles(int argc,char **argv)
{
	int c=argc;
	char **v=argv;

	while(c--) {
		addfile(argc,argv,*v++);
	}
	if(verify)
		printf("Approximately %d.%d' of tape used\n",count/bpi/12,
			(count*10/bpi/12)%10);
}

/* add a single file to a DUMP tape */
static void addfile(int argc,char **argv,char *f)
{
	struct stat s;

	/* look up file information */
	if(stat(f,&s)<0) {
		fprintf(stderr,"?Error accessing %s\n",f);
		exit(1);
	}

	/* if it's a bare directory, recurse for all files in that dir */
	if(S_ISDIR(s.st_mode)) {  /* is it a directory? */
		DIR *dir;
		struct dirent *d;
		char *p;

		/* if the dir contains DIR.LIST, use that to find files */
		if(dirlist(argc,argv,f)==0) return;

		/* otherwise open the directory */
		if((dir=opendir(f))==NULL) {
			fprintf(stderr,"?Error opening directory %s\n",f);
			exit(1);
		}

		/* read through dir entries */
		while((d=readdir(dir))!=NULL) {
			if(d->d_name[0]=='.')	/* ignore ./.., hidden files */
				continue;
			/* alloc space for path+'/'+file+NUL */
			if((p=malloc(strlen(f)+1+strlen(d->d_name)+1))==NULL)
				nomem();
			/* combine path, /, filename, NUL */
			strcpy(p,f);
			strcat(p,"/");
			strcat(p,d->d_name);
			/* recurse */
			addfile(argc,argv,p);
			free(p);
		}
		closedir(dir);
		return;
	}

	/* extract dir name/filename, convert to UFD/FN1/FN2 */
	extitsname(f,ufd,fn1,fn2);

	memcpy(&cdate,localtime(&s.st_mtime),sizeof(struct tm));
	memcpy(&rdate,localtime(&s.st_atime),sizeof(struct tm));

	if(S_ISLNK(s.st_mode)) {  /* it's a link */
		int len=readlink(f,sbuf,sizeof(sbuf)-1);  /* get link name */
				/* (-1 to allow for adding NUL) */
		if(len<0) {
			perror("?Error following link");
			exit(1);
		}
		sbuf[len]='\0';	/* mark end (readlink() doesn't) */
		extitsname(sbuf,lufd,lfn1,lfn2);  /* convert to ITS style */
		islink=1;
	}
	else islink=0;

	/* now that we've gotten all the names and dates, actually save it */
	save(f);
}

/* save a file based using information in UFD/FN1/FN2/CDATE etc. */
void save(char *f)
{
	if(verify) printf("%s => %s;%s %s ",f,ufd,fn1,fn2);

	resetbuf();		/* init file header record (7 words) */
	outword(-7L,0L);	/* 1: AOBJN ptr giving length */
	outsix(ufd);		/* 2: UFD */
	outsix(fn1);		/* 3: filename 1 */
	outsix(fn2);		/* 4: filename 2 */
	outword(islink,0L);	/* 5: linkf,,pack */
	outword((((unsigned long)cdate.tm_year)<<9L)|
		(((unsigned long)cdate.tm_mon+1L)<<5L)|
		(unsigned long)cdate.tm_mday,
		((((unsigned long)cdate.tm_hour*60L)+
		(unsigned long)cdate.tm_min)*60L+
		(unsigned long)cdate.tm_sec)*2L);  /* 6: date of creation */
	/* note:  year field is officially only 7 bits, but the 2 bits to */
	/* left of it are unused in UFD entries so hopefully it's safe to */
	/* grab them */
	/* tm_year and UFD year field are both YEAR-1900 */
	outword((((unsigned long)cdate.tm_year)<<9L)|
		(((unsigned long)cdate.tm_mon+1L)<<5L)|
		(unsigned long)cdate.tm_mday,
		((((unsigned long)cdate.tm_hour*60L)+
		(unsigned long)cdate.tm_min)*60L+
		(unsigned long)cdate.tm_sec)*2L);  /* 7: date of last ref */
	tapeflush();		/* finish off label record */

	if(islink) {		/* it's a link, not a file */
		outsix(lfn1);	/* write it out (note funny order) */
		outsix(lfn2);
		outsix(lufd);
		tapeflush();	/* end of record (just 15 bytes) */
	}
	else {
		unpack(f);	/* add the file itself */
		tapeflush();	/* finish off final record */
	}
	tapemark();		/* write EOF */

	if(verify) printf("[OK]\n");
}

/* list files on tape */
static void listfiles(int argc,char **argv)
{
	scantape(argc,argv,listfile);
}

/* list a single file (called back by scantape()) */
static void listfile()
{
	printf("%s;%s %s\n",ufd,fn1,fn2);  /* print ITS filename */
	while(taperead()==0) ;		/* skip until tape mark */
}

/* extract files from tape */
static void extfiles(int argc,char **argv)
{
	scantape(argc,argv,extfile);
}

/* extract a single file (called back by scantape()) */
static void extfile()
{
	static char fname[6+1+6+1+6+1]; /* filename = "ufd/fn1.fn2"<NUL> */
	static char lname[6+1+6+1+6+1]; /* same, for link name */
	struct stat s;
	struct utimbuf u;

	if(verify) printf("%s;%s %s ",ufd,fn1,fn2);  /* print ITS filename */
	weenixname(ufd);	/* convert to WEENIX equivalent */
	weenixname(fn1);
	weenixname(fn2);
	sprintf(fname,"%s/%s.%s",ufd,fn1,fn2);  /* combine (known to fit) */
	if(verify) printf("=> %s ",fname);  /* print WEENIX filename */

	/* create directory if it doesn't exist */
	if(stat(ufd,&s)<0&&errno==ENOENT) {
		if(mkdir(ufd,0755)<0) {
			fflush(stdout);
			perror(ufd);
			exit(1);
		}
	}

	/* create the file (or link) */
	if(islink) {			/* it's a link */
		if(taperead()<0) {
			fprintf(stderr,"?Unexpected EOF\n");
			exit(1);
		}
		insix(lfn1);		/* read it in */
		insix(lfn2);
		insix(lufd);
		weenixname(lufd);	/* convert to WEENIX style */
		weenixname(lfn1);
		weenixname(lfn2);
		sprintf(lname,"%s/%s.%s",lufd,lfn1,lfn2);  /* combine */
		if(symlink(lname,fname)<0) {  /* create link */
			perror(fname);
			exit(1);
		}
		/* can't apply dates since target may not exist */
		taperead();		/* read the EOF mark */
	}
	else {				/* regular file */
		pack(fname);		/* pack tape file into disk file */

		/* apply file dates from tape */
		if(cdate.tm_year!=0) {		/* creation date (if known) */
			u.modtime=mktime(&cdate);  /* convert to time_t */
			if(rdate.tm_year!=0)	/* ref date (if known) */
				u.actime=mktime(&rdate);
			else u.actime=u.modtime;  /* use creation date if not */
			if(utime(fname,&u)<0) {
				perror("?Error setting file dates");
				exit(1);
			}
		}
	}

	if(verify) printf("[OK]\n");
}

/* scan the tape and process each file found (after setting up globals) */
static void scantape(int argc,char **argv,void (*process)())
{
	unsigned long l,r,len;

	if(taperead()<0) {	/* read volume header */
		fprintf(stderr,"?Null tape\n");
		exit(1);
	}

	if(type) {		/* display volume header info */
		inword(&l,&r);	/* 1: AOBJN ptr giving length */
		len=01000000L-l; /* length of record */
		if(len>=3) {
			inword(&l,&r);
			printf("Tape %ld, reel %ld",l,r);
			insix(ufd);
			printf(", created %c%c/%c%c/%c%c\n",
				ufd[2],ufd[3],ufd[4],ufd[5],ufd[0],ufd[1]);
		}
	}

	while(taperead()==0) {	/* read file label */
		inword(&l,&r);	/* 1: AOBJN ptr giving length */
		len=01000000L-l; /* length of record */
		if(len<4) {	/* must have at least filename */
			fprintf(stderr,"?Invalid tape format\n");
			exit(1);
		}
		insix(ufd);	/* 2: UFD */
		insix(fn1);	/* 3: FN1 */
		insix(fn2);	/* 4: FN2 */
		if(len>=5)	/* 5: linkf,,pack */
			inword(&islink,&r);
		else islink=0;	/* (assume file if missing) */

		if(len>=6) {	/* 6: creation date */
			inword(&l,&r);
			cdate.tm_year=(l>>9L);
			cdate.tm_mon=((l>>5L)&017)-1;
			cdate.tm_mday=l&037;
			cdate.tm_hour=r/(60L*60L*2L);
			cdate.tm_min=(r/(60L*2L))%60L;
			cdate.tm_sec=(r/2L)%60L;
			cdate.tm_isdst=(-1);
		}
		else cdate.tm_year=0;

		if(len>=7) {	/* 7: reference date */
			inword(&l,&r);
			rdate.tm_year=(l>>9L);
			rdate.tm_mon=((l>>5L)&017)-1;
			rdate.tm_mday=l&037;
			rdate.tm_hour=r/(60L*60L*2L);
			rdate.tm_min=(r/(60L*2L))%60L;
			rdate.tm_sec=(r/2L)%60L;
			rdate.tm_isdst=(-1);
		}
		else rdate.tm_year=0;

		(*process)();	/* process the file */
	}
}

static void usage(int rc)
{
	fprintf(stderr,"\
\n\
ITSTAR V1.00  By John Wilson <wilson@dbit.com>\n\
Access ITS DUMP tapes\n\
\n\
Usage:  itstar switches file1 file2 file3 ...\n\
\n\
switches:\n\
  -c            create tape\n\
  -t            type out tape contents\n\
  -r            append files to tape\n\
  -x            extract files from tape\n\
  -f /dev/xxxx  specify local tape drive name\n\
  -f file       use tape image file instead\n\
  -f -          use STDIN/STDOUT for image file\n\
  -f HOST:DEV   use \"rmt\" remote tape server\n\
  -v            verify (display) names of all files accessed\n\
\n");

/* need some way to differentiate rmt protocol from my own weird one,
   and to specify port (assume I ever sink to the level of supporting rmt) */

	exit(rc);
}

/* write a 6-character ASCII string as a word of SIXBIT */
/* it is assumed that the string contains no non-sixbit characters */
outsix(char *s)
{
	unsigned long six[6], *p;
	int i;
	unsigned long l, r;

	for(i=6,p=six;i--;*p++=0L);	/* init in case < 6 chars */
	for(i=6,p=six;*s&&i--;*p++=((*s++-040)&077));  /* ASCII char -40 */
	l=(six[0]<<12L)|(six[1]<<6L)|six[2];  /* pack it all up */
	r=(six[3]<<12L)|(six[4]<<6L)|six[5];
	outword(l,r);
}

/* read a 36-bit SIXBIT word as 0-6 ASCII characters */
insix(char *s)
{
	char *p;
	int i;
	unsigned long l, r;

	inword(&l,&r);			/* read it */

	s[0]=((l>>12L)&077)+040;	/* unpack all six 6-bit bytes */
	s[1]=((l>>6L)&077)+040;
	s[2]=(l&077)+040;
	s[3]=((r>>12L)&077)+040;
	s[4]=((r>>6L)&077)+040;
	s[5]=(r&077)+040;

	for(i=6,p=s;i--;s++)
		if(*s!=' ') p=s+1;	/* look for last non-blank, if any */
	*p='\0';			/* put a NUL after it */
}

/* extract ITS filename components from a WEENIX filename, as much as we can */
/* (results stored in UFD/FN1/FN2 arrays, 0-6 chars each w/NUL terminator */
static void extitsname(char *f,char *ufd,char *fn1,char *fn2)
{
	int len;
	char *p, *q, *pfn;

	/* extract dir name/filename, convert to UFD/FN1/FN2 */
	pfn=rindex(f,'/');	/* filename starts after last "/" */
	if(pfn==NULL) {		/* if any */
		ufd[0]='\0';	/* no UFD */
		pfn=f;
	}
	else {			/* extract UFD while we're here */
		for(p=pfn;p>f;)	/* search for preceding "/" */
			if(*--p=='/') {
				p++;  /* first char of final path element */
				break;
			}
		len=pfn-p;	/* length */
		if(len>6) len=6;  /* stop at 6 */
		strncpy(ufd,p,len);  /* copy UFD */
		ufd[len]='\0';	/* mark end */
		pfn++;		/* filename starts after final "/" */
	}

	/* split apart filename and extension */
	p=index(pfn,'.');	/* extension starts after first "." */
	if(p==NULL) {		/* if any */
		fn2[0]='\0';	/* no FN2 */
		len=strlen(pfn);  /* length */
	}
	else {
		if((q=index(p+1,'.'))==NULL)  /* stop at next dot */
			len=strlen(p+1);  /* no dot, get length of ext */
		else len=(q-(p+1));  /* ext runs until next dot */
		if(len>6) len=6;  /* stop at 6 */
		strncpy(fn2,p+1,len);  /* copy FN2 */
		fn2[len]='\0';	/* mark end */
		len=p-pfn;	/* length of FN1 */
	}
	if(len>6) len=6;	/* stop at 6 */
	strncpy(fn1,pfn,len);	/* copy FN1 */
	fn1[len]='\0';		/* mark end */

	itsname(ufd);		/* convert to ITS form */
	itsname(fn1);
	itsname(fn2);

	/* apply stupid defaults if any are missing */
	if(ufd[0]=='\0') strcpy(ufd,"UFD");
	if(fn1[0]=='\0') strcpy(fn1,"FN1");
	if(fn2[0]=='\0') strcpy(fn2,"FN2");
}

/* convert a filename element in-place from ITS to WEENIX form */
void weenixname(char *p)
{
	register char c;

	for(;c=(*p);*p++=c)
		switch(c) {
		case '.': c='_'; break;
		case '/': c='{'; break;
		case '_': c='}'; break;
		case ' ': c='~'; break;
		default: c=tolower(c);
		}
}

/* convert a filename element in-place from WEENIX to ITS form */
static void itsname(char *p)
{
	register char c;

	for(;c=(*p);*p++=c)
		switch(c) {
		case '_': c='.'; break;
		case '{': c='/'; break;
		case '}': c='_'; break;
		case '~': c=' '; break;
		default: c=toupper(c);
		}
}

/* routine to punt after a failed malloc() */
void nomem()
{
	perror("?Error allocating memory");
	exit(1);
}
