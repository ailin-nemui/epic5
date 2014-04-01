/*
 * Here's the plan
 * You type a UTF8 string, and it gives you all of the Unicode code points
 * (in hex) on output.
 */
#include <stdio.h>

int main (void) 
{
	unsigned char 	string[256];
	unsigned char *  next_point;
	unsigned char *	this_point;
	unsigned long	code_point;
	unsigned int	numbytes;

    for (;;)
    {
	fgets(string, 256, stdin);
	if (feof(stdin)) {
		exit(0);
	}
	next_point = this_point = string;

    while (*this_point != '\n')
    {
	code_point = 0;

	if (((unsigned long)(*this_point) & 0xFE) == 0xFC) {
		numbytes = 6;
		code_point = ((unsigned long)this_point[0] & 0x01) << 30;
		code_point += ((unsigned long)this_point[1] & 0x3F) << 24;
		code_point += ((unsigned long)this_point[2] & 0x3F) << 18;
		code_point += ((unsigned long)this_point[3] & 0x3F) << 12;
		code_point += ((unsigned long)this_point[4] & 0x3F) << 6;
		code_point += ((unsigned long)this_point[5] & 0x3F);
		this_point += 6;
	} else if (((unsigned long)(*this_point) & 0xFC) == 0xF8) {
		numbytes = 5;
		code_point = ((unsigned long)this_point[0] & 0x03) << 24;
		code_point += ((unsigned long)this_point[1] & 0x3F) << 18;
		code_point += ((unsigned long)this_point[2] & 0x3F) << 12;
		code_point += ((unsigned long)this_point[3] & 0x3F) << 6;
		code_point += ((unsigned long)this_point[4] & 0x3F);
		this_point += 5;
	} else if (((unsigned long)(*this_point) & 0xF8) == 0xF0) {
		numbytes = 4;
		code_point = ((unsigned long)this_point[0] & 0x07) << 18;
		code_point += ((unsigned long)this_point[1] & 0x3F) << 12;
		code_point += ((unsigned long)this_point[2] & 0x3F) << 6;
		code_point += ((unsigned long)this_point[3] & 0x3F);
		this_point += 4;
	} else if (((unsigned long)(*this_point) & 0xF0) == 0xE0) {
		numbytes = 3;
		code_point = ((unsigned long)this_point[0] & 0x0F) << 12;
		code_point += ((unsigned long)this_point[1] & 0x3F) << 6;
		code_point += ((unsigned long)this_point[2] & 0x3F);
		this_point += 3;
	} else if (((unsigned long)(*this_point) & 0xE0) == 0xC0) {
		numbytes = 2;
		code_point = (this_point[0] & 0x1F) << 6;
		code_point += (this_point[1] & 0x3F);
		this_point += 2;
	} else if (((unsigned long)(*this_point) & 0x80) == 0x00) {
		numbytes = 1;
		code_point = (this_point[0] & 0x7F);
		this_point++;
	} else {
		printf("Huh? %#x\n", (int)*this_point);
		printf("%d\n", *this_point & 0x80);
		exit(0);
	}

	printf("%#x\n", code_point);
    }
    }
}

