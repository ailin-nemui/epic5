/* ircman.c by David N. Welton <davidw@efn.org>  */
/* This is free software under the terms of the GNU GPL  */
#include <stdio.h>
#include <stdlib.h>
          
int main (int argc, char *argv[])
{         
  FILE *fd;
  FILE *pgr;
      
  char *pager;
      
  int ch; 
  int boldflag = 0;   
  int revflag = 0;
  int ulflag = 0;
        
  if (argv[1] != NULL)
    fd = fopen (argv[1], "r");
  else
    {
      fprintf(stderr, "Usage: %s file\n", argv[0]);
      exit (1);
    }
  if (fd == NULL)
    {
      fprintf(stderr, "Could not open %s\n", argv[1]);
      exit (1);
    }            

  if(pager = getenv("PAGER")) {
    pgr = popen(pager, "w");
    if (pgr == NULL)
      {
        fputs("Danger, will robinson\n", stderr);
        exit (1);
      }
  } else {
    pgr = stdout;
  }  
  
  while((ch = fgetc(fd)) != EOF )
    {      
      switch (ch)
        {
        case '^V':
          revflag ^= 1;
          continue;
          break;
        case '^B':
          boldflag ^= 1;
          continue;
          break;
        case '^_':
          ulflag ^= 1;
          continue;
          break;
        }

      if (revflag)
        { putc(ch,pgr); putc(',pgr); putc(ch,pgr); }
      else if (boldflag)
        { putc(ch,pgr); putc(',pgr); putc(ch,pgr); }
      else if (ulflag)
        { putc('_',pgr); putc(',pgr); putc(ch,pgr); }
      else
        putc(ch,pgr);
    }
  close(fd);
  pclose(pgr);
}

