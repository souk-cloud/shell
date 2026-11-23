/* util.h - 여러 명령이 공유하는 경로/파일 유틸리티 */
#ifndef UTIL_H
#define UTIL_H

#include <sys/types.h>

typedef struct {
    int recursive;     /* -r */
    int force;         /* -f */
    int interactive;   /* -i */
    int verbose;       /* -v */
    int preserve;      /* -p (권한/시간 보존) */
    int no_clobber;    /* -n */
    const char *prog;  /* 오류 메시지 접두어 ("cp", "rm", "mv") */
} FileOpts;

char *path_join(const char *dir, const char *name);   /* malloc 반환 */
char *path_basename(const char *path);                /* malloc 반환 */
char *path_dirname(const char *path);                 /* malloc 반환 */
int   is_dir(const char *path);                       /* 심볼릭 링크를 따라감 */
int   same_file(const char *a, const char *b);        /* 같은 inode 인가 */
int   is_subpath(const char *src, const char *dst);   /* dst 가 src 내부인가 */
int   confirm(const char *prompt);                    /* y/n 질문 */

int   copy_tree(const char *src, const char *dst, const FileOpts *o);   /* cp, mv */
int   remove_tree(const char *path, const FileOpts *o);                 /* rm, mv */

ssize_t read_line_fd(int fd, char *buf, size_t size);

#endif
