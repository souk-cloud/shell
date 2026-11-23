/*
 * cmd_rmdir.c - rmdir 명령                             [담당: 김소욱]
 *   rmdir [-p] [-v] DIR...
 *   -p : "a/b/c" 를 주면 c, b, a 순서로 비어 있는 동안 모두 삭제
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include "builtin.h"
#include "util.h"

int cmd_rmdir(int argc, char **argv)
{
    int parents = 0, verbose = 0, c;
    optind = 0;
    while ((c = getopt(argc, argv, "pv")) != -1) {
        if (c == 'p') parents = 1;
        else if (c == 'v') verbose = 1;
        else { fprintf(stderr, "Usage: rmdir [-pv] DIRECTORY...\n"); return 2; }
    }
    if (optind >= argc) { fprintf(stderr, "rmdir: missing operand\n"); return 1; }

    int rc = 0;
    for (int i = optind; i < argc; i++) {
        char *path = strdup(argv[i]);
        size_t len = strlen(path);
        while (len > 1 && path[len - 1] == '/') path[--len] = '\0';

        for (;;) {
            if (rmdir(path) < 0) {
                fprintf(stderr, "rmdir: failed to remove '%s': %s\n", path, strerror(errno));
                rc = 1;
                break;
            }
            if (verbose) printf("rmdir: removing directory, '%s'\n", path);
            if (!parents) break;

            char *up = path_dirname(path);
            int stop = (strcmp(up, ".") == 0 || strcmp(up, "/") == 0 || strcmp(up, path) == 0);
            free(path);
            path = up;
            if (stop) break;
        }
        free(path);
    }
    return rc;
}
