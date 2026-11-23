/*
 * cmd_grep.c - grep 명령                               [담당: 전승준]
 *   grep [OPTIONS] PATTERN [FILE...]
 *   -i 대소문자 무시  -n 줄 번호  -v 일치하지 않는 줄  -c 일치 줄 수  -l 파일명만
 *   -r 디렉터리 재귀  -E 확장 정규식  -F 고정 문자열  -w 단어 단위  -x 줄 전체 일치
 *   -H/-h 파일명 표시/숨김
 * 기본은 POSIX 기본 정규식(BRE)이며 regcomp()/regexec() 를 사용한다.
 * 종료 상태: 일치 있음 0, 없음 1, 오류 2   (파이프라인 "ls | grep txt" 에서도 동작)
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <ctype.h>
#include <dirent.h>
#include <regex.h>
#include <sys/stat.h>
#include "builtin.h"
#include "util.h"

static struct { int i, n, v, c, l, r, E, F, w, x, H, h; } G;
static regex_t re;
static const char *pat;
static int any_match, had_error;

static int is_word(char c) { return isalnum((unsigned char)c) || c == '_'; }

/* line+from 이후에서 첫 번째 일치를 찾는다 */
static int find_match(const char *line, size_t from, size_t *so, size_t *eo)
{
    if (G.F) {
        const char *p = G.i ? strcasestr(line + from, pat) : strstr(line + from, pat);
        if (!p) return 0;
        *so = (size_t)(p - line);
        *eo = *so + strlen(pat);
        return 1;
    }
    regmatch_t m;
    if (regexec(&re, line + from, 1, &m, from > 0 ? REG_NOTBOL : 0) != 0) return 0;
    *so = from + (size_t)m.rm_so;
    *eo = from + (size_t)m.rm_eo;
    return 1;
}

static int line_matches(const char *line, size_t len)
{
    size_t so, eo;
    if (G.x) {
        if (G.F) return (G.i ? strcasecmp(line, pat) : strcmp(line, pat)) == 0;
        return find_match(line, 0, &so, &eo) && so == 0 && eo == len;
    }
    if (G.w) {
        size_t from = 0;
        while (from <= len && find_match(line, from, &so, &eo)) {
            int left_ok  = (so == 0) || !is_word(line[so - 1]);
            int right_ok = (eo >= len) || !is_word(line[eo]);
            if (left_ok && right_ok && eo > so) return 1;
            from = so + 1;
        }
        return 0;
    }
    return find_match(line, 0, &so, &eo);
}

/* 스트림 하나를 검색. 일치한 줄 수 반환 */
static void search_stream(FILE *fp, const char *name, int show_name)
{
    char *line = NULL;
    size_t cap = 0;
    ssize_t n;
    long lineno = 0, count = 0;

    while ((n = getline(&line, &cap, fp)) != -1) {
        lineno++;
        if (memchr(line, '\0', (size_t)n)) {            /* 바이너리 파일 */
            if (!G.c && !G.l) {
                count++;
                any_match = 1;
                printf("Binary file %s matches\n", name);
                break;
            }
        }
        size_t len = (size_t)n;
        if (len > 0 && line[len - 1] == '\n') line[--len] = '\0';

        int matched = line_matches(line, len);
        if (G.v) matched = !matched;
        if (!matched) continue;

        count++;
        any_match = 1;
        if (G.l) break;                                 /* 파일명만 출력할 거라 더 볼 필요 없음 */
        if (G.c) continue;

        if (show_name) printf("%s:", name);
        if (G.n)       printf("%ld:", lineno);
        puts(line);
    }
    free(line);

    if (G.l) {
        if (count > 0) puts(name);
    } else if (G.c) {
        if (show_name) printf("%s:", name);
        printf("%ld\n", count);
    }
}

static void search_path(const char *path, const char *display, int show_name, int top)
{
    struct stat st;
    if (lstat(path, &st) < 0) {
        fprintf(stderr, "grep: %s: %s\n", display, strerror(errno));
        had_error = 1;
        return;
    }

    if (S_ISDIR(st.st_mode)) {
        if (!G.r) {
            fprintf(stderr, "grep: %s: Is a directory\n", display);
            had_error = 1;
            return;
        }
        DIR *d = opendir(path);
        if (!d) {
            fprintf(stderr, "grep: %s: %s\n", display, strerror(errno));
            had_error = 1;
            return;
        }
        struct dirent *e;
        while ((e = readdir(d)) != NULL) {
            if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
            char *child = path_join(path, e->d_name);
            /* 인자 없이 -r 이면 "./" 접두어 없이 표시 (GNU grep 과 동일) */
            char *shown = (top && !strcmp(display, ".") ) ? strdup(e->d_name) : path_join(display, e->d_name);
            search_path(child, shown, 1, 0);
            free(child); free(shown);
        }
        closedir(d);
        return;
    }

    if (S_ISLNK(st.st_mode) && !top) return;            /* 재귀 중에는 링크를 따라가지 않음 */

    FILE *fp = fopen(path, "r");
    if (!fp) {
        fprintf(stderr, "grep: %s: %s\n", display, strerror(errno));
        had_error = 1;
        return;
    }
    search_stream(fp, display, show_name);
    fclose(fp);
}

int cmd_grep(int argc, char **argv)
{
    memset(&G, 0, sizeof(G));
    any_match = had_error = 0;
    int c;
    optind = 0;
    while ((c = getopt(argc, argv, "inVvclrREFwxHh")) != -1) {
        switch (c) {
        case 'i': G.i = 1; break;
        case 'n': G.n = 1; break;
        case 'v': G.v = 1; break;
        case 'c': G.c = 1; break;
        case 'l': G.l = 1; break;
        case 'r': case 'R': G.r = 1; break;
        case 'E': G.E = 1; break;
        case 'F': G.F = 1; break;
        case 'w': G.w = 1; break;
        case 'x': G.x = 1; break;
        case 'H': G.H = 1; G.h = 0; break;
        case 'h': G.h = 1; G.H = 0; break;
        default:
            fprintf(stderr, "Usage: grep [-inv cl rEFwxHh] PATTERN [FILE]...\n");
            return 2;
        }
    }
    if (optind >= argc) {
        fprintf(stderr, "Usage: grep [-inv cl rEFwxHh] PATTERN [FILE]...\n");
        return 2;
    }
    pat = argv[optind++];

    if (!G.F) {
        int flags = (G.E ? REG_EXTENDED : 0) | (G.i ? REG_ICASE : 0);
        int err = regcomp(&re, pat, flags);
        if (err) {
            char msg[256];
            regerror(err, &re, msg, sizeof(msg));
            fprintf(stderr, "grep: %s\n", msg);
            return 2;
        }
    }

    int nfiles = argc - optind;
    int show_name = G.H || (!G.h && (nfiles > 1 || G.r));

    if (nfiles == 0) {
        if (G.r) search_path(".", ".", 1, 1);
        else     search_stream(stdin, "(standard input)", G.H);
    } else {
        for (int i = optind; i < argc; i++) {
            if (strcmp(argv[i], "-") == 0) search_stream(stdin, "(standard input)", show_name);
            else                           search_path(argv[i], argv[i], show_name, 1);
        }
    }

    if (!G.F) regfree(&re);
    if (had_error) return 2;
    return any_match ? 0 : 1;
}
