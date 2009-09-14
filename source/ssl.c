/* $EPIC: ssl.c,v 1.30 2009/09/14 04:49:58 jnelson Exp $ */
/*
 * ssl.c: SSL connection functions
 *
 * Original framework written by Juraj Bednar
 * Modified by B. Thomas Frazier
 *
 * Copyright © 2000 Juraj Bednar.
 * Copyright © 2000, 2005 EPIC Software Labs.
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
#include "ssl.h"

#ifdef HAVE_SSL

#include <openssl/crypto.h>
#include <openssl/x509.h>
#include <openssl/pem.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/opensslconf.h>

#include "output.h"
#include "hook.h"
#include "ssl.h"
#include "newio.h"

static	int	firsttime = 1;
static void	ssl_setup_locking (void);

/*
 * SSL_CTX_init -- Create and set up a new SSL ConTeXt object.
 * 		   This bootstraps the SSL system the first time it's called.
 * ARGS:
 *	server -- Will this CTX be used as a client (0) or server (1)?
 * RETURN VALUE:
 *	A new SSL_CTX you can use with an SSL connection.
 *	You MUST call SSL_CTX_free() on the return value later!
 */
static SSL_CTX	*SSL_CTX_init (int server)
{
	SSL_CTX	*ctx;
	
	if (firsttime)
	{
		ssl_setup_locking();
		SSL_load_error_strings();
		SSL_library_init();
		firsttime = 0;
	}

	ctx = SSL_CTX_new(server ? 
			SSLv23_server_method() : 
			SSLv23_client_method());
	SSL_CTX_set_session_cache_mode(ctx, SSL_SESS_CACHE_BOTH);
	SSL_CTX_set_timeout(ctx, 300);

	return ctx;
}

/*
 * SSL_FD_init -- Create an SSL session on a given OS file descriptor using
 *		  an SSL ConTeXt template.
 * ARGS:
 *	ctx -- An SSL ConTeXt previously returned by SSL_CTX_init().
 *	channel -- A (real) file descriptor (not a vfd!) to do SSL upon.
 * RETURN VALUE:
 *	A pointer to SSL representing an SSL session on 'channel' of the
 *	form given by 'ctx'.
 *	NULL if SSL_new() fails.
 *	Errors in SSL_connect() are ignored.
 */
static SSL *SSL_FD_init (SSL_CTX *ctx, int channel)
{
	SSL	*ssl;
	if (!(ssl = SSL_new(ctx)))
	{
		return NULL;
		panic(1, "SSL_FD_init() critical error in SSL_new()");
	}
	SSL_set_fd(ssl, channel);
	return(ssl);
}

/* * * * * * */
typedef struct	ssl_info_T {
	struct ssl_info_T *next;
	int	active;

	int	vfd;
	int	channel;
	SSL_CTX	*ctx;
	SSL *	ssl_fd;
} ssl_info;

ssl_info *ssl_list = NULL;


/*
 * find_ssl -- Get the data for an ssl-enabled connection.
 *
 * ARGS:
 *	vfd -- A virtual file descriptor, previously returned by new_open().
 * RETURN VALUE:
 *	If the vfd has been set up to use SSL, an (ssl_info *) to its data.
 *	If the vfd has not been se tup to use SSL, NULL.
 */
static ssl_info *	find_ssl (int vfd)
{
	ssl_info *x;

	for (x = ssl_list; x; x = x->next)
		if (x->vfd == vfd)
			return x;

	return NULL;
}

/*
 * new_ssl_info -- Register vfd as an ssl-using connection.
 *
 * ARGS: 
 *	vfd -- A virtual file descriptor, previously returned by new_open().
 * RETURN VALUE:
 *	If the vfd has never been previously set up to use SSL, a pointer to
 *		metadata for the vfd.  The 'channel', 'ctx' and 'ssl_fd'
 *		fields will not be filled in yet!
 *	If the vfd has previously been set up to use SSL, a pointer to the
 *		metadata for that vfd.  Existing information about the SSL
 *		connection on that vfd will be discarded!
 */
static ssl_info *	new_ssl_info (int vfd)
{
	ssl_info *x;

	if (!(x = find_ssl(vfd)))
	{
		x = new_malloc(sizeof(*x));
		x->next = ssl_list;
		ssl_list = x;
	}

	x->active = 0;
	x->vfd = vfd;
	x->channel = -1;
	x->ctx = NULL;
	x->ssl_fd = NULL;
	return x;

}

/*
 * remove_ssl_info -- Unregister vfd as an ssl-using connection.
 *
 * ARGS:
 *	vfd -- A virtual file descriptor, previously returned by new_open().
 * RETURN VALUE:
 *	If the vfd has been previously set up to use SSL, 0.
 *	If the vfd has never been previously set up to use SSL, -1.
 */
static ssl_info *	unlink_ssl_info (int vfd)
{
	ssl_info *x = NULL;

	if (ssl_list->vfd == vfd)
	{
		x = ssl_list;
		ssl_list = x->next;
		return x;
	}
	else
	{
		for (x = ssl_list; x->next; x = x->next)
		{
			if (x->next->vfd == vfd)
			{
				ssl_info *y = x->next;
				x->next = x->next->next;
				return y;
			}
		}
	}

	return NULL;
}


/*
 * startup_ssl -- Create an ssl connection on a vfd using a certain channel.
 * ARGS:
 *	vfd -- A virtual file descriptor, previously returned by new_open().
 *	channel -- The channel that is mapped to the vfd (passed to new_open())
 * RETURN VALUE:
 *	-1	Something really died
 *	 0	SSL negotiation is pending
 *	 1	SSL negotiation is complete
 */
int	startup_ssl (int vfd, int channel)
{
	ssl_info *	x;

	if (!(x = new_ssl_info(vfd)))
	{
		syserr(SRV(vfd), "Could not make new ssl info (vfd [%d]/channel [%d])",
				vfd, channel);
		errno = EINVAL;
		return -1;
	}

	say("SSL negotiation for channel [%d] in progress...", channel);
	x->channel = channel;
	x->ctx = SSL_CTX_init(0);

	if ((x->ssl_fd = SSL_FD_init(x->ctx, channel)) == NULL)
	{
		/* Get rid of the 'x' we just created */
		shutdown_ssl(vfd);
		syserr(SRV(vfd), "Could not make new SSL (vfd [%d]/channel [%d])",
				vfd, channel);
		errno = EINVAL;
		return -1;
	}

	set_non_blocking(channel);
	ssl_connect(vfd, 0);
	return 0;
}

/*
 * shutdown_ssl -- Destroy an ssl connection on a vfd.
 * ARGS:
 *	vfd -- A virtual file descriptor, previously passed to startup_ssl().
 * RETURN VALUE:
 *	-1	The vfd is not set up to do ssl. (errno is set to EINVAL)
 *	 0	The SSL session on the vfd has been shut down.
 * Any errors occuring during the shutdown are ignored.
 */
int	shutdown_ssl (int vfd)
{
	ssl_info *	x;

	if (!(x = unlink_ssl_info(vfd)))
		return -1;

	x->active = 0;
	x->vfd = -1;
	x->channel = -1;
	if (x->ssl_fd)
		SSL_shutdown(x->ssl_fd);

	if (x->ctx)
	{
		SSL_CTX_free(x->ctx);
		x->ctx = NULL;
	}
	if (x->ssl_fd)
	{
		SSL_free(x->ssl_fd);
		x->ssl_fd = NULL;
	}
	return 0;
}

/* * * * * * */
/*
 * write_ssl -- Write some binary data over an ssl connection on vfd.
 *		The data is unbuffered with BIO_flush() before returning.
 * ARGS:
 *	vfd -- A virtual file descriptor, previously passed to startup_ssl().
 *	data -- Any binary data you wish to send over 'vfd'.
 *	len -- The number of bytes in 'data' to send.
 * RETURN VALUE:
 *	-1 / EINVAL -- The vfd is not set up for ssl.
 *	Anything else -- The return value of SSL_write().
 */
int	write_ssl (int vfd, const void *data, size_t len)
{
	ssl_info *x;
	int	err;

	if (!(x = find_ssl(vfd)))
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

/*
 * read_ssl -- Post whatever data is available on 'vfd' to the newio system.
 *		This operation may block if the SSL operations block.
 * ARGS:
 *	vfd -- A virtual file descriptor, previously passed to startup_ssl().
 *	quiet -- Should errors silently ignored (1) or displayed? (0)
 * RETURN VALUE:
 *	-1 / EINVAL -- The vfd is not set up for ssl.
 *	Anything else -- The final return value of SSL_read().
 */
int	ssl_read (int vfd, int quiet)
{
	ssl_info *x;
	int	c;
	int	failsafe = 0;
	char	buffer[8192];

	if (!(x = find_ssl(vfd)))
	{
		errno = EINVAL;
		return -1;
	}

	/*
	 * So SSL_read() might read stuff from the socket (thus defeating
	 * a further select/poll) and buffer it internally.  We need to make
	 * sure we don't leave any data on the table and flush out any data
	 * that could be left over if the above read didn't do the job.
	 */
	do
	{
		/* This is to prevent an impossible deadlock */
		if (failsafe++ > 1000)
			panic(1, "Caught in SSL_pending() loop! (%d)", vfd);

		c = SSL_read(x->ssl_fd, buffer, sizeof(buffer));
		if (c < 0)
		{
		    int ssl_error = SSL_get_error(x->ssl_fd, c);
		    if (ssl_error == SSL_ERROR_NONE)
			if (!quiet)
			   syserr(SRV(vfd), "SSL_read failed with [%d]/[%d]", 
					c, ssl_error);
		}

		if (c == 0)
			errno = -1;
		else if (c > 0)
			dgets_buffer(x->channel, buffer, c);
		else
			return c;		/* Some error */
	}
	while (SSL_pending(x->ssl_fd) > 0);

	return c;
}

/* * * * * * */
int	ssl_connect (int vfd, int quiet)
{
	ssl_info *	x;
	int		errcode;
	int		ssl_err;

	if (!(x = find_ssl(vfd)))
	{
		errno = EINVAL;
		return -1;
	}

	errcode = SSL_connect(x->ssl_fd);
	if (errcode <= 0)
	{
		ssl_err = SSL_get_error(x->ssl_fd, errcode);
		if (ssl_err == SSL_ERROR_WANT_READ || 
		    ssl_err == SSL_ERROR_WANT_WRITE)
			return 1;
		else
		{
			/* Post the error */
			syserr(SRV(vfd), "ssl_connect: posting error %d", ssl_err);
			dgets_buffer(x->channel, &ssl_err, sizeof(ssl_err));
			return 1;
		}
	}

	/* Post the success */
	ssl_err = 0;
	syserr(SRV(vfd), "ssl_connect: connection successful!");
	dgets_buffer(x->channel, &ssl_err, sizeof(ssl_err));
	return 1;
}

int	ssl_connected (int vfd)
{
	char *		u_cert_issuer;
	char *		u_cert_subject;
	char *          cert_issuer;
	char *          cert_subject;
	X509 *          server_cert;
	EVP_PKEY *      server_pkey;
	ssl_info *	x;

	if (!(x = find_ssl(vfd)))
	{
		errno = EINVAL;
		return -1;
	}

	set_blocking(x->channel);

	/* It completed!  Yay! */
	if (x_debug & DEBUG_SSL)
		say("SSL negotiation using %s", SSL_get_cipher(x->ssl_fd));

	/* The man page says this never fails in reality. */
	if (!(server_cert = SSL_get_peer_certificate(x->ssl_fd)))
	{
		syserr(SRV(vfd), "SSL negotiation failed -- reporting as error");
		SSL_CTX_free(x->ctx);
		x->ctx = NULL;
		x->ssl_fd = NULL;
		write(x->channel, empty_string, 1);    /* XXX Is this correct? */
		return -1;
	}

	say("SSL negotiation for channel [%d] complete", x->channel);

	cert_subject = X509_NAME_oneline(X509_get_subject_name(server_cert),
							0, 0);
	if (!(u_cert_subject = transform_string_dyn("+URL", cert_subject, 
							0, NULL)))
		u_cert_subject = malloc_strdup(cert_subject);

	cert_issuer = X509_NAME_oneline(X509_get_issuer_name(server_cert),0,0);
	if (!(u_cert_issuer = transform_string_dyn("+URL", cert_issuer, 
							0, NULL)))
		u_cert_issuer = malloc_strdup(cert_issuer);
	
	server_pkey = X509_get_pubkey(server_cert);

	if (do_hook(SSL_SERVER_CERT_LIST, "%d %s %s %d", 
			vfd, u_cert_subject, u_cert_issuer, 
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
	return 1;
}

const char *	get_ssl_cipher (int vfd)
{
	ssl_info *x;

	if (!(x = find_ssl(vfd)))
		return empty_string;
	if (!x->ssl_fd)
		return empty_string;

	return SSL_get_cipher(x->ssl_fd);
}

int	is_ssl_enabled (int vfd)
{
	if (find_ssl(vfd))
		return 1;
	else
		return 0;
}

int	client_ssl_enabled (void)
{
	return 1;
}


# ifdef USE_PTHREAD
#include <pthread.h>
pthread_mutex_t *ssl_mutexes = NULL;

static void	ssl_locking_callback (int mode, int n, const char *file, int line)
{
	if (mode & CRYPTO_LOCK)
		pthread_mutex_lock(&ssl_mutexes[n]);
	else
		pthread_mutex_unlock(&ssl_mutexes[n]);
}

static unsigned long	ssl_id_callback (void)
{
	return (unsigned long)pthread_self();
}

static void	ssl_setup_locking (void)
{
	int	i, num;

	num = CRYPTO_num_locks();
	ssl_mutexes = (pthread_mutex_t *)new_malloc(sizeof(pthread_mutex_t) * num);
	for (i = 0; i < num; i++)
		pthread_mutex_init(&ssl_mutexes[i], NULL);

	CRYPTO_set_locking_callback(ssl_locking_callback);
	CRYPTO_set_id_callback(ssl_id_callback);
}
# else
static void	ssl_setup_locking (void)
{
	return;
}
# endif
#else

int	startup_ssl (int vfd, int channel)
{
	return -1;
}

int	shutdown_ssl (int vfd)
{
	panic(1, "shutdown_ssl(%d) called on non-ssl client", vfd);
	return -1;
}

int	write_ssl (int vfd, const void *data, size_t len)
{
	panic(1, "write_fd(%d, \"%s\", %ld) called on non-ssl client",
		vfd, (const char *)data, (long)len);
	return -1;
}

int	ssl_read (int vfd, int quiet)
{
	panic(1, "ssl_read(%d) called on non-ssl client", vfd);
	return -1;
}

const char *get_ssl_cipher (int vfd)
{
	panic(1, "get_ssl_cipher(%d) called on non-ssl client", vfd);
	return NULL;
}

int	is_ssl_enabled (int vfd)
{
	return 0;
}

int	client_ssl_enabled (void)
{
	return 0;
}

#endif

