/* parser.h - 명령행 파서 (토큰화 + 파이프라인 구조 생성) */
#ifndef PARSER_H
#define PARSER_H

#define MAX_ARGS 256
#define MAX_CMDS 32

typedef struct {
    char *argv[MAX_ARGS + 1];
    int   argc;
    char *infile;      /* '<'  뒤의 파일명 */
    char *outfile;     /* '>' 또는 '>>' 뒤의 파일명 */
    int   append;      /* '>>' 이면 1 */
} Command;

typedef struct {
    Command cmds[MAX_CMDS];
    int     ncmds;
    int     background;   /* 마지막에 '&' 가 있으면 1 */
} Pipeline;

/* 반환: 0 = 성공, 1 = 빈 입력, -1 = 문법 오류(메시지는 stderr 로 출력됨) */
int  parse_line(const char *line, Pipeline *p);
void pipeline_free(Pipeline *p);

#endif
