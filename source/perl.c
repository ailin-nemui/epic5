/* $EPIC: perl.c,v 1.9 2002/08/26 17:20:14 crazyed Exp $ */
/*
 * perl.c -- The perl interfacing routines.
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
#include "output.h"
#include <EXTERN.h>
#include <perl.h>
#include <XSUB.h>

int	isperlrunning=0, perlcalldepth=0;
PerlInterpreter	*my_perl;

EXTERN_C void xs_init _((void));
EXTERN_C void boot_DynaLoader _((CV* cv));

/* RETURN_MSTR copied from functions.c rather than including due to complications. */
#define RETURN_MSTR(x) return ((x) ? (x) : m_strdup(""));
#define SV2STR(x,y) (y)=(void*)m_strdup((char*)SvPV_nolen(x))
#ifndef SvPV_nolen
STRLEN	trash;
#define SvPV_nolen(x) SvPV((x),trash)
#endif

static XS (XS_cmd) {
	unsigned foo;
	dXSARGS;
	for (foo=0; foo<items; foo++) {
		parse_line(NULL, "$*", SvPV_nolen(ST(foo)), 0, 0);
	}
	XSRETURN(0);
}

static XS (XS_eval) {
	unsigned foo;
	dXSARGS;
	for (foo=0; foo<items; foo++) {
		parse_line(NULL, SvPV_nolen(ST(foo)), "", 0, 0);
	}
	XSRETURN(0);
}

static XS (XS_expr) {
	unsigned foo, food = 0;
	char* retval=NULL;
	char* arg=NULL;
	dXSARGS;
	for (foo=0; foo<items; foo++) {
		arg = m_strdup((char*)SvPV_nolen(ST(foo)));
		retval = (char*)parse_inline(arg, "", &food);
		XST_mPV(foo, retval);
		new_free(&arg);
		new_free(&retval);
	}
	XSRETURN(items);
}

static XS (XS_yell) {
	unsigned foo;
	dXSARGS;
	for (foo=0; foo<items; foo++) {
		yell("Perl: %s",SvPV_nolen(ST(foo)));
	}
	XSRETURN(items);
}

EXTERN_C void
xs_init(void)
{
	char *file = __FILE__;
	dXSUB_SYS;

	/* DynaLoader is a special case */
	newXS("DynaLoader::boot_DynaLoader", boot_DynaLoader, file);
	newXS("EPIC::cmd", XS_cmd, "IRC");
	newXS("EPIC::eval", XS_eval, "IRC");
	newXS("EPIC::expr", XS_expr, "IRC");
	newXS("EPIC::yell", XS_yell, "IRC");
}

/* Stopping has one big memory leak right now, so it's not used. */
void perlstartstop (int startnotstop) {
	if (startnotstop && !isperlrunning) {
		char *embedding[] = {
			"", "-e",
			"$SIG{__DIE__}=$SIG{__WARN__}=\\&EPIC::yell;"
		};
		++isperlrunning;
		my_perl = perl_alloc();
		perl_construct( my_perl );
		perl_parse(my_perl, xs_init, 3, embedding, NULL);
		if (SvTRUE(ERRSV)) yell("perl_parse: %s", SvPV_nolen(ERRSV));
		perl_run(my_perl);
		if (SvTRUE(ERRSV)) yell("perl_run: %s", SvPV_nolen(ERRSV));
	} else if (!startnotstop && isperlrunning && !perlcalldepth) {
		perl_destruct(my_perl);
		if (SvTRUE(ERRSV)) yell("perl_destruct: %s", SvPV_nolen(ERRSV));
		perl_free(my_perl);
		if (SvTRUE(ERRSV)) yell("perl_free: %s", SvPV_nolen(ERRSV));
		isperlrunning=0;
	}
}

char* perlcall (const char* sub, char* in, char* out, long item, char* input) {
	char *retval=NULL;
	int count, foo;
	an_array *array;
	dSP ;
	if (!isperlrunning){RETURN_MSTR(retval);}
	++perlcalldepth;
	ENTER; SAVETMPS;
	PUSHMARK(SP);
	if (input && *input) XPUSHs(sv_2mortal(newSVpv(input, 0)));
	if (in && *in && (array=get_array(in))) {
		for (foo=0; foo<array->size; foo++) {
			XPUSHs(sv_2mortal(newSVpv(array->item[foo], 0)));
		}
	}
	PUTBACK ;
	if (out && *out) {
		long size;
		upper(out);
		size=(array=get_array(out))?array->size:0;
		if (0>item) item=size-~item;
		if (item>size) item=-1;
	} else {
		item=-1;
	}
	if (0<=item) {
		I32 ax;
		count = perl_call_pv((char*)sub, G_EVAL|G_ARRAY);
		SPAGAIN ;
		SP -= count ;
		ax = (SP - PL_stack_base) + 1 ;
		for (foo=0; foo<count; foo++) {
			set_item(out, item+foo, (char*)SvPV_nolen(ST(foo)), 1);
		}
		retval=(void*)new_realloc((void**)(&retval),32);
		snprintf(retval,31,"%u",count);
	} else {
		SV *sv;
		count = perl_call_pv((char*)sub, G_EVAL|G_SCALAR);
		SPAGAIN ; sv=POPs ;
		SV2STR(sv,retval);
	}
	PUTBACK ;
	FREETMPS; LEAVE;
	--perlcalldepth;
	RETURN_MSTR(retval);
}

char* perleval (const char* input) {
	char *retval=NULL;
	if (input && *input) {
		SV *sv;
		perlstartstop(1);
		++perlcalldepth;
		ENTER; SAVETMPS;
		sv=perl_eval_pv((char*)input, FALSE);
		SV2STR(sv,retval);
		FREETMPS; LEAVE;
		--perlcalldepth;
	};
	RETURN_MSTR(retval);
}
