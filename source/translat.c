/*
 * translat.c:  Stuff for handling character translation tables
 * and a digraph entry facility.  Support an international IRC!
 *
 * I listen to Sex Pistols, so I assume everyone in this world,
 * and more specifically, all servers, are using ISO 8859/1
 * (Latin-1).  And in case of doubt, please consult Jarkko 'Wiz'
 * Oikarinen's document "Internet Relay Chat Protocol" (doc/Comms
 * in the ircd package), paragraph 2.2.  Besides, all of the sane
 * world has already converted to this set.  (X-Windows, Digital,
 * MS-Windows, etc.)
 * If someone please would forward me some documentation on other
 * international sets, like 8859/2 - 8859/10 etc, please do so!
 * Moreover, feedback on the tables in the definition files would
 * be greatly appreciated!
 * Another idea, to be implemented some beautiful day, would be
 * to add transliteration of the Kanji/Katakana sets used in
 * the far east.  8-)
 * Tomten <tomten@solace.hsh.se> / <tomten@lysator.liu.se>
 */

#include "irc.h"
#include "vars.h"
#include "translat.h"
#include "ircaux.h"
#include "window.h"
#include "screen.h"
#include "output.h"

/* Globals */
unsigned char	transToClient[256];    /* Server to client translation. */
unsigned char	transFromClient[256];  /* Client to server translation. */
char	translation = 0;	/* 0 for transparent (no) translation. */



/*
 * set_translation:  Called when the TRANSLATION variable is SET.
 * Attempts to load a new translation table.
 */
void	set_translation (char *tablename)
{
	FILE	*table;
	unsigned char	temp_table[512];
	char	*filename = (char *) 0;
	int	inputs[8];
	int	j,
		c = 0;
	char	buffer[81];

	if (!tablename)
	{
		translation = 0;
		return;
	}
	tablename = upper(tablename);

	/* Check for transparent mode; ISO-8859/1, Latin-1 */
	if (!strcmp("LATIN_1", tablename))
	{
		translation = 0;
		return;
	}

	/* Else try loading the translation table from disk. */
	if (get_string_var(TRANSLATION_PATH_VAR))
		malloc_strcpy(&filename, get_string_var(TRANSLATION_PATH_VAR));
	malloc_strcat(&filename, tablename);
	if ( !(table = fopen(filename, "r")) )
	{
		say("Cannot open character table definition \"%s\" !",
			tablename);
		set_string_var(TRANSLATION_VAR, (char *) 0);
		new_free(&filename);
		return;
	}

	/* Any problems in the translation tables between hosts are
	 * almost certain to be caused here.
	 * many scanf implementations do not work as defined. In particular,
	 * scanf should ignore white space including new lines (many stop
	 * at the new line character, hence the fgets and sscanf workaround),
	 * many fail to read 0xab as a hexadecimal number (failing on the
	 * x) despite the 0x being defined as optionally existing on input,
	 * and others zero out all the output variables if there is trailing
	 * non white space in the format string which doesn't appear on the
	 * input. Overall, the standard I/O libraries have a tendancy not
	 * to be very standard.
	 */
	while (fgets(buffer, 80, table))
	{
		sscanf(buffer, "0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x",
		    inputs+0, inputs+1, inputs+2, inputs+3,
		    inputs+4, inputs+5, inputs+6, inputs+7);
		for (j = 0; j<8; j++)
			temp_table[c++] = (unsigned char) inputs[j];
	}
	fclose(table);
	new_free(&filename);
	if (c == 512)
	{
		for (c = 0; c <= 255; c++)
		{
			transToClient[c] = temp_table[c];
			transFromClient[c] = temp_table[c | 256];
		}
		translation = 1;
	}
	else
	{
		say("Error loading translation table \"%s\" !", tablename);
		set_string_var(TRANSLATION_VAR, (char *) 0);
	}
}


