/*
 * This is an implementation of wcwidth() and wcswidth() (defined in
 * IEEE Std 1002.1-2001) for Unicode.
 *
 * http://www.opengroup.org/onlinepubs/007904975/functions/wcwidth.html
 * http://www.opengroup.org/onlinepubs/007904975/functions/wcswidth.html
 *
 * In fixed-width output devices, Latin characters all occupy a single
 * "cell" position of equal width, whereas ideographic CJK characters
 * occupy two such cells. Interoperability between terminal-line
 * applications and (teletype-style) character terminals using the
 * UTF-8 encoding requires agreement on which character should advance
 * the cursor by how many cell positions. No established formal
 * standards exist at present on which Unicode character shall occupy
 * how many cell positions on character terminals. These routines are
 * a first attempt of defining such behavior based on simple rules
 * applied to data provided by the Unicode Consortium.
 *
 * For some graphical characters, the Unicode standard explicitly
 * defines a character-cell width via the definition of the East Asian
 * FullWidth (F), Wide (W), Half-width (H), and Narrow (Na) classes.
 * In all these cases, there is no ambiguity about which width a
 * terminal shall use. For characters in the East Asian Ambiguous (A)
 * class, the width choice depends purely on a preference of backward
 * compatibility with either historic CJK or Western practice.
 * Choosing single-width for these characters is easy to justify as
 * the appropriate long-term solution, as the CJK practice of
 * displaying these characters as double-width comes from historic
 * implementation simplicity (8-bit encoded characters were displayed
 * single-width and 16-bit ones double-width, even for Greek,
 * Cyrillic, etc.) and not any typographic considerations.
 *
 * Much less clear is the choice of width for the Not East Asian
 * (Neutral) class. Existing practice does not dictate a width for any
 * of these characters. It would nevertheless make sense
 * typographically to allocate two character cells to characters such
 * as for instance EM SPACE or VOLUME INTEGRAL, which cannot be
 * represented adequately with a single-width glyph. The following
 * routines at present merely assign a single-cell width to all
 * neutral characters, in the interest of simplicity. This is not
 * entirely satisfactory and should be reconsidered before
 * establishing a formal standard in this area. At the moment, the
 * decision which Not East Asian (Neutral) characters should be
 * represented by double-width glyphs cannot yet be answered by
 * applying a simple rule from the Unicode database content. Setting
 * up a proper standard for the behavior of UTF-8 character terminals
 * will require a careful analysis not only of each Unicode character,
 * but also of each presentation form, something the author of these
 * routines has avoided to do so far.
 *
 * http://www.unicode.org/unicode/reports/tr11/
 *
 * Markus Kuhn -- 2007-05-26 (Unicode 5.0)
 *
 * Permission to use, copy, modify, and distribute this software
 * for any purpose and without fee is hereby granted. The author
 * disclaims all warranties with regard to this software.
 *
 * Latest version: http://www.cl.cam.ac.uk/~mgk25/ucs/wcwidth.c
 */
/*
 * Respnosible party: Jeremy Nelson at EPIC Software Labs (2014-01-30).
 * Any changes I made are donated to the public domain.
 */

#include "irc.h"
#include "ircaux.h"
#include "output.h"

struct interval {
  int first;
  int last;
};

/* auxiliary function for binary search in interval table */
static int bisearch (int ucs, const struct interval *table, int max) {
  int min = 0;
  int mid;

  if (ucs < table[0].first || ucs > table[max].last)
    return 0;
  while (max >= min) {
    mid = (min + max) / 2;
    if (ucs > table[mid].last)
      min = mid + 1;
    else if (ucs < table[mid].first)
      max = mid - 1;
    else
      return 1;
  }

  return 0;
}


/* The following two functions define the column width of an ISO 10646
 * character as follows:
 *
 *    - The null character (U+0000) has a column width of 0.
 *
 *    - Other C0/C1 control characters and DEL will lead to a return
 *      value of -1.
 *
 *    - Non-spacing and enclosing combining characters (general
 *      category code Mn or Me in the Unicode database) have a
 *      column width of 0.
 *
 *    - SOFT HYPHEN (U+00AD) has a column width of 1.
 *
 *    - Other format characters (general category code Cf in the Unicode
 *      database) and ZERO WIDTH SPACE (U+200B) have a column width of 0.
 *
 *    - Hangul Jamo medial vowels and final consonants (U+1160-U+11FF)
 *      have a column width of 0.
 *
 *    - Spacing characters in the East Asian Wide (W) or East Asian
 *      Full-width (F) category as defined in Unicode Technical
 *      Report #11 have a column width of 2.
 *
 *    - All remaining characters (including all printable
 *      ISO 8859-1 and WGL4 characters, Unicode control characters,
 *      etc.) have a column width of 1.
 *
 * This implementation assumes that (int) characters are encoded
 * in ISO 10646.
 */
int	codepoint_numcolumns (int ucs)
{
	int	retval;

  /* sorted list of non-overlapping intervals of non-spacing characters */
  /* generated by "uniset +cat=Me +cat=Mn +cat=Cf -00AD +1160-11FF +200B c" */
  static const struct interval combining[] = {
    { 0x0300, 0x036F }, { 0x0483, 0x0486 }, { 0x0488, 0x0489 },
    { 0x0591, 0x05BD }, { 0x05BF, 0x05BF }, { 0x05C1, 0x05C2 },
    { 0x05C4, 0x05C5 }, { 0x05C7, 0x05C7 }, { 0x0600, 0x0603 },
    { 0x0610, 0x0615 }, { 0x064B, 0x065E }, { 0x0670, 0x0670 },
    { 0x06D6, 0x06E4 }, { 0x06E7, 0x06E8 }, { 0x06EA, 0x06ED },
    { 0x070F, 0x070F }, { 0x0711, 0x0711 }, { 0x0730, 0x074A },
    { 0x07A6, 0x07B0 }, { 0x07EB, 0x07F3 }, { 0x0901, 0x0902 },
    { 0x093C, 0x093C }, { 0x0941, 0x0948 }, { 0x094D, 0x094D },
    { 0x0951, 0x0954 }, { 0x0962, 0x0963 }, { 0x0981, 0x0981 },
    { 0x09BC, 0x09BC }, { 0x09C1, 0x09C4 }, { 0x09CD, 0x09CD },
    { 0x09E2, 0x09E3 }, { 0x0A01, 0x0A02 }, { 0x0A3C, 0x0A3C },
    { 0x0A41, 0x0A42 }, { 0x0A47, 0x0A48 }, { 0x0A4B, 0x0A4D },
    { 0x0A70, 0x0A71 }, { 0x0A81, 0x0A82 }, { 0x0ABC, 0x0ABC },
    { 0x0AC1, 0x0AC5 }, { 0x0AC7, 0x0AC8 }, { 0x0ACD, 0x0ACD },
    { 0x0AE2, 0x0AE3 }, { 0x0B01, 0x0B01 }, { 0x0B3C, 0x0B3C },
    { 0x0B3F, 0x0B3F }, { 0x0B41, 0x0B43 }, { 0x0B4D, 0x0B4D },
    { 0x0B56, 0x0B56 }, { 0x0B82, 0x0B82 }, { 0x0BC0, 0x0BC0 },
    { 0x0BCD, 0x0BCD }, { 0x0C3E, 0x0C40 }, { 0x0C46, 0x0C48 },
    { 0x0C4A, 0x0C4D }, { 0x0C55, 0x0C56 }, { 0x0CBC, 0x0CBC },
    { 0x0CBF, 0x0CBF }, { 0x0CC6, 0x0CC6 }, { 0x0CCC, 0x0CCD },
    { 0x0CE2, 0x0CE3 }, { 0x0D41, 0x0D43 }, { 0x0D4D, 0x0D4D },
    { 0x0DCA, 0x0DCA }, { 0x0DD2, 0x0DD4 }, { 0x0DD6, 0x0DD6 },
    { 0x0E31, 0x0E31 }, { 0x0E34, 0x0E3A }, { 0x0E47, 0x0E4E },
    { 0x0EB1, 0x0EB1 }, { 0x0EB4, 0x0EB9 }, { 0x0EBB, 0x0EBC },
    { 0x0EC8, 0x0ECD }, { 0x0F18, 0x0F19 }, { 0x0F35, 0x0F35 },
    { 0x0F37, 0x0F37 }, { 0x0F39, 0x0F39 }, { 0x0F71, 0x0F7E },
    { 0x0F80, 0x0F84 }, { 0x0F86, 0x0F87 }, { 0x0F90, 0x0F97 },
    { 0x0F99, 0x0FBC }, { 0x0FC6, 0x0FC6 }, { 0x102D, 0x1030 },
    { 0x1032, 0x1032 }, { 0x1036, 0x1037 }, { 0x1039, 0x1039 },
    { 0x1058, 0x1059 }, { 0x1160, 0x11FF }, { 0x135F, 0x135F },
    { 0x1712, 0x1714 }, { 0x1732, 0x1734 }, { 0x1752, 0x1753 },
    { 0x1772, 0x1773 }, { 0x17B4, 0x17B5 }, { 0x17B7, 0x17BD },
    { 0x17C6, 0x17C6 }, { 0x17C9, 0x17D3 }, { 0x17DD, 0x17DD },
    { 0x180B, 0x180D }, { 0x18A9, 0x18A9 }, { 0x1920, 0x1922 },
    { 0x1927, 0x1928 }, { 0x1932, 0x1932 }, { 0x1939, 0x193B },
    { 0x1A17, 0x1A18 }, { 0x1B00, 0x1B03 }, { 0x1B34, 0x1B34 },
    { 0x1B36, 0x1B3A }, { 0x1B3C, 0x1B3C }, { 0x1B42, 0x1B42 },
    { 0x1B6B, 0x1B73 }, { 0x1DC0, 0x1DCA }, { 0x1DFE, 0x1DFF },
    { 0x200B, 0x200F }, { 0x202A, 0x202E }, { 0x2060, 0x2063 },
    { 0x206A, 0x206F }, { 0x20D0, 0x20EF }, { 0x302A, 0x302F },
    { 0x3099, 0x309A }, { 0xA806, 0xA806 }, { 0xA80B, 0xA80B },
    { 0xA825, 0xA826 }, { 0xFB1E, 0xFB1E }, { 0xFE00, 0xFE0F },
    { 0xFE20, 0xFE23 }, { 0xFEFF, 0xFEFF }, { 0xFFF9, 0xFFFB },
    { 0x10A01, 0x10A03 }, { 0x10A05, 0x10A06 }, { 0x10A0C, 0x10A0F },
    { 0x10A38, 0x10A3A }, { 0x10A3F, 0x10A3F }, { 0x1D167, 0x1D169 },
    { 0x1D173, 0x1D182 }, { 0x1D185, 0x1D18B }, { 0x1D1AA, 0x1D1AD },
    { 0x1D242, 0x1D244 }, { 0xE0001, 0xE0001 }, { 0xE0020, 0xE007F },
    { 0xE0100, 0xE01EF }
  };

  if (ucs == 0)
    return 0;

  /* test for C0 control chars including C1 8 bit control chars */
  if (ucs < 32 || (ucs >= 0x7f && ucs < 0xa0))
    return -1;

  /* binary search in table of non-spacing characters */
  if (bisearch(ucs, combining,
	       sizeof(combining) / sizeof(struct interval) - 1))
    return 0;

  /* if we arrive here, ucs is not a combining or C0/C1 control character */

  retval = 1 + 
    (ucs >= 0x1100 &&
     (ucs <= 0x115f ||                    /* Hangul Jamo init. consonants */
      ucs == 0x2329 || ucs == 0x232a ||
      (ucs >= 0x2e80 && ucs <= 0xa4cf &&
       ucs != 0x303f) ||                  /* CJK ... Yi */
      (ucs >= 0xac00 && ucs <= 0xd7a3) || /* Hangul Syllables */
      (ucs >= 0xf900 && ucs <= 0xfaff) || /* CJK Compatibility Ideographs */
      (ucs >= 0xfe10 && ucs <= 0xfe19) || /* Vertical forms */
      (ucs >= 0xfe30 && ucs <= 0xfe6f) || /* CJK Compatibility Forms */
      (ucs >= 0xff00 && ucs <= 0xff60) || /* Fullwidth Forms */
      (ucs >= 0xffe0 && ucs <= 0xffe6) ||
      (ucs >= 0x1f300 && ucs <= 0x1f5ff) ||	/* Emojis, unicode 6 */
      (ucs >= 0x20000 && ucs <= 0x2fffd) ||
      (ucs >= 0x30000 && ucs <= 0x3fffd)));

  return retval;
}

/* *** ADDED STUFF - NOT IN ORIGINAL *** */

int     next_code_point (const unsigned char **i, int resync)
{
        int     offset;
        unsigned char    a, b, c, d;
        const unsigned char *str;
        int     result;

	if (!i || !*i)
		return 0;	/* What is this? */

    /* Keep skipping bytes until we find one that works */
    for (; (**i); (*i)++)
    {
        str = *i;
        a = b = c = d = 0;
	result = -1;

	/* Forcibly refuse to walk past the nul */
	if (!str[0])
		return 0;

        if (str[0])
        {
                a = str[0];
                if (str[1])
                {
                        b = str[1];
                        if (str[2])
                        {
                                c = str[2];
                                if (str[3])
                                        d = str[3];
                        }
                }
        }

        if ((a & 0x80) == 0x00)
        {
                result = a;
                (*i)++;
        }

        /* The 2 high bits are set only?  -- 2 bytes */
        if ((a & 0xE0) == 0xC0)
        {
                if ((b & 0xC0) == 0x80)
                {
                        result = ((a & 0x1F) << 6) + (b & 0x3f);
                        (*i) += 2;;
                }
        }

        /* The 3 high bits are set only?  -- 3 bytes */
        else if ((a & 0xF0) == 0xE0)
        {
                if ((b & 0xC0) == 0x80)
		{
                  if ((c & 0xC0) == 0x80)
                  {
                    result = ((a & 0x0F) << 12) + 
				((b & 0x3f) << 6) + 
				(c & 0x3f);
                    (*i) += 3;
                  }
		}
        }

        /* The 4 high bits are set only?  -- 4 bytes*/
        else if ((a & 0xF8) == 0xF0)
        {
                if ((b & 0xC0) == 0x80)
		{
                  if ((c & 0xC0) == 0x80)
		  {
                    if ((d & 0xC0) == 0x80)
                    {
                      result = ((a & 0x07) << 18) + 
				((b & 0x3f) << 12) + 
				((c & 0x3f) << 6) + 
				(d & 0x3F);
                      (*i) += 4;
                    }
		  }
		}
        }

	/* If result is -1, something is wrong */
	if (result == -1)
	{
		if (resync)
			continue;
	}

        return result;

    }

    /* If we hit the end of the string, return nul */
    return 0;
}

/*
 * partial_code_point -- Tell me why 'i' is not a valid utf8 sequence
 *
 * Arguments:
 *	i	- A pointer to a string rejected by next_code_point()
 *
 * Return value:
 *	1	- The string 'i' points at a utf8 sequence that appears
 *		  to be valid, but truncated.
 *	0	- I don't see anything wrong with 'i'
 *	-1	- 'i' does not point at a valid utf8 sequence at all.
 */
int     partial_code_point (const unsigned char *i)
{
        int     offset;
        unsigned char    a, b, c, d;
        const unsigned char *str;
        int     result = -1;

        str = i;
        a = b = c = d = 0;

        if (str[0])
        {
                a = str[0];
                if (str[1])
                {
                        b = str[1];
                        if (str[2])
                        {
                                c = str[2];
                                if (str[3])
                                        d = str[3];
                        }
                }
        }

	/* A 7 bit char is not a partial incomplete sequence */
        if ((a & 0x80) == 0x00)
		return 0;

        /* The 2 high bits are set only?  -- 2 bytes */
        if ((a & 0xE0) == 0xC0)
        {
		/* if b is a nul, then it is truncated */
		if (b == 0)
			return 1;

		/* If it's valid, ok. */
                else if ((b & 0xC0) == 0x80)
			return 0;

		/* This is just garbage */
		else 
			return -1;
        }

        /* The 3 high bits are set only?  -- 3 bytes */
        else if ((a & 0xF0) == 0xE0)
        {
		/* If b is a null, it is truncated */
		if (b == 0)
			return 1;

		/* Otherwise, if 'b' is a valid next char... */
		else if ((b & 0xC0) == 0x80)
		{
			/* If c is a null, it is truncated */
			if (c == 0)
				return 1;

			/* Or, if c is a valid final char... */
			else if ((c & 0xC0) == 0x80)
				return 0;

			/* Otherwise, c is just garbage */
			else
				return -1;
		}

		/* Otherwise, 'b' is just garbage */
		else
			return -1;
        }

        /* The 4 high bits are set only?  -- 4 bytes*/
        else if ((a & 0xF8) == 0xF0)
        {
		if (b == 0)
			return 1;

		/* Otherwise, if 'b' is a valid next char... */
		else if ((b & 0xC0) == 0x80)
		{
			/* If c is a null, it is truncated */
			if (c == 0)
				return 1;

			/* Or, if c is a valid next char... */
			else if ((c & 0xC0) == 0x80)
			{
				if (d == 0)
					return 1;
				else if ((d & 0xC0) == 0x80)
					return 0;
				else
					return -1;
			}

			/* Otherwise, c is just garbage */
			else
				return -1;
		}

		else
			return -1;
        }

	return -1;
}


int	grab_codepoint (const unsigned char *x)
{
	const unsigned char *str = x;
	return next_code_point(&str, 1);
}

/*
 * quick_display_column_count - How many columns would 'str' take up?
 *
 * Arguments:
 *	str	- A UTF-8 string
 *
 * Return Value:
 * 	The number of columns 'str' would take up.
 *
 * IMPORTANT NOTE!
 *	This function does NOT properly handle attribute markers that
 *	take following characters (^C, ^X).  Whereas it properly ignores
 *	things like ^V, ^B, ^C02 would result in "2" rather than "0".
 *
 *	The correct way to get column counts is found in 
 *	ircaux.c:fix_string_width(), which involves using 
 *	new_normalize_string() and output_with_count().
 */
/* XXX DO NOT USE THIS FUNCTION IF 'str' MIGHT CONTAIN HIGHLIGHT CHARS! XXX */
int	quick_display_column_count (const unsigned char *str)
{
	const unsigned char *s;
	int	code_point;
	int	length = 0;
	int	x;

	s = str;
	while ((code_point = next_code_point(&s, 1)))
	{
		if ((x = codepoint_numcolumns(code_point)) == -1)
			x = 0;
		length += x;
	}

	return length;
}

/*
 * count_initial_codepoints - How many codepoints in 'str' before 'p'?
 *
 * Arguments:
 *	str	- A UTF-8 string
 *      p       - A character pointer somewhere inside 'str'
 *
 * Return Value:
 * 	The number of codepoints in 'str' before 'p'
 *      ie,  $mid(X 999 $str) == $p
 *
 * IMPORTANT NOTE!
 *      This is used by $regmatches() to convert a pointer to something
 *      that you can pass to $mid().
 */
int	count_initial_codepoints (const unsigned char *str, const unsigned char *p)
{
	const unsigned char *s;
	int	code_point;
	int	length = 0;
	int	x;

	s = str;
	while ((code_point = next_code_point(&s, 1)))
	{
		length++;
		if (s >= p)
			return length;
	}

	/* 
	 * This is only reached if 'p' is not in 'str'.
	 * In this case, I decided it's better to point at
	 * the end ofo the string, which yields a zero-length
	 * string.  I'm not positive this is the right call
	 */
	return length;
}


int	input_column_count (const unsigned char *str)
{
	const unsigned char *s;
	int	code_point;
	int	length = 0;
	int	x;

	s = str;
	while ((code_point = next_code_point(&s, 1)))
	{
		if ((x = codepoint_numcolumns(code_point)) == -1)
			x = 1;
		length += x;
	}

	return length;
}

/*
 * This does a QUICK code point count.
 * Every code point contains one (and only one) byte in the range:
 *	0x00-0x7F
 *	0xC0-0xFF
 * This function doesn't attempt to validate broken utf8.
 */
int	quick_code_point_count (const unsigned char *str)
{
	const unsigned char *s;
	int	count;

	for (count = 0, s = str; *s; s++)
	{
		if (*s < 0x80 || *s >= 0xC0)
			count++;
	}
	return count;
}

/*
 * previous_code_point	- Move *i back one code point.
 *			 *** IMPORTANT ***
 *			 This is technically a "quick" function since it
 *			 does not validate the string is well formed utf8.
 *
 * Arguments:
 *	st	The first byte of whatever string 'i' is pointing to.
 *	i	A pointer to the start of a CP.
 *
 * Return Value:
 *	- If *i points at the first byte of a code point, then 
 *	  the code point that ends at the byte *i - 1.
 *	- If *i does not point at the first byte of a code point,
 *	  then the code that that contains *i.
 *	- If *i points at the start of string (st), returns 0 so you can stop.
 *	In both cases, *i is moved to the first byte of the code
 *	point whose value is returned.
 */
int     previous_code_point (const unsigned char *st, const unsigned char **i)
{
	const unsigned char *	c;

	c = *i;
	if (c == st)
		return 0;		/* Time to stop */

	if (c > st && (*c < 0x80 || *c >= 0xC0))
		c--;

	while (c > st && (*c >= 0x80 && *c < 0xC0))
		c--;

	*i = c;
	return next_code_point(&c, 1);
}


/*
 * This does a QUICK count of the CP "index" of 'loc' in 'str'.
 */
int	quick_code_point_index (const unsigned char *str, const unsigned char *loc)
{
	const unsigned char *s;
	int	count;

	for (count = 0, s = str; *s && s < loc; s++)
	{
		if (*s < 0x80 || *s >= 0xC0)
			count++;
	}
	return count;
}

