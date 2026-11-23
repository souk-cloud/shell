/*
 * cmd_ls.c - ls 명령                                   [담당: 김소욱]
 *
 * 지원 옵션 : -a 숨김파일  -l 상세  -R 재귀  -h 사람이 읽는 크기(-l 과 함께)
 *             -d 디렉터리 자체  -1 한 줄에 하나  -t 수정시간순  -r 역순
 * 터미널로 출력할 때는 터미널 폭에 맞춰 여러 열로, 파이프/파일로 출력할 때는 한 줄에 하나.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <dirent.h>
#include <pwd.h>
#include <grp.h>
#include <time.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include "builtin.h"
#include "util.h"

typedef struct {
    char       *name;      /* 화면에 보일 이름 */
    char       *path;      /* stat 에 쓸 경로 */
    struct stat st;
    int         has_stat;
} Entry;

static struct { int all, longf, recursive, human, dironly, one, bytime, reverse; } O;
static int exit_rc;

static int cmp_entry(const void *a, const void *b)
{
    const Entry *x = a, *y = b;
    int r;
    if (O.bytime && x->st.st_mtime != y->st.st_mtime)
        r = (x->st.st_mtime < y->st.st_mtime) ? 1 : -1;      /* 최신이 먼저 */
    else
        r = strcoll(x->name, y->name);
    return O.reverse ? -r : r;
}

static void mode_str(mode_t m, char *s)
{
    s[0] = S_ISDIR(m) ? 'd' : S_ISLNK(m) ? 'l' : S_ISCHR(m) ? 'c' :
           S_ISBLK(m) ? 'b' : S_ISFIFO(m) ? 'p' : S_ISSOCK(m) ? 's' : '-';
    const char *rwx = "rwxrwxrwx";
    for (int i = 0; i < 9; i++) s[1 + i] = (m & (1 << (8 - i))) ? rwx[i] : '-';
    if (m & S_ISUID) s[3] = (m & S_IXUSR) ? 's' : 'S';
    if (m & S_ISGID) s[6] = (m & S_IXGRP) ? 's' : 'S';
    if (m & S_ISVTX) s[9] = (m & S_IXOTH) ? 't' : 'T';
    s[10] = '\0';
}

static void fmt_size(off_t sz, char *buf, size_t n)
{
    if (!O.human) { snprintf(buf, n, "%lld", (long long)sz); return; }
    const char *unit = "BKMGT";
    double v = (double)sz;
    int i = 0;
    while (v >= 1024.0 && i < 4) { v /= 1024.0; i++; }
    if (i == 0)      snprintf(buf, n, "%lld", (long long)sz);
    else if (v < 10) snprintf(buf, n, "%.1f%c", v, unit[i]);
    else             snprintf(buf, n, "%.0f%c", v, unit[i]);
}

static int term_width(void)
{
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0) return ws.ws_col;
    return 80;
}

static void print_long(Entry *v, int n, int show_total)
{
    int w_link = 1, w_own = 1, w_grp = 1, w_size = 1;
    long long total = 0;
    char tmp[64];

    for (int i = 0; i < n; i++) {
        if (!v[i].has_stat) continue;
        struct passwd *pw = getpwuid(v[i].st.st_uid);
        struct group  *gr = getgrgid(v[i].st.st_gid);
        int l;
        snprintf(tmp, sizeof(tmp), "%lu", (unsigned long)v[i].st.st_nlink);
        if ((l = (int)strlen(tmp)) > w_link) w_link = l;
        if (pw) l = (int)strlen(pw->pw_name);
        else { snprintf(tmp, sizeof(tmp), "%u", v[i].st.st_uid); l = (int)strlen(tmp); }
        if (l > w_own) w_own = l;
        if (gr) l = (int)strlen(gr->gr_name);
        else { snprintf(tmp, sizeof(tmp), "%u", v[i].st.st_gid); l = (int)strlen(tmp); }
        if (l > w_grp) w_grp = l;
        fmt_size(v[i].st.st_size, tmp, sizeof(tmp));
        if ((l = (int)strlen(tmp)) > w_size) w_size = l;
        total += v[i].st.st_blocks;
    }
    if (show_total) {
        if (O.human) {                       /* ls -lh 는 "total 12K" 처럼 표시 */
            fmt_size((off_t)((total + 1) / 2) * 1024, tmp, sizeof(tmp));
            printf("total %s\n", tmp);
        } else {
            printf("total %lld\n", (total + 1) / 2);
        }
    }

    time_t now = time(NULL);
    for (int i = 0; i < n; i++) {
        if (!v[i].has_stat) { printf("?????????? %s\n", v[i].name); continue; }
        struct stat *st = &v[i].st;
        char mode[11], own[32], grp[32], size[32], when[32];
        mode_str(st->st_mode, mode);

        struct passwd *pw = getpwuid(st->st_uid);
        struct group  *gr = getgrgid(st->st_gid);
        if (pw) snprintf(own, sizeof(own), "%s", pw->pw_name); else snprintf(own, sizeof(own), "%u", st->st_uid);
        if (gr) snprintf(grp, sizeof(grp), "%s", gr->gr_name); else snprintf(grp, sizeof(grp), "%u", st->st_gid);
        fmt_size(st->st_size, size, sizeof(size));

        struct tm tm;
        localtime_r(&st->st_mtime, &tm);
        if (llabs((long long)(now - st->st_mtime)) > 182LL * 24 * 3600)
            strftime(when, sizeof(when), "%b %e  %Y", &tm);
        else
            strftime(when, sizeof(when), "%b %e %H:%M", &tm);

        printf("%s %*lu %-*s %-*s %*s %s %s", mode, w_link, (unsigned long)st->st_nlink,
               w_own, own, w_grp, grp, w_size, size, when, v[i].name);
        if (S_ISLNK(st->st_mode)) {
            char target[4096];
            ssize_t k = readlink(v[i].path, target, sizeof(target) - 1);
            if (k >= 0) { target[k] = '\0'; printf(" -> %s", target); }
        }
        putchar('\n');
    }
}

static void print_columns(Entry *v, int n)
{
    int maxlen = 0;
    for (int i = 0; i < n; i++) {
        int l = (int)strlen(v[i].name);
        if (l > maxlen) maxlen = l;
    }
    int colw = maxlen + 2;
    int cols = term_width() / colw;
    if (cols < 1) cols = 1;
    int rows = (n + cols - 1) / cols;
    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++) {
            int idx = c * rows + r;       /* 열 우선 배치 (진짜 ls 와 동일) */
            if (idx >= n) break;
            int last = (idx + rows >= n);
            if (last) printf("%s", v[idx].name);
            else      printf("%-*s", colw, v[idx].name);
        }
        putchar('\n');
    }
}

static void print_entries(Entry *v, int n, int show_total)
{
    if (O.longf)                                   print_long(v, n, show_total);
    else if (!O.one && isatty(STDOUT_FILENO))      print_columns(v, n);
    else for (int i = 0; i < n; i++)               puts(v[i].name);
}

static void free_entries(Entry *v, int n)
{
    for (int i = 0; i < n; i++) { free(v[i].name); free(v[i].path); }
    free(v);
}

static void list_dir(const char *path, int header)
{
    DIR *d = opendir(path);
    if (!d) {
        fprintf(stderr, "ls: cannot open directory '%s': %s\n", path, strerror(errno));
        exit_rc = 2;
        return;
    }
    Entry *v = NULL;
    int n = 0, cap = 0;
    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        if (!O.all && de->d_name[0] == '.') continue;
        if (n == cap) { cap = cap ? cap * 2 : 32; v = realloc(v, cap * sizeof(Entry)); }
        v[n].name = strdup(de->d_name);
        v[n].path = path_join(path, de->d_name);
        v[n].has_stat = (lstat(v[n].path, &v[n].st) == 0);
        n++;
    }
    closedir(d);
    qsort(v, n, sizeof(Entry), cmp_entry);

    if (header) printf("%s:\n", path);
    print_entries(v, n, 1);

    if (O.recursive) {
        for (int i = 0; i < n; i++) {
            if (v[i].has_stat && S_ISDIR(v[i].st.st_mode) &&
                strcmp(v[i].name, ".") && strcmp(v[i].name, "..")) {
                putchar('\n');
                list_dir(v[i].path, 1);
            }
        }
    }
    free_entries(v, n);
}

int cmd_ls(int argc, char **argv)
{
    memset(&O, 0, sizeof(O));
    exit_rc = 0;
    optind = 0;
    int c;
    while ((c = getopt(argc, argv, "alRhd1tr")) != -1) {
        switch (c) {
        case 'a': O.all = 1; break;
        case 'l': O.longf = 1; break;
        case 'R': O.recursive = 1; break;
        case 'h': O.human = 1; break;
        case 'd': O.dironly = 1; break;
        case '1': O.one = 1; break;
        case 't': O.bytime = 1; break;
        case 'r': O.reverse = 1; break;
        default:
            fprintf(stderr, "Usage: ls [-alRhd1tr] [FILE]...\n");
            return 2;
        }
    }

    char *dot[] = { ".", NULL };
    char **targets = argv + optind;
    int nt = argc - optind;
    if (nt == 0) { targets = dot; nt = 1; }

    Entry *files = calloc(nt, sizeof(Entry));
    char **dirs = calloc(nt, sizeof(char *));
    int nf = 0, nd = 0;

    for (int i = 0; i < nt; i++) {
        struct stat st;
        if (lstat(targets[i], &st) < 0) {
            fprintf(stderr, "ls: cannot access '%s': %s\n", targets[i], strerror(errno));
            exit_rc = 2;
            continue;
        }
        /* 심볼릭 링크가 디렉터리를 가리키면 (-l, -d 가 아닐 때) 디렉터리로 취급 */
        if (S_ISLNK(st.st_mode) && !O.longf && !O.dironly) {
            struct stat t;
            if (stat(targets[i], &t) == 0) st = t;
        }
        if (S_ISDIR(st.st_mode) && !O.dironly) {
            dirs[nd++] = targets[i];
        } else {
            files[nf].name = strdup(targets[i]);
            files[nf].path = strdup(targets[i]);
            files[nf].st = st;
            files[nf].has_stat = 1;
            nf++;
        }
    }

    qsort(files, nf, sizeof(Entry), cmp_entry);
    if (nf) print_entries(files, nf, 0);

    int header = (nt > 1) || O.recursive;
    for (int i = 0; i < nd; i++) {
        if (nf || i > 0) putchar('\n');
        list_dir(dirs[i], header);
    }
    free_entries(files, nf);
    free(dirs);
    return exit_rc;
}
