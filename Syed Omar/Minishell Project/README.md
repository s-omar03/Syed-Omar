# Mini Shell (msh)

A Unix-style shell written from scratch in C using Linux system calls.

## Features
- Runs commands with `fork` and `exec`
- **Pipelines of any length**, for example `ls -l /dev | grep tty | wc -l`, built from pipes created at run time
- **Job control:** run in the background with `&`, and use `jobs`, `fg` and `bg`
- **Signals:** `Ctrl-C` interrupts the foreground job, `Ctrl-Z` stops it, and a `SIGCHLD` handler reaps finished background jobs and prints their exit status
- **Built-ins:** `cd`, `pwd`, `jobs`, `fg`, `bg`, `exit`
- **Special variables:** `$?` (last exit status), `$$` (shell PID), `$SHELL`
- **Custom prompt:** `PS1=NEW_PROMPT` (no spaces around `=`)
- `exit` cleans up child processes so none are left behind

## Build and run
```bash
make
./msh
make clean
```

## Example session
```
msh$ ls -l | wc -l
msh$ sleep 30 &
msh$ jobs
msh$ fg
msh$ echo $?
msh$ PS1=myshell>
```

## Files
| File | Purpose |
|------|---------|
| `minishell.c`, `minishell.h` | Main loop, parsing, built-ins, pipelines, signal handlers |
| `job_control.c` | Job table, background/foreground states, `SIGCHLD` reaping |

## Concepts used
C, process creation (`fork`, `exec`, `wait`), pipes, process groups, signals (`SIGINT`, `SIGTSTP`, `SIGCHLD`), terminal control.
