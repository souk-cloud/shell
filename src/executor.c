/*
 * executor.c - 파이프라인 실행   [담당: 김소욱]
 *
 *   cmd1 < in | cmd2 | cmd3 > out &
 *
 *  1) 명령 개수만큼 fork 하고, pipe() 로 앞 명령의 stdout 을 뒤 명령의 stdin 에 연결
 *  2) 모든 자식을 하나의 프로세스 그룹(pgid)에 넣는다  -> Ctrl-C/Z 가 그룹 전체에 전달
 *  3) '<', '>', '>>' 는 자식에서 open + dup2 로 처리 (파이프보다 우선)
 *  4) 포그라운드면 터미널을 넘겨주고 대기, 백그라운드면 바로 복귀
 *  - cd/exit/jobs/fg/bg 는 쉘 상태를 바꾸므로 단독 실행일 때 fork 없이 직접 실행
 *  - 내장 명령이 아니면 execvp 로 외부 프로그램 실행 (sleep, vi, wc ...)
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include "executor.h"
#include "builtin.h"
#include "jobs.h"
#include "shell.h"

void reset_child_signals(void)
{
    signal(SIGINT,  SIG_DFL);
    signal(SIGQUIT, SIG_DFL);
    signal(SIGTSTP, SIG_DFL);
    signal(SIGTTIN, SIG_DFL);
    signal(SIGTTOU, SIG_DFL);
    signal(SIGCHLD, SIG_DFL);
}

/* '<' '>' '>>' 처리. 실패하면 -1 */
static int setup_redirection(const Command *c)
{
    if (c->infile) {
        int fd = open(c->infile, O_RDONLY);
        if (fd < 0) {
            fprintf(stderr, "myshell: %s: %s\n", c->infile, strerror(errno));
            return -1;
        }
        dup2(fd, STDIN_FILENO);
        close(fd);
    }
    if (c->outfile) {
        int flags = O_WRONLY | O_CREAT | (c->append ? O_APPEND : O_TRUNC);
        int fd = open(c->outfile, flags, 0666);
        if (fd < 0) {
            fprintf(stderr, "myshell: %s: %s\n", c->outfile, strerror(errno));
            return -1;
        }
        dup2(fd, STDOUT_FILENO);
        close(fd);
    }
    return 0;
}

/* 자식 프로세스 본체 (돌아오지 않음) */
static void run_child(Command *c, pid_t pgid, int in_fd, int out_fd,
                      int close_fd, int background, int first)
{
    pid_t me = getpid();
    if (pgid == 0) pgid = me;
    setpgid(0, pgid);
    if (!background && shell_interactive) tcsetpgrp(STDIN_FILENO, pgid);
    reset_child_signals();   /* tcsetpgrp 뒤에 해야 SIGTTOU 로 멈추지 않는다 */

    if (in_fd != STDIN_FILENO)   { dup2(in_fd, STDIN_FILENO);   close(in_fd); }
    if (out_fd != STDOUT_FILENO) { dup2(out_fd, STDOUT_FILENO); close(out_fd); }
    if (close_fd >= 0) close(close_fd);

    /* 백그라운드 작업이 터미널 입력을 가로채지 못하도록 */
    if (background && first && !c->infile) {
        int null = open("/dev/null", O_RDONLY);
        if (null >= 0) { dup2(null, STDIN_FILENO); close(null); }
    }

    if (setup_redirection(c) < 0) _exit(1);

    const Builtin *b = find_builtin(c->argv[0]);
    if (b) {
        int rc = b->fn(c->argc, c->argv);
        fflush(NULL);
        _exit(rc & 0xff);
    }

    execvp(c->argv[0], c->argv);
    if (errno == ENOENT) fprintf(stderr, "myshell: %s: command not found\n", c->argv[0]);
    else                 fprintf(stderr, "myshell: %s: %s\n", c->argv[0], strerror(errno));
    _exit(errno == ENOENT ? 127 : 126);
}

void execute_pipeline(Pipeline *p, const char *cmdline)
{
    const Builtin *b0 = find_builtin(p->cmds[0].argv[0]);

    /* 단독 실행되는 쉘 제어 명령 (cd, exit, jobs, fg, bg) */
    if (p->ncmds == 1 && b0 && b0->in_parent) {
        fflush(NULL);
        last_status = b0->fn(p->cmds[0].argc, p->cmds[0].argv);
        fflush(NULL);
        return;
    }

    fflush(NULL);   /* fork 전에 stdio 버퍼 비우기 (중복 출력 방지) */

    pid_t pids[MAX_CMDS];
    pid_t pgid = 0;
    int   prev_read = -1;
    int   n = p->ncmds;
    int   launched = 0;

    for (int i = 0; i < n; i++) {
        int fd[2] = { -1, -1 };
        if (i < n - 1 && pipe(fd) < 0) {
            perror("myshell: pipe");
            break;
        }

        pid_t pid = fork();
        if (pid < 0) {
            perror("myshell: fork");
            if (fd[0] >= 0) { close(fd[0]); close(fd[1]); }
            break;
        }
        if (pid == 0) {
            run_child(&p->cmds[i], pgid, prev_read >= 0 ? prev_read : STDIN_FILENO,
                      fd[1] >= 0 ? fd[1] : STDOUT_FILENO, fd[0],
                      p->background, i == 0);
        }

        /* 부모 */
        if (pgid == 0) pgid = pid;
        setpgid(pid, pgid);          /* 자식과의 경쟁 상태를 피하려고 양쪽에서 호출 */
        pids[launched++] = pid;
        if (prev_read >= 0) close(prev_read);
        if (fd[1] >= 0) close(fd[1]);
        prev_read = fd[0];
    }
    if (prev_read >= 0) close(prev_read);

    if (launched == 0) return;

    Job *j = job_add(pgid, cmdline, pids, launched, p->background);
    if (p->background) {
        printf("[%d] %d\n", j->id, (int)pids[launched - 1]);
        fflush(stdout);
        last_status = 0;
    } else {
        if (shell_interactive) tcsetpgrp(STDIN_FILENO, pgid);
        job_wait_fg(j);
    }
}
