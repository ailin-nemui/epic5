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
/* Python commit #3 */

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
 *	PyImport_AppendInittab("epic", &PyInit_epic);
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
 * Embed Python by allowing users to do a /pyload filename
 */
int	python_load (const char *filename)
{
	PyObject *pname, *pmodule, *pdict, *pfunc;
	PyObject *pargs, *pvalue;
	int	i;

	/* 1. Convert the filename into a python string */
	if (!(pname = PyUnicode_DecodeFSDefault(filename)))
	{
		yell("Failed to parse input filename: %s", filename);
		return -1;
	}

	/* 2. Import the file */
	pmodule = PyImport_Import(pname);
	Py_DECREF(pname);
	if (!pmodule)
	{
		PyErr_Print();
		yell("Failed to import module");
		return -1;
	}

#if 0
	/* 3. Find the __init__ function */
	pfunc = PyObject_GetAttrString(pmodule, "__init__");
	if (!pfunc || !PyCallable_Check(pfunc))
	{
		if (PyErr_Occurred())
			PyErr_Print();
		yell("Cannot find __init__ in this module");
	}

	/* 4. Create a tuple and put the commit_id in it */
	/* We use a tuple because maybe in the future we add more stuff. */
	pargs = PyTuple_New(1);	
	if (!(pvalue = PyLong_FromLong(commit_id)))
	{
		Py_DECREF(pargs);
		Py_DECREF(pmodule);
		say("Cannot convert argument to call __init__. Argh.");
		return -1;
	}
	PyTuple_SetItem(pargs, 0, pvalue);

	/* 5. Call the function, with the tuple as the argument */
	pvalue = PyObject_CallObject(pfunc, pargs);
	Py_DECREF(pargs);
	if (!pvalue) 
	{
		Py_DECREF(pfunc);
		Py_DECREF(pmodule);
		PyErr_Print();
		yell("Call to __init__ failed. argh.");
		return -1;
	}

	/* 6. Interpret the return value as an integer */
	retval = PyLong_AsLong(pvalue);
	if (retval == 0) 
		say("Initialization of module successful.");
	else
		say("Module load failed, returning error %ld", retval);

	/* 7. Clean up after ourselves */
	Py_DECREF(pvalue);
	Py_XDECREF(pfunc);
	Py_DECREF(pmodule);
#endif

	/* This is called at shut-down time, not every time! */
	/*
	if (Py_FinalizeEx() < 0) {
		return -1;
	*/
	return 0;
}


/*********************************************************/
/* 
 * Embed Python by allowing users to call a python function
 */
char *	python_eval (char *input)
{
	char 	*name_part, *module_str, *method_str;
	PyObject *module, *method;
	char 	*args_part;
	char 	*retval = NULL;

	GET_FUNC_ARG(name_part, input);
	module_str = name_part;
	if (!(method_str = strchr(module, '.')))
	{
		/* Error */
		RETURN_EMPTY;
	}
	if (!(module = PyState_FindModule(module_str)))
	{
		/* Error */
		RETURN_EMPTY;
	}

	if (input && *input) 
	{
		ruby_startstop(1);
		rubyval = rb_rescue2(internal_rubyeval, (VALUE)input, 
					eval_failed, 0,
					rb_eException, 0);
		if (rubyval == Qnil)
			retval = NULL;
		else
		{
			VALUE x;
			x = rb_obj_as_string(rubyval);
			retval = StringValuePtr(x);
		}
	}

	RETURN_STR(retval);	/* XXX Is this malloced or not? */
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

