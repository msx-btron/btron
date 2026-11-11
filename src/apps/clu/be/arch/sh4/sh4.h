#include <stdint.h>
#include <string.h>

/* outbufsize is the full capacity of outbuf, including the NUL. */
char * decodeSH4(unsigned long int address, char *outbuf, int outbufsize, int *lendis, unsigned long int offset0);
