#include <stdint.h>

/* outbufsize is the full capacity of outbuf, including the NUL. */
void decodeM68K(unsigned long int start, char *outbuf, int outbufsize, int *outlen, unsigned long int offset0);
