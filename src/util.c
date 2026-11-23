/*
 * util.c - 경로 처리, 파일 복사/삭제 공용 함수   [담당: 전승준]
 * cp / mv 가 copy_tree 를, rm / mv 가 remove_tree 를 함께 사용한다.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <dirent.h>
#include <limits.h>
#include <sys/stat.h>
#include "util.h"

char *path_join(const char *dir, const char *name)
{
    size_t dl = strlen(dir);
    int slash = (dl > 0 && dir[dl - 1] == '/');
    char *r = malloc(dl + strlen(name) + 2);
    sprintf(r, "%s%s%s", dir, slash ? "" : "/", name);
    return r;
}

char *path_basename(const char *path)
{
    size_t len = strlen(path);
    while (len > 1 && path[len - 1] == '/') len--;
    if (len == 1 && path[0] == '/') return strdup("/");
    size_t start = len;
    while (start > 0 && path[start - 1] != '/') start--;
    return strndup(path + start, len - start);
}

char *path_dirname(const char *path)
{
    size_t len = strlen(path);
    while (len > 1 && path[len - 1] == '/') len--;
    size_t i = len;
    while (i > 0 && path[i - 1] != '/') i--;
    if (i == 0) return strdup(".");
    while (i > 1 && path[i - 1] == '/') i--;
    return strndup(path, i);
}

int is_dir(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

int same_file(const char *a, const char *b)
{
    struct stat sa, sb;
    if (stat(a, &sa) < 0 || stat(b, &sb) < 0) return 0;
    return sa.st_dev == sb.st_dev && sa.st_ino == sb.st_ino;
}

int is_subpath(const char *src, const char *dst)
{
    char rs[PATH_MAX], rd[PATH_MAX];
    if (!realpath(src, rs)) return 0;
    if (!realpath(dst, rd)) {              /* dst 가 아직 없으면 부모 + 이름으로 계산 */
        char *dn = path_dirname(dst), *bn = path_basename(dst);
        char rdn[PATH_MAX];
        int ok = realpath(dn, rdn) != NULL;
        free(dn);
        if (!ok) { free(bn); return 0; }
        int n = snprintf(rd, sizeof(rd), "%s/%s", rdn, bn);
        free(bn);
        if (n < 0 || (size_t)n >= sizeof(rd)) return 0;
    }
    size_t n = strlen(rs);
    return strncmp(rs, rd, n) == 0 && (rd[n] == '/' || rd[n] == '\0');
}

/* fd 0 에서 한 줄을 읽는다. stdio 버퍼를 쓰지 않으므로 자식 프로세스와
 * stdin 을 안전하게 공유할 수 있다.  반환: 길이, EOF -> -1, 시그널 중단 -> -2 */
ssize_t read_line_fd(int fd, char *buf, size_t size)
{
    size_t n = 0;
    int discarding = 0;
    for (;;) {
        char c;
        ssize_t r = read(fd, &c, 1);
        if (r < 0) {
            if (errno == EINTR) return -2;
            return -1;
        }
        if (r == 0) {
            if (n == 0 && !discarding) return -1;
            break;
        }
        if (c == '\n') break;
        if (n + 1 < size) buf[n++] = c; else discarding = 1;
    }
    buf[n] = '\0';
    return (ssize_t)n;
}

int confirm(const char *prompt)
{
    char buf[64];
    fprintf(stderr, "%s", prompt);
    fflush(stderr);
    ssize_t n = read_line_fd(STDIN_FILENO, buf, sizeof(buf));
    return n > 0 && (buf[0] == 'y' || buf[0] == 'Y');
}

/* ------------------------------------------------------------------ */
/* 복사                                                                */
/* ------------------------------------------------------------------ */
static int copy_file(const char *src, const char *dst, const struct stat *sst,
                     const FileOpts *o)
{
    int in = open(src, O_RDONLY);
    if (in < 0) {
        fprintf(stderr, "%s: cannot open '%s' for reading: %s\n", o->prog, src, strerror(errno));
        return 1;
    }
    int out = open(dst, O_WRONLY | O_CREAT | O_TRUNC, sst->st_mode & 0777);
    if (out < 0 && o->force) {
        unlink(dst);
        out = open(dst, O_WRONLY | O_CREAT | O_TRUNC, sst->st_mode & 0777);
    }
    if (out < 0) {
        fprintf(stderr, "%s: cannot create regular file '%s': %s\n", o->prog, dst, strerror(errno));
        close(in);
        return 1;
    }

    char buf[65536];
    ssize_t n;
    int rc = 0;
    while ((n = read(in, buf, sizeof(buf))) != 0) {
        if (n < 0) {
            if (errno == EINTR) continue;
            fprintf(stderr, "%s: error reading '%s': %s\n", o->prog, src, strerror(errno));
            rc = 1; break;
        }
        ssize_t off = 0;
        while (off < n) {
            ssize_t w = write(out, buf + off, n - off);
            if (w < 0) {
                if (errno == EINTR) continue;
                fprintf(stderr, "%s: error writing '%s': %s\n", o->prog, dst, strerror(errno));
                rc = 1; goto done;
            }
            off += w;
        }
    }
done:
    if (rc == 0 && o->preserve) {
        fchmod(out, sst->st_mode & 07777);
        struct timespec ts[2] = { sst->st_atim, sst->st_mtim };
        futimens(out, ts);
    }
    close(in);
    close(out);
    return rc;
}

int copy_tree(const char *src, const char *dst, const FileOpts *o)
{
    struct stat st;
    if (lstat(src, &st) < 0) {
        fprintf(stderr, "%s: cannot stat '%s': %s\n", o->prog, src, strerror(errno));
        return 1;
    }

    /* ---- 디렉터리 ---- */
    if (S_ISDIR(st.st_mode)) {
        if (!o->recursive) {
            fprintf(stderr, "%s: -r not specified; omitting directory '%s'\n", o->prog, src);
            return 1;
        }
        struct stat dst_st;
        if (stat(dst, &dst_st) == 0) {
            if (!S_ISDIR(dst_st.st_mode)) {
                fprintf(stderr, "%s: cannot overwrite non-directory '%s' with directory '%s'\n",
                        o->prog, dst, src);
                return 1;
            }
        } else if (mkdir(dst, (st.st_mode & 0777) | 0700) < 0) {
            fprintf(stderr, "%s: cannot create directory '%s': %s\n", o->prog, dst, strerror(errno));
            return 1;
        }
        if (o->verbose) printf("'%s' -> '%s'\n", src, dst);

        DIR *d = opendir(src);
        if (!d) {
            fprintf(stderr, "%s: cannot open directory '%s': %s\n", o->prog, src, strerror(errno));
            return 1;
        }
        int rc = 0;
        struct dirent *e;
        while ((e = readdir(d)) != NULL) {
            if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
            char *s = path_join(src, e->d_name);
            char *t = path_join(dst, e->d_name);
            if (copy_tree(s, t, o)) rc = 1;
            free(s); free(t);
        }
        closedir(d);
        if (o->preserve) {
            chmod(dst, st.st_mode & 07777);
            struct timespec ts[2] = { st.st_atim, st.st_mtim };
            utimensat(AT_FDCWD, dst, ts, 0);
        }
        return rc;
    }

    /* ---- 덮어쓰기 검사 ---- */
    struct stat ex;
    if (lstat(dst, &ex) == 0) {
        if (same_file(src, dst)) {
            fprintf(stderr, "%s: '%s' and '%s' are the same file\n", o->prog, src, dst);
            return 1;
        }
        if (o->no_clobber) return 0;
        if (o->interactive) {
            char q[PATH_MAX + 64];
            snprintf(q, sizeof(q), "%s: overwrite '%s'? ", o->prog, dst);
            if (!confirm(q)) return 0;
        }
    }

    /* ---- 심볼릭 링크 (재귀 복사에서는 링크 자체를 복제) ---- */
    if (S_ISLNK(st.st_mode) && o->recursive) {
        char target[PATH_MAX];
        ssize_t n = readlink(src, target, sizeof(target) - 1);
        if (n < 0) {
            fprintf(stderr, "%s: cannot read symlink '%s': %s\n", o->prog, src, strerror(errno));
            return 1;
        }
        target[n] = '\0';
        unlink(dst);
        if (symlink(target, dst) < 0) {
            fprintf(stderr, "%s: cannot create symlink '%s': %s\n", o->prog, dst, strerror(errno));
            return 1;
        }
        if (o->verbose) printf("'%s' -> '%s'\n", src, dst);
        return 0;
    }

    /* ---- 일반 파일 (링크는 가리키는 파일의 내용을 복사) ---- */
    struct stat real;
    if (stat(src, &real) < 0) {
        fprintf(stderr, "%s: cannot stat '%s': %s\n", o->prog, src, strerror(errno));
        return 1;
    }
    if (!S_ISREG(real.st_mode)) {
        fprintf(stderr, "%s: cannot copy special file '%s'\n", o->prog, src);
        return 1;
    }
    int rc = copy_file(src, dst, &real, o);
    if (rc == 0 && o->verbose) printf("'%s' -> '%s'\n", src, dst);
    return rc;
}

/* ------------------------------------------------------------------ */
/* 삭제                                                                */
/* ------------------------------------------------------------------ */
int remove_tree(const char *path, const FileOpts *o)
{
    struct stat st;
    char q[PATH_MAX + 64];

    if (lstat(path, &st) < 0) {
        if (o->force && errno == ENOENT) return 0;
        fprintf(stderr, "%s: cannot remove '%s': %s\n", o->prog, path, strerror(errno));
        return 1;
    }

    if (S_ISDIR(st.st_mode)) {
        if (!o->recursive) {
            fprintf(stderr, "%s: cannot remove '%s': Is a directory\n", o->prog, path);
            return 1;
        }
        DIR *d = opendir(path);
        if (!d) {
            fprintf(stderr, "%s: cannot open directory '%s': %s\n", o->prog, path, strerror(errno));
            return 1;
        }
        /* 순회 도중 삭제하지 않도록 이름을 먼저 모은다 */
        char **names = NULL;
        int n = 0, cap = 0;
        struct dirent *e;
        while ((e = readdir(d)) != NULL) {
            if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
            if (n == cap) { cap = cap ? cap * 2 : 16; names = realloc(names, cap * sizeof(char *)); }
            names[n++] = strdup(e->d_name);
        }
        closedir(d);

        int rc = 0;
        for (int i = 0; i < n; i++) {
            char *child = path_join(path, names[i]);
            if (remove_tree(child, o)) rc = 1;
            free(child);
            free(names[i]);
        }
        free(names);
        if (rc) return rc;

        if (o->interactive) {
            snprintf(q, sizeof(q), "%s: remove directory '%s'? ", o->prog, path);
            if (!confirm(q)) return 0;
        }
        if (rmdir(path) < 0) {
            fprintf(stderr, "%s: cannot remove '%s': %s\n", o->prog, path, strerror(errno));
            return 1;
        }
        if (o->verbose) printf("removed directory '%s'\n", path);
        return 0;
    }

    if (o->interactive) {
        snprintf(q, sizeof(q), "%s: remove file '%s'? ", o->prog, path);
        if (!confirm(q)) return 0;
    }
    if (unlink(path) < 0) {
        fprintf(stderr, "%s: cannot remove '%s': %s\n", o->prog, path, strerror(errno));
        return 1;
    }
    if (o->verbose) printf("removed '%s'\n", path);
    return 0;
}
