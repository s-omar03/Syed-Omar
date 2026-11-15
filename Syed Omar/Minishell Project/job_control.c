#include "minishell.h"

void block_sigchld(sigset_t *old) {
    sigset_t s;
    sigemptyset(&s);
    sigaddset(&s, SIGCHLD);
    sigprocmask(SIG_BLOCK, &s, old);
}

// Add a new command to jobs list (as a running background-style entry;
// the caller changes the state if needed)
Job *add_job(pid_t pid, char *command) {
    sigset_t old;
    Job *j = calloc(1, sizeof(Job));
    if (!j) return NULL;

    block_sigchld(&old);
    j->job_id = next_job_id++;
    j->pid = pid;
    j->last_pid = pid;
    j->state = JOB_RUNNING;
    snprintf(j->command, sizeof j->command, "%s", command);

    // append at the tail so that the list stays ordered by job id
    Job **pp = &job_list;
    while (*pp) pp = &(*pp)->next;
    *pp = j;
    sigprocmask(SIG_SETMASK, &old, NULL);
    return j;
}

// Remove a job from jobs list
void remove_job(pid_t pid) {
    sigset_t old;
    block_sigchld(&old);
    for (Job **pp = &job_list; *pp; pp = &(*pp)->next) {
        if ((*pp)->pid == pid) {
            Job *dead = *pp;
            *pp = dead->next;
            free(dead);
            break;
        }
    }
    sigprocmask(SIG_SETMASK, &old, NULL);
}

// Find a job by pgid / last pid. pid == 0 returns the most recent job.
Job *find_job(pid_t pid, int state) {
    Job *best = NULL;
    for (Job *j = job_list; j; j = j->next) {
        if (j->state == JOB_DONE) continue;
        if (state >= 0 && j->state != state) continue;
        if (pid) {
            if (j->pid == pid || j->last_pid == pid) return j;
        } else {
            best = j;                       // list is ordered, keep the last
        }
    }
    return best;
}

// Print all jobs with its detail.
void list_jobs() {
    for (Job *j = job_list; j; j = j->next) {
        if (j->state == JOB_DONE) continue;
        printf("[%d]  %-8s %s  (pid %d)\n", j->job_id,
               j->state == JOB_STOPPED ? "Stopped" : "Running",
               j->command, (int)j->pid);
    }
}

// fg [pid] : resume a job and wait for it in the foreground
void bring_job_to_foreground(int pid) {
    Job *j = find_job(pid, -1);
    if (!j) {
        fprintf(stderr, "msh: fg: no such job\n");
        last_status = 1;
        return;
    }
    printf("%s\n", j->command);
    j->state = JOB_FOREGROUND;              // SIGCHLD handler must not reap it
    kill(-j->pid, SIGCONT);
    wait_foreground(j->pid, j->last_pid, j->command, j);
}

// bg [pid] : resume a stopped job in the background
void send_job_to_background(int pid) {
    Job *j = find_job(pid, JOB_STOPPED);
    if (!j) {
        fprintf(stderr, "msh: bg: no stopped job\n");
        last_status = 1;
        return;
    }
    j->state = JOB_RUNNING;
    kill(-j->pid, SIGCONT);
    printf("[%d]+ %s &\n", j->job_id, j->command);
    last_status = 0;
}

// SIGCHLD: reap finished background jobs and print their exit status.
// (Entries are only marked DONE here; memory is freed from the main loop.)
void sigchld_handler(int sig) {
    (void)sig;
    int saved = errno;
    for (Job *j = job_list; j; j = j->next) {
        if (j->state != JOB_RUNNING) continue;
        int st;
        for (;;) {
            pid_t r = waitpid(-j->pid, &st, WNOHANG);
            if (r > 0) {
                if (r == j->last_pid) j->exit_status = status_code(st);
                continue;
            }
            if (r == -1 && errno == ECHILD) {       // whole job has finished
                char buf[MAX_INPUT_SIZE + 64];
                int n = snprintf(buf, sizeof buf,
                                 "\n[%d]+ Done  %s  (exit status %d)\n",
                                 j->job_id, j->command, j->exit_status);
                write(STDOUT_FILENO, buf, n);
                j->state = JOB_DONE;
            }
            break;
        }
    }
    errno = saved;
}

// Free entries that the SIGCHLD handler marked as DONE.
void remove_finished_jobs() {
    sigset_t old;
    block_sigchld(&old);
    Job **pp = &job_list;
    while (*pp) {
        if ((*pp)->state == JOB_DONE) {
            Job *dead = *pp;
            *pp = dead->next;
            free(dead);
        } else {
            pp = &(*pp)->next;
        }
    }
    sigprocmask(SIG_SETMASK, &old, NULL);
}

// Used by "exit": do not leave orphans behind.
void kill_all_jobs() {
    for (Job *j = job_list; j; j = j->next) {
        if (j->state == JOB_DONE) continue;
        kill(-j->pid, SIGTERM);
        kill(-j->pid, SIGCONT);
    }
}
