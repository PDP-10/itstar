/*

  Open an input file, decompressing it if needed.

  By John Wilson.

  04/11/1993  JMBW  Created.
  08/09/1993  JMBW  Convert dates, uncompress .Z files automatically.
  07/14/1998  JMBW  Separated from DUMP.C.

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

#include <fcntl.h>
#define zopen apple_zopen
#include <stdio.h>
#undef zopen
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>

#define ZCAT "/bin/zcat"

void nomem();

static void uncompress(char *file);

/* open a file for input, uncompressing it if needed, return NULL on failure */
/* this is a bit tangled because either we have a filename supplied by the */
/* user, which includes the ".Z" if it's compressed, or else we have a */
/* filename read from DIR.LIST, which may or may not need to have ".Z" added */
FILE *zopen(char *file)
{
	FILE *f;
	int len;

	/* see if name already ends in ".Z" */
	len=strlen(file);
	if(len>2&&file[len-2]=='.'&&file[len-1]=='Z') {
		/* it does, trim that off (uncompress() adds it) */
		char *name;
		if((name=malloc(len-2+1))==NULL) nomem();
		strncpy(name,file,len-2);	/* copy name */
		name[len-2]='\0';		/* hack off ".Z" */
		uncompress(name);		/* uncompress */
		f=fopen(name,"rb");		/* open it */
		free(name);
	}
	else {		/* no ".Z", try opening it directly */
		if((f=fopen(file,"rb"))==NULL) {  /* doesn't exist */
			uncompress(file);	/* uncompress */
			f=fopen(file,"rb");	/* one more try */
		}
	}
	return(f);
}

/* uncompress a file, or try anyway */
static void uncompress(char *file)
{
	char *filez;
	int h, hz;
	int stat;
	pid_t pid;

	if((filez=malloc(strlen(file)+2+1))==NULL)  /* allow for ".Z"<0> */
		nomem();
	sprintf(filez,"%s.Z",file);
	if((hz=open(filez,O_RDONLY,0))>=0) { /* .Z file exists */
		if((h=creat(file,0644))<0) {
			fprintf(stderr,"?Can't create %s\n",file);
			exit(1);
		}
		if((pid=fork())<0) {
			perror("?Failed to create child process");
			exit(1);
		}
		if(!pid) {	/* child process */
			static char *argv[] = { "zcat", NULL };
			static char *envp[] = { NULL };
			/* use STDIN/STDOUT instead of JCL */
			/* so "-read-.-this-" won't annoy zcat */
			dup2(hz,0), close(hz); /* STDIN */
			dup2(h,1), close(h); /* STDOUT */
			execve(ZCAT,argv,envp);
			/* shouldn't have survived execve() */
			perror(ZCAT);
			exit(127);
		}
		close(hz), close(h);
		wait(&stat);
		if(WEXITSTATUS(stat) != 0) { /* non-zero RC */
			fprintf(stderr,"?Error uncompressing %s\n",filez);
			exit(1);
		}
		unlink(filez); /* delete file.Z */
		free(filez);
	}
}
