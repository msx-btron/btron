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
 *      @(#)str_align.h (sys)
 *
 *	Bit alignment definitions for structure
 */

#ifndef __SYS_STR_ALIGN_H__
#define __SYS_STR_ALIGN_H__

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
 *
 *    Modified by T.Yokobayashi since 2015/11/25.
 *
 *----------------------------------------------------------------------
 */

/*
 *	@(#)str_align_common.h (sys) 2019/05/31
 *
 *	Bit alignment definitions for structure
 */

#ifndef __SYS_STR_ALIGN_COMMON_H__
#define __SYS_STR_ALIGN_COMMON_H__

#ifndef __SYS_STR_ALIGN_DEPEND_H__
#define __SYS_STR_ALIGN_DEPEND_H__

/* 32 bit alignment */
#if BIGENDIAN
#  define _pad_b(n)	int :n;
#  define _pad_l(n)
#else
#  define _pad_b(n)
#  define _pad_l(n)	int :n;
#endif

#define _align64

#endif /* __SYS_STR_ALIGN_DEPEND_H__ */


#endif /* __SYS_STR_ALIGN_COMMON_H__ */


/*----------------------------------------------------------------------*/
#if 0
#|History of "str_align_common.h"
#|-------------------------------
#|* 2015/11/25	[app_sh7760]用に、APP_SH7760 処理の追加
#|* 2016/03/01	[rpi_bcm283x]用に、RPI_BCM283x 処理の追加
#|* 2016/03/14	[app_rzt1]用に、APP_RZT1 処理の追加
#|
#endif


#endif /* __SYS_STR_ALIGN_H__ */
