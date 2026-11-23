/*
 * parser.c - 명령행 파서   [담당: 김소욱]
 *
 * 1단계(lexer) : 문자열 -> 토큰 (WORD, |, <, >, >>, &)
 *                따옴표('...', "..."), 백슬래시 이스케이프 처리
 * 2단계(parser): 토큰 -> Pipeline (명령 배열 + 리다이렉션 + 백그라운드 여부)
 *                따옴표 없는 단어의 * ? [ 는 glob() 으로 확장
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <glob.h>
#include "parser.h"

typedef enum { T_WORD, T_PIPE, T_LT, T_GT, T_APPEND, T_AMP } TokType;

typedef struct {
    TokType type;
    char   *text;     /* T_WORD 일 때만 사용 */
    int     quoted;   /* 따옴표/이스케이프가 하나라도 있었으면 1 (glob 금지) */
} Token;

static int is_op_char(char c) { return c == '|' || c == '<' || c == '>' || c == '&'; }

static void free_tokens(Token *t, int n)
{
    for (int i = 0; i < n; i++) free(t[i].text);
    free(t);
}

/* 문자열을 토큰 배열로 변환. 실패 시 NULL */
static Token *tokenize(const char *s, int *ntok)
{
    int cap = 16, n = 0;
    Token *toks = malloc(cap * sizeof(Token));
    if (!toks) return NULL;

    while (*s) {
        if (isspace((unsigned char)*s)) { s++; continue; }

        if (n + 1 >= cap) {
            cap *= 2;
            toks = realloc(toks, cap * sizeof(Token));
        }
        Token *t = &toks[n];
        t->text = NULL; t->quoted = 0;

        if (is_op_char(*s)) {
            if (*s == '|')      { t->type = T_PIPE; s++; }
            else if (*s == '<') { t->type = T_LT;   s++; }
            else if (*s == '&') { t->type = T_AMP;  s++; }
            else {              /* '>' */
                if (s[1] == '>') { t->type = T_APPEND; s += 2; }
                else             { t->type = T_GT;     s++;    }
            }
            n++;
            continue;
        }

        /* 단어 읽기 */
        size_t wcap = 32, wlen = 0;
        char *w = malloc(wcap);
        while (*s && !isspace((unsigned char)*s) && !is_op_char(*s)) {
            char c = *s;
            if (wlen + 2 >= wcap) { wcap *= 2; w = realloc(w, wcap); }

            if (c == '\'') {                       /* '...' : 전부 문자 그대로 */
                const char *e = strchr(s + 1, '\'');
                if (!e) {
                    fprintf(stderr, "myshell: syntax error: unmatched '\n");
                    free(w); free_tokens(toks, n); return NULL;
                }
                size_t len = e - (s + 1);
                while (wlen + len + 2 >= wcap) { wcap *= 2; w = realloc(w, wcap); }
                memcpy(w + wlen, s + 1, len); wlen += len;
                s = e + 1; t->quoted = 1;
            } else if (c == '"') {                 /* "..." : \" \\ 만 이스케이프 */
                s++; t->quoted = 1;
                while (*s && *s != '"') {
                    if (wlen + 2 >= wcap) { wcap *= 2; w = realloc(w, wcap); }
                    if (*s == '\\' && (s[1] == '"' || s[1] == '\\')) s++;
                    w[wlen++] = *s++;
                }
                if (*s != '"') {
                    fprintf(stderr, "myshell: syntax error: unmatched \"\n");
                    free(w); free_tokens(toks, n); return NULL;
                }
                s++;
            } else if (c == '\\' && s[1]) {        /* \x : x 를 문자 그대로 */
                w[wlen++] = s[1]; s += 2; t->quoted = 1;
            } else {
                w[wlen++] = c; s++;
            }
        }
        w[wlen] = '\0';
        t->type = T_WORD; t->text = w;
        n++;
    }
    *ntok = n;
    return toks;
}

/* argv 에 인자 추가 (실패 시 -1) */
static int add_arg(Command *c, char *arg)
{
    if (c->argc >= MAX_ARGS) {
        fprintf(stderr, "myshell: too many arguments\n");
        free(arg);
        return -1;
    }
    c->argv[c->argc++] = arg;
    c->argv[c->argc] = NULL;
    return 0;
}

/* 와일드카드 확장 후 argv 에 추가 */
static int add_word(Command *c, const Token *t)
{
    if (!t->quoted && strpbrk(t->text, "*?[")) {
        glob_t g;
        if (glob(t->text, 0, NULL, &g) == 0) {
            for (size_t i = 0; i < g.gl_pathc; i++)
                if (add_arg(c, strdup(g.gl_pathv[i])) < 0) { globfree(&g); return -1; }
            globfree(&g);
            return 0;
        }
        globfree(&g);   /* 매치 없으면 문자 그대로 전달 */
    }
    return add_arg(c, strdup(t->text));
}

static int syntax_error(const char *near)
{
    fprintf(stderr, "myshell: syntax error near '%s'\n", near);
    return -1;
}

int parse_line(const char *line, Pipeline *p)
{
    int ntok = 0;
    memset(p, 0, sizeof(*p));

    Token *tok = tokenize(line, &ntok);
    if (!tok) return -1;
    if (ntok == 0) { free(tok); return 1; }

    Command *cur = &p->cmds[0];
    p->ncmds = 1;
    int rc = 0;

    for (int i = 0; i < ntok && rc == 0; i++) {
        Token *t = &tok[i];
        switch (t->type) {
        case T_WORD:
            if (add_word(cur, t) < 0) rc = -1;
            break;

        case T_PIPE:
            if (cur->argc == 0) { rc = syntax_error("|"); break; }
            if (p->ncmds >= MAX_CMDS) {
                fprintf(stderr, "myshell: too many commands in pipeline\n");
                rc = -1; break;
            }
            cur = &p->cmds[p->ncmds++];
            break;

        case T_LT: case T_GT: case T_APPEND: {
            const char *name = (t->type == T_LT) ? "<" : (t->type == T_GT) ? ">" : ">>";
            if (i + 1 >= ntok || tok[i + 1].type != T_WORD) {
                rc = syntax_error(i + 1 >= ntok ? "newline" : name);
                break;
            }
            char *file = strdup(tok[++i].text);
            if (t->type == T_LT) { free(cur->infile);  cur->infile  = file; }
            else {
                free(cur->outfile); cur->outfile = file;
                cur->append = (t->type == T_APPEND);
            }
            break;
        }

        case T_AMP:
            if (i != ntok - 1) { rc = syntax_error("&"); break; }
            p->background = 1;
            break;
        }
    }

    if (rc == 0 && cur->argc == 0) {
        if (p->ncmds > 1)       rc = syntax_error("newline");
        else if (cur->infile || cur->outfile || p->background)
                                rc = syntax_error("newline");
    }

    free_tokens(tok, ntok);
    if (rc < 0) pipeline_free(p);
    return rc;
}

void pipeline_free(Pipeline *p)
{
    for (int i = 0; i < p->ncmds; i++) {
        Command *c = &p->cmds[i];
        for (int j = 0; j < c->argc; j++) free(c->argv[j]);
        free(c->infile);
        free(c->outfile);
    }
    memset(p, 0, sizeof(*p));
}
