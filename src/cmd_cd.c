/*
 * cmd_cd.c - cd 명령                                   [담당: 김소욱]
 *   cd          : $HOME 으로 이동
 *   cd -        : 이전 디렉터리($OLDPWD)로 이동하고 경로를 출력
 *   cd ~, ~/x   : 홈 디렉터리 확장
 * cd 는 쉘 프로세스 자신의 작업 디렉터리를 바꿔야 하므로 자식이 아닌 부모에서 실행된다.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>
#include "builtin.h"

int cmd_cd(int argc, char **argv)
{
    if (argc > 2) { fprintf(stderr, "cd: too many arguments\n"); return 1; }

    const char *target;
    int print_dir = 0;
    char expanded[PATH_MAX];

    if (argc == 1) {
        target = getenv("HOME");
        if (!target) { fprintf(stderr, "cd: HOME not set\n"); return 1; }
    } else if (strcmp(argv[1], "-") == 0) {
        target = getenv("OLDPWD");
        if (!target) { fprintf(stderr, "cd: OLDPWD not set\n"); return 1; }
        print_dir = 1;
    } else if (argv[1][0] == '~' && (argv[1][1] == '/' || argv[1][1] == '\0')) {
        const char *home = getenv("HOME");
        if (!home) { fprintf(stderr, "cd: HOME not set\n"); return 1; }
        snprintf(expanded, sizeof(expanded), "%s%s", home, argv[1] + 1);
        target = expanded;
    } else {
        target = argv[1];
    }

    char old[PATH_MAX];
    if (!getcwd(old, sizeof(old))) old[0] = '\0';

    if (chdir(target) < 0) {
        fprintf(stderr, "cd: %s: %s\n", target, strerror(errno));
        return 1;
    }

    char now[PATH_MAX];
    if (getcwd(now, sizeof(now))) {
        if (old[0]) setenv("OLDPWD", old, 1);
        setenv("PWD", now, 1);
        if (print_dir) puts(now);
    }
    return 0;
}
