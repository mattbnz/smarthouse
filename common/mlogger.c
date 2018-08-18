// Arduino Data Logger
// 
// Copyright (C) 2017 - Matt Brown
//
// All rights reserved.
//
// Logs serial output written by the Arduino running MeterReader.ino
//
// Also supports water relay on/off control via SIGUSR1/SIGUSR2
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <linux/limits.h>
#include <signal.h>
#include <sys/select.h>
#include <syslog.h>
#include <termios.h>
#include <time.h>

struct logfile {
    char *logdir;
    time_t expires_at;
    int fd;
};

unsigned long long counter;
unsigned long long fp_counter;
int watts;
int water_on;
int water_off;

void handle_signal(int signal) {
    switch (signal) {
        case SIGUSR1:
            water_on = 1;
            break;
        case SIGUSR2:
            water_off = 1;
            break;
    }
}

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

void write_metrics(struct logfile* log) {
    char tmp[PATH_MAX];  // Blah blah, don't run this on insane fses.
    char path[PATH_MAX];
    if (sprintf((char *)&path, "%s/metrics", log->logdir) == -1) {
        syslog(LOG_CRIT, "Cannot make path for metrics file!");
        return;
    }
    if (sprintf((char *)&tmp, "%s.tmp", (char *)&path) == -1) {
        syslog(LOG_CRIT, "Cannot make path for temp metrics file!");
        return;
    }
    int fd = open(tmp, O_WRONLY | O_TRUNC | O_CREAT, 0644);
    if (fd == -1) {
        syslog(LOG_CRIT, "Failed to open temp metrics file!");
        return;
    }
    const char *header = "# TYPE meter_wh_total counter\n"
		"# TYPE meter_watts gauge\n"
		"# TYPE meter_fp_wh_total counter\n\n";
    write(fd, header, strlen(header));

    char buf[256];
    int len = snprintf((char *)buf, sizeof(buf), "meter_wh_total %llu\n", counter);
    write(fd, buf, len);
    len = snprintf((char *)buf, sizeof(buf), "meter_fp_wh_total %llu\n", fp_counter);
    write(fd, buf, len);
    len = snprintf((char *)buf, sizeof(buf), "meter_watts %d\n", watts);
    write(fd, buf, len);
    close(fd);
    // Move temp file to proper location.
    rename(tmp, path);
}


int process_line(struct logfile *log, char *buf, char *nl) {
    // Write to dump file.
    char ts[1024];
    time_t now = time(NULL);
    int tslen = sprintf((char *)&ts, "%lld ", (long long)now);
    if (!check_logfile(log, now)) {
        return 3;
    }
    write(log->fd, &ts, tslen);
    write(log->fd, buf, (nl - buf)+1);
	// Make a null terminated copy and tokenize it to extract node/temp/bat.
    char line[1024];
    char *linep;
    int len = (nl - buf) + 1;
    assert(len < 1023);
    strncpy((char *)&line, buf, len);
    line[len+1] = '\0';
	char *type = NULL, *data = NULL;
	int i;
    for (i=0, linep=(char *)&line; ;i++, linep = NULL) {
        char *tok = strtok(linep, " ");
        if (tok == NULL) {
            break;
        }
        switch (i) {
            case 1:
				type = tok;
				break;
            case 2:
                data = tok;
                break;
        }
    }
    int write=0;
    if (type && strncmp(type, "PULSE", 5) == 0) {
        counter++;
        write=1;
	} else if (type && strncmp(type, "FP_PULSE", 8) == 0) {
		fp_counter++;
		write=1;
    } else if (type && strncmp(type, "LDR", 3) == 0) {
        if (data) {
            watts = atoi(data);
            write=1;
        }
    }
    if (write) {
        write_metrics(log);
    }
    return 1;
}


int main(int argc, char **argv) {
    openlog("mlogger", LOG_CONS | LOG_PID, LOG_DAEMON);
    if (argc != 3) {
        fprintf(stdout, "Usage: %s SERIAL_PORT LOG_DIR\n", argv[0]);
        return 1;
    }
    syslog(LOG_INFO, "Started");

    /* Set up signal handler for water commands */
    struct sigaction sa;
    sa.sa_handler = &handle_signal;
    sa.sa_flags = SA_RESTART;
    sigfillset(&sa.sa_mask);
    water_on = 0;
    water_off = 0;
    if (sigaction(SIGUSR1, &sa, NULL) == -1) {
        syslog(LOG_CRIT, "Unable to handle SIGUSR2!");
        return 1;
    }
    if (sigaction(SIGUSR2, &sa, NULL) == -1) {
        syslog(LOG_CRIT, "Unable to handle SIGUSR2!");
        return 1;
    }

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
        while (bufbytes > 0) {
            char *nl = memchr((char *)&buf, '\n', bufbytes);
            if (!nl) {
                break;
            }
            if (valid == 1) {
                rv = process_line(&log, (char *)&buf, nl);
                if (rv != 1) {
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
        // Handle water commands
        if (water_off == 1) {
            const char buf ='w';
            write(fd, (char *)&buf, 1);
            syslog(LOG_INFO, "Water off");
            water_off=0;
            // If we've just turned the water off, don't let a race
            // immediately turn it back on!
            water_on=0;
        } else if (water_on == 1) {
            const char buf ='W';
            write(fd, (char *)&buf, 1);
            syslog(LOG_INFO, "Water on");
            water_on=0;
        }
    }
    close(fd);
    syslog(LOG_INFO, "Exiting");
}
