void weenixname(char *p);
void save(char *f);

void resetbuf();
void tapeflush();
int taperead();
void skipfile();
void inword(long *l,long *r);
void outword(register unsigned long l,register unsigned long r);
int nextword(long *l,long *r);
int remaining();

void opentape(char *name,int create,int writable);
void closetape();
void posnbot();
void posneot();
int getrec(char *buf,int len);
void putrec(char *buf,int len);
void tapemark();

int dirlist(int argc,char **argv,char *d);
void pack(char *file);
void unpack(char *file);
