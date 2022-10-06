/*
 * debug.h -- the runtime debug settings.  Can also be done on command line.
 * Copyright 1997 EPIC Software Labs
 */

#ifndef __debug_h__
#define __debug_h__

extern 	unsigned long 	x_debug;
	BUILT_IN_COMMAND(xdebugcmd);
extern	char *	function_xdebug (char *);

#define DEBUG_LOCAL_VARS	(1UL << 0)
#define DEBUG_1			(1UL << 1)
#define DEBUG_CTCPS		(1UL << 2)
#define DEBUG_DCC_SEARCH	(1UL << 3)
#define DEBUG_OUTBOUND		(1UL << 4)
#define DEBUG_INBOUND		(1UL << 5)
#define DEBUG_DCC_XMIT		(1UL << 6)
#define DEBUG_WAITS		(1UL << 7)
#define DEBUG_8			(1UL << 8)
#define DEBUG_SERVER_CONNECT	(1UL << 9)
#define DEBUG_CRASH		(1UL << 10)
#define DEBUG_NO_COLOR		(1UL << 11)
#define DEBUG_NOTIFY		(1UL << 12)
#define DEBUG_REGEX		(1UL << 13)
#define DEBUG_REGEX_DEBUG	(1UL << 14)
#define DEBUG_BROKEN_CLOCK	(1UL << 15)
#define DEBUG_CHANNELS		(1UL << 16)
#define DEBUG_UNKNOWN		(1UL << 17)
#define DEBUG_SEQUENCE_POINTS	(1UL << 18)
#define DEBUG_19		(1UL << 19)
#define DEBUG_NEW_MATH_DEBUG    (1UL << 20)
#define DEBUG_21		(1UL << 21)
#define DEBUG_EXTRACTW		(1UL << 22)
#define DEBUG_SLASH_HACK	(1UL << 23)
#define DEBUG_SSL		(1UL << 24)
#define DEBUG_LASTLOG		(1UL << 25)
#define DEBUG_EXTRACTW_DEBUG	(1UL << 26)
#define DEBUG_MESSAGE_FROM	(1UL << 27)
#define DEBUG_WHO_QUEUE		(1UL << 28)
#define DEBUG_UNICODE		(1UL << 29)
#define DEBUG_DWORD 		(1UL << 30)
#define DEBUG_RECODE            (1UL << 31)

#endif
