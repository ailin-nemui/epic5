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
/* Python commit #14 */

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
#include "newio.h"
#include "server.h"

void	output_traceback (void);

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
 * In order to make python support work, though, we have to honor this 
 * distinction.  I've chosen to do this through the /PYTHON command and the 
 * $python() function
 *
 * 	You can only use the /PYTHON command to run statements.
 *	Using /PYTHON with an expression results in an exception being thrown.
 *
 *	You can only use the $python() function to evaluate expressions
 *	Using $python() with a statement results in an exception being thrown.
 *
 * How do you know whether what you're doing is an expression or a statement, 
 * when if you just throw everything into one file you don't have to worry 
 * about it? Good question.  I don't know.  Good luck!
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
 * These functions provide the basic facilities of ircII.  
 * All of these functions take one string argument.
 *   epic.echo	- yell() -- Like /echo, unconditionally output to screen
 *   epic.say	- say()	 -- Like /xecho -s, output if not suppressed with ^
 *   epic.cmd	- runcmds() -- Run a block of code, 
 * 			but don't expand $'s (like from the input line)
 *   epic.eval	- runcmds() -- Run a block of code, 
 *			expand $'s, but $* is []  (this is lame)
 *   epic.expr	- parse_inline() -- Evaluate an expression string 
 *			and return the result 
 *   epic.call	- call_function() -- Evaluate a "funcname(argument list)" 
 *			string and return the result
 *
 * These functions provide a route-around of ircII, if that's what you want.
 * All of these functions take a symbol name ("name") and a string containing 
 *	the arguments (if appropriate)
 * NONE OF THESE FUNCTIONS UNDERGO $-EXPANSION!  
 *	(If you need that, use the stuff ^^^ above)
 *
 *   epic.run_command - run an alias (preferentially) or a builtin command.
 *        Example: epic.run_command("xecho", "-w 0 hi there!  This does not get expanded")
 *   epic.call_function - call an alias (preferentially) or a builtin function
 *        Example: epic.call_function("windowctl", "refnums")
 *   epic.get_set - get a /SET value (only)
 *        Example: epic.get_set("mail")
 *   epic.get_assign - get an /ASSIGN value (only)
 *        Example: epic.get_assign("myvar")
 *   epic.get_var - get an /ASSIGN value (preferentially) or a /SET 
 *   epic.set_set - set a /SET value (only)
 *        Example: epic.set_set("mail", "ON")
 *   epic.set_assign - set a /ASSIGN value
 *        Example: epic.set_assign("myvar", "5")
 *
 * These functions allow you to register file descriptors (like socket) 
 * with epic, which will call back your method when they're interesting.
 *
 *   epic.callback_when_readable(python_file, python_function, python_object)
 *   epic.callback_when_writable(python_file, python_function, python_object)
 *	When 'python_function' is None, it cancels the callback.
 *	The 'python_function' needs to be a static function (?)
 *	The 'python_object' can be anything, i guess.
 */

/********************** Higher level interface to things *********************/

/*
 * epic.echo("hello, world") -- Output something without a banner (like /echo)
 *
 * Arguments:
 *	self - ignored (the "epic" module)
 *	args - A tuple containing
 *		1. A string containing stuff to output
 *
 * Return value:
 *	NULL - PyArg_ParseTuple() didn't like your tuple
 *	   0 - The stuff was displayed successfully.
 */
static	PyObject *	epic_echo (PyObject *self, PyObject *args)
{
	char *	str;

	/*
	 * https://docs.python.org/3/extending/extending.html
	 * tells me that if PyArg_ParseTuple() doesn't like the
	 * argument list, it will set an exception and return
	 * NULL.  And all i should do is return NULL.
	 * So that is why i do that here (and everywhere)
	 */
	if (!PyArg_ParseTuple(args, "z", &str)) {
		return NULL;
	}

	yell(str);
	return PyLong_FromLong(0L);
}

/*
 * epic.say("Warning, WIll Robinson") -- Output something with a banner
 *					(like internal commands do)
 *
 * Arguments:
 *	self - ignored (the "epic" module)
 *	args - A tuple containing
 *		1. A string containing stuff to output
 *
 * Notes: 
 *	The banner is (of course) /SET BANNER.
 *
 * Return value:
 *	NULL - PyArg_ParseTuple() didn't like your tuple (and threw exception)
 *	 0 - The stuff was displayed successfully.
 */
static	PyObject *	epic_say (PyObject *self, PyObject *args)
{
	char *	str;

	if (!PyArg_ParseTuple(args, "z", &str)) {
		return NULL;
	}

	say(str);
	return PyLong_FromLong(0L);
}

/*
 * epic.cmd("join #epic") -- Run an ircII command like at the input line
 *
 * Arguments:
 *	self - ignored (the "epic" module)
 *	args - A tuple containing
 *		1. A string containing an ircII command statement
 *
 * Notes:
 *	'args' is run as a single ircII statement (as if you had typed
 *	it at the command line).  $'s are not expanded; semicolons are
 *	not treated as statement separates, and braces have no special 
 *	meaning.
 *
 * Return value:
 *	NULL - PyArg_ParseTuple() didn't like your tuple (and threw exception)
 *	 0 - The stuff was run successfully.
 */
static	PyObject *	epic_cmd (PyObject *self, PyObject *args)
{
	char *	str;

	if (!PyArg_ParseTuple(args, "z", &str)) {
		return NULL;
	}

	runcmds("$*", str);
	return PyLong_FromLong(0L);
}

/*
 * epic.eval("echo My nick is $N") -- Run an ircII command line in an alias
 *
 * Arguments:
 *	self - ignored (the "epic" module)
 *	args - A tuple containing
 *		1. A string containing an ircII statement
 *
 * Notes: 
 * 	$* is treated as the empty string ([]) and so any $0, $1 type 
 *	expansions will expand to the empty string.  Maybe this will
 *	change in the future, and you'll be permitted to pass in $*.
 *
 * Return value:
 *	NULL - PyArg_ParseTuple() didn't like your tuple (and threw exception)
 *	 0 - The stuff ran successfully.
 */
static	PyObject *	epic_eval (PyObject *self, PyObject *args)
{
	char *	str;

	if (!PyArg_ParseTuple(args, "z", &str)) {
		return NULL;
	}

	runcmds(str, "");
	return PyLong_FromLong(0L);
}

/*
 * epic.expr("var + 2 * numonchannel()") -- Return the result of an expression
 *
 * Arguments:
 *	self - ignored (the "epic" module)
 *	args - A tuple containing
 *		1. A string containing an ircII expression
 *
 * Notes:
 *	$* is treated as the empty string ([]) and so any $0, $1 type
 *	expansions will expand to the empty string.  Maybe this will
 *	change in the future, and you'll be permitted to pass in $*.
 *
 * Return value:
 *	NULL - PyArg_ParseTuple() didn't like your tuple (and threw exception)
 *	NULL - Py_BuildValue() threw an exception converting the expr 
 *		result (a string) into a python string.
 *	A string - The result of the expression
 *		- All ircII expressions result in a string, even if that
 *		  string contains an integer or whatever.
 *		  ie, "2 + 2" is "4" == a string containing the number 4.
 */
static	PyObject *	epic_expr (PyObject *self, PyObject *args)
{
	char *	str;
	char *	exprval;
	PyObject *retval;

	if (!PyArg_ParseTuple(args, "z", &str)) {
		return NULL;
	}

	exprval = parse_inline(str, "");
	if (!(retval = Py_BuildValue("z", exprval))) {
		return NULL;
	}
	new_free(&exprval);
	return retval;
}

/*
 * epic.expand("text with ${2+2} $vars in it") -- Expand a string and return it
 *
 * Arguments: 
 *	self - ignored (the "epic" module)
 *	args - A tuple containing
 *		1. A string containing an ircII string
 *
 * Notes:
 *	$* is treated as the empty string ([]) and so any $0, $1 type 
 * 	expansions will expand to the empty string.  Maybe this will 
 *	change in the future, and you'll be permitted to pass in $*.
 *
 * Return value:
 *	NULL - PyArg_ParseTuple() didn't like your tuple (and threw exception)
 *	NULL - Py_BuildValue() couldn't convert the retval to python string (throws exception)
 *	A string - The result of the expansion
 */
static	PyObject *	epic_expand (PyObject *self, PyObject *args)
{
	char *	str;
	char *	expanded;
	PyObject *retval;

	if (!PyArg_ParseTuple(args, "z", &str)) {
		return NULL;
	}

	expanded = expand_alias(str, "");
	retval = Py_BuildValue("z", expanded);
	new_free(&expanded);
	return retval;
}

/*
 * epic.call("function(arglist)") -- Call a function directly (with $-expansion)
 *
 * Arguments:
 *	self - ignored (the "epic" module)
 *	args - A tuple containing
 *		1. A string containing a function call, with parenthesis
 *
 * Notes:
 *	The 'arglist' will be expanded ($-expandos will be honored)
 *	$* will be treated as the empty string (see the caveats above)
 *
 * Return value:
 *	NULL - PyArg_ParseTuple() didn't like your tuple (and threw exception)
 *	NULL - Py_BuildValue() couldn't convert the retval to python string 
 *		(throws exception)
 *	A string - The return value of the function call
 *		All function calls return exactly one string, even if
 *		that string contains a number or a list of words.
 *		You may need to process the string in python.
 */
static	PyObject *	epic_call (PyObject *self, PyObject *args)
{
	char *	str;
	char *	funcval;
	PyObject *retval;

	if (!PyArg_ParseTuple(args, "z", &str)) {
		return NULL;
	}

	funcval = call_function(str, "");
	retval = Py_BuildValue("z", funcval);
	new_free(&funcval);
	return retval;
}

/* Lower level interfaces to things */


/*
 * epic.run_command("COMMAND", "arglist") - Call a command (alias or builtin) 
 *					directly WITHOUT expansion
 *
 * Arguments:
 * 	self - ignored (the "epic" module)
 *	args - A tuple containing:
 *		1. A string containing an alias name or a built in command name
 *		2. A string representing the argument list
 *
 * Note: All ircII commands take one string as the argument, even if that string
 * 	contains some serialization of a collection.  Technically each command
 *	is permitted to do whatever it wants with its arguments; but the 
 *	convention is to accept a space separated list of words.  Again, this 
 *	is not a requirement, justthe way most things work.
 *
 * Note: The argument list is _NOT_ expanded; it is passed literally in to 
 * 		the cmd.
 * Note: XXX - The $* that is passed in is NULL; I'm not sure if this is 
 *		correct or not.
 * Note: Aliases are prefered to builtin commands, just like ircII does it.
 * 
 * Return Value:
 *	NULL             - PyArg_ParseTuple() didn't like your tuple 
 *					(and threw exception)
 *	NULL / NameError - The command you tried to run doesn't exist.
 *	None		 - The command was run successfully
 */
static	PyObject *	epic_run_command (PyObject *self, PyObject *args)
{
	char *	symbol;
	char *	my_args;
	PyObject *retval;
	void    (*builtin) (const char *, char *, const char *) = NULL;
const 	char *	alias = NULL;
	void *	arglist = NULL;

	if (!PyArg_ParseTuple(args, "zz", &symbol, &my_args)) {
		return NULL;
	}

	upper(symbol);
	alias = get_cmd_alias(symbol, &arglist, &builtin);
	if (alias) 
		call_user_command(symbol, alias, my_args, arglist);
	else if (builtin)
		builtin(symbol, my_args, empty_string);
	else
	{
		PyErr_Format(PyExc_NameError, "%s : Command does not exist", symbol);
		return NULL;
	}

	/* Success! */
	Py_INCREF(Py_None);
	return Py_None;
}


/*
 * epic.call_function("FUNCTION", "arglist") -- Call a function 
 *				(alias or builtin) directly WITHOUT expansion
 *
 * Arguments:
 *	self - ignored (the "epic" module)
 *	args - A tuple containing:
 *		1. A string containing an alias name or built in function name
 *		2. A string representing the argument list
 *
 * Note: All ircII functions take one string as their argument, even if that 
 *	string contains some representation of a collection.  Technically each
 *	function is permitted to do whatever it wants with its arguments; but 
 *	the convention is to accept a space separated list of words.  Again, 
 * 	this is not a requirement, just the way most things work.
 * 
 * Note: The argument list is _NOT_ expanded; it is passed literally to the fn.
 * Note: XXX - The $* that is passed in is NULL; 
 *		I'm not sure if this is correct or not.
 * Note: Aliases are preferred to builtins, just like ircII.
 * 
 * Return Value:
 *	NULL             - PyArg_ParseTuple() didn't like your tuple 
 *					(and threw exception)
 *	NULL / NameError - The function you tried to run doesn't exist.
 *	A string - the return value of the function (which may be zero-length for its own reasons)
 */
static	PyObject *	epic_call_function (PyObject *self, PyObject *args)
{
	char *	symbol;
	char *	my_args;
	PyObject *retval;
	const char *alias;
	void *	arglist;
	char * (*builtin) (char *) = NULL;
	char *	funcval = NULL;

	if (!PyArg_ParseTuple(args, "zz", &symbol, &my_args)) {
		return PyLong_FromLong(-1);
	}

	upper(symbol);
        alias = get_func_alias(symbol, &arglist, &builtin);
        if (alias)
                funcval = call_user_function(symbol, alias, my_args, arglist);
	else if (builtin)
                funcval = builtin(my_args);
	else
	{
		PyErr_Format(PyExc_NameError, "%s : Function does not exist", symbol);
		return NULL;
	}

	retval = Py_BuildValue("z", funcval);
	new_free(&funcval);
	return retval;
}

/*
 * epic.get_set("SETNAME") - Return the value of /SET SETNAME
 *
 * Arguments:
 * 	self - ignored (the "epic" module)
 *	args - A tuple containing:
 *		1. A /SET name (ie, "AUTO_REJOIN")
 *
 * Note: Although /SETs have types (integer, boolean, string), the 
 *	value of a SET is always a string.
 *
 * Return value:
 *	NULL             - PyArg_ParseTuple() didn't like your tuple 
 *					(and threw exception)
 *	NULL / NameError - The set you tried to fetch doesn't exist.
 *	A string - the value of /SET SETNAME 
 */
static	PyObject *	epic_get_set (PyObject *self, PyObject *args)
{
	char *	symbol;
	PyObject *retval;
	char *	funcval = NULL;

	if (!PyArg_ParseTuple(args, "z", &symbol)) {
		return NULL;
	}
 
	upper(symbol);
	if (make_string_var2(symbol, &funcval) < 0)
	{
		PyErr_Format(PyExc_NameError, "%s : SET variable does not exist", symbol);
		return NULL;
	}

	retval = Py_BuildValue("z", funcval);
	new_free(&funcval);
	return retval;
}

/*
 * epic.get_assign("VARNAME") - Return the value of /ASSIGN VARNAME
 * 
 * Arguments:
 *	self - ignored (the "epic" module)
 *	args - A tuple containing:
 *		1. A /ASSIGN name (ie, "MYVAR")
 *
 * Note: Variables are always strings, even if they contain a number.
 *
 * Return value:
 *	NULL             - PyArg_ParseTuple() didn't like your tuple 
 *					(and threw exception)
 *	NULL / NameError - The set you tried to fetch doesn't exist.
 *	A string - the value of /ASSIGN VARNAME (or empty if it does not exist)
 *		XXX - This is probably wrong, asking for an unknown 
 *			assign should probably return None or throw an 
 *			exception or something.
 */
static	PyObject *	epic_get_assign (PyObject *self, PyObject *args)
{
	char *	symbol;
	PyObject *retval;
	char *	funcval = NULL;
        char *		(*efunc) (void) = NULL;
        IrcVariable *	sfunc = NULL;
	const char *	assign = NULL;

	if (!PyArg_ParseTuple(args, "z", &symbol)) {
		return NULL;
	}

	upper(symbol);
        if (!(assign = get_var_alias(symbol, &efunc, &sfunc)))
	{
		PyErr_Format(PyExc_NameError, "%s : ASSIGN variable does not exist", symbol);
		return NULL;
	}

	retval = Py_BuildValue("z", assign);
	/* _DO NOT_ FREE 'assign'! */
	return retval;
}

/*  
 * epic.get_var("NAME") - Return the value of a variable [see notes]
 *
 * Arguments: 
 *	self - ignored (the "epic" module)
 *	args - A tuple containing:
 *		1. A variable symbol name of some sort
 *
 * Note:  A variable name can be a local variable, a global variable, or a set 
 *	variable. The return value is what you would get if you did $<NAME>.
 * Note:  XXX - Because of how this is implemented, this supports ":local" and 
 *	"::global", although I'm not sure it will stay this way forever.
 *
 * Return value:
 *	NULL             - PyArg_ParseTuple() didn't like your tuple 
 *					(and threw exception)
 *	A string - The value of $<NAME>
 *		The zero-length string if $<NAME> does not exist.
 *
 * Note: Because of how ircII does this, it is not possible to differentiate
 *	between a variable that does not exist and a variable that is unset.
 *	If you need that level of granularity (which should be unusual)
 *	you should use epic.get_var(), epic.get_assign(), etc.
 */
static	PyObject *	epic_get_var (PyObject *self, PyObject *args)
{
	char *	symbol;
	PyObject *retval;
	char *	funcval = NULL;

	if (!PyArg_ParseTuple(args, "z", &symbol)) {
		return NULL;
	}

	upper(symbol);
	funcval = get_variable(symbol); 
	retval = Py_BuildValue("z", funcval);
	new_free(&funcval);
	return retval;
}

/*
 * epic.set_set("setname", "value") -- do a /SET
 * XXX - TODO - XXX
 *
 * Arguments:
 *	self - ignored (the "epic" module)
 *	args - A tuple containing
 *		1. A string - the SET to be set
 *		2. A string - the value to be SET
 * Note:
 *	SETs have types, which are not currently exposed through this
 *	interface.  You need to pass in a string that contains a value
 *	of the correct type (ie, if the SET is an integer, you need to
 *	pass in a string containing an integer.  If the SET is a boolean,
 *	you need to pass in a string containing "TRUE" or "FALSE".)  
 * 	Who knows -- this may improve in the future.
 *
 * Return value:
 *	NULL             - PyArg_ParseTuple() didn't like your tuple 
 *					(and threw exception)
 *	NULL / NotImplelmentedError	- This function is not implemented yet
 */
static	PyObject *	epic_set_set (PyObject *self, PyObject *args)
{
	char *	symbol;
	char *	value;
	PyObject *retval;

	if (!PyArg_ParseTuple(args, "zz", &symbol, &value)) {
		return NULL;
	}

	/* XXX - TODO - /SET symbol value */
	PyErr_Format(PyExc_NotImplementedError, "Not Implemented Yet");
	return NULL;

	return PyLong_FromLong(0);
}

/*
 * epic.set_assign("varname", "value") -- do an /ASSIGN
 * XXX - TODO - XXX
 *
 * Arguments: 
 *	self - ignored (the "epic" module)
 *	args - A tuple containing
 *		1. A string -- the variable to be /ASSIGNed
 *		2. A string -- the value to be /ASSIGNED
 *
 * Return value:
 *	NULL             - PyArg_ParseTuple() didn't like your tuple 
 *					(and threw exception)
 *	NULL / NotImplelmentedError	- This function is not implemented yet
 */
static	PyObject *	epic_set_assign (PyObject *self, PyObject *args)
{
	char *	symbol;
	char *	value;
	PyObject *retval;

	if (!PyArg_ParseTuple(args, "zz", &symbol, &value)) {
		return NULL;
	}

	/* XXX - TODO - /ASSIGN symbol value */
	PyErr_Format(PyExc_NotImplementedError, "Not Implemented Yet");
	return NULL;

}

/* * * * * * * * * */
typedef struct {
	int		vfd;
	PyObject *	read_callback;
	PyObject *	write_callback;
	PyObject *	except_callback;
	int		flags;
} PythonFDCallback;

/* This should be in a header file somewhere... */
#if defined(HAVE_SYSCONF) && defined(_SC_OPEN_MAX)
#  define IO_ARRAYLEN   sysconf(_SC_OPEN_MAX)
#else
# if defined(FD_SETSIZE)
#  define IO_ARRAYLEN   FD_SETSIZE
# else
#  define IO_ARRAYLEN   NFDBITS
# endif
#endif

PythonFDCallback **python_vfd_callback = NULL;

void	init_python_fd_callbacks (void)
{
	int	vfd;
	int	max_fd = IO_ARRAYLEN;

	if (python_vfd_callback)
		return;		/* Panic is so 12008 */

	python_vfd_callback = (PythonFDCallback **)new_malloc(sizeof(PythonFDCallback *) * max_fd);
	for (vfd = 0; vfd < max_fd; vfd++)
		python_vfd_callback[vfd] = NULL;
}

PythonFDCallback *	get_python_vfd_callback (int vfd)
{
	if (vfd < 0 || vfd > IO_ARRAYLEN)
		return NULL;

	return python_vfd_callback[vfd];
}

int	set_python_vfd_callback (int vfd, PythonFDCallback *callback)
{
	if (callback == NULL)
		return -1;

	if (python_vfd_callback[vfd] && python_vfd_callback[vfd] != callback)
		return -1;

	if (callback->vfd == -1)
		callback->vfd = vfd;

	if (vfd != callback->vfd)
		return -1;

	python_vfd_callback[vfd] = callback;
	return 0;
}

PythonFDCallback *	new_python_fd_callback (int vfd)
{
	PythonFDCallback *callback;

	callback= (PythonFDCallback *)new_malloc(sizeof(PythonFDCallback));
	callback->vfd = vfd;
	callback->read_callback = NULL;
	callback->write_callback = NULL;
	callback->except_callback = NULL;
	callback->flags = 0;
	set_python_vfd_callback(vfd, callback);
	return callback;
}

int	destroy_python_fd_callback (int vfd)
{
	PythonFDCallback *callback;

	if (!(callback = python_vfd_callback[vfd]))
		return -1;

	callback->vfd = -1;
	callback->read_callback = NULL;
	callback->write_callback = NULL;
	callback->except_callback = NULL;
	callback->flags = 0;
	python_vfd_callback[vfd] = NULL;
	new_free((void **)&callback);
	return 0;
}



void 	call_python_function_1arg (PyObject *pFunc, int vfd)
{
	PyObject *args_py = NULL;
	PyObject *pArgs = NULL, *pRetVal = NULL;
	PyObject *retval_repr = NULL;
	char 	*r = NULL, *retvalstr = NULL;

	if (!PyCallable_Check(pFunc))
	{
		my_error("python_fd_callback: The callback was not a function");
		goto c_p_f_error;
	}

	if (!(pArgs = PyTuple_New(1)))
		goto c_p_f_error;

	if (!(args_py = Py_BuildValue("i", vfd)))
		goto c_p_f_error;

	if ((PyTuple_SetItem(pArgs, 0, args_py))) 
		goto c_p_f_error;
	args_py = NULL;			/* args_py now belongs to the tuple! */

	if (!(pRetVal = PyObject_CallObject(pFunc, pArgs)))
		goto c_p_f_error;

        if (!(retval_repr = PyObject_Repr(pRetVal)))
		goto c_p_f_error;

	goto c_p_f_cleanup;

c_p_f_error:
	output_traceback();

c_p_f_cleanup:
	Py_XDECREF(pArgs);
	Py_XDECREF(pRetVal);
}

void	do_python_fd (int vfd)
{
	PythonFDCallback *callback;
	char	buffer[BIG_BUFFER_SIZE];
	int	n;

	if (!((callback = get_python_vfd_callback(vfd))))
	{
		yell("do_python_fd: FD %d doesn't belong to me - new_close()ing it.", vfd);
		new_close(vfd);
	}

	if ((n = dgets(vfd, buffer, BIG_BUFFER_SIZE, -1)) > 0)
	{
		if (callback->read_callback)
			call_python_function_1arg(callback->read_callback, vfd);
		else if (callback->write_callback)
			call_python_function_1arg(callback->write_callback, vfd);
	}
	else if (n < 0)
	{
		if (callback->except_callback)
			call_python_function_1arg(callback->except_callback, vfd);
	}
}


void	do_python_fd_failure (int vfd, int error)
{
	PythonFDCallback *callback;

	if (!((callback = get_python_vfd_callback(vfd))))
	{
		yell("do_python_fd_failure: FD %d doesn't belong to me - new_close()ing it myself.", vfd);
		new_close(vfd);
	}

	if (callback->except_callback)
		call_python_function_1arg(callback->except_callback, vfd);
}


/*
 * epic.callback_when_readable(fd, read_callback, except_function, flags)
 * 
 * Arguments:
 *	self - ignored (the "epic" module)
 *	args - A tuple containing
 *		1. fd - An integer - a unix file descriptor (from python)
 *		2. read_callback - A CallableObject (ie, a method) - A python
 *				method that takes one integer argument. It will
 *				be called each time the 'fd' is readable.
 *				You must "handle" the fd, or it will busy-loop.
 *				If you block while handling the fd, epic 
 *				will block.
 *					Arg1 - fd
 *		3. except_callback - A CallableObject (ie, a method) - A python
 *				method that takes two integer arguments. It will
 *				be called if the 'fd' becomes invalid.  This 
 *				would happen if somneone close()d the FD, but
 *				didn't call _epic.cancel_callback().
 *				You should call _epic.cancel_callback(), or 
 *				it will busy-loop.  If you block while handling
 *				the fd, epic will block.
 *					Arg1 - fd, Arg2 - error code
 *		4. flags - Reserved for future expansion.  Pass in 0 for now.
 *
 * Return value:
 *	NULL 		- PyArg_ParseTuple() didn't like your tuple
 *					(and threw exception)
 *	0		- The command succeeded
 */
static	PyObject *	epic_callback_when_readable (PyObject *self, PyObject *args)
{
	long	vfd, flags;
	PyObject *read_callback, *except_callback;
	PythonFDCallback *callback;

	/*
	 * https://docs.python.org/3/extending/extending.html
	 * tells me that if PyArg_ParseTuple() doesn't like the
	 * argument list, it will set an exception and return
	 * NULL.  And all i should do is return NULL.
	 * So that is why i do that here (and everywhere)
	 */
	if (!PyArg_ParseTuple(args, "lOOl", &vfd, &read_callback, &except_callback, &flags)) {
		return NULL;
	}

	/*
	 * 1. Look up python fd record, create if necessary
	 * 2. Set read_callback in the python fd record
	 * 3. Call new_open() if necessary
	 */
	if (!(callback = get_python_vfd_callback(vfd)))
		callback = new_python_fd_callback(vfd);
	else
	{
		if (callback->write_callback)
			new_close_with_option(vfd, 1);
	}

	callback->write_callback = NULL;
	callback->read_callback = read_callback;
	callback->except_callback = except_callback;
	callback->flags = flags;

	new_open(vfd, do_python_fd, NEWIO_PASSTHROUGH_READ, 0, from_server);
	new_open_failure_callback(vfd, do_python_fd_failure);
	return PyLong_FromLong(0L);
}

/*
 * epic.callback_when_writable(fd, write_callback, except_function, flags)
 * 
 * Arguments:
 *	self - ignored (the "epic" module)
 *	args - A tuple containing
 *		1. fd - An integer - a unix file descriptor (from python)
 *		2. read_callback - A CallableObject (ie, a method) - A python
 *				method that takes one integer argument. It will
 *				be called each time the 'fd' is writable.
 *				You must "handle" the fd, or it will busy-loop.
 *				If you block while handling the fd, epic 
 *				will block.
 *		3. except_callback - A CallableObject (ie, a method) - A python
 *				method that takes one integer argument. It will
 *				be called if the 'fd' becomes invalid.  This 
 *				would happen if somneone close()d the FD, but
 *				didn't call _epic.cancel_callback().
 *				You should call _epic.cancel_callbacks(), or 
 *				it will busy-loop.  If you block while handling
 *				the fd, epic will block.
 *		4. flags - Reserved for future expansion.  Pass in 0 for now.
 *
 * Return value:
 *	NULL 		- PyArg_ParseTuple() didn't like your tuple
 *					(and threw exception)
 *	0		- The command succeeded
 */
static	PyObject *	epic_callback_when_writable (PyObject *self, PyObject *args)
{
	long	vfd, flags;
	PyObject *write_callback, *except_callback;
	PythonFDCallback *callback;

	/*
	 * https://docs.python.org/3/extending/extending.html
	 * tells me that if PyArg_ParseTuple() doesn't like the
	 * argument list, it will set an exception and return
	 * NULL.  And all i should do is return NULL.
	 * So that is why i do that here (and everywhere)
	 */
	if (!PyArg_ParseTuple(args, "lOOl", &vfd, &write_callback, &except_callback, &flags)) {
		return NULL;
	}

	/*
	 * 1. Look up python fd record, create if necessary
	 * 2. Set write_callback in the python fd record
	 * 3. Call new_open() if necessary.
	 */
	if (!(callback = get_python_vfd_callback(vfd)))
		callback = new_python_fd_callback(vfd);
	else
	{
		if (callback->read_callback)
			new_close_with_option(vfd, 1);
	}

	callback->read_callback = NULL;
	callback->write_callback = write_callback;
	callback->except_callback = except_callback;
	callback->flags = flags;

	new_open(vfd, do_python_fd, NEWIO_PASSTHROUGH_WRITE, 0, from_server);
	new_open_failure_callback(vfd, do_python_fd_failure);
	return PyLong_FromLong(0L);
}

/*
 * epic.cancel_callback(fd)
 * 
 * Arguments:
 *	self - ignored (the "epic" module)
 *	args - A tuple containing
 *		1. fd - An integer - a unix file descriptor (from python)
 *
 * Return value:
 *	NULL 		- PyArg_ParseTuple() didn't like your tuple
 *					(and threw exception)
 *	0		- The command succeeded
 */
static	PyObject *	epic_cancel_callback (PyObject *self, PyObject *args)
{
	long	vfd;
	PythonFDCallback *callback;

	/*
	 * https://docs.python.org/3/extending/extending.html
	 * tells me that if PyArg_ParseTuple() doesn't like the
	 * argument list, it will set an exception and return
	 * NULL.  And all i should do is return NULL.
	 * So that is why i do that here (and everywhere)
	 */
	if (!PyArg_ParseTuple(args, "l", &vfd)) {
		return NULL;
	}

	/*
	 * 1. Look up python fd record (bail if does not exist)
	 * 2. Call new_close_with_option()
	 * 3. Clear the python fd record
	 * (4. The python script has to close the fd...)
	 */
	if ((callback = get_python_vfd_callback(vfd)))
	{
		callback->read_callback = NULL;
		callback->write_callback = NULL;
		callback->except_callback = NULL;
		callback->flags = 0;
		new_close_with_option(vfd, 1);
	}

	return PyLong_FromLong(0L);
}

/*
 * INTERNAL USE ONLY --
 * When you want to register a python module.method as a hardcoded
 * builtin ircII command, you need a BUILT_IN_COMMAND() to be registered
 * as the callback.  Because of how ircII works, the /COMMAND that you 
 * run ends up in 'command', so this shim just does the glue:
 *
 * In python:
 *   epic.builtin_cmd("module.method") 
 * In ircII:
 *   /MODULE.METHOD ...args...
 *
 * XXX - It's not clear if 'subargs' should be passed to the python function.
 */
BUILT_IN_COMMAND(pyshim)
{
	char *	retval = NULL;

	retval = call_python_directly(command, args);
	new_free(&retval);
}

/*
 * epic.builtin_cmd("module.method") -- Register a python module.method 
 *					as a builtin ircII cmd
 * Arguments:
 *	self - ignored (the "epic" object)
 *	args - A tuple containing
 *		1. A string - the name of "module.method")
 *			This will become /MODULE.METHOD in ircII.
 *
 * Return value:
 *	NULL     - PyArg_ParseTuple() didn't like your tuple 
 *				(and threw exception)
 *	 None 	- The command was registered successfully
 */
static	PyObject *	epic_builtin_cmd (PyObject *self, PyObject *args)
{
	char *	symbol;

	if (!PyArg_ParseTuple(args, "z", &symbol)) {
		return NULL;
	}

	/* XXX TODO - Test this -- does 'symbol' need to be strdup()d?  XXX TODO */
	add_builtin_cmd_alias(symbol, pyshim);

	/* Success! */
	Py_INCREF(Py_None);
	return Py_None;
}


static	PyMethodDef	epicMethods[] = {
      /* Higher level facilities  - $-expansion supported */
	{ "echo", 	   epic_echo, 	METH_VARARGS, 	"Unconditionally output to screen (yell)" },
	{ "say", 	   epic_say, 	METH_VARARGS, 	"Output to screen unless suppressed (say)" },
	{ "cmd", 	   epic_cmd, 	METH_VARARGS, 	"Run a block statement without expansion (runcmds)" },
	{ "eval", 	   epic_eval, 	METH_VARARGS, 	"Run a block statement with expansion (but $* is empty)" },
	{ "expr", 	   epic_expr, 	METH_VARARGS, 	"Return the result of an expression (parse_inline)" },
	{ "call", 	   epic_call, 	METH_VARARGS, 	"Call a function with expansion (but $* is empty) (call_function)" },
	{ "expand",	   epic_expand,	METH_VARARGS,	"Expand some text with $s" },

      /* Lower level facilities - $-expansion NOT supported */
	{ "run_command",   epic_run_command,	METH_VARARGS,	"Run an alias or builtin command" },
	{ "call_function", epic_call_function,	METH_VARARGS,	"Call an alias or builtin function" },
	{ "get_set",       epic_get_set,	METH_VARARGS,	"Get a /SET value (only)" },
	{ "get_assign",    epic_get_assign,	METH_VARARGS,	"Get a /ASSIGN value (only)" },
	{ "get_var",       epic_get_assign,	METH_VARARGS,	"Get a variable (either /ASSIGN or /SET)" },
	{ "set_set",       epic_set_set,	METH_VARARGS,	"Set a /SET value (only)" },
	{ "set_assign",    epic_set_assign,	METH_VARARGS,	"Set a /ASSIGN value (only)" },
	{ "builtin_cmd",   epic_builtin_cmd,	METH_VARARGS,	"Make a Python function an EPIC builtin command" },

      /* Lower level IO facilities */
	{ "callback_when_readable",  epic_callback_when_readable, METH_VARARGS,	"Register a python function for FD event callbacks" },
	{ "callback_when_writable",  epic_callback_when_writable, METH_VARARGS,	"Register a python function for FD event callbacks" },
	{ "cancel_callback",         epic_cancel_callback,        METH_VARARGS,	"Unregister FD event callbacks" },

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
 * python_eval_expression - Call a Python Expression -- the $python() function
 *
 * Note: This can only be used to call a Python Expression.
 * 	It cannot be used to call a python statement.
 *
 * See at the top of the file -- Python makes a distinction between
 * statements and expressions (like ircII does) and has different 
 * API calls for each (PyRun_String() and PyRun_SimpleString()).
 * 
 * Arguments:
 *	input - A string to pass to the Python interpreter.  
 *		It must be a python expression, or python will throw exception.
 *
 * Return Value:
 *	The return value of the python expression, as a new_malloc()ed string.
 * 	YOU are responsible for new_free()ing the value later (if you call this
 *	other than as the $python() function, of course)
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
		init_python_fd_callbacks();
	}

	/*
	 * https://docs.python.org/3/c-api/veryhigh.html
 	 * says that this returns NULL if an exception is raised.
	 * So in that case, we try to handle/format the exception.
	 */
	if (!(retval = PyRun_String(input, Py_eval_input, global_vars, global_vars)))
		output_traceback();

	/* Convert retval to a string */
	retval_repr = PyObject_Repr(retval);
	r = PyUnicode_AsUTF8(retval_repr);
	retvalstr = malloc_strdup(r);

	Py_XDECREF(retval);
	Py_XDECREF(retval_repr);
	RETURN_MSTR(retvalstr);	
}

/*
 * python_eval_statement -- Call a Python Statement -- The /PYTHON function
 * 
 * Note: This can only be used to call a Python Statement.
 *	It cannot be used to call a python expression.
 *
 * See at the top of the file -- Python makes a distinction between
 * statements and expressions (like ircII does) and has different 
 * API calls for each (PyRun_String() and PyRun_SimpleString()).
 * 
 * Arguments:
 *	input - A string to pass to the Python interpreter.  
 *		It must be a python statement, or python will throw exception
 *
 * Note: It's not clear if the statement throws an exception whether or not
 *	we can catch and format it.  We'll see!
 */
void	python_eval_statement (char *input)
{
	if (p_initialized == 0)
	{
		PyImport_AppendInittab("_epic", &PyInit_epic);
		Py_Initialize();
		p_initialized = 1;
		global_vars = PyModule_GetDict(PyImport_AddModule("__main__"));
		init_python_fd_callbacks();
	}

	/* 
	 * https://docs.python.org/3/c-api/veryhigh.html
	 * says that "returns 0 on success or -1 if an exception is raised.
	 * If there was an error, there is no way to get the exception
	 * information;"  That's not 100% clear.  So I decided to assume 
	 * that if -1 is returned, we should see if there is exception 
	 * information and output it if we can.  There is the possibility
	 * that python has just dumped the exception to stdout and that
	 * we are out of luck for reformatting it.
	 */
	if (PyRun_SimpleString(input))
		output_traceback();
}


/*
 * The /PYTHON command: Evalulate the args as a PYTHON block and ignore the 
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

/*
 * 
 * Call a python module.method directly without quoting hell
 *  The return value (if any) is ignored.
 *
 * Arguments:
 *	orig_object - a string containing "module.method", the name of a python method
 *	args - a string containing the argument that will be passed to "module.method"
 *
 * Return value:
 *	A new_malloc()ed string containing the repr()d return value of the method (success)
 *	A new_malloc()ed zero length string, if something went wrong (failure)
 */
char *	call_python_directly (const char *orig_object, char *args)
{
	char 	*object = NULL, *module = NULL, *method = NULL;
	PyObject *mod_py = NULL, *meth_py = NULL, *args_py = NULL;
	PyObject *pModule = NULL, *pFunc = NULL, *pArgs = NULL, *pRetVal = NULL;
	PyObject *retval_repr = NULL;
	char 	*r = NULL, *retvalstr = NULL;

	object = LOCAL_COPY(orig_object);
	module = object;
	if (!(method = strchr(module, '.')))
	{
		my_error("Usage: /PYDIRECT module.method arguments");
		RETURN_EMPTY;
	}
	*method++ = 0;

	mod_py = Py_BuildValue("z", module);
	pModule = PyImport_Import(mod_py);
	Py_XDECREF(mod_py);

	if (pModule == NULL)
	{
		my_error("PYDIRECT: Unable to import module %s", module);
		goto c_p_d_error;
	}

	pFunc = PyObject_GetAttrString(pModule, method);
	if (pFunc == NULL)
	{
		my_error("PYDIRECT: The module %s has nothing named %s", module, method);
		goto c_p_d_error;
	}

	if (!PyCallable_Check(pFunc))
	{
		my_error("PYDIRECT: The thing named %s.%s is not a function", module, method);
		goto c_p_d_error;
	}

	if (!(pArgs = PyTuple_New(1)))
		goto c_p_d_error;

	if (!(args_py = Py_BuildValue("z", args)))
		goto c_p_d_error;

	if ((PyTuple_SetItem(pArgs, 0, args_py))) 
		goto c_p_d_error;
	args_py = NULL;			/* args_py now belongs to the tuple! */

	if (!(pRetVal = PyObject_CallObject(pFunc, pArgs)))
		goto c_p_d_error;

        if (!(retval_repr = PyObject_Repr(pRetVal)))
		goto c_p_d_error;

        if (!(r = PyUnicode_AsUTF8(retval_repr)))
		goto c_p_d_error;

        retvalstr = malloc_strdup(r);
	goto c_p_d_cleanup;

c_p_d_error:
	output_traceback();

c_p_d_cleanup:
	Py_XDECREF(pArgs);
	Py_XDECREF(pFunc);
	Py_XDECREF(pModule);
	Py_XDECREF(pRetVal);

	RETURN_MSTR(retvalstr);
}

/*
 * /PYDIRECT -- Call a python module.method directly, without using 
 *		PyRun[Simple]String() [which handles syntax parsing]
 *
 * Usage:
 *	/PYDIRECT module.method arguments
 *
 * You can only call "module.method" python functions that accept exactly
 * one string as its input parameters.  If you need to call anything more 
 * sophisticated than that, you need to use /PYTHON or $python() to handle
 * the parsing.
 */
BUILT_IN_COMMAND(pydirect_cmd)
{
	char *pyfuncname;
	char *x;

	pyfuncname = new_next_arg(args, &args);
	x = call_python_directly(pyfuncname, args);
	new_free(&x);
}


/*
 * output_traceback - output a python exception in an epic-friendly way
 *
 * Certain things may cause Python Exceptions. When we detect that an
 * exception has occurred, we call this function to handle it.
 * 
 * Arguments: None
 * Return value : None
 */
void	output_traceback (void)
{
	PyObject *ptype, *pvalue, *ptraceback;
	PyObject *ptype_repr, *pvalue_repr, *ptraceback_repr;
	char *ptype_str, *pvalue_str, *ptraceback_str;

	say("The python evaluation threw an exception:");
	PyErr_Print();
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
#if 0
		ptraceback_repr = PyObject_Repr(ptraceback);
		ptraceback_str = PyUnicode_AsUTF8(ptraceback_repr);
		say("Traceback: %s", ptraceback_str);
#endif
	}
	Py_XDECREF(ptype);
	Py_XDECREF(pvalue);
	Py_XDECREF(ptraceback);
	return;
}

