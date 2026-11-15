#ifndef MINISHELL_H
#define MINISHELL_H

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>
#include <limits.h>
#include <ctype.h>

// Define constants
#define MAX_INPUT_SIZE 1024
#define MAX_ARGS 64
#define MAX_JOBS 64
#define MAX_STAGES 32
#define PROMPT_SIZE 128

// Job states
enum { JOB_RUNNING, JOB_STOPPED, JOB_FOREGROUND, JOB_DONE };

// Job control structures
// A "job" is one command line (a single command or a whole pipeline).
// pid is the process-group id (= pid of the first process of the job).
typedef struct Job {
    int job_id;
    pid_t pid;                      // process group id of the job
    pid_t last_pid;                 // last process of a pipeline (gives exit status)
    int state;                      // JOB_RUNNING / JOB_STOPPED / ...
    int exit_status;                // filled in when the job finishes
    char command[MAX_INPUT_SIZE];
    struct Job *next;
} Job;

// Shared state
extern Job *job_list;
extern int next_job_id;
extern int last_status;                       // value of $?
extern pid_t shell_pid, shell_pgid;           // value of $$
extern char shell_path[PATH_MAX];             // value of $SHELL
extern char prompt[PROMPT_SIZE];              // PS1
extern int interactive;
extern volatile sig_atomic_t fg_pgid;         // 0 => no foreground job
extern int run_in_background;                 // set by parse_command() when line ends with &

// Function declarations
void init_shell();
void main_loop();
void parse_command(char *input, char **args);
int execute_command(char **args);
int is_builtin_command(char **args);
void handle_builtin_command(char **args);
void handle_redirection_and_piping(char **args);
void signal_handler(int sig);
void init_signal_handlers();

// job_control.c
Job *add_job(pid_t pid, char *command);
void remove_job(pid_t pid);
void list_jobs();
void bring_job_to_foreground(int pid);        // pid == 0 -> most recent job
void send_job_to_background(int pid);         // pid == 0 -> most recent stopped job
Job *find_job(pid_t pid, int state);          // state < 0 -> any state
void block_sigchld(sigset_t *old);
void remove_finished_jobs();
void sigchld_handler(int sig);
void kill_all_jobs();

// minishell.c
void wait_foreground(pid_t pgid, pid_t last_pid, const char *cmd, Job *existing);
int status_code(int status);

#endif // MINISHELL_H
