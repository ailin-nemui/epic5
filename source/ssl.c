/* $EPIC: ssl.c,v 1.6 2002/12/23 15:11:27 jnelson Exp $ */
/*
 * ssl.c: SSL connection functions
 *
 * Original framework written by Juraj Bednar
 * Modified by B. Thomas Frazier
 *
 * Copyright © 2000 Juraj Bednar.
 * Copyright © 2000, 2002 EPIC Software Labs.
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

#ifdef HAVE_SSL
#include "ircaux.h"
#include "ssl.h"

SSL_CTX	*SSL_CTX_init (int server)
{
	SSL_CTX	*ctx;
	
	SSLeay_add_ssl_algorithms();
	SSL_load_error_strings();
	ctx = SSL_CTX_new(server ? SSLv3_server_method() : SSLv3_client_method());
	SSL_CTX_set_session_cache_mode(ctx, SSL_SESS_CACHE_BOTH);
	SSL_CTX_set_timeout(ctx, 300);
	return(ctx);
}

SSL *SSL_FD_init (SSL_CTX *ctx, int des)
{
	SSL	*ssl;
	if (!(ssl = SSL_new(ctx)))
	{
		return NULL;
		panic("SSL_FD_init() critical error in SSL_new()");
	}
	SSL_set_fd(ssl, des);
	SSL_connect(ssl);
	return(ssl);
}

#endif
