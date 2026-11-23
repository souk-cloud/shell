/* executor.h - 파이프라인 실행기 */
#ifndef EXECUTOR_H
#define EXECUTOR_H

#include "parser.h"

/* 파싱된 파이프라인을 실행한다. cmdline 은 작업 목록에 표시할 문자열. */
void execute_pipeline(Pipeline *p, const char *cmdline);

/* 자식 프로세스에서 시그널 처분을 기본값으로 되돌린다 */
void reset_child_signals(void);

#endif
