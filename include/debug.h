/*
 * debug.h -- the runtime debug settings.  Can also be done on command line.
 * Copyright 1997 EPIC Software Labs
 */

#ifndef __debug_h__
#define __debug_h__

extern 	unsigned long 	x_debug;
	BUILT_IN_COMMAND(xdebugcmd);
extern	char *	function_xdebug (char *);

#define DEBUG_LOCAL_VARS	1 << 0
#define DEBUG_1			1 << 1
#define DEBUG_CTCPS		1 << 2
#define DEBUG_DCC_SEARCH	1 << 3
#define DEBUG_OUTBOUND		1 << 4
#define DEBUG_INBOUND		1 << 5
#define DEBUG_DCC_XMIT		1 << 6
#define DEBUG_WAITS		1 << 7
#define DEBUG_8			1 << 8
#define DEBUG_SERVER_CONNECT	1 << 9
#define DEBUG_CRASH		1 << 10
#define DEBUG_11		1 << 11
#define DEBUG_NOTIFY		1 << 12
#define DEBUG_REGEX		1 << 13
#define DEBUG_REGEX_DEBUG	1 << 14
#define DEBUG_BROKEN_CLOCK	1 << 15
#define DEBUG_CHANNELS		1 << 16
#define DEBUG_UNKNOWN		1 << 17
#define DEBUG_18		1 << 18
#define DEBUG_19		1 << 19
#define DEBUG_NEW_MATH_DEBUG    1 << 20
#define DEBUG_21		1 << 21
#define DEBUG_EXTRACTW		1 << 22
#define DEBUG_SLASH_HACK	1 << 23
#define DEBUG_SSL		1 << 24
#define DEBUG_LASTLOG		1 << 25
#define DEBUG_EXTRACTW_DEBUG	1 << 26
#define DEBUG_MESSAGE_FROM	1 << 27
#define DEBUG_WHO_QUEUE		1 << 28
#define DEBUG_UNICODE		1 << 29
#define DEBUG_DWORD 1 << 30

#endif
