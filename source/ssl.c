/* $EPIC: ssl.c,v 1.8 2005/01/23 21:41:28 jnelson Exp $ */
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

int	ssl_reader (void *ssl_aux, char **buffer, size_t *buffer_size, size_t *start)
{
	int	c, numb;
	size_t	numbytes;
	int	failsafe = 0;

	c = SSL_read((SSL *)ssl_aux, (*buffer) + (*start), 
			(*buffer_size) - (*start) - 1);

	/*
	 * So SSL_read() might read stuff from the socket (thus defeating
	 * a further select/poll) and buffer it internally.  We need to make
	 * sure we don't leave any data on the table and flush out any data
	 * that could be left over if the above read didn't do the job.
	 */

	/* So if any byte are left buffered by SSL... */
	while ((numb = SSL_pending((SSL *)ssl_aux)) > 0)
	{
		numbytes = numb;		/* We know it's positive ! */

		/* This is to prevent an impossible deadlock */
		if (failsafe++ > 1000)
			panic("Caught in SSL_pending() loop! (%d)", numbytes);

		/* NUL terminate what we just read */
		(*buffer)[(*start) + c] = 0;

		/* Move the write position past what we just read */
		*start = (*start) + c;

		/* If there is not enough room to store the rest of the bytes */
		if (numbytes > (*buffer_size) - (*start) - 1)
		{
			/* Resize the buffer... */
			*buffer_size = (*buffer_size) + numbytes;
			RESIZE((*buffer), char, (*buffer_size) + 2);
		}

		/* And read everything that is left. */
		c = SSL_read((SSL *)ssl_aux, (*buffer) + (*start),
				(*buffer_size) - (*start) - 1);
	}

	return c;
}


#endif
