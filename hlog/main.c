#include <err.h>
#include <stdio.h>  /* fprintf */
#include <stdlib.h> /* EXIT_SUCCESS */
#include <unistd.h> /* getopt */

#include "hlog.h"

HLOG_OUTLET_DECL(even);
HLOG_OUTLET_DECL(odd);

HLOG_OUTLET_SHORT_DEFN(even, all);
HLOG_OUTLET_SHORT_DEFN(odd, all);

static void
usage(const char *progname)
{
    fprintf(stderr, "usage: %s [-l outlet] [-L outlet]\n", progname);
    exit(EXIT_FAILURE);
}

int
main(int argc, char **argv)
{
    int ch, i;

    while ((ch = getopt(argc, argv, "l:L:")) != -1) {
        switch (ch) {
        case 'l':
            if (hlog_set_state(optarg, HLOG_OUTLET_S_ON, false) == -1)
                err(EXIT_FAILURE, "could not enable outlet '%s'", optarg);
            break;
        case 'L':
            if (hlog_set_state(optarg, HLOG_OUTLET_S_OFF, false) == -1)
                err(EXIT_FAILURE, "could not disable outlet '%s'", optarg);
            break;
        default:
            usage(argv[0]);
            break;
        }
    }

    argc -= optind;
    argv += optind;

    if (argc != 0)
        errx(EXIT_FAILURE, "unexpected command-line arguments");

    for (i = 0; i < 10; i++) {
        if (i % 2 == 0)
            hlog_fast(even, "%d is even", i);
        else
            hlog_fast(odd, "%d is odd", i);
    }
    return EXIT_SUCCESS;
}
