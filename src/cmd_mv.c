/*
 * cmd_mv.c - mv 명령                                   [담당: 전승준]
 *   mv [-ifnv] SOURCE DEST
 *   mv [-ifnv] SOURCE... DIRECTORY
 *   같은 파일시스템이면 rename() 한 번으로 끝나고,
 *   다른 파일시스템(EXDEV)이면 복사 후 원본 삭제로 대체한다.
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

static int move_one(const char *src, const char *dst, const FileOpts *o)
{
    struct stat st;
    if (lstat(src, &st) < 0) {
        fprintf(stderr, "mv: cannot stat '%s': %s\n", src, strerror(errno));
        return 1;
    }

    struct stat ex;
    if (lstat(dst, &ex) == 0) {
        if (same_file(src, dst)) {
            fprintf(stderr, "mv: '%s' and '%s' are the same file\n", src, dst);
            return 1;
        }
        if (o->no_clobber) return 0;
        if (o->interactive) {
            char q[PATH_MAX + 64];
            snprintf(q, sizeof(q), "mv: overwrite '%s'? ", dst);
            if (!confirm(q)) return 0;
        }
        if (S_ISDIR(ex.st_mode) && !S_ISDIR(st.st_mode)) {
            fprintf(stderr, "mv: cannot overwrite directory '%s' with non-directory\n", dst);
            return 1;
        }
    }

    if (S_ISDIR(st.st_mode) && is_subpath(src, dst)) {
        fprintf(stderr, "mv: cannot move '%s' to a subdirectory of itself, '%s'\n", src, dst);
        return 1;
    }

    if (rename(src, dst) == 0) {
        if (o->verbose) printf("renamed '%s' -> '%s'\n", src, dst);
        return 0;
    }
    if (errno != EXDEV) {
        fprintf(stderr, "mv: cannot move '%s' to '%s': %s\n", src, dst, strerror(errno));
        return 1;
    }

    /* 파일시스템이 다름: 복사 + 삭제 */
    FileOpts cp = { .prog = "mv", .recursive = 1, .preserve = 1, .force = 1 };
    if (copy_tree(src, dst, &cp)) return 1;
    FileOpts rm = { .prog = "mv", .recursive = 1, .force = 1 };
    if (remove_tree(src, &rm)) return 1;
    if (o->verbose) printf("renamed '%s' -> '%s'\n", src, dst);
    return 0;
}

int cmd_mv(int argc, char **argv)
{
    FileOpts o = { .prog = "mv" };
    int c;
    optind = 0;
    while ((c = getopt(argc, argv, "ifnv")) != -1) {
        switch (c) {
        case 'i': o.interactive = 1; break;
        case 'f': o.force = 1; o.interactive = 0; break;
        case 'n': o.no_clobber = 1; break;
        case 'v': o.verbose = 1; break;
        default:
            fprintf(stderr, "Usage: mv [-ifnv] SOURCE DEST\n       mv [-ifnv] SOURCE... DIRECTORY\n");
            return 2;
        }
    }
    int n = argc - optind;
    char **args = argv + optind;
    if (n < 1) { fprintf(stderr, "mv: missing file operand\n"); return 1; }
    if (n < 2) { fprintf(stderr, "mv: missing destination file operand after '%s'\n", args[0]); return 1; }

    const char *dest = args[n - 1];
    int dest_is_dir = is_dir(dest);
    if (n > 2 && !dest_is_dir) {
        fprintf(stderr, "mv: target '%s' is not a directory\n", dest);
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
        if (move_one(args[i], target, &o)) rc = 1;
        free(target);
    }
    return rc;
}
