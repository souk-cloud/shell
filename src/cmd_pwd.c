/*
 * cmd_pwd.c - pwd 명령                                 [담당: 김소욱]
 *   pwd [-L|-P]   -P(기본): getcwd() 의 실제 경로,  -L: $PWD 가 유효하면 그것을 출력
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

int cmd_pwd(int argc, char **argv)
{
    int logical = 0, c;
    optind = 0;
    while ((c = getopt(argc, argv, "LP")) != -1) {
        if (c == 'L') logical = 1;
        else if (c == 'P') logical = 0;
        else { fprintf(stderr, "Usage: pwd [-L|-P]\n"); return 2; }
    }
    if (optind < argc) { fprintf(stderr, "pwd: too many arguments\n"); return 1; }

    if (logical) {
        const char *env = getenv("PWD");
        struct stat a, b;
        if (env && env[0] == '/' && stat(env, &a) == 0 && stat(".", &b) == 0 &&
            a.st_dev == b.st_dev && a.st_ino == b.st_ino) {
            puts(env);
            return 0;
        }
    }
    char cwd[PATH_MAX];
    if (!getcwd(cwd, sizeof(cwd))) {
        fprintf(stderr, "pwd: %s\n", strerror(errno));
        return 1;
    }
    puts(cwd);
    return 0;
}
