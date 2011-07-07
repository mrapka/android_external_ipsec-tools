/*
 * Copyright (C) 2011 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <signal.h>
#include <poll.h>

#ifdef ANDROID_CHANGES
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <android/log.h>
#include <cutils/sockets.h>
#include <private/android_filesystem_config.h>
#endif

#include "config.h"
#include "gcmalloc.h"
#include "session.h"
#include "schedule.h"
#include "plog.h"

#ifdef ANDROID_CHANGES

static int get_control_and_arguments(int *argc, char ***argv)
{
    static char *args[32];
    int control;
    int i;

    if ((i = android_get_control_socket("racoon")) == -1) {
        return -1;
    }
    do_plog(LLV_DEBUG, "Waiting for control socket");
    if (listen(i, 1) == -1 || (control = accept(i, NULL, 0)) == -1) {
        do_plog(LLV_ERROR, "Cannot get control socket");
        exit(1);
    }
    close(i);

    args[0] = (*argv)[0];
    for (i = 1; i < 32; ++i) {
        unsigned char bytes[2];
        if (recv(control, &bytes[0], 1, 0) != 1
            || recv(control, &bytes[1], 1, 0) != 1) {
            do_plog(LLV_ERROR, "Cannot get argument length");
            exit(1);
        } else {
            int length = bytes[0] << 8 | bytes[1];
            int offset = 0;

            if (length == 0xFFFF) {
                break;
            }
            args[i] = malloc(length + 1);
            while (offset < length) {
                int n = recv(control, &args[i][offset], length - offset, 0);
                if (n > 0) {
                    offset += n;
                } else {
                    do_plog(LLV_ERROR, "Cannot get argument value");
                    exit(1);
                }
            }
            args[i][length] = 0;
        }
    }
    do_plog(LLV_DEBUG, "Received %d arguments", i - 1);

    *argc = i;
    *argv = args;
    return control;
}

#endif

extern void setup(int argc, char **argv);

char *pname;

static int monitor_count;
static struct {
    int (*callback)(void *ctx, int fd);
    void *ctx;
} monitors[10];
static struct pollfd pollfds[10];

static void terminate(int signal)
{
    exit(1);
}

static void terminated()
{
    do_plog(LLV_INFO, "Bye\n");
}

int main(int argc, char **argv)
{
#ifdef ANDROID_CHANGES
    int control = get_control_and_arguments(&argc, &argv);
    if (control != -1) {
        pname = "%p";
    }
#endif

    do_plog(LLV_INFO, "ipsec-tools 0.8.0 (http://ipsec-tools.sf.net)\n");
    setup(argc, argv);

    signal(SIGHUP, terminate);
    signal(SIGINT, terminate);
    signal(SIGTERM, terminate);
    signal(SIGPIPE, SIG_IGN);
    atexit(terminated);

    while (1) {
        struct timeval *tv = schedular();
        int timeout = tv->tv_sec * 1000 + tv->tv_usec / 1000 + 1;

        if (poll(pollfds, monitor_count, timeout) > 0) {
            int i;
            for (i = 0; i < monitor_count; ++i) {
                if (pollfds[i].revents & POLLHUP) {
                    do_plog(LLV_ERROR, "fd %d is closed\n", pollfds[i].fd);
                    exit(1);
                }
                if (pollfds[i].revents & POLLIN) {
                    monitors[i].callback(monitors[i].ctx, pollfds[i].fd);
                }
            }
        }
    }
    return 0;
}

/* session.h */

void monitor_fd(int fd, int (*callback)(void *, int), void *ctx, int priority)
{
    if (fd < 0 || monitor_count == 10) {
        do_plog(LLV_ERROR, "Cannot monitor fd");
        exit(1);
    }
    monitors[monitor_count].callback = callback;
    monitors[monitor_count].ctx = ctx;
    pollfds[monitor_count].fd = fd;
    pollfds[monitor_count].events = POLLIN;
    ++monitor_count;
}

void unmonitor_fd(int fd)
{
    exit(1);
}

/* plog.h */

void do_plog(int level, char *format, ...)
{
    if (level >= 0 && level <= 5) {
#ifdef ANDROID_CHANGES
        static int levels[6] = {
            ANDROID_LOG_ERROR, ANDROID_LOG_WARN, ANDROID_LOG_INFO,
            ANDROID_LOG_INFO, ANDROID_LOG_DEBUG, ANDROID_LOG_VERBOSE
        };
        va_list ap;
        va_start(ap, format);
        __android_log_vprint(levels[level], "racoon", format, ap);
        va_end(ap);
#else
        static char *levels = "EWNIDV";
        fprintf(stderr, "%c: ", levels[level]);
        va_list ap;
        va_start(ap, format);
        vfprintf(stderr, format, ap);
        va_end(ap);
#endif
    }
}

char *binsanitize(char *data, size_t length)
{
    char *output = racoon_malloc(length + 1);
    if (output) {
        size_t i;
        for (i = 0; i < length; ++i) {
            output[i] = (data[i] < ' ' || data[i] > '~') ? '?' : data[i];
        }
        output[length] = '\0';
    }
    return output;
}
