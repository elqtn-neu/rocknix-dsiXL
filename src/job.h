// job.h: a shell command run in the background while the menu goes on (Wi-Fi, getting art, updating): its output
// goes to a file the menu reads as it comes.
#pragma once

typedef struct {
    int pid;                            /* 0: none */
    int rc;                             /* its exit status, once done */
    char out[128];                      /* where its output goes */
} Job;

int job_start(Job *j, const char *tag, const char *cmd);   /* 0 started */
int job_running(Job *j);                /* 1 still running; 0 done (j->rc) or never started */
/* the output so far (at most n-1 bytes, the end of it when there's more) */
int job_output(const Job *j, char *buf, int n);
void job_stop(Job *j);                  /* it and whatever it started */
