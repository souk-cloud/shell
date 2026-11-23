/*
 * cmd_cp.c - cp 명령                                   [담당: 전승준]
 *   cp [-rRfipvn] SOURCE DEST
 *   cp [-rRfipvn] SOURCE... DIRECTORY
 *   -r/-R 디렉터리 재귀 복사  -f 강제  -i 덮어쓰기 확인  -p 권한/시간 보존
 *   -v 상세  -n 덮어쓰지 않음
 * 실제 복사 로직은 util.c 의 copy_tree() 를 mv 와 공유한다.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "builtin.h"
#include "util.h"

int cmd_cp(int argc, char **argv)
{
    FileOpts o = { .prog = "cp" };
    int c;
    optind = 0;
    while ((c = getopt(argc, argv, "rRfipvn")) != -1) {
        switch (c) {
        case 'r': case 'R': o.recursive = 1; break;
        case 'f': o.force = 1; break;
        case 'i': o.interactive = 1; break;
        case 'p': o.preserve = 1; break;
        case 'v': o.verbose = 1; break;
        case 'n': o.no_clobber = 1; break;
        default:
            fprintf(stderr, "Usage: cp [-rRfipvn] SOURCE DEST\n       cp [-rRfipvn] SOURCE... DIRECTORY\n");
            return 2;
        }
    }
    int n = argc - optind;
    char **args = argv + optind;
    if (n < 1) { fprintf(stderr, "cp: missing file operand\n"); return 1; }
    if (n < 2) { fprintf(stderr, "cp: missing destination file operand after '%s'\n", args[0]); return 1; }

    const char *dest = args[n - 1];
    int dest_is_dir = is_dir(dest);
    if (n > 2 && !dest_is_dir) {
        fprintf(stderr, "cp: target '%s' is not a directory\n", dest);
        return 1;
    }

    int rc = 0;
    for (int i = 0; i < n - 1; i++) {
        char *target;
        if (dest_is_dir) {
            char *base = path_basename(args[i]);
            target = path_join(dest, base);
            free(base);
        } else {
            target = strdup(dest);
        }

        if (is_dir(args[i]) && o.recursive && is_subpath(args[i], target)) {
            fprintf(stderr, "cp: cannot copy a directory, '%s', into itself, '%s'\n", args[i], target);
            rc = 1;
        } else if (copy_tree(args[i], target, &o)) {
            rc = 1;
        }
        free(target);
    }
    return rc;
}
