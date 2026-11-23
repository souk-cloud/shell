/*
 * main.c - 쉘 메인 루프   [담당: 김소욱]
 *   프롬프트 출력 -> 한 줄 읽기 -> 파싱 -> 실행 을 "exit" 또는 EOF(Ctrl-D) 까지 반복
 *
 * 시그널 정책
 *   쉘 자신 : SIGINT 는 핸들러(줄바꿈 후 새 프롬프트), SIGQUIT/SIGTSTP/SIGTTIN/SIGTTOU 는 무시
 *             -> 쉘은 Ctrl-C, Ctrl-\, Ctrl-Z 로 죽거나 멈추지 않는다
 *   자식    : 모두 기본 동작으로 복원 -> 실행 중인 프로그램만 종료/정지된다
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <locale.h>
#include <ctype.h>
#include "shell.h"
#include "parser.h"
#include "executor.h"
#include "jobs.h"
#include "util.h"

#define LINE_MAX_LEN 8192

int   shell_interactive = 0;
pid_t shell_pgid = 0;
int   shell_running = 1;
int   shell_exit_code = 0;
int   last_status = 0;

static Pipeline pl;   /* 66KB 정도라 정적으로 둔다 */

static void sigint_handler(int sig)
{
    (void)sig;
    ssize_t r = write(STDOUT_FILENO, "\n", 1);   /* ^C 뒤에 줄바꿈 */
    (void)r;
}

static void init_shell(void)
{
    setlocale(LC_ALL, "");
    shell_interactive = isatty(STDIN_FILENO);

    if (shell_interactive) {
        /* 포그라운드가 될 때까지 대기 */
        while (tcgetpgrp(STDIN_FILENO) != (shell_pgid = getpgrp()))
            kill(-shell_pgid, SIGTTIN);
    }

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sigemptyset(&sa.sa_mask);
    sa.sa_handler = sigint_handler;   /* SA_RESTART 를 쓰지 않아야 read() 가 EINTR 로 깨어난다 */
    sigaction(SIGINT, &sa, NULL);

    signal(SIGQUIT, SIG_IGN);
    signal(SIGTSTP, SIG_IGN);
    signal(SIGTTIN, SIG_IGN);
    signal(SIGTTOU, SIG_IGN);

    if (shell_interactive) {
        shell_pgid = getpid();
        if (setpgid(shell_pgid, shell_pgid) < 0 && errno != EPERM)
            perror("myshell: setpgid");
        tcsetpgrp(STDIN_FILENO, shell_pgid);
    }
}

static void print_prompt(void)
{
    char cwd[4096];
    if (!getcwd(cwd, sizeof(cwd))) strcpy(cwd, "?");

    const char *home = getenv("HOME");
    char shown[4200];
    size_t hl = home ? strlen(home) : 0;
    if (hl > 1 && strncmp(cwd, home, hl) == 0 && (cwd[hl] == '/' || cwd[hl] == '\0'))
        snprintf(shown, sizeof(shown), "~%s", cwd + hl);
    else
        snprintf(shown, sizeof(shown), "%s", cwd);

    printf("\033[1;32mmyshell\033[0m:\033[1;34m%s\033[0m$ ", shown);
    fflush(stdout);
}

/* 앞뒤 공백 제거 (제자리) */
static char *trim(char *s)
{
    while (isspace((unsigned char)*s)) s++;
    size_t n = strlen(s);
    while (n > 0 && isspace((unsigned char)s[n - 1])) s[--n] = '\0';
    return s;
}

int main(void)
{
    char line[LINE_MAX_LEN];

    init_shell();

    while (shell_running) {
        jobs_update_and_report();          /* 끝난 백그라운드 작업 알림 */
        if (shell_interactive) print_prompt();

        ssize_t n = read_line_fd(STDIN_FILENO, line, sizeof(line));
        if (n == -2) continue;             /* Ctrl-C: 핸들러가 줄바꿈 출력, 프롬프트 다시 */
        if (n == -1) {                     /* EOF (Ctrl-D) */
            if (shell_interactive) printf("exit\n");
            shell_exit_code = last_status;
            break;
        }

        char *cmd = trim(line);
        if (*cmd == '\0') continue;

        int rc = parse_line(cmd, &pl);
        if (rc < 0) { last_status = 2; continue; }
        if (rc > 0) continue;

        /* 작업 목록에 보일 문자열: 끝의 '&' 는 제거 */
        char text[LINE_MAX_LEN];
        snprintf(text, sizeof(text), "%s", cmd);
        if (pl.background) {
            size_t len = strlen(text);
            if (len > 0 && text[len - 1] == '&') text[--len] = '\0';
            while (len > 0 && isspace((unsigned char)text[len - 1])) text[--len] = '\0';
        }

        execute_pipeline(&pl, text);
        pipeline_free(&pl);

        if (shell_running == 0 && shell_interactive) printf("exit\n");
    }

    jobs_cleanup();
    return shell_exit_code;
}
