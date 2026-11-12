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
 *----------------------------------------------------------------------
 */

/*
 *	@(#)typedef.h (T-Kernel / TRON Standard Data Type Definitions)
 *
 *	Normalized & deduplicated in include/kernel/typedef.h
 */

#ifndef __KERNEL_TYPEDEF_H__
#define __KERNEL_TYPEDEF_H__

#ifndef __TYPEDEF_H__
#define __TYPEDEF_H__
#endif

#ifndef __TK_TYPEDEF_H__
#define __TK_TYPEDEF_H__
#endif

#include <kernel/machine.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef TKERNEL_CHECK_CONST
#define	CONST	const
#else
#define	CONST
#endif

/*
 * -----------------------------------------------------------------------------
 * 1. General-purpose TRON Fundamental Types (deduplicated against btron/types.h)
 * -----------------------------------------------------------------------------
 */
#if !defined(_BTRON_TYPES_H_)
typedef signed char	B;	/* Signed 8 bit integer */
typedef signed short	H;	/* Signed 16 bit integer */
#if defined(__LP64__) || defined(_LP64)
typedef signed int	W;	/* Signed 32 bit integer */
typedef signed long long D;	/* Signed 64 bit integer */
typedef unsigned char	UB;	/* Unsigned 8 bit integer */
typedef unsigned short  UH;	/* Unsigned 16 bit integer */
typedef unsigned int	UW;	/* Unsigned 32 bit integer */
typedef unsigned long long UD;	/* Unsigned 64 bit integer */
#else
typedef signed long	W;	/* Signed 32 bit integer */
typedef signed long long D;	/* Signed 64 bit integer */
typedef unsigned char	UB;	/* Unsigned 8 bit integer */
typedef unsigned short  UH;	/* Unsigned 16 bit integer */
typedef unsigned long	UW;	/* Unsigned 32 bit integer */
typedef unsigned long long UD;	/* Unsigned 64 bit integer */
#endif

typedef char		VB;	/* Nonuniform type 8 bit data */
typedef short		VH;	/* Nonuniform type 16 bit data */
typedef long		VW;	/* Nonuniform type 32 bit data */
typedef long long	VD;	/* Nonuniform type 64 bit data */
typedef void		*VP;	/* Nonuniform type data pointer */

typedef volatile B	_B;	/* Volatile statement attached */
typedef volatile H	_H;
typedef volatile W	_W;
typedef volatile D	_D;
typedef volatile UB	_UB;
typedef volatile UH	_UH;
typedef volatile UW	_UW;
typedef volatile UD	_UD;

typedef signed int	INT;	/* Processor bit width signed integer */
typedef unsigned int	UINT;	/* Processor bit width unsigned integer */

typedef INT		ID;	/* ID general */
typedef	W		MSEC;	/* Time general (millisecond) */

typedef void		(*FP)();	/* Function address general */
typedef INT		(*FUNCP)();	/* Function address general */

#define LOCAL		static		/* Local symbol definition */
#define EXPORT				/* Global symbol definition */
#define IMPORT		extern		/* Global symbol reference */

typedef UINT		BOOL;
#ifndef TRUE
#define TRUE		1	/* True */
#endif
#ifndef FALSE
#define FALSE		0	/* False */
#endif

typedef UH		TC;		/* TRON character code */
#ifndef TNULL
#define TNULL		((TC)0)		/* End of TRON code character string */
#endif
#endif /* !_BTRON_TYPES_H_ */

/*
 * -----------------------------------------------------------------------------
 * 2. T-Kernel OS Specification Types
 * -----------------------------------------------------------------------------
 */
typedef INT		FN;		/* Function code */
typedef INT		RNO;		/* Rendezvous number */
typedef UW		ATR;		/* Object/handler attribute */
#if !defined(_BTRON_TYPES_H_)
typedef INT		ER;		/* Error code */
#endif
typedef INT		PRI;		/* Priority */
typedef W		TMO;		/* Time out setting (ms) */
typedef D		TMO_U;		/* Time out setting (us) */
typedef UW		RELTIM;		/* Relative time (ms) */
typedef UD		RELTIM_U;	/* Relative time (us) */

typedef struct systim {			/* System time (ms) */
	W	hi;			/* Upper 32 bits */
	UW	lo;			/* Lower 32 bits */
} SYSTIM;

typedef	D	SYSTIM_U;		/* System time (us) */

/*
 * Common constants
 */
#ifndef NULL
#define NULL		0		/* Invalid address */
#endif
#ifndef TA_NULL
#define TA_NULL		0U		/* No special attributes indicated */
#endif
#ifndef TMO_POL
#define TMO_POL		0		/* Polling */
#endif
#ifndef TMO_FEVR
#define TMO_FEVR	(-1)		/* Permanent wait */
#endif

/* ------------------------------------------------------------------------ */

/*
 * 64 bits value
 */
typedef struct dw {
#if BIGENDIAN
	W	hi;	/* Upper 32 bits */
	UW	lo;	/* Lower 32 bits */
#else
	UW	lo;	/* Lower 32 bits */
	W	hi;	/* Upper 32 bits */
#endif
} DW;

#ifdef __cplusplus
}
#endif

#endif /* __KERNEL_TYPEDEF_H__ */
