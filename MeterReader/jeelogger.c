// Jeelink Data Logger
// 
// Copyright (C) 2012 - Matt Brown
//
// All rights reserved.
//
// Logs packets written to the serial port by the Jeelink receiving packets from
// the Jeenode running MeterReader.ino. Assumes the Jeelink is running something
// like the RF12demo sketch from Jeelib.
#include <assert.h>
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

int process_line(struct logfile *log, char *buf, char *nl) {
    // Write to dump file.
    printf("process_line\n");
    char ts[1024];
    time_t now = time(NULL);
    int tslen = sprintf((char *)&ts, "%lld ", (long long)now);
    if (!check_logfile(log, now)) {
        return 3;
    }
    write(log->fd, &ts, tslen);
    write(log->fd, buf, (nl - buf)+1);
    // Maintain in-memory state.
    if (strncmp(buf, "OK", 2) != 0) {
        return 1;
    }
    float t;
    printf("float: %lu\n", sizeof(t));
    return 1;
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
    // Set up the device. But only if it looks like we were given a device,
    // this makes it easy to test using a plain input file.
    if (strncmp(argv[1], "/dev/", 5) == 0) {
        struct termios options;
        tcgetattr(fd, &options);
        cfsetispeed(&options, B57600);
        cfsetospeed(&options, B57600);
        options.c_cflag |= (CLOCAL | CREAD);
        tcsetattr(fd, TCSANOW, &options);

        // Configure the Jeelink
        int n = write(fd, "26 i\r8 b\r5 g\r", 13);
        if (n < 0) {
            syslog(LOG_CRIT, "Failed to configure JeeLink!");
            return 2;
        }
        // Display the help (to assist with verifying config settings), then
        // enter quiet mode (don't report corrupted packets).
        n = write(fd, "h\r1 q\r", 6);
        if (n < 0) {
            syslog(LOG_CRIT, "Failed to verify config and enter quiet mode!");
            return 2;
        }
    }

    // Loop reading the port, any line less than 1023 characters will be
    // written to an hourly file with a timestamp prefix when found. Lines
    // greater than this length are ignored.
    fd_set rfds;
    char buf[1024];
    char *bufp = (char *)&buf;
    long unsigned int bufsize = sizeof(buf);
    int bufbytes = 0;
    int valid = 1;
    struct logfile log;
    log.logdir = strdup(argv[2]);
    log.expires_at = -1;
    log.fd = -1;
    syslog(LOG_INFO, "Entering main read loop");
    while (1) {
        FD_ZERO(&rfds);
        FD_SET(fd, &rfds);
        int rv = select(fd+1, &rfds, NULL, NULL, NULL);
        if (!rv) {
            continue;
        }
        int avail = bufsize - bufbytes;
        if (avail == 0) {
            // Full buffer, newline couldn't be found in it.
            // > 1024 char lines are bogus, so drop the buffer, and keep
            // dropping until the next newline is seen.
            bufp = (char *)&buf;
            bufbytes = 0;
            avail = sizeof(buf);
            valid = 0;
        }
        int n = read(fd, bufp, avail);
        if (n == -1) {
            syslog(LOG_ERR, "Read failed: %s", strerror(errno));
            continue;
        } else if (n == 0 && bufbytes == 0) {
            break;
        }
        bufp += n;
        bufbytes += n;
        assert(bufp <= ((char *)buf + bufsize));
        assert(bufbytes <= bufsize);
        char *nl = memchr((char *)&buf, '\n', bufbytes);
        if (!nl) {
            continue;
        }
        if (valid == 1) {
            rv = process_line(&log, (char *)&buf, nl);
            if (rv != 1) {
                printf("bad line\n");
                return rv;
            }
        }
        // Copy remaining buffer (after the newline) back to the start.
        bufbytes= bufp-(nl+1);
        memmove(&buf, nl+1, bufbytes);
        bufp = ((char *)&buf) + bufbytes;
        // mark as valid because this is the start of a new (maybe valid) line.
        valid = 1;
    }
    close(fd);
    syslog(LOG_INFO, "Exiting");
}
