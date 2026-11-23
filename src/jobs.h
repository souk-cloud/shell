/* jobs.h - 작업(job) 관리: 포그라운드/백그라운드/정지 상태 추적 */
#ifndef JOBS_H
#define JOBS_H

#include <sys/types.h>
#include "parser.h"

typedef enum { P_RUNNING, P_STOPPED, P_DONE } ProcState;

typedef struct {
    pid_t     pid;
    ProcState state;
    int       status;      /* waitpid 상태값 */
} Proc;

typedef struct Job {
    int         id;
    pid_t       pgid;
    char       *cmdline;
    Proc        procs[MAX_CMDS];
    int         nprocs;
    int         background;
    int         notified;  /* 정지 사실을 이미 알렸는가 */
    struct Job *next;
} Job;

Job *job_add(pid_t pgid, const char *cmd, const pid_t *pids, int n, int bg);
Job *job_find(int id);          /* id <= 0 이면 가장 최근 작업 */
void job_wait_fg(Job *j);       /* 포그라운드 대기 (Ctrl-Z 로 정지되면 복귀) */
void job_continue(Job *j, int fg);
void jobs_update_and_report(void); /* 종료/정지된 자식 수거 후 상태 알림 */
void jobs_print(void);          /* jobs 명령 */
void jobs_cleanup(void);        /* 쉘 종료 시 남은 작업 정리 */

#endif
