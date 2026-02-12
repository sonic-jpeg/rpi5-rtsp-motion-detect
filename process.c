#include "process.h"
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/wait.h>
#include <errno.h>
#include <string.h>

static int make_pipe(int pipefd[2]) {
    return pipe(pipefd);
}

int process_spawn(
    process_t *p,
    char *const argv[],
    int want_stdin,
    int want_stdout,
    int want_stderr
) {
    int in_pipe[2]  = {-1, -1};
    int out_pipe[2] = {-1, -1};
    int err_pipe[2] = {-1, -1};

    memset(p, 0, sizeof(*p));
    p->stdin_fd = p->stdout_fd = p->stderr_fd = -1;

    if (want_stdin  && make_pipe(in_pipe))  return -1;
    if (want_stdout && make_pipe(out_pipe)) return -1;
    if (want_stderr && make_pipe(err_pipe)) return -1;

    pid_t pid = fork();
    if (pid < 0) return -1;

    if (pid == 0) {
        /* ---- CHILD ---- */

        if (want_stdin) {
            dup2(in_pipe[0], STDIN_FILENO);
        }
        if (want_stdout) {
            dup2(out_pipe[1], STDOUT_FILENO);
        }
        if (want_stderr) {
            dup2(err_pipe[1], STDERR_FILENO);
        }

        /* close all pipe fds */
        if (in_pipe[0]  != -1) close(in_pipe[0]);
        if (in_pipe[1]  != -1) close(in_pipe[1]);
        if (out_pipe[0] != -1) close(out_pipe[0]);
        if (out_pipe[1] != -1) close(out_pipe[1]);
        if (err_pipe[0] != -1) close(err_pipe[0]);
        if (err_pipe[1] != -1) close(err_pipe[1]);

        execvp(argv[0], argv);

        /* exec failed */
        _exit(127);
    }

    /* ---- PARENT ---- */
    p->pid = pid;

    if (want_stdin) {
        close(in_pipe[0]);
        p->stdin_fd = in_pipe[1];
    }
    if (want_stdout) {
        close(out_pipe[1]);
        p->stdout_fd = out_pipe[0];
    }
    if (want_stderr) {
        close(err_pipe[1]);
        p->stderr_fd = err_pipe[0];
    }

    return 0;
}

void process_terminate(process_t *p) {
    if (p->pid > 0) {
        kill(p->pid, SIGTERM);
    }
}

int process_wait(process_t *p, int *exit_code) {
    int status;
    pid_t r;

    do {
        r = waitpid(p->pid, &status, 0);
    } while (r < 0 && errno == EINTR);

    if (r < 0) return -1;

    if (exit_code) {
        if (WIFEXITED(status))
            *exit_code = WEXITSTATUS(status);
        else if (WIFSIGNALED(status))
            *exit_code = 128 + WTERMSIG(status);
        else
            *exit_code = -1;
    }

    return 0;
}

void process_close(process_t *p) {
    if (p->stdin_fd  != -1) close(p->stdin_fd);
    if (p->stdout_fd != -1) close(p->stdout_fd);
    if (p->stderr_fd != -1) close(p->stderr_fd);

    p->stdin_fd = p->stdout_fd = p->stderr_fd = -1;
}
