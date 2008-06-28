/* $EPIC: ruby.c,v 1.10 2008/06/28 04:19:18 jnelson Exp $ */
/*
 * ruby.c -- Calling RUBY from epic.
 *
 * Copyright © 2006 EPIC Software Labs.
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
#include <ruby.h>

VALUE	rubyclass;
int	is_ruby_running = 0;

#define RUBY_STARTUP 			\
	VALUE x;			\
	char *my_string;		\
					\
	x = rb_obj_as_string(string);	\
	my_string = STR2CSTR(x);

static VALUE epic_echo (VALUE module, VALUE string)
{
	RUBY_STARTUP
	yell("%s", my_string);
	return Qnil;
}

static VALUE epic_say (VALUE module, VALUE string)
{
	RUBY_STARTUP
	say("%s", my_string);
	return Qnil;
}

static VALUE epic_cmd (VALUE module, VALUE string)
{
	RUBY_STARTUP
	runcmds("$*", my_string);
	return Qnil;
}

static VALUE epic_eval (VALUE module, VALUE string)
{
	RUBY_STARTUP
	runcmds(my_string, "");
	return Qnil;
}

static VALUE epic_expr (VALUE module, VALUE string)
{
	char *exprval;
	RUBY_STARTUP

	exprval = parse_inline(my_string, "");
	return rb_str_new(exprval, strlen(exprval));
}

static VALUE epic_call (VALUE module, VALUE string)
{
	char *funcval;
	RUBY_STARTUP

	funcval = call_function(my_string, "");
	return rb_str_new(funcval, strlen(funcval));
}

/* Called by the epic hooks to activate tcl on-demand. */
void ruby_startstop (int value)
{
	VALUE	rubyval;

	if (is_ruby_running)
	{
		if (value)
		{
			/* Do shutdown stuff */
		}
		is_ruby_running = 0;
		return;
	}

	++is_ruby_running;
	ruby_init();
	ruby_init_loadpath();
	ruby_script(malloc_strdup(irc_version));
	rubyclass = rb_define_class("EPIC", rb_cObject);
	rb_define_singleton_method(rubyclass, "echo", epic_echo, 1);
	rb_define_singleton_method(rubyclass, "say", epic_say, 1);
	rb_define_singleton_method(rubyclass, "cmd", epic_cmd, 1);
	rb_define_singleton_method(rubyclass, "eval", epic_eval, 1);
	rb_define_singleton_method(rubyclass, "expr", epic_expr, 1);
	rb_define_singleton_method(rubyclass, "call", epic_call, 1);
	rb_gc_register_address(&rubyclass);

	/* XXX Is it a hack to do it like this instead of in pure C? */
	rubyval = rb_eval_string("EPICstdout = Object.new\n"
                                 "def EPICstdout.write(string) \n"
				 "   string.chomp! \n"
				 "   EPIC.echo(\"RUBY-ERROR: #{string}\") \n"
				 "end \n"
				 "$stderr = EPICstdout");
	if (rubyval == Qnil)
		say("stderr assignment returned Qnil");
}

/*
 * Used by the $ruby(...) function: evalulate ... as a RUBY statement, and 
 * return the result of the statement.
 */
static VALUE	internal_rubyeval (VALUE *a)
{
	int	foo;
	VALUE	rubyval;

	rubyval = rb_eval_string((char *)a);
	if (rubyval == Qnil)
		return Qnil;
	else
		return rubyval;
}

static	VALUE	eval_failed (VALUE args, VALUE error_info)
{
	VALUE err_info_str;
	char *ick;

	err_info_str = rb_obj_as_string(error_info);
	ick = STR2CSTR(err_info_str);
	yell("RUBY-ERROR: %s", ick);	
	return Qnil;
}

char *	rubyeval (char *input)
{
	VALUE	rubyval;
	char *	retval = NULL;

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
			retval = STR2CSTR(x);
		}
	}

	RETURN_STR(retval);	/* XXX Is this malloced or not? */
}

/*
 * The /RUBY function: Evalulate the args as a RUBY block and ignore the 
 * return value of the statement.
 */
BUILT_IN_COMMAND(rubycmd)
{
        char *body, *x;

        if (*args == '{')
        {
                if (!(body = next_expr(&args, '{')))
                {
                        error("RUBY: unbalanced {");
                        return;
                }
        }
        else
                body = args;

        x = rubyeval(body);
        new_free(&x);
}

