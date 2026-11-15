#include "minishell.h"

// Global job list
Job *job_list = NULL;
int next_job_id = 1;

int last_status = 0;
pid_t shell_pid, shell_pgid;
char shell_path[PATH_MAX];
char prompt[PROMPT_SIZE] = "msh> ";
int interactive = 0;
volatile sig_atomic_t fg_pgid = 0;
int run_in_background = 0;

static char cmd_text[MAX_INPUT_SIZE];       // the command line as typed (for jobs list)

int main() {
    init_shell();
    main_loop();
    return 0;
}

void init_shell() {
    // Initialize the shell (e.g., set up environment, signal handlers)
    shell_pid = getpid();
    interactive = isatty(STDIN_FILENO);

    ssize_t n = readlink("/proc/self/exe", shell_path, sizeof shell_path - 1);
    if (n > 0) shell_path[n] = '\0';
    else       strcpy(shell_path, "msh");

    if (interactive) {
        // become the leader of our own group and own the terminal
        while (tcgetpgrp(STDIN_FILENO) != (shell_pgid = getpgrp()))
            kill(-shell_pgid, SIGTTIN);
        setpgid(shell_pid, shell_pid);
        shell_pgid = shell_pid;
        tcsetpgrp(STDIN_FILENO, shell_pgid);
    } else {
        shell_pgid = getpgrp();
    }
    init_signal_handlers();
}

// PS1=NEW_PROMPT  (no whitespace around '='; "PS1 = x" is an ordinary command)
static int try_set_prompt(const char *line) {
    if (strncmp(line, "PS1=", 4) != 0 || line[4] == '\0')
        return 0;
    snprintf(prompt, sizeof prompt, "%s", line + 4);
    return 1;
}

void main_loop() {
    char input[MAX_INPUT_SIZE];
    char *args[MAX_ARGS];

    while (1) {
        remove_finished_jobs();
        printf("%s", prompt);
        fflush(stdout);

        if (fgets(input, sizeof(input), stdin) == NULL) {
            if (feof(stdin)) {
                putchar('\n');
                kill_all_jobs();
                exit(last_status); // End of file (Ctrl+D)
            }
            clearerr(stdin);
            continue;
        }

        // trim whitespace / newline
        input[strcspn(input, "\n")] = '\0';
        char *line = input;
        while (isspace((unsigned char)*line)) line++;
        size_t len = strlen(line);
        while (len && isspace((unsigned char)line[len - 1])) line[--len] = '\0';
        if (len == 0) continue;             // empty input: just show the prompt again

        if (try_set_prompt(line)) continue;

        snprintf(cmd_text, sizeof cmd_text, "%s", line);
        parse_command(line, args);
        if (args[0] != NULL) {
            execute_command(args);
        }
    }
}

// Tokenize the input string into arguments and store in args.
// - handles 'single' and "double" quotes
// - '|' is always returned as its own token
// - a trailing '&' is removed and recorded in run_in_background
// - $? $$ $SHELL (whole words) are expanded
void parse_command(char *input, char **args) {
    static char buf[MAX_ARGS][PATH_MAX];    // storage for tokens
    int n = 0;
    char *s = input;

    run_in_background = 0;
    while (*s && n < MAX_ARGS - 1) {
        while (isspace((unsigned char)*s)) s++;
        if (!*s) break;

        char *out = buf[n];
        size_t cap = sizeof buf[n] - 1;

        if (*s == '|') {
            *out++ = '|';
            s++;
        } else {
            char quote = 0;
            while (*s && (quote || (!isspace((unsigned char)*s) && *s != '|'))) {
                if (quote) {
                    if (*s == quote) quote = 0;
                    else if ((size_t)(out - buf[n]) < cap) *out++ = *s;
                } else if (*s == '"' || *s == '\'') {
                    quote = *s;
                } else if ((size_t)(out - buf[n]) < cap) {
                    *out++ = *s;
                }
                s++;
            }
        }
        *out = '\0';
        args[n] = buf[n];
        n++;
    }
    args[n] = NULL;

    // trailing '&'
    if (n > 0 && strcmp(args[n - 1], "&") == 0) {
        run_in_background = 1;
        args[--n] = NULL;
    } else if (n > 0) {
        size_t l = strlen(args[n - 1]);
        if (l > 1 && args[n - 1][l - 1] == '&') {   // e.g. "sleep 5&"
            args[n - 1][l - 1] = '\0';
            run_in_background = 1;
        }
    }

    // special variables
    for (int i = 0; i < n; i++) {
        if (strcmp(args[i], "$?") == 0)
            snprintf(args[i], PATH_MAX, "%d", last_status);
        else if (strcmp(args[i], "$$") == 0)
            snprintf(args[i], PATH_MAX, "%d", (int)shell_pid);
        else if (strcmp(args[i], "$SHELL") == 0)
            snprintf(args[i], PATH_MAX, "%s", shell_path);
    }
}

int execute_command(char **args) {
    // built-ins run inside the shell itself (only when not part of a pipeline)
    int has_pipe = 0;
    for (int i = 0; args[i]; i++)
        if (strcmp(args[i], "|") == 0) has_pipe = 1;

    if (!has_pipe && is_builtin_command(args)) {
        handle_builtin_command(args);
    } else {
        handle_redirection_and_piping(args);
    }
    return 0;
}

int is_builtin_command(char **args) {
    // Check if the command is a built-in command (e.g., cd, exit, fg, bg)
    if (strcmp(args[0], "cd") == 0 || strcmp(args[0], "exit") == 0 ||
        strcmp(args[0], "fg") == 0 || strcmp(args[0], "bg") == 0 ||
        strcmp(args[0], "jobs") == 0 || strcmp(args[0], "pwd") == 0) {
        return 1;
    }
    return 0;
}

void handle_builtin_command(char **args) {
    // Handle built-in commands
    // cd, exit, fg, bg, jobs etc
    if (strcmp(args[0], "exit") == 0) {
        kill_all_jobs();
        exit(args[1] ? atoi(args[1]) : last_status);
    } else if (strcmp(args[0], "cd") == 0) {
        const char *dir = args[1] ? args[1] : getenv("HOME");
        if (!dir || chdir(dir) != 0) {
            fprintf(stderr, "msh: cd: %s: %s\n", dir ? dir : "(HOME not set)", strerror(errno));
            last_status = 1;
        } else {
            last_status = 0;
        }
    } else if (strcmp(args[0], "pwd") == 0) {
        char cwd[PATH_MAX];
        if (getcwd(cwd, sizeof cwd)) { puts(cwd); last_status = 0; }
        else { perror("msh: pwd"); last_status = 1; }
    } else if (strcmp(args[0], "jobs") == 0) {
        list_jobs();
        last_status = 0;
    } else if (strcmp(args[0], "fg") == 0) {
        bring_job_to_foreground(args[1] ? atoi(args[1]) : 0);
    } else if (strcmp(args[0], "bg") == 0) {
        send_job_to_background(args[1] ? atoi(args[1]) : 0);
    }
}

// Apply < > >> found in a stage's argument list, and strip them from argv.
static int apply_redirections(char **argv) {
    int w = 0;
    for (int i = 0; argv[i]; i++) {
        int fd = -1, target = -1;
        if (strcmp(argv[i], "<") == 0)       { target = STDIN_FILENO;  fd = open(argv[i + 1] ? argv[i + 1] : "", O_RDONLY); }
        else if (strcmp(argv[i], ">") == 0)  { target = STDOUT_FILENO; fd = open(argv[i + 1] ? argv[i + 1] : "", O_WRONLY | O_CREAT | O_TRUNC, 0644); }
        else if (strcmp(argv[i], ">>") == 0) { target = STDOUT_FILENO; fd = open(argv[i + 1] ? argv[i + 1] : "", O_WRONLY | O_CREAT | O_APPEND, 0644); }
        else { argv[w++] = argv[i]; continue; }

        if (!argv[i + 1] || fd < 0) {
            fprintf(stderr, "msh: %s: %s\n", argv[i + 1] ? argv[i + 1] : "redirection",
                    argv[i + 1] ? strerror(errno) : "missing file name");
            return -1;
        }
        dup2(fd, target);
        close(fd);
        i++;                                // skip the file name
    }
    argv[w] = NULL;
    return 0;
}

// Handle redirection and piping: create one child per pipeline stage and
// connect them with pipes created dynamically.
void handle_redirection_and_piping(char **args) {
    char **stage[MAX_STAGES];
    int nstage = 0;

    // split args on "|" tokens
    stage[nstage++] = args;
    for (int i = 0; args[i]; i++) {
        if (strcmp(args[i], "|") == 0) {
            args[i] = NULL;
            if (nstage >= MAX_STAGES) { fprintf(stderr, "msh: too many pipes\n"); last_status = 2; return; }
            stage[nstage++] = &args[i + 1];
        }
    }
    for (int i = 0; i < nstage; i++) {
        if (stage[i][0] == NULL) {
            fprintf(stderr, "msh: syntax error near '|'\n");
            last_status = 2;
            return;
        }
    }

    sigset_t old;
    block_sigchld(&old);                    // don't let the handler race with fork/add_job

    int prev_read = -1;
    pid_t pgid = 0, pid = 0;

    for (int i = 0; i < nstage; i++) {
        int fd[2] = { -1, -1 };
        if (i < nstage - 1 && pipe(fd) < 0) { perror("msh: pipe"); break; }

        pid = fork();
        if (pid < 0) { perror("msh: fork"); break; }

        if (pid == 0) {                     // ---- child ----
            setpgid(0, pgid);               // pgid == 0 -> new group
            signal(SIGINT,  SIG_DFL);
            signal(SIGTSTP, SIG_DFL);
            signal(SIGTTOU, SIG_DFL);
            signal(SIGTTIN, SIG_DFL);
            signal(SIGCHLD, SIG_DFL);
            sigset_t none; sigemptyset(&none);
            sigprocmask(SIG_SETMASK, &none, NULL);

            if (prev_read != -1) { dup2(prev_read, STDIN_FILENO); close(prev_read); }
            if (fd[1] != -1)     { dup2(fd[1], STDOUT_FILENO); close(fd[1]); close(fd[0]); }

            if (apply_redirections(stage[i]) < 0) _exit(1);
            if (stage[i][0] == NULL) _exit(0);

            if (is_builtin_command(stage[i])) {     // e.g. "pwd | cat"
                handle_builtin_command(stage[i]);
                _exit(last_status);
            }
            execvp(stage[i][0], stage[i]);
            fprintf(stderr, "msh: %s: %s\n", stage[i][0],
                    errno == ENOENT ? "command not found" : strerror(errno));
            _exit(errno == ENOENT ? 127 : 126);
        }

        // ---- parent ----
        if (pgid == 0) pgid = pid;
        setpgid(pid, pgid);                 // also done in child: avoids a race
        if (prev_read != -1) close(prev_read);
        if (fd[1] != -1)     close(fd[1]);
        prev_read = fd[0];
    }
    if (prev_read != -1) close(prev_read);

    if (pgid == 0) {                        // nothing was started
        sigprocmask(SIG_SETMASK, &old, NULL);
        last_status = 1;
        return;
    }

    if (run_in_background) {
        Job *j = add_job(pgid, cmd_text);
        if (j) j->last_pid = pid;
        printf("[%d] %d\n", j ? j->job_id : 0, (int)pgid);
        last_status = 0;
        sigprocmask(SIG_SETMASK, &old, NULL);
    } else {
        sigprocmask(SIG_SETMASK, &old, NULL);
        wait_foreground(pgid, pid, cmd_text, NULL);
    }
}

int status_code(int st) {
    if (WIFEXITED(st))   return WEXITSTATUS(st);
    if (WIFSIGNALED(st)) return 128 + WTERMSIG(st);
    if (WIFSTOPPED(st))  return 128 + WSTOPSIG(st);
    return 0;
}

// Give the terminal to the job and wait until it exits or is stopped (Ctrl-Z).
void wait_foreground(pid_t pgid, pid_t last_pid, const char *cmd, Job *existing) {
    int st;
    fg_pgid = pgid;
    if (interactive) tcsetpgrp(STDIN_FILENO, pgid);

    for (;;) {
        pid_t r = waitpid(-pgid, &st, WUNTRACED);
        if (r == -1) {
            if (errno == EINTR) continue;
            break;                          // ECHILD: every process has finished
        }
        if (WIFSTOPPED(st)) {
            int tmp;                        // drain stop reports of the other stages
            while (waitpid(-pgid, &tmp, WUNTRACED | WNOHANG) > 0) ;
            Job *j = existing;
            if (j) j->state = JOB_STOPPED;
            else {
                j = add_job(pgid, (char *)cmd);
                if (j) { j->last_pid = last_pid; j->state = JOB_STOPPED; }
            }
            printf("\n[%d]+ Stopped  %s  (pid %d)\n", j ? j->job_id : 0, cmd, (int)r);
            last_status = 128 + WSTOPSIG(st);
            goto out;
        }
        if (r == last_pid) last_status = status_code(st);
    }
    if (existing) remove_job(existing->pid);
out:
    fg_pgid = 0;
    if (interactive) tcsetpgrp(STDIN_FILENO, shell_pgid);
}

// Ctrl-C / Ctrl-Z reaching the shell itself.
// While a job owns the terminal the kernel sends these straight to the job's
// process group; if one still reaches us we forward it. With no foreground
// job, Ctrl-C just redraws the prompt and Ctrl-Z is ignored.
void signal_handler(int sig) {
    if (fg_pgid != 0) {
        kill(-fg_pgid, sig);
    } else if (sig == SIGINT) {
        write(STDOUT_FILENO, "\n", 1);
        write(STDOUT_FILENO, prompt, strlen(prompt));
    }
}

void init_signal_handlers() {
    // Initialize signal handlers
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;

    sa.sa_handler = signal_handler;  sigaction(SIGINT,  &sa, NULL);
                                     sigaction(SIGTSTP, &sa, NULL);
    sa.sa_handler = sigchld_handler; sigaction(SIGCHLD, &sa, NULL);
    signal(SIGTTOU, SIG_IGN);
    signal(SIGTTIN, SIG_IGN);
}
