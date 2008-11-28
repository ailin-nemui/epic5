/* $EPIC: tcl.c,v 1.13 2008/11/28 16:28:03 jnelson Exp $ */
/*
 * tcl.c -- The tcl interfacing routines.
 *
 * Copyright © 2001, 2006 EPIC Software Labs.
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
#include "ifcmd.h"
#include "extlang.h"
#include <tcl.h>
#ifdef TK
#include <tk.h>
#endif

Tcl_Interp *my_tcl;
int	istclrunning = 0;

/*
 * A new TCL command, [echo], which displays back on the epic window.
 */
static int	Tcl_echoCmd (ClientData clientData, Tcl_Interp *interp, int objc, const char **objv)
{
	int	i;
	size_t	clue = 0;
	char	*msg = NULL;
	for (i = 1; i < objc; i++)
		malloc_strcat_wordlist_c(&msg, space, objv[i], &clue);
	say("%s", msg);
	new_free(&msg);
	return TCL_OK;
}

/*
 * A new TCL command [epic] with several subcmds:
 *	[epic cmd ...]		Runs ... as a block of code w/o $ expansion.
 *	[epic eval ...]		Runs ... as block of code, with $ expansion.
 *				$* will be the empty string when expanded.
 *	[epic expr ...]		Evals ... as an expression, returns result.
 *	[epic call ...]		Call ... where ... is "$func(args)"
 *				Returning the result.  $* is the empty string.
 */
static int	Tcl_epicCmd (ClientData clientData, Tcl_Interp *interp, int objc, const char **objv)
{
	int	i;
	char	*retval = NULL;
	char	*arg = NULL;

	if (objc < 2) {
		Tcl_WrongNumArgs(interp, 0, NULL, "{cmd|eval|expr|call} ?epic-expression? ...");
		return TCL_ERROR;
	} else if (!strcmp(objv[1], "cmd")) {
		for (i = 2; i < objc; i++)
			runcmds("$*", objv[i]);
	} else if (!strcmp(objv[1], "eval")) {
		for (i = 2; i < objc; i++)
			runcmds(objv[i], "");
	} else if (!strcmp(objv[1], "expr")) {
		for (i = 2; i < objc; i++) {
			retval = (char*)parse_inline((arg = malloc_strdup(objv[i])), "");
			Tcl_AppendElement(interp, retval);
			new_free(&retval);
			new_free(&arg);
		}
	} else if (!strcmp(objv[1], "call")) {
		for (i = 2; i < objc; i++) {
			retval = (char*)call_function((arg = malloc_strdup(objv[i])), "");
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

/* A new TCL command [tkon] which turns on tk (natch?) */
#ifdef TK
static int	Tcl_tkonCmd (ClientData clientData, Tcl_Interp *interp, int objc, char **objv)
{
	Tk_Init(interp);
	return TCL_OK;
}
#endif

/* Called by the epic hooks to activate tcl on-demand. */
void	tclstartstop (int startnotstop) 
{
	if (startnotstop && !istclrunning) 
	{
		++istclrunning;
		my_tcl = Tcl_CreateInterp();
		Tcl_Init(my_tcl);
		Tcl_CreateCommand(my_tcl, "echo", Tcl_echoCmd, 
				(ClientData)NULL, (void (*)())NULL);
		Tcl_CreateCommand(my_tcl, "epic", Tcl_epicCmd, 
				(ClientData)NULL, (void (*)())NULL);
#ifdef TK
		Tcl_CreateCommand(my_tcl, "tkon", Tcl_tkonCmd, 
				(ClientData)NULL, (void (*)())NULL);
#endif
	} 
	else if (!startnotstop && istclrunning) 
	{
		Tcl_DeleteInterp(my_tcl);
		istclrunning=0;
	}
}

/*
 * Used by the $tcl(...) function: evalulate ... as a TCL statement, and 
 * return the result of the statement.
 */
char *	tcleval (char* input) 
{
	char *retval=NULL;

	if (input && *input) 
	{
		tclstartstop(1);
		Tcl_Eval(my_tcl, input);
		retval = malloc_strdup(Tcl_GetStringResult(my_tcl));
	}
	RETURN_MSTR(retval);
}

/*
 * The /TCL function: Evalulate the args as a TCL statement and ignore the 
 * return value of the statement.
 */
BUILT_IN_COMMAND(tclcmd)
{
        char *body, *x;

        if (*args == '{')
        {
                if (!(body = next_expr(&args, '{')))
                {
                        my_error("TCL: unbalanced {");
                        return;
                }
        }
        else
                body = args;

        x = tcleval(body);
        new_free(&x);
}

