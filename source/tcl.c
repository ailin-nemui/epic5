/* $EPIC: tcl.c,v 1.3 2003/07/09 21:10:25 jnelson Exp $ */
/*
 * tcl.c -- The tcl interfacing routines.
 *
 * Copyright © 2001 EPIC Software Labs.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notices, the above paragraph (the one permitting redistribution),
 *    this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The names of the author(s) may not be used to endorse or promote
 *    products derived from this software without specific prior written
 *    permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "irc.h"
#include "ircaux.h"
#include "array.h"
#include "alias.h"
#include "commands.h"
#include "functions.h"
#include "output.h"
#include <tcl.h>
#ifdef TK
#include <tk.h>
#endif

Tcl_Interp *my_tcl;
int	istclrunning = 0;

int Tcl_echoCmd (clientData, interp, objc, objv)
	ClientData	clientData;
	Tcl_Interp	*interp;
	int		objc;
	char		**objv;
{
	int	i;
	size_t	clue = 0;
	char	*msg = NULL;
	for (i = 1; i < objc; i++)
		m_sc3cat_s(&msg, space, objv[i], &clue);
	say("%s", msg);
	new_free(&msg);
	return TCL_OK;
}

int Tcl_epicCmd (clientData, interp, objc, objv)
	ClientData	clientData;
	Tcl_Interp	*interp;
	int		objc;
	char		**objv;
{
	int	i;
	unsigned food = 0;
	char	*retval = NULL;
	char	*arg = NULL;

	if (objc < 2) {
		Tcl_WrongNumArgs(interp, 0, NULL, "{cmd|eval|expr|call} ?epic-expression? ...");
		return TCL_ERROR;
	} else if (!strcmp(objv[1], "cmd")) {
		for (i = 2; i < objc; i++)
			parse_line(NULL, "$*", objv[i], 0, 0);
	} else if (!strcmp(objv[1], "eval")) {
		for (i = 2; i < objc; i++)
			parse_line(NULL, objv[i], "", 0, 0);
	} else if (!strcmp(objv[1], "expr")) {
		for (i = 2; i < objc; i++) {
			retval = (char*)parse_inline((arg = malloc_strdup(objv[i])), "", &food);
			Tcl_AppendElement(interp, retval);
			new_free(&retval);
			new_free(&arg);
		}
	} else if (!strcmp(objv[1], "call")) {
		for (i = 2; i < objc; i++) {
			retval = (char*)call_function((arg = malloc_strdup(objv[i])), "", &food);
			Tcl_AppendElement(interp, retval);
			new_free(&retval);
			new_free(&arg);
		}
	} else {
		Tcl_WrongNumArgs(interp, 0, NULL, "{cmd|eval|expr|call} ?epic-expression? ...");
		return TCL_ERROR;
	}
	return TCL_OK;
}

#ifdef TK
int Tcl_tkonCmd (clientData, interp, objc, objv)
	ClientData	clientData;
	Tcl_Interp	*interp;
	int		objc;
	char		**objv;
{
	Tk_Init(interp);
	return TCL_OK;
}
#endif

void tclstartstop (int startnotstop) {
	if (startnotstop && !istclrunning) {
		++istclrunning;
		my_tcl = Tcl_CreateInterp();
		Tcl_Init(my_tcl);
		Tcl_CreateCommand (my_tcl, "echo", Tcl_echoCmd, (ClientData)NULL, (void (*)())NULL);
		Tcl_CreateCommand (my_tcl, "epic", Tcl_epicCmd, (ClientData)NULL, (void (*)())NULL);
#ifdef TK
		Tcl_CreateCommand (my_tcl, "tkon", Tcl_tkonCmd, (ClientData)NULL, (void (*)())NULL);
#endif
	} else if (!startnotstop && istclrunning) {
		Tcl_DeleteInterp(my_tcl);
		istclrunning=0;
	}
}

char* tcleval (char* input) {
	char *retval=NULL;
	if (input && *input) {
		tclstartstop(1);
		Tcl_Eval(my_tcl, input);
		retval = malloc_strdup(Tcl_GetStringResult(my_tcl));
	};
	RETURN_MSTR(retval);
}
