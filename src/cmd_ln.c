/*
 * cmd_ln.c - ln 명령                                   [담당: 김소욱]
 *   ln [-s] [-f] [-n] [-v] TARGET [LINK_NAME]
 *   ln [-s] [-f] [-n] [-v] TARGET... DIRECTORY
 *   -s 심볼릭 링크  -f 기존 대상 삭제 후 생성  -n 이미 있으면 건너뜀  -v 상세 출력
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>
#include <sys/stat.h>
#include "builtin.h"
#include "util.h"

static int make_link(const char *target, const char *linkname, int sym, int force,
                     int noclobber, int verbose)
{
    struct stat st;
    if (lstat(linkname, &st) == 0) {
        if (noclobber) return 0;
        if (!force) {
            fprintf(stderr, "ln: failed to create %s link '%s': File exists\n",
                    sym ? "symbolic" : "hard", linkname);
            return 1;
        }
        if (S_ISDIR(st.st_mode)) {
            fprintf(stderr, "ln: cannot overwrite directory '%s'\n", linkname);
            return 1;
        }
        unlink(linkname);
    }

    int rc = sym ? symlink(target, linkname) : link(target, linkname);
    if (rc < 0) {
        int e = errno;
        if (!sym && e == EPERM && is_dir(target))
            fprintf(stderr, "ln: '%s': hard link not allowed for directory\n", target);
        else
            fprintf(stderr, "ln: failed to create %s link '%s'%s'%s': %s\n",
                    sym ? "symbolic" : "hard", linkname, sym ? " -> " : " => ", target, strerror(e));
        return 1;
    }
    if (verbose) printf("'%s' %s '%s'\n", linkname, sym ? "->" : "=>", target);
    return 0;
}

int cmd_ln(int argc, char **argv)
{
    int sym = 0, force = 0, noclobber = 0, verbose = 0, c;
    optind = 0;
    while ((c = getopt(argc, argv, "sfnv")) != -1) {
        switch (c) {
        case 's': sym = 1; break;
        case 'f': force = 1; break;
        case 'n': noclobber = 1; break;
        case 'v': verbose = 1; break;
        default:
            fprintf(stderr, "Usage: ln [-sfnv] TARGET [LINK_NAME]\n       ln [-sfnv] TARGET... DIRECTORY\n");
            return 2;
        }
    }
    int n = argc - optind;
    char **args = argv + optind;
    if (n < 1) {
        fprintf(stderr, "ln: missing file operand\n");
        return 1;
    }

    int rc = 0;
    if (n == 1) {                       /* ln target -> 현재 디렉터리에 같은 이름 */
        char *base = path_basename(args[0]);
        rc = make_link(args[0], base, sym, force, noclobber, verbose);
        free(base);
        return rc;
    }

    const char *dest = args[n - 1];
    if (is_dir(dest)) {                 /* 마지막 인자가 디렉터리 -> 그 안에 생성 */
        for (int i = 0; i < n - 1; i++) {
            char *base = path_basename(args[i]);
            char *name = path_join(dest, base);
            if (make_link(args[i], name, sym, force, noclobber, verbose)) rc = 1;
            free(base); free(name);
        }
        return rc;
    }
    if (n > 2) {
        fprintf(stderr, "ln: target '%s' is not a directory\n", dest);
        return 1;
    }
    return make_link(args[0], dest, sym, force, noclobber, verbose);
}
