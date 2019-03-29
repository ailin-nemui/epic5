/*
 * Copyright 2006 EPIC Software Labs.
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

#ifndef __EXTLANG_H__
#define __EXTLANG_H__

#ifdef HAVE_PERL
extern	void	perlstartstop (int);
extern	char *	perlcall (char *, char *, char *, long, char *);
extern	char *	perleval (char *);
BUILT_IN_COMMAND(perlcmd);
#endif

#ifdef HAVE_TCL
extern	void	tclstartstop (int);
extern	char *	tcleval (char *);
BUILT_IN_COMMAND(tclcmd);
#endif

#ifdef HAVE_RUBY
extern	void	ruby_startstop (int);
extern	char *	rubyeval (char *);
BUILT_IN_COMMAND(rubycmd);
#endif

#ifdef HAVE_PYTHON
extern	char *	python_eval_expression (char *);
extern	void	python_eval_statement (char *);
extern	char *  call_python_directly (const char *, char *args);
BUILT_IN_COMMAND(pythoncmd);
BUILT_IN_COMMAND(pydirect_cmd);
#endif

#endif
