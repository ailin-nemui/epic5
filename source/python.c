/*
 * python.c -- Calling Python from epic.
 *
 * Copyright 2016 EPIC Software Labs.
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
/* Python commit #4 */

#include "irc.h"
#include "ircaux.h"
#include "array.h"
#include "alias.h"
#include "commands.h"
#include "functions.h"
#include "output.h"
#include "ifcmd.h"
#include "extlang.h"
#include <Python.h>

static	int	p_initialized = 0;
static	PyObject *global_vars = NULL;

/*
 * I owe many thanks to https://docs.python.org/3.6/extending/embedding.html
 * for teaching how to embed and extend Python in a C program.
 */

/*
 * Psuedo-code of how to load up a python script and call an init function
 *
 * 1. Call Py_Initialize()
 */

/*************************************************************************/
/*
 * Extended Python by creating an "import epic" module.
 */
/*
 * Psuedo-code of how to create a python module that points at your C funcs
 *
 * All of these functions take one string argument.
 *  epic.echo	- yell() -- Like /echo, unconditionally output to screen
 *  epic.say	- say()	 -- Like /xecho -s, output if not suppressed with ^
 *  epic.cmd	- runcmds() -- Run a block of code, but don't expand $'s (like from the input line)
 *  epic.eval	- runcmds() -- Run a block of code, expand $'s, but $* is []  (this is lame)
 *  epic.expr	- parse_inline() -- Evaluate an expression string and return the result 
 *  epic.call	- call_function() -- Evaluate a "funcname(argument list)" string and return the result
 */

static	PyObject *	epic_echo (PyObject *self, PyObject *args)
{
	char *	str;

	if (!PyArg_ParseTuple(args, "s", &str)) {
		return PyInt_FromLong(-1);
	}

	yell(str);
	return PyInt_FromLong(0);
}

static	PyObject *	epic_say (PyObject *self, PyObject *args)
{
	char *	str;

	if (!PyArg_ParseTuple(args, "s", &str)) {
		return PyInt_FromLong(-1);
	}

	say(str);
	return PyInt_FromLong(0);
}

static	PyObject *	epic_cmd (PyObject *self, PyObject *args)
{
	char *	str;

	if (!PyArg_ParseTuple(args, "s", &str)) {
		return PyInt_FromLong(-1);
	}

	runcmds("$*", str);
	return PyInt_FromLong(0);
}

static	PyObject *	epic_eval (PyObject *self, PyObject *args)
{
	char *	str;

	if (!PyArg_ParseTuple(args, "s", &str)) {
		return PyInt_FromLong(-1);
	}

	runcmds(str, "");
	return PyInt_FromLong(0);
}

static	PyObject *	epic_expr (PyObject *self, PyObject *args)
{
	char *	str;
	char *	exprval;

	if (!PyArg_ParseTuple(args, "s", &str)) {
		return PyInt_FromLong(-1);
	}

	exprval = parse_inline(str, "");
	return PyBuildValue("s", exprval);
}

static	PyObject *	epic_call (PyObject *self, PyObject *args)
{
	char *	str;
	char *	funcval;

	if (!PyArg_ParseTuple(args, "s", &str)) {
		return PyInt_FromLong(-1);
	}

	funcval = call_function(str, "");
	return PyBuildValue("s", funcval);
}

static	PyMethodDef	epicMethods[] = {
	{ "echo", 	epic_echo, 	METH_VARARGS, 	"Unconditionally output to screen (yell)" },
	{ "say", 	epic_say, 	METH_VARARGS, 	"Output to screen unless suppressed (say)" },
	{ "cmd", 	epic_cmd, 	METH_VARARGS, 	"Run a block statement without expansion (runcmds)" },
	{ "eval", 	epic_eval, 	METH_VARARGS, 	"Run a block statement with expansion (but $* is empty)" },
	{ "expr", 	epic_expr, 	METH_VARARGS, 	"Return the result of an expression (parse_inline)" },
	{ "call", 	epic_call, 	METH_VARARGS, 	"Call a function with expansion (but $* is empty) (call_function)" },
	{ NULL,		NULL,		0,		NULL }
};

static	PyModuleDef	epicModule = {
	PyModuleDef_HEAD_INIT,	
	"_epic", 	NULL, 		-1,		epicMethods,
	NULL,		NULL,		NULL,		NULL
};

/* 
 * This is called via
 *	PyImport_AppendInittab("_epic", &PyInit_epic);
 * before
 *	PyInitialize();
 * in main().
 */
static	PyObject *	PyInit_epic (void)
{
	return PyModule_Create(&epicModule);
}


/***********************************************************/
/* 
 * Embed Python by allowing users to call a python function
 */
char *	python_eval (char *input)
{
	PyObject *retval;
	char 	*r, *retvalstr = NULL;

	if (p_initialized == 0)
	{
		PyImport_AppendInittab("_epic", &PyInit_epic);
		Py_Initialize();
		p_initialized = 1;
		global_vars = PyModule_GetDict(PyImport_AddModule("__main__"));
	}

	/* Convert retval to a string */
	retval = PyRun_String(input, Py_eval_input, global_vars, global_vars);
	PyArg_ParseTuple(retval, "s", &r);
	retvalstr = malloc_strdup(r);

	Py_XDECREF(retval);
	RETURN_MSTR(retvalstr);	
}

/*
 * The /PYTHON function: Evalulate the args as a PYTHON block and ignore the 
 * return value of the statement.
 */
BUILT_IN_COMMAND(pythoncmd)
{
        char *body, *x;

        if (*args == '{')
        {
                if (!(body = next_expr(&args, '{')))
                {
                        my_error("PYTHON: unbalanced {");
                        return;
                }
        }
        else
                body = args;

        x = python_eval(body);
        new_free(&x);
}

