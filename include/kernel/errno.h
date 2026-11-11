/*
 *----------------------------------------------------------------------
 *    T-Kernel 2.0 Software Package
 *
 *    Copyright 2011 by Ken Sakamura.
 *    This software is distributed under the latest version of T-License 2.x.
 *----------------------------------------------------------------------
 *
 *    Released by T-Engine Forum(http://www.t-engine.org/) at 2011/05/17.
 *    Modified by TRON Forum(http://www.tron.org/) at 2015/06/01.
 *
 *----------------------------------------------------------------------
 */

/*
 *	@(#)errno.h (T-Kernel)
 *
 *	T-Kernel Error Code
 */

#ifndef __TK_ERRNO_H__
#define __TK_ERRNO_H__

#ifndef _in_asm_source_
#include <kernel/typedef.h>

#define MERCD(er)	( (ER)(er) >> 16 )	/* Main error code */
#define SERCD(er)	( (H)(er) )		/* Sub-error code */
#define ERCD(mer, ser)	( (ER)(((UW)(mer) << 16) | ((UW)(ser) & 0x0000FFFF)) )
#else
#define ERCD(mer, ser)	( (ER)(((UW)(mer) << 16) | ((UW)(ser) & 0x0000FFFF)) )
#endif /* _in_asm_source_ */

#ifndef E_OK
#define E_OK		0		/* Completed successfully */
#endif

#ifndef E_SYS
#define E_SYS		ERCD(-5, 0)	/* System error */
#endif
#ifndef E_NOCOP
#define E_NOCOP		ERCD(-6, 0)	/* Coprocessor disable */
#endif
#ifndef E_NOSPT
#define E_NOSPT		ERCD(-9, 0)	/* Unsupported function */
#endif
#ifndef E_RSFN
#define E_RSFN		ERCD(-10, 0)	/* Reserved function code number */
#endif
#ifndef E_RSATR
#define E_RSATR		ERCD(-11, 0)	/* Reserved attribute */
#endif
#ifndef E_PAR
#define E_PAR		ERCD(-17, 0)	/* Parameter error */
#endif
#ifndef E_ID
#define E_ID		ERCD(-18, 0)	/* Incorrect ID number */
#endif
#ifndef E_CTX
#define E_CTX		ERCD(-25, 0)	/* Context error */
#endif
#ifndef E_MACV
#define E_MACV		ERCD(-26, 0)	/* Inaccessible memory/access violation */
#endif
#ifndef E_OACV
#define E_OACV		ERCD(-27, 0)	/* Object access violation */
#endif
#ifndef E_ILUSE
#define E_ILUSE		ERCD(-28, 0)	/* Incorrect system call use */
#endif
#ifndef E_NOMEM
#define E_NOMEM		ERCD(-33, 0)	/* Insufficient memory */
#endif
#ifndef E_LIMIT
#define E_LIMIT		ERCD(-34, 0)	/* Exceed system limits */
#endif
#ifndef E_OBJ
#define E_OBJ		ERCD(-41, 0)	/* Incorrect object state */
#endif
#ifndef E_NOEXS
#define E_NOEXS		ERCD(-42, 0)	/* Object does not exist */
#endif
#ifndef E_QOVR
#define E_QOVR		ERCD(-43, 0)	/* Queuing overflow */
#endif
#ifndef E_RLWAI
#define E_RLWAI		ERCD(-49, 0)	/* Forcibly release wait state */
#endif
#ifndef E_TMOUT
#define E_TMOUT		ERCD(-50, 0)	/* Polling fail/time out */
#endif
#ifndef E_DLT
#define E_DLT		ERCD(-51, 0)	/* Waited object was deleted */
#endif
#ifndef E_DISWAI
#define E_DISWAI	ERCD(-52, 0)	/* Release wait caused by wait disable */
#endif

#ifndef E_IO
#define E_IO		ERCD(-57, 0)	/* Output/input error */
#endif
#ifndef E_NOMDA
#define E_NOMDA		ERCD(-58, 0)	/* No media */
#endif
#ifndef E_BUSY
#define E_BUSY		ERCD(-65, 0)	/* Busy state */
#endif
#ifndef E_ABORT
#define E_ABORT		ERCD(-66, 0)	/* Aborted */
#endif
#ifndef E_RONLY
#define E_RONLY		ERCD(-67, 0)	/* write protected */
#endif

#endif /* __TK_ERRNO_H__ */
