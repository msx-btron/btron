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
 *	@(#)segment_common.h (sys) 2019/05/31
 *
 *	Segment management
 */

#ifndef __SYS_SEGMENT_COMMON_H__
#define __SYS_SEGMENT_COMMON_H__

#include <basic.h>

/*
 * System-dependent definition
 */
#ifndef __SYS_SEGMENT_DEPEND_H__
#define __SYS_SEGMENT_DEPEND_H__

#ifdef __cplusplus
extern "C" {
#endif

/* Access level definitions */
typedef enum {
	MapUser		= 0x02,
	MapSystem	= 0x00,
	MapRead		= 0x00,
	MapWrite	= 0x30,
	MapExecute	= 0x04
} MapModeLevel;

/*
 * MapMemory() attr
 */
#define MM_USER		0x020U	/* User */
#define MM_SYSTEM	0x000U	/* System */
#define MM_READ		0x000U	/* Read */
#define MM_WRITE	0x300U	/* Write */
#define MM_EXECUTE	0x040U	/* Execute */
#define MM_CDIS		0x00cU	/* Cache disabled */

#ifdef __cplusplus
}
#endif
#endif /* __SYS_SEGMENT_DEPEND_H__ */


#endif /* __SYS_SEGMENT_COMMON_H__ */


/*----------------------------------------------------------------------*/
#if 0
#|History of "segment_common.h"
#|-----------------------------
#|* 2015/11/25	[app_sh7760]用に、APP_SH7760 処理の追加
#|* 2016/03/01	[rpi_bcm283x]用に、APP_BCM283x 処理の追加
#|* 2016/03/14	[app_rzt1]用に、APP_RZT1 処理の追加
#|
#endif
