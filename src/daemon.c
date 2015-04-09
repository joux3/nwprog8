#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <syslog.h>
#include <string.h>
#include <signal.h>
#include "daemon.h"
#include "logging.h"

// daemon technique adapted from 
// http://www.linuxprofilm.com/articles/linux-daemon-howto.html
// and http://stackoverflow.com/a/17955149
void init_daemon() {
    pid_t pid = fork();
    if (pid < 0) {
        log_error("Failed to fork process!\n");
        exit(4);
    }
    if (pid > 0) {
        exit(0); // let the parent die 
    }
    // now we're running in the child

    // ignore signals that we don't care about
    signal(SIGCHLD, SIG_IGN);
    signal(SIGHUP, SIG_IGN);

    // create a new SID for the child process 
    pid_t sid = setsid();
    if (sid < 0) {
        log_error("Failed to call setsid!\n");
        exit(2);
    }

    // fork a second time to make the PID not equal to the newly created SID
    // (so that our process can't take control of a terminal again)
    pid = fork();
    if (pid < 0) {
        log_error("Failed to fork process!\n");
        exit(4);
    }
    if (pid > 0) {
        // let the parent terminate
        exit(0);
    }
    // now we're running in the child (again)

    // make potential files created readable by all
    umask(0);

    // reopen log file the take the umask into account
    log_reopen(); 

    // change the working directory to something we know should exist
    if ((chdir("/")) < 0) {
        log_error("Failed to call chdir to /!\n");
        exit(2);
    }

    // close standard streams
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);
}
