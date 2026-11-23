# shell (myshell) — 리눅스 시스템 프로그래밍으로 만든 쉘

오픈소스 프로젝트(전공설계) 팀 프로젝트. C 언어와 리눅스 시스템 콜(`fork`, `execvp`, `pipe`, `dup2`,
`waitpid`, `setpgid`, `tcsetpgrp`, `sigaction` …)만으로 구현한 간이 쉘입니다.

## 빌드 & 실행
```bash
sudo apt install build-essential     # gcc, make (Ubuntu)
make                                  # ./myshell 생성
./myshell
make test                             # 자동 테스트 (GNU 명령과 비교 97개 + 시그널 27개)
```

## 구현 기능
| # | 요구사항 | 구현 |
|---|----------|------|
| 1 | `exit` 로 종료 | `exit [n]`, Ctrl-D(EOF) 도 종료 |
| 2 | `&` 백그라운드 | `cmd &`, `jobs`, `fg [%n]`, `bg [%n]`, 종료 시 `Done` 알림 |
| 3 | 인터럽트 키 | Ctrl-C(SIGINT), Ctrl-Z(SIGTSTP), Ctrl-\(SIGQUIT) — 쉘은 살아있고 포그라운드 작업만 영향 |
| 4 | 재지향/파이프 | `<`  `>`  `>>`  `\|` (여러 단 파이프, 재지향과 조합 가능) |
| 5 | 내장 명령 11개 | `ls pwd cd mkdir rmdir ln cp rm mv cat grep` (직접 구현) |
| + | 부가 기능 | 따옴표/이스케이프, `*` `?` 와일드카드, 외부 명령 실행(`sleep`, `vi` …), `echo`, `help` |

> 참고: 과제 문서에는 "SIGQUIT: Ctrl-Z" 로 적혀 있으나 실제 터미널에서 Ctrl-Z 는 **SIGTSTP**(정지),
> SIGQUIT 는 **Ctrl-\\** 입니다. 두 키 모두 bash 와 동일하게 동작하도록 구현했습니다.

## 소스 구조 (`src/`)
| 파일 | 역할 |
|------|------|
| `main.c` | 메인 루프, 프롬프트, 시그널 초기화 |
| `parser.c/.h` | 토큰화(따옴표/이스케이프), 파이프라인 구조 생성, 와일드카드 |
| `executor.c/.h` | fork / pipe / dup2 / 리다이렉션 / 프로세스 그룹 |
| `jobs.c/.h` | 작업 목록, 포그라운드 대기, 정지/재개, 백그라운드 수거 |
| `builtin.c/.h` | 내장 명령 테이블, exit · jobs · fg · bg · echo · help |
| `util.c/.h` | 경로 함수, 복사/삭제 공용 함수 (cp·mv·rm 공유) |
| `cmd_*.c` | 파일 명령 11개 (1명령 = 1파일) |

## 역할 분담 (팀원 2명)
| 담당 | 명령 | 공통 기능 |
|------|------|-----------|
| 김소욱 | `ls` `pwd` `cd` `ln` `rmdir` (5개) | 메인 루프 · 파서 · 프로세스 실행(파이프/재지향) · 내장 명령 테이블 · `exit` |
| 전승준 | `mkdir` `cp` `rm` `mv` `cat` `grep` (6개) | 시그널 · 프로세스 그룹 · 작업 제어(`&`, `jobs/fg/bg`) · 공용 `util.c` · 자동 테스트 |

코드량은 두 사람 모두 약 1,260~1,300줄로 균형을 맞췄습니다.

## GitHub 협업 방법
```bash
git clone https://github.com/souk-cloud/shell.git && cd shell
git checkout -b feature/<이름>-<기능>        # 예: feature/kim-ls
git add src/cmd_ls.c && git commit -m "feat(ls): -l, -R 옵션 구현"
git push -u origin feature/<이름>-<기능>     # GitHub 에서 Pull Request → 팀원 리뷰 → main 병합
```
커밋 메시지 규칙: `feat(범위): 설명`, `fix(범위): 설명`, `test: 설명`, `docs: 설명`

## 라이선스
[MIT License](LICENSE) — 자유롭게 사용·복사·수정·배포할 수 있습니다. © 2026 김소욱, 전승준
