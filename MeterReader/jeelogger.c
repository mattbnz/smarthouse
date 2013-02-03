// Jeelink Data Logger
// 
// Copyright (C) 2012 - Matt Brown
//
// All rights reserved.
//
// Logs packets written to the serial port by the Jeelink receiving packets from
// the Jeenode running MeterReader.ino. Assumes the Jeelink is running something
// like the RF12demo sketch from Jeelib.
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <linux/limits.h>
#include <sys/select.h>
#include <syslog.h>
#include <termios.h>
#include <time.h>

struct logfile {
    char *logdir;
    time_t expires_at;
    int fd;
};

int create_logfile(struct logfile* log, time_t now) {
    struct tm* now_tm = gmtime(&now);
    char path[PATH_MAX];  // Blah blah, don't run this on insane fses.
    if (sprintf((char *)&path, "%s/%04d%02d%02d%02d.log", log->logdir,
               1900+now_tm->tm_year, now_tm->tm_mon+1, now_tm->tm_mday, 
               now_tm->tm_hour) == -1) {
        syslog(LOG_CRIT, "Cannot make path for new logfile!");
        return 0;
    }
    syslog(LOG_INFO, "Creating new logfile for %lld at %s", (long long)now,
           path);
    log->fd = open(path, O_WRONLY | O_APPEND | O_CREAT, 0644);
    if (log->fd == -1) {
        syslog(LOG_CRIT, "Failed to open new logfile: %s", path);
        return 0;
    }
    log->expires_at = now + (59 - now_tm->tm_sec) +
        (60 * (59 - now_tm->tm_min));
    return 1;
}


int check_logfile(struct logfile* log, time_t now) {
    if (log->expires_at == -1) {
        return create_logfile(log, now);
    } else if (now <= log->expires_at) {
        return 1;
    }
    // Rotation needed.
    close(log->fd);
    return create_logfile(log, now);
}

int main(int argc, char **argv) {
    openlog("jeelogger", LOG_CONS | LOG_PID, LOG_DAEMON);
    if (argc != 3) {
        fprintf(stdout, "Usage: %s SERIAL_PORT LOG_DIR\n", argv[0]);
        return 1;
    }
    syslog(LOG_INFO, "Started");

    /* Open the serial port and set to 57600 */
    int fd = open(argv[1], O_RDWR | O_NOCTTY);
    if (fd == -1) {
        syslog(LOG_CRIT, "Unable to open serial port %s", argv[1]);
        return 1;
    }
    struct termios options;
    tcgetattr(fd, &options);
    cfsetispeed(&options, B57600);
    cfsetospeed(&options, B57600);
    options.c_cflag |= (CLOCAL | CREAD);
    tcsetattr(fd, TCSANOW, &options);

    // Tell the Jeelink to display help (with config info at the bottom) and
    // enter quiet mode (don't report corrupted packets).
    int n = write(fd, "h\r1 q\r", 6);
    if (n < 0) {
        syslog(LOG_CRIT, "Failed to write initial commands");
        return 2;
    }

    // Loop reading the port, storing into a buffer, writing full lines to an
    // hourly file with a timestamp prefix when found. Assumes no lines longer
    // than ~1k chars.  expected is <80char.
    fd_set rfds;
    int rv;
    char line[1024];
    char *linep = (char *)&line;
    char buf[1024];
    struct logfile log;
    log.logdir = strdup(argv[2]);
    log.expires_at = -1;
    log.fd = -1;
    syslog(LOG_INFO, "Entering main read loop");
    while (1) {
        FD_ZERO(&rfds);
        FD_SET(fd, &rfds);
        rv = select(fd+1, &rfds, NULL, NULL, NULL);
        if (!rv) {
            continue;
        }
        n = read(fd, &buf, sizeof(buf));
        if (n > 0) {
            memcpy(linep, &buf, n);
            linep += n;
        }
        char ts[1024];
        time_t now = time(NULL);
        int tslen = sprintf((char *)&ts, "%lld ", (long long)now);
        while (1) {
            char *p = strchr((char *)&line, '\n');
            if (p && p < linep) {
                if (!check_logfile(&log, now)) {
                    return 3;
                }
                write(log.fd, &ts, tslen);
                write(log.fd, &line, ((p+1) - (char *)&line));
                int len = linep - (p+1);
                char tmp[1024];
                memcpy(&tmp, p+1, len);
                memcpy(&line, &tmp, len);
                linep = (char *)&line + len;
            } else {
                break;
            }
        }
    }
    close(fd);
    syslog(LOG_INFO, "Exiting");
}
