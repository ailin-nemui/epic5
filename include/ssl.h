/*
 * ssl.h -- header file for ssl.c
 *
 */

#ifdef HAVE_SSL

#include <openssl/crypto.h>
#include <openssl/x509.h>
#include <openssl/pem.h>
#include <openssl/ssl.h>
#include <openssl/err.h>

#ifndef TRUE
#define TRUE 1
#define FALSE 0
#endif

#define CHK_NULL(x) if ((x) == NULL) { say("SSL error - NULL data from server"); return; }
#define CHK_ERR(err, s) if ((err) == -1) { say("SSL prime error - %s", s); return; }
#define CHK_SSL(err,fd) if ((err) == -1) { say("SSL CHK error - %d %d", err, SSL_get_error(fd, err)); return; }

/* Make these what you want for cert & key files */

/* extern	SSL_CTX*	ctx;	*/
/* extern	SSL_METHOD	*meth;	*/

	void		SSL_show_errors	(void);
	SSL_CTX*	SSL_CTX_init	(int server);
	SSL		*SSL_FD_init	(SSL_CTX *ctx, int des);

#endif
