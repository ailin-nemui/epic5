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
		if (count == 0)
			printf("%d", byte & 0xFF);
		else
			printf(",%d", byte & 0xFF);
		if (count > 0 && count % 8 == 0)
			printf("\n");
		count++;
	}
	printf("};\n");
}

