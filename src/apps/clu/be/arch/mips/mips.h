#include <stdint.h>

/* outbufsize is the full capacity of outbuf, including the NUL. */
char * decodeMIPS(unsigned long int address, char *outbuf, int outbufsize, int *lendis, unsigned long int offset);
