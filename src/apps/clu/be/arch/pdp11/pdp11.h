#include <stdint.h>

/* outbufsize is the full capacity of outbuf, including the NUL. */
char * decodePDP11(unsigned long int address, char *outbuf, int outbufsize, int *len, unsigned long int offset0);
