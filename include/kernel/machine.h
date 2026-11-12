/*
 *----------------------------------------------------------------------
 *    T-Kernel 2.0 Software Package
 *
 *    Copyright 2011 by Ken Sakamura.
 *    This software is distributed under the latest version of T-License 2.x.
 *----------------------------------------------------------------------
 */

/*
 *	@(#)machine.h (Kernel Architecture & Machine Definitions)
 *
 *	Machine type definition normalized in include/kernel/machine.h
 */

#ifndef __KERNEL_MACHINE_H__
#define __KERNEL_MACHINE_H__

#ifndef __MACHINE_H__
#define __MACHINE_H__
#endif

#ifndef __SYS_MACHINE_DEPEND_H__
#define __SYS_MACHINE_DEPEND_H__

/*
 * CPU_xxxx		CPU type
 * ALLOW_MISALIGN	1 if access to misalignment data is allowed
 * BIGENDIAN		1 if big endian
 * VIRTUAL_ADDRESS	1 if virtual memory
 * ALLOCA_NOSPT		1 if alloca() is not supported
 */

#ifndef _RPI_BCM283x_
#define _RPI_BCM283x_		1
#endif

#if (TYPE_RPI == 1)				/*** BCM2835 ***/
#define CPU_ARM1176			1
#define CPU_ARMv6			1
#define CPU_BCM2835			1
#elif (TYPE_RPI == 2)			/*** BCM2836 ***/
#define CPU_CORTEX_A7		1
#define CPU_ARMv7			1
#define CPU_BCM2836			1
#elif (TYPE_RPI == 3)			/*** BCM2837 ***/
#define CPU_CORTEX_A53		1
#define CPU_ARMv7			1
#define CPU_BCM2837			1
#endif

#define CPU_ARM				1
#define RPI_BCM283x			1
#define ALLOW_MISALIGN		0
#define BIGENDIAN			0
#define VIRTUAL_ADDRESS		1
#define ALLOCA_NOSPT		0

#ifndef _Csym
#define _Csym				0
#endif

#endif /* __SYS_MACHINE_DEPEND_H__ */

/* ===== Common definitions ============================================= */

#ifndef Inline
#ifdef __cplusplus
#define Inline		inline
#else
#define Inline		static __inline__
#endif
#endif

#ifndef Asm
#ifdef __GNUC__
#define Asm		__asm__ volatile
#endif
#endif

#if _Csym == 0
#define Csym(sym)	sym
#else
#define Csym(sym)	_##sym
#endif

#endif /* __KERNEL_MACHINE_H__ */

