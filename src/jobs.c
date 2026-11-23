/*
 * jobs.c - 작업 제어   [담당: 전승준]
 *
 * 하나의 명령행(파이프라인)은 하나의 프로세스 그룹 = 하나의 Job 이다.
 *   - 포그라운드 : 터미널을 넘겨주고 waitpid(WUNTRACED) 로 대기
 *   - 백그라운드 : 기다리지 않고 프롬프트로 복귀, 다음 프롬프트 직전에 수거
 *   - Ctrl-Z     : SIGTSTP 로 정지 -> Stopped 상태로 기록, fg / bg 로 재개
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/wait.h>
#include "jobs.h"
#include "shell.h"

static Job *head = NULL;

Job *job_add(pid_t pgid, const char *cmd, const pid_t *pids, int n, int bg)
{
    Job *j = calloc(1, sizeof(Job));
    j->pgid = pgid;
    j->cmdline = strdup(cmd);
    j->nprocs = n;
    j->background = bg;
    for (int i = 0; i < n; i++) {
        j->procs[i].pid = pids[i];
        j->procs[i].state = P_RUNNING;
    }
    int id = 1;
    Job *tail = NULL;
    for (Job *k = head; k; k = k->next) {
        if (k->id >= id) id = k->id + 1;
        tail = k;
    }
    j->id = id;
    if (tail) tail->next = j; else head = j;
    return j;
}

static void job_remove(Job *j)
{
    Job **pp = &head;
    while (*pp && *pp != j) pp = &(*pp)->next;
    if (*pp) *pp = j->next;
    free(j->cmdline);
    free(j);
}

Job *job_find(int id)
{
    Job *last = NULL;
    for (Job *k = head; k; k = k->next) {
        if (id > 0 && k->id == id) return k;
        last = k;
    }
    return id > 0 ? NULL : last;
}

static int job_done(const Job *j)
{
    for (int i = 0; i < j->nprocs; i++)
        if (j->procs[i].state != P_DONE) return 0;
    return 1;
}

static int job_running(const Job *j)
{
    for (int i = 0; i < j->nprocs; i++)
        if (j->procs[i].state == P_RUNNING) return 1;
    return 0;
}

static int job_stopped(const Job *j) { return !job_done(j) && !job_running(j); }

static Proc *find_proc(pid_t pid, Job **jout)
{
    for (Job *j = head; j; j = j->next)
        for (int i = 0; i < j->nprocs; i++)
            if (j->procs[i].pid == pid) { *jout = j; return &j->procs[i]; }
    return NULL;
}

static void update_proc(Proc *p, int st)
{
    if (WIFSTOPPED(st)) {
        p->state = P_STOPPED;
    } else {
        p->state = P_DONE;
        p->status = st;
    }
}

static void last_status_from(const Job *j)
{
    int st = j->procs[j->nprocs - 1].status;
    if (WIFEXITED(st))        last_status = WEXITSTATUS(st);
    else if (WIFSIGNALED(st)) last_status = 128 + WTERMSIG(st);
}

/* 시그널로 죽은 프로세스를 사용자에게 알린다 (Ctrl-C 는 줄바꿈만) */
static void report_signals(const Job *j)
{
    int newline = 0;
    for (int i = 0; i < j->nprocs; i++) {
        int st = j->procs[i].status;
        if (j->procs[i].state != P_DONE || !WIFSIGNALED(st)) continue;
        int sig = WTERMSIG(st);
        if (sig == SIGPIPE) continue;
        if (sig == SIGINT) {
            if (!newline) { putchar('\n'); newline = 1; }
            continue;
        }
        if (!newline) { putchar('\n'); newline = 1; }
        printf("%s%s\n", strsignal(sig), WCOREDUMP(st) ? " (core dumped)" : "");
    }
    fflush(stdout);
}

void job_wait_fg(Job *j)
{
    while (job_running(j)) {
        int st;
        pid_t pid = waitpid(-j->pgid, &st, WUNTRACED);
        if (pid < 0) {
            if (errno == EINTR) continue;
            for (int i = 0; i < j->nprocs; i++)       /* ECHILD 등: 더 기다릴 자식 없음 */
                if (j->procs[i].state == P_RUNNING) j->procs[i].state = P_DONE;
            break;
        }
        for (int i = 0; i < j->nprocs; i++)
            if (j->procs[i].pid == pid) update_proc(&j->procs[i], st);
    }

    if (shell_interactive) tcsetpgrp(STDIN_FILENO, shell_pgid);   /* 터미널 회수 */

    if (job_done(j)) {
        last_status_from(j);
        report_signals(j);
        job_remove(j);
    } else if (job_stopped(j)) {
        j->background = 1;       /* 이후 bg/fg 로만 제어 */
        j->notified = 1;
        last_status = 128 + SIGTSTP;
        printf("\n[%d]+  Stopped                 %s\n", j->id, j->cmdline);
        fflush(stdout);
    }
}

void job_continue(Job *j, int fg)
{
    for (int i = 0; i < j->nprocs; i++)
        if (j->procs[i].state == P_STOPPED) j->procs[i].state = P_RUNNING;
    j->notified = 0;
    j->background = !fg;

    if (fg) {
        if (shell_interactive) tcsetpgrp(STDIN_FILENO, j->pgid);
        kill(-j->pgid, SIGCONT);
        job_wait_fg(j);
    } else {
        kill(-j->pgid, SIGCONT);
    }
}

static void describe_done(const Job *j, char *buf, size_t n)
{
    int st = j->procs[j->nprocs - 1].status;
    if (WIFSIGNALED(st))                     snprintf(buf, n, "%s", strsignal(WTERMSIG(st)));
    else if (WIFEXITED(st) && WEXITSTATUS(st)) snprintf(buf, n, "Exit %d", WEXITSTATUS(st));
    else                                     snprintf(buf, n, "Done");
}

void jobs_update_and_report(void)
{
    int st;
    pid_t pid;
    while ((pid = waitpid(-1, &st, WNOHANG | WUNTRACED | WCONTINUED)) > 0) {
        Job *j;
        Proc *p = find_proc(pid, &j);
        if (!p) continue;
        if (WIFCONTINUED(st)) { p->state = P_RUNNING; j->notified = 0; }
        else update_proc(p, st);
    }

    for (Job *j = head, *next; j; j = next) {
        next = j->next;
        if (job_done(j)) {
            if (j->background) {
                char d[64];
                describe_done(j, d, sizeof(d));
                printf("[%d]+  %-22s %s\n", j->id, d, j->cmdline);
                fflush(stdout);
            }
            job_remove(j);
        } else if (job_stopped(j) && !j->notified) {
            printf("[%d]+  Stopped                 %s\n", j->id, j->cmdline);
            fflush(stdout);
            j->notified = 1;
        }
    }
}

void jobs_print(void)
{
    Job *cur = job_find(0);
    for (Job *j = head; j; j = j->next) {
        const char *state = job_stopped(j) ? "Stopped" : "Running";
        printf("[%d]%c  %-22s %s%s\n", j->id, j == cur ? '+' : ' ', state, j->cmdline,
               job_stopped(j) ? "" : " &");
    }
}

void jobs_cleanup(void)
{
    for (Job *j = head; j; j = j->next) {
        kill(-j->pgid, SIGHUP);
        kill(-j->pgid, SIGCONT);
    }
}
