/* $EPIC: ircsig.c,v 1.4 2002/07/29 22:27:05 jnelson Exp $ */
/*
 * ircsig.c: has a `my_signal()' that uses sigaction().
 *
 * Copyright (c) 1993-1996 Matthew R. Green.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
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
/*
 * written by matthew green, 1993.
 *
 * i stole bits of this from w. richard stevens' `advanced programming
 * in the unix environment' -mrg
 */

#include "irc.h"
#include "irc_std.h"

int	block_signal (int sig_no)
{
	sigset_t set;

	sigemptyset(&set);
	sigaddset(&set, sig_no);
	return sigprocmask(SIG_BLOCK, &set, NULL);
}

int	unblock_signal (int sig_no)
{
	sigset_t set;

	sigemptyset(&set);
	sigaddset(&set, sig_no);
	return sigprocmask(SIG_UNBLOCK, &set, NULL);
}

sigfunc *my_signal (int sig_no, sigfunc *sig_handler)
{
        struct sigaction sa, osa;

	if (sig_no < 0)
		return NULL;		/* Signal not implemented */

        sa.sa_handler = sig_handler;
        sigemptyset(&sa.sa_mask);
        sigaddset(&sa.sa_mask, sig_no);

        /* this is ugly, but the `correct' way.  i hate c. -mrg */
        sa.sa_flags = 0;
#if defined(SA_RESTART) || defined(SA_INTERRUPT)
        if (SIGALRM == sig_no || SIGINT == sig_no)
        {
# if defined(SA_INTERRUPT)
                sa.sa_flags |= SA_INTERRUPT;
# endif /* SA_INTERRUPT */
        }
        else
        {
# if defined(SA_RESTART)
                sa.sa_flags |= SA_RESTART;
# endif /* SA_RESTART */
        }
#endif /* SA_RESTART || SA_INTERRUPT */

        if (0 > sigaction(sig_no, &sa, &osa))
                return (SIG_ERR);

        return (osa.sa_handler);
}
