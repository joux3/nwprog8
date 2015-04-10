#ifndef DAEMON_H
#define DAEMON_H

#define PID_FILE_PATH "/tmp/nwprog8.pid"

void init_daemon();
int pid_file_exists();

#endif
