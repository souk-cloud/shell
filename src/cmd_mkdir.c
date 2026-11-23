/*
 * cmd_mkdir.c - mkdir 명령                             [담당: 전승준]
 *   mkdir [-p] [-v] [-m MODE] DIR...
 *   -p 중간 경로까지 생성(이미 있어도 오류 아님)  -v 생성 메시지  -m 8진수 권한
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

static int make_one(const char *path, int parents, int verbose, mode_t mode, int has_mode)
{
    if (!parents) {
        if (mkdir(path, has_mode ? mode : 0777) < 0) {
            fprintf(stderr, "mkdir: cannot create directory '%s': %s\n", path, strerror(errno));
            return 1;
        }
        if (has_mode) chmod(path, mode);     /* umask 의 영향을 받지 않도록 */
        if (verbose) printf("mkdir: created directory '%s'\n", path);
        return 0;
    }

    /* -p : 앞에서부터 '/' 단위로 하나씩 만든다 */
    char buf[PATH_MAX];
    if (strlen(path) >= sizeof(buf)) {
        fprintf(stderr, "mkdir: path too long\n");
        return 1;
    }
    strcpy(buf, path);
    size_t len = strlen(buf);
    while (len > 1 && buf[len - 1] == '/') buf[--len] = '\0';

    for (char *p = buf + 1; ; p++) {
        if (*p != '/' && *p != '\0') continue;
        char saved = *p;
        *p = '\0';
        int last = (saved == '\0');
        if (mkdir(buf, 0777) == 0) {
            if (verbose) printf("mkdir: created directory '%s'\n", buf);
            if (last && has_mode) chmod(buf, mode);
        } else if (errno == EEXIST) {
            if (!is_dir(buf)) {
                fprintf(stderr, "mkdir: cannot create directory '%s': File exists\n", buf);
                return 1;
            }
        } else {
            fprintf(stderr, "mkdir: cannot create directory '%s': %s\n", buf, strerror(errno));
            return 1;
        }
        if (last) break;
        *p = saved;
    }
    return 0;
}

int cmd_mkdir(int argc, char **argv)
{
    int parents = 0, verbose = 0, has_mode = 0, c;
    mode_t mode = 0777;
    optind = 0;
    while ((c = getopt(argc, argv, "pvm:")) != -1) {
        switch (c) {
        case 'p': parents = 1; break;
        case 'v': verbose = 1; break;
        case 'm': {
            char *end;
            long m = strtol(optarg, &end, 8);
            if (*end != '\0' || m < 0 || m > 07777) {
                fprintf(stderr, "mkdir: invalid mode '%s'\n", optarg);
                return 1;
            }
            mode = (mode_t)m; has_mode = 1;
            break;
        }
        default:
            fprintf(stderr, "Usage: mkdir [-pv] [-m MODE] DIRECTORY...\n");
            return 2;
        }
    }
    if (optind >= argc) { fprintf(stderr, "mkdir: missing operand\n"); return 1; }

    int rc = 0;
    for (int i = optind; i < argc; i++)
        if (make_one(argv[i], parents, verbose, mode, has_mode)) rc = 1;
    return rc;
}
