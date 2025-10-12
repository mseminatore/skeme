#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "parser.h"

// globals
int g_bDebug = FALSE;
const char * g_szOutputFilename = "a.out";
int yydebug = FALSE;
int g_bSymbols = FALSE;

//--------------------------------
// print usage banner
//--------------------------------
void usage()
{
    printf("\nUsage: %s [-d] [-i] [-o <outputfile>] [-t] <inputfile>\n", APP_NAME);
    printf("  -d : enable debug output\n");
    printf("  -i : take input from stdin\n");
    printf("  -o : specify output file (default is a.out)\n");
    printf("  -t : generate symbol table\n\n");
    exit(0);
}

//-----------------------------------
// get options from the command line
//-----------------------------------
int getopt(int n, char *args[])
{
	int i;
    
	for (i = 1; args[i] && args[i][0] == '-'; i++)
	{
        // flag for enabling verbose logging
		if (args[i][1] == 'v')
			g_bDebug = TRUE;

        // flag for parser debugging
        if (args[i][1] == 'd')
            yydebug = 1;

        // flag for setting output file name
		if (args[i][1] == 'o')
		{
			g_szOutputFilename = args[i + 1];
			i++;
		}

        // flag for taking input from stdio
        if (args[i][1] == 'i')
            yyin = stdin;

        // flag for symbols
        if (args[i][1] == 't')
            g_bSymbols = TRUE;
	}

	return i;
}

//-----------------------------------
//
//-----------------------------------
int main(int argc, char *argv[])
{
    char infile[BUF_SIZE] = "stdin";

    // show usage if no arguments given
    if (argc == 1)
		usage();

    printf("%s v%s - a Scheme-like little language by Mark Seminatore (c) 2025\n", APP_NAME, APP_VER);

	int iFirstArg = getopt(argc, argv);

    if (yyin != stdin)
    {
        push_file_stack(argv[iFirstArg]);
        strcpy(infile, argv[iFirstArg]);
    }

    // fout = fopen(g_szOutputFilename, "wb");
    sk_init();
    if (g_bDebug)
        printf("Output file: '%s'\n", g_szOutputFilename);
    
    // parse the input file
    sk_parse();

    printf("Parsed '%s' with %d errors and %d warnings\n", infile, err_count, warn_count);

    if (g_bSymbols)
        dump_symbols();
}