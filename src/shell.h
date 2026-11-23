/* shell.h - 쉘 전역 상태 선언 */
#ifndef SHELL_H
#define SHELL_H

#include <sys/types.h>

extern int   shell_interactive;  /* stdin 이 터미널이면 1 */
extern pid_t shell_pgid;         /* 쉘 자신의 프로세스 그룹 ID */
extern int   shell_running;      /* 0 이 되면 메인 루프 종료 ("exit") */
extern int   shell_exit_code;    /* exit [n] 의 n */
extern int   last_status;        /* 마지막 포그라운드 작업의 종료 상태 */

#endif
