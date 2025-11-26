#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include "parser.h"
#include "term_colors.h"

#define PROMPT TERM_BRIGHT_GREEN"skeme> "TERM_RESET

SKState sk;

// globals
int g_bDebug = FALSE;
const char * g_szOutputFilename = "a.out";
int yydebug = FALSE;
int g_bSymbols = FALSE;
int g_bDone = FALSE;
extern int g_bParseLog;

CodeChunk repl_chunk;

//--------------------------------
// print usage banner
//--------------------------------
void usage()
{
    printf("\nUsage: %s [-d] [-i] [-o <outputfile>] [-t] <inputfile>\n", APP_NAME);
    printf("  -d : enable parser debug output\n");
    printf("  -i : take input from stdin\n");
    printf("  -p : parser logging\n");
    printf("  -t : generate symbol table\n\n");
    printf("  -v : enable verbose output\n");
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

        // flag for enabling parser logging
		if (args[i][1] == 'p')
			g_bParseLog = TRUE;

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
  //  if (argc == 1)
		//usage();

	// default input is stdin
    yyin = stdin;

    printf("\n%s v%s - a Scheme-like little language by Mark Seminatore (c) 2025\n\n", APP_NAME, APP_VER);
    sk_init(&sk);

	int iFirstArg = getopt(argc, argv);

    // fout = fopen(g_szOutputFilename, "wb");
    LOG("Output file: '%s'\n", g_szOutputFilename);

    // initialize repl code chunk
    vm_init_chunk(&repl_chunk);
    vm_set_current_chunk(&repl_chunk);

    if (argc != iFirstArg /* yyin != stdin */)
    {
        push_file_stack(argv[iFirstArg]);
        strcpy(infile, argv[iFirstArg]);

        // parse the input file
        sk_parse(&sk);
        vm_exec(&vm, repl_chunk.code);

        LOG("Parsed %d lines in '%s' with %d errors and %d warnings\n", lineno, infile, err_count, warn_count);

        if (vm.sp)
        {
            sk_display(&sk);
            puts("");
            LOG("%d items left on stack.\n", vm.sp);
        }
    }
    else
    {
        // REPL loop
        while (!g_bDone)
        {
            vm_reset(&vm);  // TODO - maybe we don't want to throw away everything each time?

            printf(PROMPT);

            sk_parse(&sk);
            vm_exec(&vm, repl_chunk.code);
            if (vm.sp)
            {
                sk_display(&sk);
                puts("");
                assert(0 == vm.sp);
                LOG("%d items left on stack.\n", vm.sp);
            }
        }
    }

    vm_free_chunk(&vm, &repl_chunk);

    if (g_bSymbols)
        dump_symbols();

    LOG("Shutting down...");
    //sk_shutdown(&sk);
    LOG("Done.\n");
	return 0;
}