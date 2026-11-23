/*
 * cmd_rm.c - rm 명령                                   [담당: 전승준]
 *   rm [-rRfiv] FILE...
 *   -r/-R 디렉터리 재귀 삭제  -f 없는 파일 무시/확인 안 함  -i 하나씩 확인  -v 상세
 * 안전장치: "." ".." "/" 는 삭제를 거부한다.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include "builtin.h"
#include "util.h"

static int is_dangerous(const char *path)
{
    char *base = path_basename(path);
    int bad = !strcmp(base, ".") || !strcmp(base, "..");
    free(base);
    if (bad) {
        fprintf(stderr, "rm: refusing to remove '.' or '..' directory: skipping '%s'\n", path);
        return 1;
    }
    char real[PATH_MAX];
    if (realpath(path, real) && strcmp(real, "/") == 0) {
        fprintf(stderr, "rm: it is dangerous to operate recursively on '/'\n");
        return 1;
    }
    return 0;
}

int cmd_rm(int argc, char **argv)
{
    FileOpts o = { .prog = "rm" };
    int c;
    optind = 0;
    while ((c = getopt(argc, argv, "rRfiv")) != -1) {
        switch (c) {
        case 'r': case 'R': o.recursive = 1; break;
        case 'f': o.force = 1; o.interactive = 0; break;
        case 'i': o.interactive = 1; o.force = 0; break;
        case 'v': o.verbose = 1; break;
        default:
            fprintf(stderr, "Usage: rm [-rRfiv] FILE...\n");
            return 2;
        }
    }
    if (optind >= argc) {
        if (o.force) return 0;
        fprintf(stderr, "rm: missing operand\n");
        return 1;
    }

    int rc = 0;
    for (int i = optind; i < argc; i++) {
        if (is_dangerous(argv[i])) { rc = 1; continue; }
        if (remove_tree(argv[i], &o)) rc = 1;
    }
    return rc;
}
