#if defined(__STDC_HOSTED__) && __STDC_HOSTED__ == 1 && !defined(BTRON_UEFI_TARGET)
#include_next <strings.h>
#else
#ifndef _GL_STRINGS_H_
#define _GL_STRINGS_H_

#include <string.h>

#endif /* _GL_STRINGS_H_ */
#endif /* HOSTED */

