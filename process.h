#pragma once
#include <sys/types.h>

typedef struct {
    pid_t pid;
    int stdin_fd;   // parent writes -> child stdin
    int stdout_fd;  // parent reads  <- child stdout
    int stderr_fd;  // parent reads  <- child stderr
} process_t;

/* spawn a process like subprocess.Popen */
int process_spawn(
    process_t *p,
    char *const argv[],
    int want_stdin,
    int want_stdout,
    int want_stderr
);

/* terminate process (SIGTERM) */
void process_terminate(process_t *p);

/* wait for process exit */
int process_wait(process_t *p, int *exit_code);

/* cleanup fds */
void process_close(process_t *p);
