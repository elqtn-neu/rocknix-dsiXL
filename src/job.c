// job.c: see job.h.
#define _GNU_SOURCE
#include "job.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>

int job_start(Job *j, const char *tag, const char *cmd) {
    job_stop(j);
    const char *tmp = getenv("DSIXL_TMP"); if (!tmp || !*tmp) tmp = "/tmp";
    snprintf(j->out, sizeof j->out, "%s/dsixl-%s.out", tmp, tag);
    int fd = open(j->out, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0) return -1;
    pid_t p = fork();
    if (p < 0) { close(fd); return -1; }
    if (p == 0) {
        setpgid(0, 0);                              /* its own group: stopped together */
        dup2(fd, 1); dup2(fd, 2);
        int nul = open("/dev/null", O_RDONLY); if (nul >= 0) dup2(nul, 0);
        signal(SIGTERM, SIG_DFL); signal(SIGINT, SIG_DFL); signal(SIGCHLD, SIG_DFL);
        execl("/bin/sh", "sh", "-c", cmd, (char *)0);
        _exit(127);
    }
    close(fd);
    setpgid(p, p);
    j->pid = p; j->rc = -1;
    return 0;
}

int job_running(Job *j) {
    if (j->pid <= 0) return 0;
    int st;
    pid_t r = waitpid(j->pid, &st, WNOHANG);
    if (r == 0) return 1;
    j->rc = r == j->pid && WIFEXITED(st) ? WEXITSTATUS(st) : -1;
    j->pid = 0;
    return 0;
}

int job_output(const Job *j, char *buf, int n) {
    buf[0] = 0;
    FILE *f = fopen(j->out, "rb");
    if (!f) return 0;
    fseek(f, 0, SEEK_END); long len = ftell(f);
    long from = len > n - 1 ? len - (n - 1) : 0;
    fseek(f, from, SEEK_SET);
    int got = (int)fread(buf, 1, (size_t)(n - 1), f);
    fclose(f);
    buf[got > 0 ? got : 0] = 0;
    return got;
}

void job_stop(Job *j) {
    if (j->pid <= 0) return;
    kill(-j->pid, SIGTERM);
    for (int i = 0; i < 20 && job_running(j); i++) usleep(10000);
    if (j->pid > 0) { kill(-j->pid, SIGKILL); waitpid(j->pid, 0, 0); j->pid = 0; }
}
