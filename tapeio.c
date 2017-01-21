/*

  Routines to do magtape I/O.

  By John Wilson <wilson@dbit.com>, JOHNW.

  There are no per-instance variables, so only a single magtape can be active
  at any one time.

  Entry points:

  opentape, closetape, posnbot, posneot, getrec, putrec, tapemark.

  08/10/1993  JMBW  IBM mainframe TCP socket stuff (was using many files).
  07/08/1994  JMBW  Local magtape code.
  03/13/1995  JMBW  Converted to separate routines.
  07/19/1998  JMBW  Added support for "rmt" remote tape protocol.
  08/27/2001  JMBW  Support SIMH style tape files (with padding).

  This file is part of itstar.

  itstar is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  itstar is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with itstar.  If not, see <http://www.gnu.org/licenses/>.

*/

#include <errno.h>
#include <netdb.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>	/* for lseek() SEEK_SET, SEEK_END under Linux */

#ifdef _AIX /* maybe this will be enough to make it compile on AIX */
#include <sys/tape.h>
#define MTWEOF STWEOF
#define MTREW STREW
#define MTFSR STFSR
#define MTFSF STFSF
#define MTBSR STRSR
#define MTIOCTOP STIOCTOP
/* not sure about these two (SCSI only): */
#define MTSETBLK STSETBLK
#define MTSETDENSITY STSETDENSITY
#define mtop stop
#elif __APPLE__
#define MTWEOF 0
#define MTREW 0
#define MTFSR 0
#define MTFSF 0
#define MTBSR 0
#define MTIOCTOP 0
#define MTSETBLK 0
#define MTSETDENSITY 0
struct mtop { int mt_op; int mt_count; };
#else
#include <sys/mtio.h>
#if defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__)
#define MTSETBLK MTSETBSIZ
#define MTSETDENSITY MTSETDNSTY
#endif
#endif

#ifndef O_BINARY
#define O_BINARY 0
#endif

#include "tapsrv.h"	/* get TAPESRV command opcodes */

/* default tape drive device name */
#define TAPE "/dev/nrmt0"
/* default tape density */
#define BPI 1600

void nomem();

static void doread(), dowrite(), sendcode(), getrc();
static int response(), doioctl();
void tapemark();

static char *tape;	/* tape filename */

unsigned long bpi=BPI;	/* tape density (for tape length msg) */

int simh=1;		/* NZ => SIMH file format (records padded to even */
			/* lengths) */
			/* 0 => Ersatz-11 file format (no padding) */

/* magtape commands */
static struct mtop mt_weof={ MTWEOF, 1 }; /* operation, count */
static struct mtop mt_rew={ MTREW, 1 };
static struct mtop mt_fsr={ MTFSR, 1 };
static struct mtop mt_fsf={ MTFSF, 1 };
static struct mtop mt_bsr={ MTBSR, 1 };
/* SCSI only: */
static struct mtop mt_setblk={ MTSETBLK, 0 };  /* blockize = 0 (variable) */
static struct mtop mt_setden={ MTSETDENSITY, 0x02 };  /* density = 1600 */

static char netbuf[80];	/* buffer for net commands and responses */

/* exactly one of the following is set to indicate device type */
static int tapetape=0;	/* NZ => honest to god tape drive */
static int tapefile=0;	/* NZ => file containing image of a tape */
static int tapesock=0;	/* NZ => MTS TAPESRV tape server through TCP socket */
static int tapermt=0;	/* NZ => WEENIX rmt tape server */
static int tapefd;	/* tape drive, file, or socket file descriptor */

static int waccess;	/* NZ => tape opened for write access access */

unsigned long count;	/* count of frames written to tape */

static struct sockaddr_in addr;  /* socket addr structure for remote TAPESRV */

/* open the tape drive (or whatever) */
/* "create" =1 to create if file, "writable" =1 to open with write access */
void opentape(char *name,int create,int writable)
{
	char *p, *host, *user, *port;
	int len;

	waccess=writable;			/* remember if we're writing */
	count=0;				/* nothing transferred yet */

	/* get tape filename */
	tape=name;
	if(tape==NULL) tape=getenv("TAPE");	/* get from environment */
	if(tape==NULL) tape=TAPE;		/* or use our default */

	/* just a file if no colon in filename */
	if((p=index(tape,':'))==NULL) {
/* there's probably a better way to handle this, in case a file is really
   a link to a tape drive -- handler index or something? */
		if(strncmp(tape,"/dev/",5)==0) {
			/* assume tape if starts with /dev/ */
			tapetape++;
			tapefd=open(tape,(writable?O_RDWR:O_RDONLY),0);
		}
		else {	/* otherwise file */
			tapefile++;
			if(strcmp(tape,"-")==0) { /* stdin/stdout */
				if(writable) tapefd=1;
				else tapefd=0;
			}
			else {
				if(create)
					tapefd=open(tape,O_CREAT|O_TRUNC|
						O_WRONLY|O_BINARY,0644);
				else	tapefd=open(tape,(writable?O_RDWR:
						O_RDONLY)|O_BINARY,0);
			}
		}
		if(tapefd<0) {
			perror("?Open failure");
			exit(1);
		}
	}
	else {	/* "rmt" tape server on remote host */
/*		tapesock++; */
		tapermt++;
		/* split filename around ':' */
		len=p-tape;
		port=p+1;

		/* can't necessarily modify tape[] so copy it first */
		if((host=malloc(len+1))==NULL) nomem();
		strncpy(host,tape,len);		/* copy hostname */
		host[len]=0;			/* tack on null */

#if 0
/* left over from old IBM mainframe tape server code (JMBW's use only) */
/* filename syntax is host:portno instead of host:devname */
		/* get IP address */
		if((addr.sin_addr.s_addr=inet_addr(host))!=-1)
			addr.sin_family=AF_INET; /* a.b.c.d */
		else {	/* must be host name */
			struct hostent *h=gethostbyname(host);
			if(h==NULL) {
				fprintf(stderr,"?No such host:  %s\n",
					host);
				exit(1);
			}
			addr.sin_family=h->h_addrtype;
			bcopy(h->h_addr,(char *)&addr.sin_addr,h->h_length);
		}

		/* create socket */
		if((tapefd=socket(addr.sin_family,SOCK_STREAM,
			IPPROTO_TCP))<0) {
			perror("?Error creating socket");
			exit(1);
		}

		/* connect to server */
		addr.sin_port=htons(atoi(port));
		if(connect(tapefd,(struct sockaddr *)&addr,sizeof(addr))<0) {
			perror("?Connect failed");
			exit(1);
		}
#endif
		/* connect to "rexec" server */
		if((p=index(host,'@'))==NULL) {
			p=host;		/* no @, point at hostname */
			user=NULL;
		}
		else {
			*p++='\0';	/* shoot out @, point at host name */
			user=(*p!='\0')?host:NULL;  /* keep non-null user */
		}
#ifndef __APPLE__
		if((tapefd=rexec(&p,htons(512),user,NULL,"/etc/rmt",
			(int *)NULL))<0) {
			perror("?Connection failed");
			exit(1);
		}
#endif

		/* build rmt "open device" command */
		if((1+strlen(port)+1+1+1+1)>sizeof(netbuf)) {
			/* allow for "O devname LF 0/2 LF NUL" */
			fprintf(stderr,"?Device name too long\n");
			exit(1);
		}
		len=sprintf(netbuf,"O%s\n%d\n",port,writable?O_RDWR:O_RDONLY);
		dowrite(tapefd,netbuf,len);
		if(response()<0) {
			perror("?Error opening tape drive");
			exit(1);
		}
		free(host);
	}

	/* SCSI setup for local/remote tape drive */
	if(tapetape||tapermt) {
		/* (ignore errors in case not SCSI) */
		/* set variable record length mode */
		doioctl(&mt_setblk);
		/* set density to 1600 */
		doioctl(&mt_setden);
	}
}

/* close the tape drive */
void closetape()
{
	if(waccess) {			/* opened for create/append */
		tapemark();		/* add one more tape mark */
					/* (should have one already) */
	}
	if(tapesock) {
		sendcode(TS_CLS);	/* orderly disconnect */
		getrc();
	}
	if(tapermt) {
		dowrite(tapefd,"C\n",2);
		if(response()<0) {
			perror("?Error closing remote tape");
			exit(1);
		}
	}
	if(close(tapefd)<0) {
		perror("?Error closing tape");
		exit(1);
	}
}

/* rewind tape */
void posnbot()
{
	if(tapesock) {			/* MTS tape server */
		sendcode(TS_REW);	/* cmd=$CONTROL *TAPE* REW */
		getrc();		/* check return code */
	}
	else if(tapefile) {		/* image file */
		if(lseek(tapefd,0L,SEEK_SET)<0) {
			perror("?Seek failed");
			exit(1);
		}
	}
	else {				/* local/remote tape drive */
		if(doioctl(&mt_rew)<0) {
			perror("?Rewind failed");
			exit(1);
		}
	}
}

/* position tape at EOT (between the two tape marks) */
void posneot()
{
	if(tapesock) {			/* MTS tape server */
		sendcode(TS_EOT);	/* cmd=go to LEOT */
		getrc();		/* check return code */
	}
	else if(tapefile) {		/* image file */
		if(lseek(tapefd,-4L,SEEK_END)<0) {
			perror("?Seek failed");
			exit(1);
		}
	}
	else {				/* local/remote tape drive */
		doioctl(&mt_bsr);	/* in case already at LEOT */
		while(1) {
			/* space forward a file */
			if(doioctl(&mt_fsf)<0) {
				perror("?Error spacing to EOT");
				exit(1);
			}
			/* space one record more to see if double EOF */
			if(doioctl(&mt_fsr)<0) break;
/* might want to check errno to make sure it's the right error */
		}
#if 1
		/* "man mtio" doesn't say whether MTFSR actually moves past */
		/* the tape mark, let's assume it does */
		if(doioctl(&mt_bsr)<0) {  /* get between them */
			perror("?Error backspacing at EOT");
			exit(1);
		}
#endif
	}
}

/* read a tape record, return actual length (0=tape mark) */
int getrec(char *buf,int len)
{
	unsigned char byte[4];		/* 32 bits for length field(s) */
	unsigned long l;		/* at least 32 bits */
	unsigned char scratch[1];
	int i;

	if(tapesock) {			/* MTS tape server */
		sendcode(TS_RDR);	/* read a record */
		getrc();		/* get return code */
		doread(tapefd,byte,2);	/* get record length */
		l=(byte[1]<<8)|byte[0];	/* compose into halfword */
		if(l>len) goto toolong;	/* don't read if too long for buf */
		if(l!=0) doread(tapefd,buf,l);  /* get data unless tape mark */
	}
	else if(tapefile) {		/* image file */
		doread(tapefd,byte,4);	/* get record length */
		l=((unsigned long)byte[3]<<24L)|((unsigned long)byte[2]<<16L)|
			((unsigned long)byte[1]<<8L)|(unsigned long)byte[0];
					/* compose into longword */
		if(l>len) goto toolong;	/* don't read if too long for buf */
		if(l!=0) {		/* get data unless tape mark */
			doread(tapefd,buf,l);  /* read data */
			/* SIMH pads odd records, read scratch byte */
			if(simh&&(l&1)) doread(tapefd,scratch,1);
			doread(tapefd,byte,4);  /* get trailing record length */
			if((((unsigned long)byte[3]<<24L)|
				((unsigned long)byte[2]<<16L)|
				((unsigned long)byte[1]<<8)|
				(unsigned long)byte[0])!=l) {	/* should match */
				fprintf(stderr,"?Corrupt tape image\n");
				exit(1);
			}
		}
	}
	else if(tapermt) {		/* rmt tape server */
		len=sprintf(netbuf,"R%d\n",len);
		dowrite(tapefd,netbuf,len);
		if((i=response())<0) {
			perror("?Error reading tape");
			exit(1);
		}
		l = i;
		if(l) doread(tapefd,buf,l);
	}
	else {				/* local tape drive */
		if((i=read(tapefd,buf,len))<0) {
			perror("?Error reading tape");
			exit(1);
		}
		l = i;
	}
	return(l);
toolong:
	fprintf(stderr,"?%ld byte tape record too long for %d byte buffer\n",
		l,len);
	exit(1);
}

/* write a tape record */
void putrec(char *buf,int len)
{
	unsigned char l[4];
	static unsigned char zero[1] = { 0 };

	if(tapesock) {			/* MTS tape server */
		sendcode(len);		/* command code is length */
		dowrite(tapefd,buf,len);  /* write data */
		getrc();		/* check return code */
	}
	else if(tapefile) {		/* image file */
		l[0]=len&0377;		/* PDP-11 byte order */
		l[1]=(len>>8)&0377;
		l[2]=0;			/* our recs are always < 64 KB */
		l[3]=0;
		dowrite(tapefd,l,4);	/* write longword length */
		dowrite(tapefd,buf,len);  /* write data */
		/* SIMH pads odd records */
		if(simh&&(len&1)) dowrite(tapefd,zero,1);
					/* add byte if odd */
		dowrite(tapefd,l,4);	/* write length again */
	}
	else if(tapermt) {		/* rmt tape */
		int n;
		n=sprintf(netbuf,"W%d\n",len);
		dowrite(tapefd,netbuf,n);
		dowrite(tapefd,buf,len);
	}
	else dowrite(tapefd,buf,len);	/* just write the data if tape */

	count+=len+(BPI*3/5);	/* add to byte count (+0.6" tape gap) */
}

/* write a tape mark */
void tapemark()
{
	static char zero[4]={ 0, 0, 0, 0 };

	if(tapesock) {			/* MTS tape server */
		sendcode(TS_WTM);	/* cmd=$CONTROL *TAPE* WTM */
		getrc();		/* check return code */
	}
	else if(tapefile) {		/* image file */
		dowrite(tapefd,zero,4);	/* write longword length */
	}
	else {				/* local/remote tape drive */
		if(doioctl(&mt_weof)<0) {
			perror("?Failed writing tape mark");
			exit(1);
		}
	}
	count+=3*BPI;			/* 3" of tape */
}

/* do a write and check the return status, punt on error */
static void dowrite(int handle,char *buf,int len)
{
	if(write(handle,buf,len)!=len) {
		perror("?Error on write");
		exit(1);
	}
}

/* do a read and keep trying until we get all bytes */
static void doread(int handle,char *buf,int len)
{
	int n;
	while(len) {
		if((n=read(handle,buf,len))<0) {
			perror("?Error on read");
			exit(1);
		}
		if(n==0) {
			fprintf(stderr,"?Unexpected end of file\n");
			exit(1);
		}
		buf+=n;
		len-=n;
	}
}

/* send halfword command code (or record length) to MTS tape server */
static void sendcode(int halfword)
{
	char byte[2];
	byte[0]=halfword&0377;		/* PDP-11 byte order */
	byte[1]=(halfword>>8)&0377;
	dowrite(tapefd,byte,2);		/* write the halfword */
}

/* get return code from MTS tape server, punt if bad */
static void getrc()
{
	char rc[1];
	doread(tapefd,rc,1);
	if(rc[0]!=0x00) {	/* X'00' is success, X'FF' is failure */
		fprintf(stderr,"?Remote tape I/O error\n");
		exit(1);
	}
}

/* get response from "rmt" server */
static int response()
{
	char c, rc;
	int n;

	doread(tapefd,&rc,1);	/* get success/error code */
	if(rc!='A'&&rc!='E') {	/* must be Acknowledge or Error */
		fprintf(stderr,"?Invalid rmt response code:  %c\n",rc);
		exit(1);
	}

	/* get numeric value (returned by both A and E responses) */
	for(n=0;;) {
		doread(tapefd,&c,1);  /* get next digit */
		if(c<'0'||c>'9') break;  /* not a digit */
		n=n*10+(c-'0');	/* add new digit in */
		/* ideally would check for overflow */
	}
	if(c!='\n') {		/* first non-digit char must be <LF> */
		fprintf(stderr,"?Invalid rmt response terminator:  %3.3o\n",
			((int)c)&0377);
		exit(1);
	}
	if(rc=='A') return(n);	/* success, return value >=0 */
				/* (unless overflowed) */
	do doread(tapefd,&c,1);
	while(c!='\n');		/* ignore until next LF */
	errno=n;		/* set error number */
	return(-1);
}

/* send ioctl() command to local or remote tape drive */
static int doioctl(struct mtop *op)
{
	int len;

	if(tapetape) return(ioctl(tapefd,MTIOCTOP,op));
	else {	/* "rmt" tape server */
		/* form cmd (better hope remote MT_OP values are the same) */
		len=sprintf(netbuf,"I%d\n%d\n",op->mt_op,op->mt_count);
		dowrite(tapefd,netbuf,len);
		return(response());
	}
}
