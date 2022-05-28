/*
 * Here's the plan
 *    ... stuff ... | stringify 
 */
#include <stdio.h>

int main (int argc, char **argv)
{
	int	count = 0;
	int	byte;

	printf("const char %s[] = {", argv[1]);
	while ((byte = getchar()) != EOF)
	{
		if (byte == '\n')
			byte = '\t';
		if (count > 0)
			printf(",");
		printf("%d", byte & 0xFF);
		if (count > 0 && count % 8 == 0)
			printf("\n");
		count++;
	}
	if (count > 0)
		printf(",");
	printf("0};\n");
	return 0;
}

