#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include "sk.h"
#include "term_colors.h"

#define PROMPT TERM_BRIGHT_GREEN"skeme> "TERM_RESET

SKState sk;

// globals
int g_bDebug = FALSE;
const char * g_szOutputFilename = "a.out";
int yydebug = FALSE;
int g_bInteractive = FALSE;
extern int g_bProfile;

CodeChunk repl_chunk;

//--------------------------------
// print usage banner
//--------------------------------
void usage()
{
    printf("\nUsage: %s [-d] [-i] [-t] [-profile] <inputfile>\n", APP_NAME);
    printf("  -d       : enable parser debug output\n");
    printf("  -h       : display usage\n");
    printf("  -i       : take input from stdin\n");
    printf("  -p       : parser logging\n");
    printf("  -profile : enable VM profiling\n");
    printf("  -t       : generate symbol table\n");
    printf("  -v       : enable verbose output\n");
    exit(0);
}

//-----------------------------------
// get options from the command line
//-----------------------------------
int getopt(SKState* pSK, int n, char *args[])
{
	int i;
    
	for (i = 1; args[i] && args[i][0] == '-'; i++)
	{
        // multi-char flags
        if (strcmp(args[i], "-profile") == 0) {
            g_bProfile = TRUE;
            continue;
        }

        // flag for enabling verbose logging
		if (args[i][1] == 'v')
			g_bDebug = TRUE;

        // flag for enabling parser logging
		if (args[i][1] == 'p')
			pSK->parse_log_enabled = TRUE;

        // flag for parser debugging
        if (args[i][1] == 'd')
            yydebug = 1;

        // flag for usage
        if (args[i][1] == 'h')
            usage();

        // flag for setting output file name
		if (args[i][1] == 'o')
		{
			g_szOutputFilename = args[i + 1];
			i++;
		}

        // flag for taking input from stdio
        if (args[i][1] == 'i')
            g_bInteractive = TRUE;

        // flag for symbols (removed: symbol table no longer exists)
	}

	return i;
}

//-----------------------------------
//
//-----------------------------------
int main(int argc, char *argv[])
{
    char infile[BUF_SIZE] = "stdin";

    printf("\n%s v%s - a Scheme-like little language by Mark Seminatore (c) 2025\n\n", APP_NAME, APP_VER);

    sk_init(&sk, argc, argv);

	int iFirstArg = getopt(&sk, argc, argv);

    // initialize repl code chunk
    vm_init_chunk(&repl_chunk);
    vm_set_current_chunk(&sk.vm, &repl_chunk);

	sk_define(&sk, "__version__", vm_new_string(&sk.vm, APP_VER));
	sk_define(&sk, "__author__", vm_new_string(&sk.vm, "Mark Seminatore"));
	sk_define(&sk, "__app_name__", vm_new_string(&sk.vm, APP_NAME));
	sk_define(&sk, "interactive", vm_newBoolean(&sk.vm, g_bInteractive));

    if (argc != iFirstArg)
    {
        if (!push_filename_stack(&sk, argv[iFirstArg]))
            return -1;

        strcpy(infile, argv[iFirstArg]);

        // parse the input file
        sk_parse(&sk);

        // Clear any compile-time GC roots from the stack before execution
        vm_clear_stack(&sk.vm);

		// execute the parsed code
        vm_exec(&sk.vm, repl_chunk.code);

        LOG("Parsed %d lines in '%s' with %d errors and %d warnings\n", sk.lineno, infile, sk.err_count, sk.warn_count);

        if (sk.vm.sp)
        {
            sk_display(&sk);
            //puts("");
            LOG("%d items left on stack.\n", sk.vm.sp);
        }

        if (!g_bInteractive)
        {
            // clean up and exit
            sk.bDone = TRUE;
		}
    }

    // default input is stdin
	if (!sk.bDone)
    {
        push_fileptr_stack(&sk, stdin, "stdin");
    }

    // REPL loop
    while (!sk.bDone)
    {
        vm_reset(&sk.vm);

        if (sk.yyin == stdin)
            printf(PROMPT);

        sk_parse(&sk);
        
        // Exit if we've reached EOF on stdin
        if (sk.yyin == NULL || (sk.yyin == stdin && feof(sk.yyin)))
        {
            sk.bDone = TRUE;
            break;
        }
        
        vm_exec(&sk.vm, repl_chunk.code);

        if (sk.vm.sp)
        {
            sk_display(&sk);
            if (sk.vm.sp)
            {
                fprintf(stderr, "%d items left on stack.\n", sk.vm.sp);
                vm_print_stack(&sk.vm);
            }
        }
    }

    vm_free_chunk(&sk.vm, &repl_chunk);

    LOG("max stack size: %d, max call depth: %d\n", sk.vm.max_stack_size, sk.vm.max_call_depth);

    vm_print_profile(&sk.vm);

    LOG("Shutting down...");
    sk_shutdown(&sk);
    LOG("Done.\n");
	return 0;
}