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
/* Python commit #6 */

#include <Python.h>
#include "irc.h"
#include "ircaux.h"
#include "array.h"
#include "alias.h"
#include "commands.h"
#include "functions.h"
#include "output.h"
#include "ifcmd.h"
#include "extlang.h"

static	int	p_initialized = 0;
static	PyObject *global_vars = NULL;

/*
 * ObRant
 *
 * So Python is a language like ircII where there is a distinction made between
 * "statements" and "expressions".  In both languages, a "statement" is a call
 * that does not result in a return value, and expression is.  I can't blame 
 * python for this.  But it is different from Perl, Ruby, and TCL, which treat
 * everything as a "callable" and then you either get a return value (for an 
 * expression) or an empty string (for a statement).
 *
 * In order to make python support work, though, we have to honor this distinction.
 * I've chosen to do this through the /PYTHON command and the $python() function
 *
 * 	You can only use the /PYTHON command to run statements.
 *	Using /PYTHON with an expression will result in an exception being thrown.
 *
 *	You can only use the $python() function to evaluate expressions
 *	Using $python() with a statement will result in an exception being thrown.
 *
 * How do you know whether what you're doing is an expression or a statement, when
 * if you just throw everything into one file you don't have to worry about it?
 * Good question.  I don't know.  Good luck!
 */

/*
 * I owe many thanks to 
 *	https://docs.python.org/3.6/extending/embedding.html
 *	https://docs.python.org/3.6/c-api/veryhigh.html
 *	https://docs.python.org/3/c-api/init.html
 *	https://docs.python.org/3/c-api/exceptions.html
 *	https://www6.software.ibm.com/developerworks/education/l-pythonscript/l-pythonscript-ltr.pdf
 *	http://boost.cppll.jp/HEAD/libs/python/doc/tutorial/doc/using_the_interpreter.html
 *	  (for explaining what the "start" flag is to PyRun_String)
 * for teaching how to embed and extend Python in a C program.
 */

/*
 * I owe many many thanks to skully for working with me on this
 * and giving me good advice on how to make this not suck.
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
		return PyLong_FromLong(-1);
	}

	yell(str);
	return PyLong_FromLong(0);
}

static	PyObject *	epic_say (PyObject *self, PyObject *args)
{
	char *	str;

	if (!PyArg_ParseTuple(args, "s", &str)) {
		return PyLong_FromLong(-1);
	}

	say(str);
	return PyLong_FromLong(0);
}

static	PyObject *	epic_cmd (PyObject *self, PyObject *args)
{
	char *	str;

	if (!PyArg_ParseTuple(args, "s", &str)) {
		return PyLong_FromLong(-1);
	}

	runcmds("$*", str);
	return PyLong_FromLong(0);
}

static	PyObject *	epic_eval (PyObject *self, PyObject *args)
{
	char *	str;

	if (!PyArg_ParseTuple(args, "s", &str)) {
		return PyLong_FromLong(-1);
	}

	runcmds(str, "");
	return PyLong_FromLong(0);
}

static	PyObject *	epic_expr (PyObject *self, PyObject *args)
{
	char *	str;
	char *	exprval;

	if (!PyArg_ParseTuple(args, "s", &str)) {
		return PyLong_FromLong(-1);
	}

	exprval = parse_inline(str, "");
	return Py_BuildValue("s", exprval);
}

static	PyObject *	epic_call (PyObject *self, PyObject *args)
{
	char *	str;
	char *	funcval;

	if (!PyArg_ParseTuple(args, "s", &str)) {
		return PyLong_FromLong(-1);
	}

	funcval = call_function(str, "");
	return Py_BuildValue("s", funcval);
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
char *	python_eval_expression (char *input)
{
	PyObject *retval;
	PyObject *retval_repr;
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
	if (retval == NULL)
	{
		PyObject *ptype, *pvalue, *ptraceback;
		PyObject *ptype_repr, *pvalue_repr, *ptraceback_repr;
		char *ptype_str, *pvalue_str, *ptraceback_str;

		say("The python evaluation returned NULL. hrm.");
		PyErr_Fetch(&ptype, &pvalue, &ptraceback);
		if (ptype != NULL)
		{
			ptype_repr = PyObject_Repr(ptype);
			ptype_str = PyUnicode_AsUTF8(ptype_repr);
			say("Exception Type: %s", ptype_str);
		}
		if (pvalue != NULL)
		{
			pvalue_repr = PyObject_Repr(pvalue);
			pvalue_str = PyUnicode_AsUTF8(pvalue_repr);
			say("Value: %s", pvalue_str);
		}
		if (ptraceback != NULL)
		{
			ptraceback_repr = PyObject_Repr(ptraceback);
			ptraceback_str = PyUnicode_AsUTF8(ptraceback_repr);
			say("Traceback: %s", ptraceback_str);
		}
		Py_XDECREF(ptype);
		Py_XDECREF(pvalue);
		Py_XDECREF(ptraceback);
		RETURN_EMPTY;
	}

	retval_repr = PyObject_Repr(retval);
	r = PyUnicode_AsUTF8(retval_repr);
	retvalstr = malloc_strdup(r);

	Py_XDECREF(retval);
	Py_XDECREF(retval_repr);
	RETURN_MSTR(retvalstr);	
}

void	python_eval_statement (char *input)
{
	int	retval;

	if (p_initialized == 0)
	{
		PyImport_AppendInittab("_epic", &PyInit_epic);
		Py_Initialize();
		p_initialized = 1;
		global_vars = PyModule_GetDict(PyImport_AddModule("__main__"));
	}

	/* 
	 * XXX - This outputs to stdout (yuck) on exception.
	 * I need to figure out how to defeat that.
	 */
	if ((retval = PyRun_SimpleString(input)))
	{
		PyObject *ptype, *pvalue, *ptraceback;
		PyObject *ptype_repr, *pvalue_repr, *ptraceback_repr;
		char *ptype_str, *pvalue_str, *ptraceback_str;

		say("The python evaluation failed. hrm.");
		PyErr_Fetch(&ptype, &pvalue, &ptraceback);
		if (ptype != NULL)
		{
			ptype_repr = PyObject_Repr(ptype);
			ptype_str = PyUnicode_AsUTF8(ptype_repr);
			say("Exception Type: %s", ptype_str);
		}
		if (pvalue != NULL)
		{
			pvalue_repr = PyObject_Repr(pvalue);
			pvalue_str = PyUnicode_AsUTF8(pvalue_repr);
			say("Value: %s", pvalue_str);
		}
		if (ptraceback != NULL)
		{
			ptraceback_repr = PyObject_Repr(ptraceback);
			ptraceback_str = PyUnicode_AsUTF8(ptraceback_repr);
			say("Traceback: %s", ptraceback_str);
		}
		Py_XDECREF(ptype);
		Py_XDECREF(pvalue);
		Py_XDECREF(ptraceback);
		return;
	}
}


/*
 * The /PYTHON function: Evalulate the args as a PYTHON block and ignore the 
 * return value of the statement.
 */
BUILT_IN_COMMAND(pythoncmd)
{
        char *body;

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

        python_eval_statement(body);
}

