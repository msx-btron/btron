#ifndef _ARMADILLO_H_
#define _ARMADILLO_H_

#include <stdint.h>
#include "adefs.h"

int ArmadilloDisassemble(unsigned int opcode, unsigned long PC, struct ad_insn **out);
int ArmadilloDone(struct ad_insn **insn);
/* outbufsize is the full capacity of outbuf, including the NUL. */
char *decodeARM64(unsigned long int start, char *outbuf, int outbufsize, int *outlen, unsigned long int offset0);

#endif
