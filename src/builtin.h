/* builtin.h - 내장 명령 테이블 */
#ifndef BUILTIN_H
#define BUILTIN_H

typedef int (*cmd_fn)(int argc, char **argv);

typedef struct {
    const char *name;
    cmd_fn      fn;
    int         in_parent;   /* 1 이면 쉘 프로세스 안에서 직접 실행 (cd, exit, fg ...) */
} Builtin;

const Builtin *find_builtin(const char *name);

/* ---- 쉘 제어 명령 (builtin.c) ---- */
int cmd_exit(int argc, char **argv);
int cmd_jobs(int argc, char **argv);
int cmd_fg(int argc, char **argv);
int cmd_bg(int argc, char **argv);
int cmd_echo(int argc, char **argv);
int cmd_help(int argc, char **argv);

/* ---- 파일 명령: 팀원별 구현 (cmd_*.c) ---- */
int cmd_ls(int argc, char **argv);
int cmd_pwd(int argc, char **argv);
int cmd_cd(int argc, char **argv);
int cmd_mkdir(int argc, char **argv);
int cmd_rmdir(int argc, char **argv);
int cmd_ln(int argc, char **argv);
int cmd_cp(int argc, char **argv);
int cmd_rm(int argc, char **argv);
int cmd_mv(int argc, char **argv);
int cmd_cat(int argc, char **argv);
int cmd_grep(int argc, char **argv);

#endif
