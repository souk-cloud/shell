/*
 * builtin.c - 내장 명령 테이블과 쉘 제어 명령 (exit, jobs, fg, bg, echo, help)   [담당: 김소욱]
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "builtin.h"
#include "jobs.h"
#include "shell.h"

static const Builtin table[] = {
    /* 쉘 상태를 바꾸는 명령은 부모(쉘)에서 직접 실행해야 한다 */
    { "exit",  cmd_exit,  1 },
    { "cd",    cmd_cd,    1 },
    { "jobs",  cmd_jobs,  1 },
    { "fg",    cmd_fg,    1 },
    { "bg",    cmd_bg,    1 },
    /* 나머지는 자식 프로세스에서 실행 -> 리다이렉션/파이프/시그널이 자연스럽게 동작 */
    { "ls",    cmd_ls,    0 },
    { "pwd",   cmd_pwd,   0 },
    { "mkdir", cmd_mkdir, 0 },
    { "rmdir", cmd_rmdir, 0 },
    { "ln",    cmd_ln,    0 },
    { "cp",    cmd_cp,    0 },
    { "rm",    cmd_rm,    0 },
    { "mv",    cmd_mv,    0 },
    { "cat",   cmd_cat,   0 },
    { "grep",  cmd_grep,  0 },
    { "echo",  cmd_echo,  0 },
    { "help",  cmd_help,  0 },
    { NULL, NULL, 0 }
};

const Builtin *find_builtin(const char *name)
{
    for (const Builtin *b = table; b->name; b++)
        if (strcmp(b->name, name) == 0) return b;
    return NULL;
}

/* exit [n] : 메인 루프를 끝내도록 표시한다 */
int cmd_exit(int argc, char **argv)
{
    int code = last_status;
    if (argc > 2) {
        fprintf(stderr, "exit: too many arguments\n");
        return 1;
    }
    if (argc == 2) {
        char *end;
        long v = strtol(argv[1], &end, 10);
        if (*end != '\0') {
            fprintf(stderr, "exit: %s: numeric argument required\n", argv[1]);
            code = 2;
        } else {
            code = (int)(v & 0xff);
        }
    }
    shell_exit_code = code;
    shell_running = 0;
    return code;
}

int cmd_jobs(int argc, char **argv)
{
    (void)argc; (void)argv;
    jobs_print();
    return 0;
}

/* "%2" 또는 "2" 형태의 작업 번호 파싱. 없으면 가장 최근 작업 */
static Job *parse_job_arg(const char *cmd, int argc, char **argv)
{
    int id = 0;
    if (argc > 1) {
        const char *s = argv[1];
        if (*s == '%') s++;
        if (!isdigit((unsigned char)*s)) {
            fprintf(stderr, "%s: %s: invalid job spec\n", cmd, argv[1]);
            return NULL;
        }
        id = atoi(s);
    }
    Job *j = job_find(id);
    if (!j) {
        if (argc > 1) fprintf(stderr, "%s: %s: no such job\n", cmd, argv[1]);
        else          fprintf(stderr, "%s: no current job\n", cmd);
    }
    return j;
}

int cmd_fg(int argc, char **argv)
{
    Job *j = parse_job_arg("fg", argc, argv);
    if (!j) return 1;
    printf("%s\n", j->cmdline);
    fflush(stdout);
    job_continue(j, 1);
    return last_status;
}

int cmd_bg(int argc, char **argv)
{
    Job *j = parse_job_arg("bg", argc, argv);
    if (!j) return 1;
    printf("[%d]+ %s &\n", j->id, j->cmdline);
    fflush(stdout);
    job_continue(j, 0);
    return 0;
}

int cmd_echo(int argc, char **argv)
{
    int i = 1, newline = 1;
    if (argc > 1 && strcmp(argv[1], "-n") == 0) { newline = 0; i = 2; }
    for (; i < argc; i++) {
        fputs(argv[i], stdout);
        if (i + 1 < argc) putchar(' ');
    }
    if (newline) putchar('\n');
    return 0;
}

int cmd_help(int argc, char **argv)
{
    (void)argc; (void)argv;
    puts("myshell 내장 명령");
    puts("  exit [n]                      쉘 종료");
    puts("  cd [dir|-]                    디렉터리 이동");
    puts("  pwd [-LP]                     현재 디렉터리 출력");
    puts("  ls [-alRhd1tr] [file...]      파일/디렉터리 목록");
    puts("  mkdir [-pv] [-m mode] dir...  디렉터리 생성");
    puts("  rmdir [-pv] dir...            빈 디렉터리 삭제");
    puts("  ln [-sfnv] target [link]      하드/심볼릭 링크 생성");
    puts("  cp [-rRfipvn] src... dest     복사");
    puts("  mv [-ifnv] src... dest        이동/이름 변경");
    puts("  rm [-rRfiv] file...           삭제");
    puts("  cat [-nbEs] [file...]         파일 내용 출력");
    puts("  grep [-inv cl rEFwxHh] pat [file...]  패턴 검색");
    puts("  jobs / fg [%n] / bg [%n]      작업 제어");
    puts("  echo [-n] ... / help          기타");
    puts("특수 기능: cmd &  (백그라운드)  |  (파이프)  <  >  >>  (재지향)");
    puts("          Ctrl-C(SIGINT)  Ctrl-Z(SIGTSTP)  Ctrl-\\(SIGQUIT)");
    return 0;
}
