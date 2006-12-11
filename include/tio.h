/*
 * tio: threaded stdio to avoid blocking.
 */

#ifndef TIO_H
#define TIO_H

#ifdef WITH_THREADED_STDOUT
struct FILE;

struct tio_file_stru;
typedef struct tio_file_stru tio_file;

extern tio_file *tio_stdout;

	void		 tio_init	(void);
	tio_file	*tio_open	(FILE *);
	void		 tio_fputc	(int, tio_file *);
	void		 tio_fputs	(char const *, tio_file *);
	void		 tio_flush	(tio_file *);
	void		 tio_close	(tio_file *);

#define tio_putc(c) tio_fputc(c, tio_stdout)
#define tio_puts(s) tio_fputs(s, tio_stdout)
#endif

#endif
