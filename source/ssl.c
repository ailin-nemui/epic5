/* $EPIC: ssl.c,v 1.9 2005/02/09 02:23:25 jnelson Exp $ */
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

#include <openssl/crypto.h>
#include <openssl/x509.h>
#include <openssl/pem.h>
#include <openssl/ssl.h>
#include <openssl/err.h>

#include "irc.h"
#ifdef HAVE_SSL
#include "ircaux.h"
#include "output.h"
#include "hook.h"
#include "ssl.h"

static SSL_CTX	*SSL_CTX_init (int server)
{
	SSL_CTX	*ctx;
	
	SSLeay_add_ssl_algorithms();
	SSL_load_error_strings();
	ctx = SSL_CTX_new(server ? SSLv3_server_method() : SSLv3_client_method());
	SSL_CTX_set_session_cache_mode(ctx, SSL_SESS_CACHE_BOTH);
	SSL_CTX_set_timeout(ctx, 300);
	return(ctx);
}

static SSL *SSL_FD_init (SSL_CTX *ctx, int des)
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

/* * * * * * */
typedef struct	ssl_info_T {
	struct ssl_info_T *next;
	int	active;

	int	fd;
	SSL_CTX	*ctx;
	SSL *	ssl_fd;
} ssl_info;

ssl_info *ssl_list = NULL;


static ssl_info *	find_ssl (int fd)
{
	ssl_info *x;

	for (x = ssl_list; x; x = x->next)
		if (x->fd == fd)
			return x;

	return NULL;
}

static ssl_info *	new_ssl_info (int fd)
{
	ssl_info *x;

	if (!(x = find_ssl(fd)))
	{
		x = new_malloc(sizeof(*x));
		x->next = ssl_list;
		ssl_list = x;
	}

	x->active = 0;
	x->fd = fd;
	x->ctx = NULL;
	x->ssl_fd = NULL;
	return x;
}

int	startup_ssl (int fd)
{
	char *		u_cert_issuer;
	char *		u_cert_subject;
	char *          cert_issuer;
	char *          cert_subject;
	X509 *          server_cert;
	EVP_PKEY *      server_pkey;
	ssl_info *	x;

	if (!(x = new_ssl_info(fd)))
	{
		errno = EINVAL;
		return -1;
	}

	say("SSL negotiation for fd [%d] in progress...", fd);
	x->ctx = SSL_CTX_init(0);
	x->ssl_fd = SSL_FD_init(x->ctx, fd);

	if (x_debug & DEBUG_SSL)
		say("SSL negotiation using %s", SSL_get_cipher(x->ssl_fd));

	say("SSL negotiation for fd [%d] complete", fd);

	/* The man page says this never fails in reality. */
	if (!(server_cert = SSL_get_peer_certificate(x->ssl_fd)))
	{
		say("SSL negotiation failed");
		say("WARNING: Bailing to no encryption");
		SSL_CTX_free(x->ctx);
		x->ctx = NULL;
		x->ssl_fd = NULL;
		write(fd, empty_string, 1);	/* XXX Is this correct? */
		return -1;
	}

	cert_subject = X509_NAME_oneline(X509_get_subject_name(server_cert),0,0);
	u_cert_subject = urlencode(cert_subject);
	cert_issuer = X509_NAME_oneline(X509_get_issuer_name(server_cert),0,0);
	u_cert_issuer = urlencode(cert_issuer);

	server_pkey = X509_get_pubkey(server_cert);

	if (do_hook(SSL_SERVER_CERT_LIST, "%d %s %s %d", 
			fd, u_cert_subject, u_cert_issuer, 
			EVP_PKEY_bits(server_pkey))) 
	{
		say("SSL certificate subject: %s", cert_subject) ;
		say("SSL certificate issuer: %s", cert_issuer);
		say("SSL certificate public key length: %d bits", 
					EVP_PKEY_bits(server_pkey));
	}

	new_free(&u_cert_issuer);
	new_free(&u_cert_subject);
	free(cert_issuer);
	free(cert_subject);
	return 0;
}


int	shutdown_ssl (int fd)
{
	ssl_info *x;

	if (!(x = find_ssl(fd)))
	{
		errno = EINVAL;
		return -1;
	}

	SSL_shutdown(x->ssl_fd);
	if (x->ssl_fd)
		SSL_free(x->ssl_fd);
	if (x->ctx)
		SSL_CTX_free(x->ctx);

	x->ssl_fd = NULL;
	x->ctx = NULL;
	return 0;
}

/* * * * * * */
int	write_ssl (int fd, const void *data, size_t len)
{
	ssl_info *x;
	int	err;

	if (!(x = find_ssl(fd)))
	{
		errno = EINVAL;
		return -1;
	}

	if (x->ssl_fd == NULL)
	{
		errno = EINVAL;
		say("SSL write error - ssl socket = 0");
		return -1;
	}

	err = SSL_write(x->ssl_fd, data, len);
	BIO_flush(SSL_get_wbio(x->ssl_fd));
	return err;
}

int	ssl_reader (int fd, char **buffer, size_t *buffer_size, size_t *start)
{
	ssl_info *x;
	int	c, numb;
	size_t	numbytes;
	int	failsafe = 0;

	if (!(x = find_ssl(fd)))
	{
		errno = EINVAL;
		return -1;
	}

	c = SSL_read(x->ssl_fd, (*buffer) + (*start), 
			(*buffer_size) - (*start) - 1);

	/*
	 * So SSL_read() might read stuff from the socket (thus defeating
	 * a further select/poll) and buffer it internally.  We need to make
	 * sure we don't leave any data on the table and flush out any data
	 * that could be left over if the above read didn't do the job.
	 */

	/* So if any byte are left buffered by SSL... */
	while ((numb = SSL_pending(x->ssl_fd)) > 0)
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
		c = SSL_read(x->ssl_fd, (*buffer) + (*start),
				(*buffer_size) - (*start) - 1);
	}

	return c;
}

const char *	get_ssl_cipher (int fd)
{
	ssl_info *x;

	if (!(x = find_ssl(fd)))
		return empty_string;
	if (!x->ssl_fd)
		return empty_string;

	return SSL_get_cipher(x->ssl_fd);
}

int	is_ssl_enabled (int fd)
{
	if (find_ssl(fd))
		return 1;
	else
		return 0;
}

#else

int	startup_ssl (int fd)
{
	panic("startup_ssl(%d) called on non-ssl client", fd);
}

int	shutdown_ssl (int fd)
{
	panic("shutdown_ssl(%d) called on non-ssl client", fd);
}

int	write_ssl (int fd, const void *data, size_t len)
{
	panic("write_fd(%d, \"%s\", %ld) called on non-ssl client",
		fd, data, len);
}

int	ssl_reader (int fd, char **buf, size_t *len, size_t *start)
{
	panic("ssl_reader(%d) called on non-ssl client", fd);
}

const char *get_ssl_cipher (int fd)
{
	panic("get_ssl_cipher(%d) called on non-ssl client", fd);
}

int	is_ssl_enabled (int fd)
{
	return 0;
}

#endif

