/* $EPIC: ssl.c,v 1.33 2015/04/15 04:06:19 jnelson Exp $ */
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
#include "newio.h"
#include "vars.h"

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
/*
	SSL_CTX_load_verify_locations(ctx, "/usr/local/share/certs/ca-root-nss.crt", NULL);
*/
	SSL_CTX_load_verify_locations(ctx, get_string_var(SSL_ROOT_CERT_FILE_VAR), NULL);
	
	return ctx;
}

/* * * * * * */
typedef struct ssl_metadata {
	int	vfd;
	int	verify_result;
	char *	pem;
	char *	cert_hash;
	int	pkey_bits;
	char *	subject;
	char *	u_cert_subject;
	char *	issuer;
	char *	u_cert_issuer;
	char *	ssl_version;
} ssl_metadata;

typedef struct	ssl_info_T {
	struct ssl_info_T *next;
	int	active;
	int	vfd;		/* The Virtual File Descriptor (new_open()) */
	int	channel;	/* The physical connection for the vfd */

	SSL_CTX	*ctx;		/* All our SSLs have their own ConTeXt */
	SSL *	ssl_fd;		/* Each of our SSLs have their own (SSL *) */
	ssl_metadata	md;	/* Plain text info about SSL connection */
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

	x->md.vfd = vfd;
	x->md.verify_result = 0;
	x->md.pem = NULL;
	x->md.cert_hash = NULL;
	x->md.pkey_bits = 0;
	x->md.subject = NULL;
	x->md.u_cert_subject = NULL;
	x->md.issuer = NULL;
	x->md.u_cert_issuer = NULL;
	x->md.ssl_version = NULL;

	return x;
}

/*
 * unlink_ssl_info -- Unregister vfd as an ssl-using connection.
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
 * ssl_startup -- Create an ssl connection on a vfd using a certain channel.
 * ARGS:
 *	vfd -- A virtual file descriptor, previously returned by new_open().
 *	channel -- The channel that is mapped to the vfd (passed to new_open())
 * RETURN VALUE:
 *	-1	Something really died
 *	 0	SSL negotiation is pending
 *	 1	SSL negotiation is complete
 */
int	ssl_startup (int vfd, int channel)
{
	ssl_info *	x;
	SSL *		ssl;

	if (!(x = new_ssl_info(vfd)))
	{
		syserr(SRV(vfd), "Could not make new ssl info "
				 "(vfd [%d]/channel [%d])",
				vfd, channel);
		errno = EINVAL;
		return -1;
	}

	say("SSL negotiation for channel [%d] in progress...", channel);
	x->channel = channel;
	x->ctx = SSL_CTX_init(0);
	if (!(x->ssl_fd = SSL_new(x->ctx)))
	{
		/* Get rid of the 'x' we just created */
		ssl_shutdown(vfd);
		syserr(SRV(vfd), "Could not make new SSL "
				 "(vfd [%d]/channel [%d])",
				vfd, channel);
		errno = EINVAL;
		return -1;
	}

	SSL_set_fd(x->ssl_fd, channel);
	set_non_blocking(channel);
	ssl_connect(vfd, 0);
	return 0;
}

/*
 * ssl_shutdown -- Destroy an ssl connection on a vfd.
 * ARGS:
 *	vfd -- A virtual file descriptor, previously passed to startup_ssl().
 * RETURN VALUE:
 *	-1	The vfd is not set up to do ssl. (errno is set to EINVAL)
 *	 0	The SSL session on the vfd has been shut down.
 * Any errors occuring during the shutdown are ignored.
 */
int	ssl_shutdown (int vfd)
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

	x->md.vfd = -1;
	x->md.verify_result = 0;
	new_free(&x->md.pem);
	new_free(&x->md.cert_hash);
	x->md.pkey_bits = 0;
	new_free(&x->md.subject);
	new_free(&x->md.u_cert_subject);
	new_free(&x->md.issuer);
	new_free(&x->md.u_cert_issuer);
	new_free(&x->md.ssl_version);
	return 0;
}

/* * * * * * */
/*
 * ssl_write -- Write some binary data over an ssl connection on vfd.
 *		The data is unbuffered with BIO_flush() before returning.
 * ARGS:
 *	vfd -- A virtual file descriptor, previously passed to startup_ssl().
 *	data -- Any binary data you wish to send over 'vfd'.
 *	len -- The number of bytes in 'data' to send.
 * RETURN VALUE:
 *	-1 / EINVAL -- The vfd is not set up for ssl.
 *	Anything else -- The return value of SSL_write().
 */
int	ssl_write (int vfd, const void *data, size_t len)
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
			syserr(SRV(vfd), "ssl_connect: posting error %d", 
						ssl_err);
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

/*
 * ssl_connected - retrieve the vital statstics about an established SSL
 *			connection
 * Arguments:
 *	vfd	 - The Virtual File Descriptor for the SSL connection
 *		   (as returned by new_open())
 *	info	 - A struct that holds all the important information about
 *		   the connection (that doesn't depend on what is using
 *		   the connection)
 *
 * A brief review of history:
 *	1. First, you /server irc.hostname.com
 *	2. That launches a nonblocking socket connect()ion, 
 *	3. When that completes, if server "type" is "irc-ssl", a nonblocking
 *	   SSL_connect() is launched.
 *	4. When that completes, we get called.
 *
 * What this does: 
 *	1. Sets the socket back to blocking, and informs the user.
 *	2. Fetches the SSL's peer certificate
 *	3. Verifies the peer's certificate with SSL_get_verify_result
 *		and fills in x->md.verify_result
 *	4. Fills in the following values in 'x->md':
 *	   a. x->md.pem		(peer certificate in PEM (base64) format)
 *	   b. x->md.cert_hash	(X509_digest() of #1)
 *	   c. x->md.pkey_bits	(how many bits are in the public key)
 *	   d. x->md.subject	(who the certificate names; ie, the irc server)
 *	   e. x->md.u_cert_subject (urlified version of #4)
 *	   f. x->md.issuer	(who issued the certificate; ie, the CA)
 *	   g. x->md.u_cert_issuer  (urlified version of #6)
 *	   h. x->md.ssl_version	(what SSL we're using, ie, "TLSv1")
 *	5. Hook /on ssl_server_cert with the above information
 *	6. Cleans up
 */
int	ssl_connected (int vfd)
{
	X509 *          server_cert;	/* X509 in SSL internal fmt */
	ssl_info *	x;		/* EPIC info about the conn */
	BIO *		mem;		/* A place to write SSL strings */
	long		bytes;		/* How big 'mem' is */
	char *		p = NULL;	/* A place for C strings from 'mem' */
	unsigned char 	h[256];		/* A place for X509_digest() to write */
	unsigned	hlen;		/* How big digest in 'h' is */
	unsigned char	htext[1024];	/* A human readable version of 'h' */
	unsigned	i;		/* How many bytes in 'h' we converted */

	/*
	 * First off -- do I think this is even an SSL enabled connection?
	 */
	if (!(x = find_ssl(vfd)))
	{
		errno = EINVAL;
		return -1;
	}

	/*
	 * STEP 1: 
	 * We had set nonblocking when we started the SSL negotiation
	 * becuase that plays nicer with OpenSSL.  But we want our connections
	 * to be blocking when we start reading from them.
	 */
	set_blocking(x->channel);
	say("SSL negotiation using %s", SSL_get_cipher(x->ssl_fd));
	say("SSL protocol using %s", SSL_get_version(x->ssl_fd));

	/*
	 * STEP 2: 
	 * Fetch the server's certificate.
	 * If we are unable to get the certificate, then something
	 * failed and we should just bail right here. 
	 */
	if (!(server_cert = SSL_get_peer_certificate(x->ssl_fd)))
	{
		syserr(SRV(vfd), "SSL negotiation failed - reporting as error");
		SSL_CTX_free(x->ctx);
		x->ctx = NULL;
		x->ssl_fd = NULL;
		write(x->channel, empty_string, 1);  /* XXX Is this correct? */
		return -1;
	}
	say("SSL negotiation for channel [%d] complete", x->channel);


	/* * */
	/*
	 * STEP 3:
	 * Do the whole Certificate Verification thing.  Most SSL 
	 * certs on irc probably won't verify becuase they're self-signed.
	 *
	 * A few are real; but we can't verify them unless epic knows
	 * where your mozilla CA file is.
	 * (See SSL_get_verify_results() for more info on that)
	 */
	x->md.verify_result = SSL_get_verify_result(x->ssl_fd);
	if (x->md.verify_result == X509_V_OK)
		say("SSL Certificate is verifiable (wow!)");
	else
	{
		const char *msg;

		msg = X509_verify_cert_error_string(x->md.verify_result);
		say("SSL Certificate is not verifiable (big surprise): "
				"Reason %d (%s)", x->md.verify_result, msg);
	}

	/* * */
	/*
	 * STEP 4:
	 * (Advance apology -- in order to get strings out of OpenSSL,
	 *  you have to dance with the devils of its BIO objects.)
 	 */

	/* 
	 * STEP 4a:	The server's certificate PEM
	 */
	mem = BIO_new(BIO_s_mem());
	PEM_write_bio_X509(mem, server_cert);
	bytes = BIO_get_mem_data(mem, &p);
	p[bytes] = 0;
	malloc_strcpy(&x->md.pem, p);
	BIO_free(mem);		/* This invalidates 'p' */
	say("SSL Certificate is: %s", x->md.pem);

	/*
	 * STEP 4b: 	The server's certificate hash
	 */
	memset(h, 0, sizeof(h));
	hlen = sizeof(h);
	X509_digest(server_cert, EVP_sha1(), h, &hlen);

	/* This converts 'h' into a string like AB:CD:EF:01:02.. */
	for (i = 0; i < hlen; i++)
	{
		if (i > 0)
			htext[i * 3 - 1] = ':';

		snprintf(htext + (i * 3), 
			sizeof(htext) - (i * 3), "%02x", h[i]);
	}
	x->md.cert_hash = malloc_strdup(htext);
	say("SSL Certificate Digest: %s", x->md.cert_hash);

	/*
	 * STEP 4d: 	The server's certificate's "subject" [hostname]
	 * STEP 4e:	The server's certificate's "subject" (urlified)
	 */
	mem = BIO_new(BIO_s_mem());
	X509_NAME_print_ex(mem, X509_get_subject_name(server_cert), 
				2, XN_FLAG_RFC2253 & ~ASN1_STRFLGS_ESC_MSB);
	bytes = BIO_get_mem_data(mem, &p);
	p[bytes] = 0;
	x->md.subject = malloc_strdup(p);
	BIO_free(mem);				/* This invalidates 'p' */

	if (!(x->md.u_cert_subject = transform_string_dyn("+URL", 
							  x->md.subject,
							  0, NULL)))
		x->md.u_cert_subject = malloc_strdup(x->md.subject);

	/*
	 * STEP 4f:	The server's certificate's "issuer" [CA]
	 * STEP 4g:	The server's certificate's "issuer" (urlified)
	 */
	mem = BIO_new(BIO_s_mem());
	X509_NAME_print_ex(mem, X509_get_issuer_name(server_cert), 
				2, XN_FLAG_RFC2253 & ~ASN1_STRFLGS_ESC_MSB);
	bytes = BIO_get_mem_data(mem, &p);
	p[bytes] = 0;
	x->md.issuer = malloc_strdup(p);
	BIO_free(mem);				/* This invalidates 'p' */

	if (!(x->md.u_cert_issuer = transform_string_dyn("+URL", 
							  x->md.issuer,
							  0, NULL)))
		x->md.u_cert_issuer = malloc_strdup(x->md.issuer);

	/*
	 * STEP 4c:	The server's certificate's public key's bit-size
	 */
	x->md.pkey_bits = EVP_PKEY_bits(X509_get_pubkey(server_cert));

	/*
	 * STEP 4h:	The connection's SSL protocol (ie, TLSv1 or SSLv3)
	 */
	x->md.ssl_version = malloc_strdup(SSL_get_version(x->ssl_fd));	


	/* ==== */
	/*
	 * STEP 5: Tell the user about the connection
	 *
	 * This is what we tell the user:
	 *	$0 - The connection fd (should be a way to convert this
	 *		to a server refnum)
	 *	$1 - The "subject" (ie, server name)
	 *	$2 - The "Issuer" 
	 *	$3 - How many bits does the public key use?
	 *	$4 - The verification result (0 = pass; not 0 = fail)
	 *	$5 - What ssl are we using? (SSLv3 or TLSv1)
	 *	$6 - What is the digest of the certificate?
	 *
	 * It's expected from here that a script could stash this in
	 * a file somewhere and use it to detect if the certificate
	 * changed between connections.   Or something clever.
	 */
	if (do_hook(SSL_SERVER_CERT_LIST, "%d %s %s %d %d %s %s", 
			x->md.vfd, 
			x->md.u_cert_subject, x->md.u_cert_issuer, 
			x->md.pkey_bits, x->md.verify_result, 
			x->md.ssl_version, x->md.cert_hash))
	{
		say("SSL certificate subject: %s", x->md.subject);
		say("SSL certificate issuer: %s",  x->md.issuer);
		say("SSL certificate public key length: %d bits", 
						   x->md.pkey_bits);
		say("SSL Certificate was verified: %d (reason: %d)", 
					x->md.verify_result == X509_V_OK ? 1:0,
					x->md.verify_result);
		say("SSL Certificate SHA1 Hash: %s", 
						   x->md.cert_hash);
	}

	/*
	 * STEP 6: Clean up after ourselves
	 */
	return 1;
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




#define LOOKUP_SSL(vfd, missingval)	\
	ssl_info *x;			\
					\
	if (!(x = find_ssl(vfd)))	\
		return missingval ;	\
	if (!x->ssl_fd)			\
		return missingval ;	\


/* XXX - Legacy -- Roll into new API */
const char *	get_ssl_cipher (int vfd)
{
	LOOKUP_SSL(vfd, empty_string)

	return SSL_get_cipher(x->ssl_fd);
}

int	get_ssl_verify_result (int vfd)
{
	LOOKUP_SSL(vfd, 0)
	return x->md.verify_result;
}

const char *	get_ssl_pem (int vfd)
{
	LOOKUP_SSL(vfd, empty_string)
	return x->md.pem;
}

const char *	get_ssl_cert_hash (int vfd)
{
	LOOKUP_SSL(vfd, empty_string)
	return x->md.cert_hash;
}

int	get_ssl_pkey_bits (int vfd)
{
	LOOKUP_SSL(vfd, 0)
	return x->md.pkey_bits;
}

const char *	get_ssl_subject (int vfd)
{
	LOOKUP_SSL(vfd, empty_string)
	return x->md.subject;
}

const char *	get_ssl_u_cert_subject (int vfd)
{
	LOOKUP_SSL(vfd, empty_string)
	return x->md.u_cert_subject;
}

const char *	get_ssl_issuer (int vfd)
{
	LOOKUP_SSL(vfd, empty_string)
	return x->md.issuer;
}

const char *	get_ssl_u_cert_issuer (int vfd)
{
	LOOKUP_SSL(vfd, empty_string)
	return x->md.u_cert_issuer;
}

const char *	get_ssl_ssl_version (int vfd)
{
	LOOKUP_SSL(vfd, empty_string)
	return x->md.ssl_version;
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

int	ssl_startup (int vfd, int channel)
{
	return -1;
}

int	ssl_shutdown (int vfd)
{
	panic(1, "ssl_shutdown(%d) called on non-ssl client", vfd);
	return -1;
}

int	ssl_write (int vfd, const void *data, size_t len)
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

int		get_ssl_verify_result (int vfd) { return 0; }
const char *	get_ssl_pem (int vfd) { return empty_string; }
const char *	get_ssl_cert_hash (int vfd) { return empty_string; }
int		get_ssl_pkey_bits (int vfd) { return 0; }
const char *	get_ssl_subject (int vfd) { return empty_string; }
const char *	get_ssl_u_cert_subject (int vfd) { return empty_string; }
const char *	get_ssl_issuer (int vfd) { return empty_string; }
const char *	get_ssl_u_cert_issuer (int vfd) { return empty_string; }
const char *	get_ssl_ssl_version (int vfd) { return empty_string; }


#endif

