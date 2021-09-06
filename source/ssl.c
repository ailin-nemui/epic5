/*
 * ssl.c: SSL connection functions
 *
 * Original framework written by Juraj Bednar
 * Modified by B. Thomas Frazier
 *
 * Copyright 2000 Juraj Bednar.
 * Copyright 2000, 2021 EPIC Software Labs.
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

/* 
 * This code intends to support OpenSSL-1.1.* and compatible libs.
 * If you're using a different version and it doesn't work right,
 * please get in touch so we can improve things.
 */

#include "irc.h"
#include "ircaux.h"
#include "ssl.h"
#include "vars.h"

#include <openssl/crypto.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/opensslconf.h>

#include "output.h"
#include "hook.h"
#include "newio.h"
#include "vars.h"

/* * * SSL INFO STUFF * * * */
typedef struct ssl_metadata {
	int	vfd;
	int	verify_error;
	char *	pem;
	char *	cert_hash;
	int	pkey_bits;
	char *	subject;
	char *	u_cert_subject;
	char *	issuer;
	char *	u_cert_issuer;
	char *	ssl_version;
	char *	sans;
	ssl_cert_error *ssl_cert_errors;
	/* All errors set one of these flags */
	int	checkhost_error;
	int	self_signed_error;
	int	other_error;
	int	most_serious_error;
} ssl_metadata;

typedef struct	ssl_info_T {
	struct ssl_info_T *next;
	int	active;
	int	vfd;		/* The Virtual File Descriptor (new_open()) */
	int	channel;	/* The physical connection for the vfd */

	SSL_CTX	*ctx;		/* All SSLs share the same ConTeXt */
	SSL *	ssl;		/* Each of our SSLs have their own (SSL *) */
	ssl_metadata	md;	/* Plain text info about SSL connection */
	char *	hostname;	/* The hostname we connected to (for hostname checking) */
} ssl_info;

static	ssl_info *	ssl_list = NULL;
static	int		mydata_index;
static	char *		x509_default_cert_location_file = NULL;
static	char *		x509_default_cert_location_dir = NULL;
static 	SSL_CTX	*	ctx = NULL;


static SSL_CTX	*	SSL_CTX_init 		(int);
static ssl_info *	get_ssl_info 		(int);
static ssl_info *	new_ssl_info 		(int);
static ssl_info *	unlink_ssl_info 	(int);
static int 		get_x509_pkey_bits	(X509 *);
static char *		get_bio_mem_string	(BIO *);
static char *		get_x509_pem		(X509 *);
static char *		get_x509_name		(X509_NAME *);
static char *		internal_get_x509_sans 	(X509 * const);
static void		ssl_setup_locking 	(void);
static ssl_cert_error *	new_ssl_cert_error 	(int, int, const char *, ssl_cert_error *);
static void		destroy_ssl_cert_errors (ssl_cert_error *);
static int 		verify_callback 	(int, X509_STORE_CTX *);


/* * * /SET CALLBACK HANDLERS * * * */
/*
 * set_ssl_root_certs_location - Called when you do /SET SSL_ROOT_CERTS_LOCATION
 *	It determines the values to use with SSL_CTX_load_verify_locations().
 *
 * Arguments: 
 *	stuff	- The new value of /SET SSL_ROOT_CERTS_LOCATION
 *
 * Return Value:
 *	...
 *
 * Notes:
 *	Two values are maintained in this function:
 *	1. x509_default_cert_location_dir
 *	2. x509_default_cert_file
 *
 *	Now, if /set ssl_root_certs_location is set to a directory, then it
 *	will overrule #1 above.  If it is set to a file, it will overrule
 *	#2 above.  If it is unset, both values will be reset to their 
 *	default values (as OpenSSL reckons it). 
 * 	
 *	None of this is guaranteed to work.  Providing a certificate chain
 *	is a function of your OS/distribution, and some systems (FreeBSD)
 *	don't supply any certificate chains out of the box.  The location is
 *	*wildly* OS/distribution specific.
 */
void	set_ssl_root_certs_location (void *stuff)
{
	struct stat 	st;
	VARIABLE *	v;
	const char *	p;

	if (x_debug & DEBUG_SSL)
		yell("SSL >>> HERE WE GO -- SETTING SSL ROOT CERTS LOCATION");
	/* 'p' will point to /SET SSL_ROOT_CERTS_LOCATION */
	v = (VARIABLE *)stuff;
	p = v->string;

	if (x_debug & DEBUG_SSL)
		yell("SSL >>> The new value is: %s", p ? p : "(unset)");

	/* If you /SET -SSL_ROOT_CERTS_LOCATION, it forces resets to default */
	if (!p)
	{
		if (x_debug & DEBUG_SSL)
			yell("SSL >>> Unsetting previous values for x509_default_cert_location_dir");
		new_free(&x509_default_cert_location_dir);
		new_free(&x509_default_cert_location_file);
	}

	/* 
	 * If you set it to a directory, we overrule the directory default.
	 * If you set it to a file, we overrule the file default.
	 * If it's neither (or it doesn't exist, we change nothing.
	 */
	else if (stat(p, &st) == 0)
	{
		if (S_ISDIR(st.st_mode))
		{
			if (x_debug & DEBUG_SSL)
				yell("SSL >>> The new value of the /set is a directory, so setting the directory");
			malloc_strcpy(&x509_default_cert_location_dir, p);
		}
		else if (S_ISREG(st.st_mode))
		{
			if (x_debug & DEBUG_SSL)
				yell("SSL >>> The new value of the /set is a file, so setting the file");
			malloc_strcpy(&x509_default_cert_location_file, p);
		}
		else
		{
			if (x_debug & DEBUG_SSL)
				yell("SSL >>> The new value of the /set is neither file nor directory, so doing nothing.");
		}
	}

	/*
	 * Now, if we get to this point, and we don't have a default 
	 * directory to use, then we use the OpenSSL default.
	 */
	if (!x509_default_cert_location_dir)
	{
		const char *	dir = NULL;

		if (x_debug & DEBUG_SSL)
			yell("SSL >>> There is no default location for the directory, looking for one.");

		/* This is where OpenSSL says our cert chain should live */
		if (!(dir = getenv(X509_get_default_cert_dir_env())))
			dir = X509_get_default_cert_dir();

		if (x_debug & DEBUG_SSL)
			yell("SSL >>> OpenSSL suggests %s", dir);

		/* If that location is a directory, use it. */
		if (dir)
		{
		    if (stat(dir, &st) == 0)
		    {
			if (S_ISDIR(st.st_mode))
			{
				if (x_debug & DEBUG_SSL)
					yell("SSL >>> We have a winner for the directory.");
				malloc_strcpy(&x509_default_cert_location_dir, dir);
			}
			else
				if (x_debug & DEBUG_SSL)
					yell("SSL >>> It wasn't a directory.");
		    }
		    else
			if (x_debug & DEBUG_SSL)
				yell("SSL >>> I couldn't stat that...");
		}
		else
			if (x_debug & DEBUG_SSL)
				yell("SSL >>> OpenSSL didn't suggest anything..");
	}
	else
		if (x_debug & DEBUG_SSL)
			yell("SSL >>> I already have a directory location: %s", x509_default_cert_location_dir);


	if (!x509_default_cert_location_file)
	{
		const char *	file = NULL;

		if (x_debug & DEBUG_SSL)
			yell("SSL >>> There is no default location for the file, looking for one.");

		/* This is where OpenSSL says our cert chain should live */
		if (!(file = getenv(X509_get_default_cert_file_env())))
			file = X509_get_default_cert_file();

		if (x_debug & DEBUG_SSL)
			yell("SSL >> OpenSSL suggests %s", file);

		/* If that location is a file, use it. */
		if (file)
		{
		    if (stat(file, &st) == 0)
		    {
			if (S_ISREG(st.st_mode))
			{
				if (x_debug & DEBUG_SSL)
					yell("SSL >>> We have a winner for the file.");
				malloc_strcpy(&x509_default_cert_location_file, file);
			}
			else
				if (x_debug & DEBUG_SSL)
					yell("SSL >>> It wasn't a file.");
		    }
		    else
			if (x_debug & DEBUG_SSL)
				yell("SSL >>> I couldn't stat that.");
		}
		else
			if (x_debug & DEBUG_SSL)
				yell("SSL >>> OpenSSL didn't suggest anything..");
	}
	else
		if (x_debug & DEBUG_SSL)
			yell("SSL >>> I already have a file location: %s", x509_default_cert_location_file);

	if (ctx)
	{
		say("Verifying SSL certificates using (dir: %s), (file: %s)",
			x509_default_cert_location_dir ? x509_default_cert_location_dir : "<none>", 
			x509_default_cert_location_file ? x509_default_cert_location_file : "<none>");
		SSL_CTX_load_verify_locations(ctx, x509_default_cert_location_file,
						   x509_default_cert_location_dir);
	}
}

void	set_ssl_ciphers (void *stuff)
{
	VARIABLE *	v;
	const char *	p;

	/* 'p' will point to /SET SSL_CIPHERS */
	v = (VARIABLE *)stuff;
	p = v->string;

	if (ctx)
		SSL_CTX_set_cipher_list(ctx, p);
}

/* * * CTX / INITIALIZATION STUFF * * * */
/*
 * SSL_CTX_init -- Create and set up global SSL ConTeXt object.
 * 		   This bootstraps the SSL system the first time it's called.
 *		   All SSL connections share the same (SSL_CTX)
 *
 * ARGS:
 *	server -- Will this CTX be used as a client (0) or server (1)?
 *			Note: Only 0 (client) is supported.
 *
 * RETURN VALUE:
 *	The global SSL_CTX you can use with an SSL connection.
 *	You _!*!_MUST NOT_!*!_ call SSL_CTX_free() on the return value later!
 */
static SSL_CTX	*SSL_CTX_init (int server)
{
	if (ctx != NULL)
		return ctx;

	mydata_index = SSL_get_ex_new_index(0, "mydata index", NULL, NULL, NULL);
	ssl_setup_locking();
	SSL_load_error_strings();
	SSL_library_init();

	ctx = SSL_CTX_new(SSLv23_client_method());
	SSL_CTX_set_session_cache_mode(ctx, SSL_SESS_CACHE_BOTH);
	SSL_CTX_set_timeout(ctx, 300);

	/* These have hopefully already been set! */
	say("Verifying SSL certificates using (dir: %s), (file: %s)",
		x509_default_cert_location_dir ? x509_default_cert_location_dir : "<none>", 
		x509_default_cert_location_file ? x509_default_cert_location_file : "<none>");
	SSL_CTX_load_verify_locations(ctx, x509_default_cert_location_file,
					   x509_default_cert_location_dir);
        SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER | SSL_VERIFY_CLIENT_ONCE, verify_callback);

	if (get_string_var(SSL_CIPHERS_VAR))
		SSL_CTX_set_cipher_list(ctx, get_string_var(SSL_CIPHERS_VAR));

	return ctx;
}

/*
 * get_ssl_info -- Get the data object for an SSL-enabled connection
 *
 * ARGS:
 *	vfd -- A virtual file descriptor, previously returned by new_open().
 * RETURN VALUE:
 *	If the vfd has been set up to use SSL, an (ssl_info *) to its data.
 *	If the vfd has not been se tup to use SSL, NULL.
 */
static ssl_info *	get_ssl_info (int vfd)
{
	ssl_info *x;

	for (x = ssl_list; x; x = x->next)
		if (x->vfd == vfd)
			return x;

	return NULL;
}

/*
 * new_ssl_info -- Create a data object for an SSL-enabled connection
 *
 * ARGS: 
 *	vfd -- A virtual file descriptor, previously returned by new_open().
 * RETURN VALUE:
 *	If the vfd has never been previously set up to use SSL, a pointer to
 *		metadata for the vfd.  The 'channel', 'ctx' and 'ssl_obj'
 *		fields will not be filled in yet!
 *	If the vfd has previously been set up to use SSL, a pointer to the
 *		metadata for that vfd.  Existing information about the SSL
 *		connection on that vfd will be discarded!
 */
static ssl_info *	new_ssl_info (int vfd)
{
	ssl_info *x;
	int	i;

	if (!(x = get_ssl_info(vfd)))
	{
		x = new_malloc(sizeof(*x));
		x->next = ssl_list;
		ssl_list = x;
	}

	x->active = 0;
	x->vfd = vfd;
	x->channel = -1;
	x->ctx = NULL;
	x->ssl = NULL;
	x->hostname = NULL;

	x->md.vfd = vfd;
	x->md.pem = NULL;
	x->md.cert_hash = NULL;
	x->md.pkey_bits = 0;
	x->md.subject = NULL;
	x->md.u_cert_subject = NULL;
	x->md.issuer = NULL;
	x->md.u_cert_issuer = NULL;
	x->md.ssl_version = NULL;
	x->md.verify_error = -1;
	x->md.checkhost_error = -1;
	x->md.self_signed_error = -1;
	x->md.other_error = -1;
	x->md.most_serious_error = X509_V_OK;
	x->md.sans = NULL;
	x->md.ssl_cert_errors = NULL;

	return x;
}

/*
 * unlink_ssl_info -- Unregister vfd as an ssl-using connection.
 *
 * ARGS:
 *	vfd -- A virtual file descriptor, previously returned by new_open().
 * RETURN VALUE:
 *	If the vfd has been previously set up to use SSL, the ssl_info for it.
 *	If the vfd has never been previously set up to use SSL, NULL.
 *	You _MUST_ tear down the SSL connection and free the metadata of the retval.
 *	Use ssl_shutdown() to close an ssl connection instead of calling here.
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
 *
 * ARGS:
 *	vfd -- A virtual file descriptor, previously returned by new_open().
 *	channel -- The channel that is mapped to the vfd (passed to new_open())
 *	hostname -- The hostname we intend to connect to.  This is used for
 *		    hostname checking in the SSL certificate
 *
 * RETURN VALUE:
 *	-1	Something really died
 *	 0	SSL negotiation is pending
 *	 1	SSL negotiation is complete (not supported)
 */
int	ssl_startup (int vfd, int channel, const char *hostname)
{
	ssl_info *	x;

	if (!(x = new_ssl_info(vfd)))
	{
		syserr(SRV(vfd), "Could not make new ssl info "
				 "(vfd [%d]/channel [%d])",
				vfd, channel);
		errno = EINVAL;
		return -1;
	}
	x->channel = channel;
	x->ctx = SSL_CTX_init(0);
	malloc_strcpy(&x->hostname, hostname);

	say("SSL negotiation for channel [%d] in progress...", channel);
	if (!(x->ssl = SSL_new(x->ctx)))
	{
		/* Get rid of the 'x' we just created */
		ssl_shutdown(vfd);
		syserr(SRV(vfd), "Could not make new SSL "
				 "(vfd [%d]/channel [%d])",
				vfd, channel);
		errno = EINVAL;
		return -1;
	}

	SSL_set_fd(x->ssl, channel);
        SSL_set_ex_data(x->ssl, mydata_index, x);

	if (!SSL_set1_host(x->ssl, hostname)) {
		syserr(SRV(vfd), "Could not set hostname for SSL peer for %d", vfd);
		errno = EINVAL;
		return -1;
	}

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
	if (x->ssl)
	{
		SSL_shutdown(x->ssl);
		SSL_free(x->ssl);
		x->ssl = NULL;
	}

	x->md.vfd = -1;
	x->md.verify_error = -1;
	x->md.checkhost_error = -1;
	x->md.self_signed_error = -1;
	x->md.other_error = -1;
	new_free(&x->md.pem);
	new_free(&x->md.cert_hash);
	x->md.pkey_bits = 0;
	new_free(&x->md.subject);
	new_free(&x->md.u_cert_subject);
	new_free(&x->md.issuer);
	new_free(&x->md.u_cert_issuer);
	new_free(&x->md.ssl_version);
	new_free(&x->md.sans);
	destroy_ssl_cert_errors(x->md.ssl_cert_errors);
	x->md.ssl_cert_errors = NULL;

	new_free(&x->hostname);
	new_free(&x);
	return 0;
}

/* * * SSL I/O FUNCTIONS * * * */
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

	if (!(x = get_ssl_info(vfd)))
	{
		errno = EINVAL;
		return -1;
	}

	if (x->ssl == NULL)
	{
		errno = EINVAL;
		say("SSL write error - ssl socket = 0");
		return -1;
	}

	err = SSL_write(x->ssl, data, len);
	BIO_flush(SSL_get_wbio(x->ssl));
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

	if (!(x = get_ssl_info(vfd)))
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

		c = SSL_read(x->ssl, buffer, sizeof(buffer));
		if (c < 0)
		{
		    int ssl_error = SSL_get_error(x->ssl, c);
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
	while (SSL_pending(x->ssl) > 0);

	return c;
}

int	ssl_connect (int vfd, int quiet)
{
	ssl_info *	x;
	int		errcode;
	int		ssl_err;

	if (!(x = get_ssl_info(vfd)))
	{
		errno = EINVAL;
		return -1;
	}

	errcode = SSL_connect(x->ssl);
	if (errcode <= 0)
	{
		ssl_err = SSL_get_error(x->ssl, errcode);
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

/* * * METADATA FUNCTIONS * * * */
static int get_x509_pkey_bits(X509 *cert)
{
	EVP_PKEY *pubkey;
	int pkey_bits;

	pubkey = X509_get_pubkey(cert);
	pkey_bits = EVP_PKEY_bits(pubkey);
	EVP_PKEY_free(pubkey);
	return pkey_bits;
}	

static char *get_bio_mem_string(BIO *mem)
{
	long bytes;
	char *p = NULL;

	bytes = BIO_get_mem_data(mem, &p);
	
	if (p && bytes >= 0 && (unsigned long)bytes < SIZE_MAX)
	{
		char *result = new_malloc((size_t)bytes + 1);
		memcpy(result, p, bytes);
		result[bytes] = 0;
		return result;
	}
	
	return malloc_strdup(empty_string);
}

static char *get_x509_pem(X509 *cert)
{
	BIO *mem = BIO_new(BIO_s_mem());
	char *result;

	PEM_write_bio_X509(mem, cert);
	result = get_bio_mem_string(mem);
	BIO_free(mem);
	return result;
}

static char *get_x509_name(X509_NAME *nm)
{
	BIO *mem = BIO_new(BIO_s_mem());
	char *result;

	X509_NAME_print_ex(mem, nm, 0, XN_FLAG_RFC2253 & ~ASN1_STRFLGS_ESC_MSB);
	result = get_bio_mem_string(mem);
	BIO_free(mem);
	return result;
}

/*
 * ssl_connected - Finalize the SSL metadata object for policy evaluation
 *
 * Arguments:
 *	vfd	 - The Virtual File Descriptor for the SSL connection
 *		   (as returned by new_open())
 *
 * A brief review of history:
 *	1. First, you /server irc.hostname.com
 *	2. That launches a nonblocking socket connect()ion, 
 *	3. When that completes, if server "type" is "irc-ssl", a nonblocking
 *	   SSL_connect() is launched.
 *	4. When that completes, we get called.
 *	5. After we get called, someone can call the get_ssl_* functions to
 *	   get vital statistics in 'x' in order to make policy decisions 
 *	   about whether they're happy with how this all ended.
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
 *
 * Ideally, you should only call this once.
 * Ideally, you shouldn't try to use an SSL before you've called this.
 */
int	ssl_connected (int vfd)
{
	X509 *	 	server_cert;	/* X509 in SSL internal fmt */
	ssl_info *	x;		/* EPIC info about the conn */
	unsigned char 	h[256];		/* A place for X509_digest() to write */
	unsigned	hlen;		/* How big digest in 'h' is */
	unsigned char	htext[1024];	/* A human readable version of 'h' */
	unsigned	i;		/* How many bytes in 'h' we converted */

	/*
	 * First off -- do I think this is even an SSL enabled connection?
	 */
	if (!(x = get_ssl_info(vfd)))
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

	/*
	 * STEP 2: 
	 * Fetch the server's certificate.
	 * If we are unable to get the certificate, then something
	 * failed and we should just bail right here. 
	 */
	if (!(server_cert = SSL_get_peer_certificate(x->ssl)))
	{
		syserr(SRV(vfd), "SSL negotiation failed - reporting as error");
		x->ctx = NULL;
		SSL_free(x->ssl);
		x->ssl = NULL;
		if (!write(x->channel, empty_string, 1)) /* XXX Is this correct? */
			(void) 0;
		return -1;
	}


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
	/* If no errors were encountered, we can be confident there were none! */
	if (x->md.verify_error == -1)
		x->md.verify_error = 0;
	if (x->md.checkhost_error == -1)
		x->md.checkhost_error = 0;
	if (x->md.self_signed_error == -1)
		x->md.self_signed_error = 0;
	if (x->md.other_error == -1)
		x->md.other_error = 0;

	/* * */
	/*
	 * STEP 4:
	 * (Advance apology -- in order to get strings out of OpenSSL,
	 *  you have to dance with the devils of its BIO objects.)
 	 */

	/* 
	 * STEP 4a:	The server's certificate PEM
	 */
	x->md.pem = get_x509_pem(server_cert);

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

	/*
	 * STEP 4d: 	The server's certificate's "subject" [hostname]
	 * STEP 4e:	The server's certificate's "subject" (urlified)
	 */
	x->md.subject = get_x509_name(X509_get_subject_name(server_cert));

	if (!(x->md.u_cert_subject = transform_string_dyn("+URL", 
							  x->md.subject,
							  0, NULL)))
		x->md.u_cert_subject = malloc_strdup(x->md.subject);

	/*
	 * STEP 4f:	The server's certificate's "issuer" [CA]
	 * STEP 4g:	The server's certificate's "issuer" (urlified)
	 */
	x->md.issuer = get_x509_name(X509_get_issuer_name(server_cert));

	if (!(x->md.u_cert_issuer = transform_string_dyn("+URL", 
							  x->md.issuer,
							  0, NULL)))
		x->md.u_cert_issuer = malloc_strdup(x->md.issuer);

	/*
	 * STEP 4c:	The server's certificate's public key's bit-size
	 */
	x->md.pkey_bits = get_x509_pkey_bits(server_cert);

	/*
	 * STEP 4h:	The connection's SSL protocol (ie, TLSv1 or SSLv3)
	 */
	x->md.ssl_version = malloc_strdup(SSL_get_version(x->ssl));

	/*
	 * STEP 4k:	Get the list of SANs
	 */
	x->md.sans = internal_get_x509_sans(server_cert);

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
	 *	$4 - The verification result (OK = "YES"; not OK = "NO")
	 *	$5 - What ssl are we using? (SSLv3 or TLSv1)
	 *	$6 - What is the digest of the certificate?
	 * 	$7 - Bad Hostname error (1 = "YES", 0 = "NO")
	 *	$8 - Self-signed error (1 = "YES", 0 = "NO")
	 *	$9 - Other error (1 = "YES", 0 = "NO")
	 *	$10 - Verify error (1 = "YES", 0 = "NO")
	 *
	 * It's expected from here that a script could stash this in
	 * a file somewhere and use it to detect if the certificate
	 * changed between connections.   Or something clever.
	 */
	if (do_hook(SSL_SERVER_CERT_LIST, "%d %s %s %d %d %s %s %d %d", 
			x->md.vfd, 
			x->md.u_cert_subject, x->md.u_cert_issuer, 
			x->md.pkey_bits, x->md.most_serious_error,
			x->md.ssl_version, x->md.cert_hash,
			x->md.checkhost_error, x->md.self_signed_error,
			x->md.other_error, x->md.verify_error))
	{
		say("SSL negotiation complete using %s (%s)", 
			x->md.ssl_version, 
			SSL_get_cipher_name(x->ssl));
		say("SSL certificate subject: %s", 
			x->md.subject);
		say("SSL certificate issuer: %s",  
			x->md.issuer);
		say("SSL certificate public key length: %d bits", 
			x->md.pkey_bits);
		say("SSL certificate digest: %s", 
			x->md.cert_hash);
		say("SSL certificate SANs: %s", 
			x->md.sans);
		say("SSL certificate verification -- Any error: %s",
			x->md.verify_error ? "YES" : "NO");
		say("SSL certificate verification -- Self-signed error: %s",
			x->md.self_signed_error ? "YES" : "NO");
		say("SSL certificate verification -- Bad Hostname error: %s",
			x->md.checkhost_error ? "YES" : "NO");
		say("SSL certificate verification -- Other serious error: %s (%s)",
			x->md.other_error ? "YES" : "NO",
			X509_verify_cert_error_string(x->md.most_serious_error));
	}

	/*
	 * STEP 6: Clean up after ourselves
	 */
	X509_free(server_cert);
	return 1;
}


/* * * AUXILLARY (EXTERNAL) API FUNCTIONS * * * */
int	is_fd_ssl_enabled (int vfd)
{
	if (get_ssl_info(vfd))
		return 1;
	else
		return 0;
}

int	client_ssl_enabled (void)
{
	return 1;
}

/* * * * * * */
#define LOOKUP_SSL(vfd, missingval)	\
	ssl_info *x;			\
					\
	if (!(x = get_ssl_info(vfd)))	\
		return missingval ;	\
	if (!x->ssl)			\
		return missingval ;	\


const char *	get_ssl_cipher (int vfd)
{
	LOOKUP_SSL(vfd, empty_string)
	return SSL_get_cipher_name(x->ssl);
}

int	get_ssl_verify_error (int vfd)
{
	LOOKUP_SSL(vfd, 0)
	return x->md.verify_error;
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

const char *	get_ssl_sans (int vfd)
{
	LOOKUP_SSL(vfd, empty_string)
	return x->md.sans;
}

int	get_ssl_checkhost_error (int vfd)
{
	LOOKUP_SSL(vfd, -1);
	return x->md.checkhost_error;
}

int	get_ssl_self_signed_error (int vfd)
{
	LOOKUP_SSL(vfd, -1);
	return x->md.self_signed_error;
}

int	get_ssl_other_error (int vfd)
{
	LOOKUP_SSL(vfd, -1);
	return x->md.other_error;
}

int	get_ssl_most_serious_error (int vfd)
{
	LOOKUP_SSL(vfd, -1);
	return x->md.most_serious_error;
}

/* 
 * internal_get_x509_sans - Get the SAN extension value from an SSL Cert
 *
 * Arguments:
 *	cert	- An SSL certificate
 *
 * Return value:
 *	If present, the SAN from the certificate 
 *	If not present, the empty string
 *	-> You *_MUST_* new_free() the return value <-
 *
 * I transmogrified this from
 *    https://newbedev.com/how-to-check-subject-alternative-names-for-a-ssl-tls-certificate 
 */
static char *	internal_get_x509_sans (X509 *const cert)
{
	int		success = 0;
	GENERAL_NAMES *	names = NULL;
	unsigned char *	utf8 = NULL;
	int		i, count;
	char *		retval = NULL;
	size_t		rvclue = 0;

        if (!cert) 
		return malloc_strdup(empty_string);

	/*
	 * If you're like me this seems like black magic. Let's unpack this.
 	 *
	 *   X509 - SSL Certificates are properly known as "X.509 documents"
 	 *          so the object that represents them in OpenSSL is (X509).
	 *   ext  - X.509 documents have core mandatory fields, but version 3
	 * 	    standard allowed a list of extension fields, which are
	 *	    still semi-hardcoded  (that is, you can't just put in 
	 *	    any extension you invent; people have to know about it)
	 *	    SANs are stored as extension #85, and openssl helpfully
	 *	    #define's NID_subject_alt_name 85
	 *   d2i  - d stands for "DEM" and i stands for "Internal".
	 *	    Data in X.509 documents are encoded as binary blobs
	 *	    and SSL certificates use DEM encoding.  So you have to
	 * 	    convert the data in the certificate to a C thing.
 	 *	    SANs are defined as having the type STACK(GENERAL_NAME),
	 *	    so this converts the DEM encoded SAN to C objects
	 *   SAN  - Subject Alternate Name -- An SSL Certificate is
 	 *	    technically allowed to cover multiple hostnames.
	 *	    They are stashed in this extension field.  
	 *	    They support wildcards (ie, "*.dal.net")
	 *	    Since certificates can only be issued to one host, if
	 *	    you need to cover multiple hosts, you're going to use
	 *	    SANs to include all the extra hostnames.  
	 *	    SANs are a data object of type (GENERAL_NAME)
	 */
        if (!(names = X509_get_ext_d2i(cert, NID_subject_alt_name, 0, 0 )))
		return malloc_strdup(empty_string);

	/* If the SAN list has no entries, then we treat that as "no sans" */
	if (!(count = sk_GENERAL_NAME_num(names)))
		return malloc_strdup(empty_string);

	/* Walk all of the SANs one at a time */
        for (i = 0; i < count; i++)
        {
		GENERAL_NAME*	entry;

		/* 
		 * Fetch the i'th (GENERAL_NAME) in the stack.
		 * However, gaps in the stack are allowed (urgh)
		 */
		if (!(entry = sk_GENERAL_NAME_value(names, i)))
			continue;

		/*
		 * GENERAL_NAMEs have types/domains.
		 * Internet Hostnames are of type "GEN_DNS".
		 * Those are the only ones we are interested in here.
		 */
		if (entry->type == GEN_DNS)
		{
			int 	len1;
			size_t	len2;
			unsigned char *utf8 = NULL;

			/*
			 * 'len1' is the number of bytes in the ASN1 string.
			 * The man page says < 0 means an error.
			 */
			if ((len1 = ASN1_STRING_to_UTF8(&utf8, entry->d.dNSName)) >= 0)
			{
			    /*
			     * Internet Hostnames don't include 'nul's.
			     * But ASN1 strings certianly can!  
			     * So if we detect an ASN1 string with an embedded nul,
			     * that's an invalid value, and we declare shenanigans.
			     */
			    if ((size_t)len1 == strlen((const char *)utf8))
				malloc_strcat_word_c(&retval, space, utf8, DWORD_NO, &rvclue);
			}

			if (utf8)
				OPENSSL_free(utf8);
		}
        }

	if (names)
		GENERAL_NAMES_free(names);

	return retval ? retval : malloc_strdup(empty_string);
}

/* * * PTHREAD NONSENSE * * * */
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

/* * * SSL CERT ERROR STUFF * * * */
static ssl_cert_error	*new_ssl_cert_error (int err, int depth, const char *oneline, ssl_cert_error *next)
{
	ssl_cert_error *ex;

	ex = new_malloc(sizeof(ssl_cert_error));
	ex->err = err;
	ex->depth = depth;
	strlcpy(ex->oneline, oneline, MAX_ONELINE);
	ex->next = next;

	return ex;
}

static void	destroy_ssl_cert_errors (ssl_cert_error *chain)
{
	ssl_cert_error *ex, *next;

	for (ex = chain; ex; ex = next)
	{
		next = ex->next;
		new_free(&ex);
	}
}


/*
 * verify_callback - Policy function for SSL Certificate Verification
 * (I pretty much cribbed this from 'man SSL_CTX_set_verify')
 *
 * OpenSSL's default handling of SSL verification errors has two problems:
 * 1) It's "lossy" - it only remembers one error, discarding others
 * 2) It's "all or nothing" - you can't pick and choose which errors 
 *    you'll tolerate
 *
 * The docs suggest that if you don't like this arrangement, you should
 * DIY your own policy function.  That's what this is.
 *
 * A server sends you a "chain" of SSL certificates, starting with the 
 * server's own certificate, and the certificate of whatever authority 
 * signed the cert, and so on, until you reach a certificate that was signed 
 * by a "Root CA" which you independently trust.  (This is usually provided 
 * by your OS, and ends up in /set ssl_root_certs_location)
 * 
 * The return value of this function controls whether the evaluation 
 * should continue.  The return value is suggested by "preverify_ok"
 *   Return value 1 ("ok") - Please continue verification
 *   Return value 0 ("not ok") - I've seen enough. stop and abort.
 * Therefore, if a handler wants to see all the errors, it needs to 
 * return 1 no matter what.  (Pedants will point out that this matters
 * based on whether SSL_VERIFY_NONE or SSL_VERIFY_PEER was used, but
 * i do not want to go into that here)
 *
 * After going back and forth over and over, I decided that this 
 * function is information-gathering only.  I must stress this point
 * clearly:  even though this function is a place where you *could*
 * handle policy, I made a deliberate choice not to do that.  Rather, 
 * this function just slurps up all the errors and stows them for
 * evaluation by the application level (ie, /server), who is
 * responsible for all policy choices.
 * 
 * Arguments:
 *	preverify_ok - 0 if OpenSSL found a certificate error
 *		       1 if OpenSSL found no further errors with a cert
 *	ctx	     - Information about the certificates being evaluated.
 *		       Because many certs might be evaluated, you have
 *		       to interrogate this to find out what is up.
 *		       Most of our actual arguments live inside here.
 *
 * Indirect arguments (pulled out of `ctx'):
 * 	err_cert     - This is the cert that was evaluated
 *	err	     - If preverify_ok == 0, the error that was found
 *	depth	     - Depth == 0 is the server's own certificate
 *		       Each increasing layer "goes back" eventually
 *		       to a root CA (hopefully) 
 *	ssl	     - This is the (SSL) object for this connection
 *	mydata	     - This is our metadata object for this connection
 *
 * Return value:
 *	This function always returns 1
 */
static int verify_callback (int preverify_ok, X509_STORE_CTX *ctx)
{
	char    subject[MAX_ONELINE];
	char    issuer[MAX_ONELINE];
	X509 *	err_cert;
	int     err, 
		depth;
	SSL *	ssl;
	ssl_info *mydata;

	/*
	 * Get the indirect arguments out of 'ctx'
	 */
	err_cert = X509_STORE_CTX_get_current_cert(ctx);
	err = X509_STORE_CTX_get_error(ctx);
	depth = X509_STORE_CTX_get_error_depth(ctx);
	ssl = X509_STORE_CTX_get_ex_data(ctx, SSL_get_ex_data_X509_STORE_CTX_idx());
	mydata = SSL_get_ex_data(ssl, mydata_index);

	X509_NAME_oneline(X509_get_subject_name(err_cert), subject, MAX_ONELINE);
	X509_NAME_oneline(X509_get_issuer_name(err_cert), issuer, MAX_ONELINE);

	/*
	 * If an error has occurred, see if it qualifies for an exemption
	 */
	if (preverify_ok == 0)
	{
		mydata->md.ssl_cert_errors = new_ssl_cert_error(err, depth, subject, 
								mydata->md.ssl_cert_errors);

		mydata->md.verify_error = 1;

		/* We have special flags for this in the metadata */
		if (err == X509_V_ERR_DEPTH_ZERO_SELF_SIGNED_CERT)
			mydata->md.self_signed_error = 1;
		else if (err == X509_V_ERR_HOSTNAME_MISMATCH)
			mydata->md.checkhost_error = 1;
		else
			mydata->md.other_error = 1;

		say("ssl cert verify error: err=%d (%s), depth=%d, %s issued by %s", 
			err, X509_verify_cert_error_string(err), 
			depth, subject, issuer);

		if (mydata->md.most_serious_error == X509_V_OK)
			mydata->md.most_serious_error = err;
		else if (mydata->md.most_serious_error == X509_V_ERR_DEPTH_ZERO_SELF_SIGNED_CERT)
			mydata->md.most_serious_error = err;
		else if (mydata->md.most_serious_error == X509_V_ERR_HOSTNAME_MISMATCH)
			if (err != X509_V_ERR_DEPTH_ZERO_SELF_SIGNED_CERT)
				mydata->md.most_serious_error = err;
	}
	else
		say("ssl cert OK at depth=%d, %s issued by %s", depth, subject, issuer);

#if 0
	/*
	 * At this point, err contains the last verification error. We can use
	 * it for something special
	 */
	if (preverify_ok == 0 && (err == X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT)) 
	{
		X509_NAME_oneline(X509_get_issuer_name(err_cert), buf, MAX_ONELINE);
		say("issuer = %s", buf);
	}
#endif

	return 1;
}

