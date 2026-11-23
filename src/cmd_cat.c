/*
 * cmd_cat.c - cat 명령                                 [담당: 전승준]
 *   cat [-n] [-b] [-E] [-s] [FILE...]
 *   -n 모든 줄 번호  -b 빈 줄 제외 번호  -E 줄 끝에 '$'  -s 연속 빈 줄 압축
 *   FILE 이 없거나 '-' 이면 표준입력을 읽는다  ->  "ls | cat", "cat < file" 가능
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include "builtin.h"

static struct { int number, number_nonblank, show_ends, squeeze; } C;
static long lineno;
static int prev_blank;

/* 옵션이 없을 때: read/write 로 빠르게 복사 */
static int raw_copy(int fd, const char *name)
{
    char buf[65536];
    ssize_t n;
    while ((n = read(fd, buf, sizeof(buf))) != 0) {
        if (n < 0) {
            if (errno == EINTR) continue;
            fprintf(stderr, "cat: %s: %s\n", name, strerror(errno));
            return 1;
        }
        ssize_t off = 0;
        while (off < n) {
            ssize_t w = write(STDOUT_FILENO, buf + off, n - off);
            if (w < 0) {
                if (errno == EINTR) continue;
                return 1;                  /* EPIPE 등: 조용히 종료 */
            }
            off += w;
        }
    }
    return 0;
}

/* 옵션이 있을 때: 줄 단위 처리 */
static int line_copy(int fd, const char *name)
{
    FILE *fp = fdopen(fd, "r");
    if (!fp) { fprintf(stderr, "cat: %s: %s\n", name, strerror(errno)); return 1; }

    char *line = NULL;
    size_t cap = 0;
    ssize_t len;
    while ((len = getline(&line, &cap, fp)) != -1) {
        int has_nl = (len > 0 && line[len - 1] == '\n');
        int blank = (len == 1 && has_nl);

        if (C.squeeze && blank && prev_blank) continue;
        prev_blank = blank;

        if (C.number_nonblank) { if (!blank) printf("%6ld\t", ++lineno); }
        else if (C.number)       printf("%6ld\t", ++lineno);

        if (has_nl) {
            fwrite(line, 1, len - 1, stdout);
            if (C.show_ends) putchar('$');
            putchar('\n');
        } else {
            fwrite(line, 1, len, stdout);
        }
    }
    free(line);
    fflush(stdout);
    int err = ferror(fp);
    fclose(fp);
    return err ? 1 : 0;
}

int cmd_cat(int argc, char **argv)
{
    memset(&C, 0, sizeof(C));
    lineno = 0;
    prev_blank = 0;
    int c;
    optind = 0;
    while ((c = getopt(argc, argv, "nbEs")) != -1) {
        switch (c) {
        case 'n': C.number = 1; break;
        case 'b': C.number_nonblank = 1; break;
        case 'E': C.show_ends = 1; break;
        case 's': C.squeeze = 1; break;
        default:
            fprintf(stderr, "Usage: cat [-nbEs] [FILE]...\n");
            return 2;
        }
    }
    int use_lines = C.number || C.number_nonblank || C.show_ends || C.squeeze;

    int rc = 0;
    int nfiles = argc - optind;
    for (int i = 0; i < (nfiles ? nfiles : 1); i++) {
        const char *name = nfiles ? argv[optind + i] : "-";
        int fd, own = 0;
        if (strcmp(name, "-") == 0) {
            fd = STDIN_FILENO;
            name = "standard input";
        } else {
            struct stat st;
            fd = open(name, O_RDONLY);
            if (fd < 0) {
                fprintf(stderr, "cat: %s: %s\n", name, strerror(errno));
                rc = 1;
                continue;
            }
            if (fstat(fd, &st) == 0 && S_ISDIR(st.st_mode)) {
                fprintf(stderr, "cat: %s: Is a directory\n", name);
                close(fd);
                rc = 1;
                continue;
            }
            own = 1;
        }

        int r;
        if (use_lines) {
            r = line_copy(own ? fd : dup(fd), name);   /* fclose 가 fd 를 닫으므로 stdin 은 dup */
        } else {
            r = raw_copy(fd, name);
            if (own) close(fd);
        }
        if (r) rc = 1;
    }
    return rc;
}
