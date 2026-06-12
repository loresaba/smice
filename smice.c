#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <linux/input.h>

/* Absolute path to the file that contains a list of all registered 
 * input devices */
#define DEVICE_INPUT_PATH "/proc/bus/input/devices"

/* Absolute path to the file that contains real-time input events generated 
 * by the kernel's evdev (event device) subsystem */
#define DEVICE_EVENT_PATH "/dev/input/"

/* Maximum number of mice supported in parallel */
#define MAX_MICE 16

/* Search for all the registered mouse devices in the system and fills the 
 * `event_fds` array with the file descriptor of the event handler 
 * associated to each of them.
 * The search ends when no more new mice are found or when 'max_mice' mice 
 * have been found.
 * Return the count of event found. 
 * It's up to the caller to close all open file descriptors. */
int getMouseEventFiles(int *event_fds, int max_mice) {
    FILE *fp = fopen(DEVICE_INPUT_PATH, "r");
    if (fp == NULL) {
        perror("Failed to open " DEVICE_INPUT_PATH);
        return 0;
    }

    // Read the devices file to search for the following pattern:
    //   `H: Handlers=mouseX eventY`
    // where X and Y are intergers that indicate the mouse and its 
    // associated event handler rispectively.
    char line[512];
    int count = 0;
    while (fgets(line, sizeof(line), fp) && count < max_mice) {
        if (strncmp(line, "H: Handlers=", 12) == 0 &&
            strstr(line, "mouse") != NULL) {
            char *evstr = strstr(line, "event");
            if (evstr != NULL) {
                int evnum = atoi(evstr+5);
                char event_path[64];
                snprintf(event_path, sizeof(event_path), 
                         "%sevent%d", DEVICE_EVENT_PATH, evnum);

                int fd;
                if ((fd = open(event_path, O_RDONLY)) == -1) {
                    perror("open");
                    continue;
                }

                event_fds[count] = fd;
                count++;
            }
        }
    }

    fclose(fp);
    return count;
}

/* Save the event read from the fist ready file descriptor in 'ie' variable.
 * It waits for one of the 'count' file descriptors to become ready. 
 * Return 1 if the event has been successfully read, 0 otherwise. */
int readEvent(struct pollfd *poll_fds, int count, struct input_event *ie) {
    if (poll(poll_fds, count, -1) == -1) {
        perror("poll");
        return 0;
    }

    for (int i = 0; i < count; i++) {
        if (poll_fds[i].revents & POLLIN) {
            if (read(poll_fds[i].fd, ie, sizeof(struct input_event)) > 0) {
                return 1;
            }
        }
    }

    return 0;
}

int main(void) {
    int mouse_event_fds[MAX_MICE];
    int mouse_count = getMouseEventFiles(mouse_event_fds, MAX_MICE);
    if (mouse_count == 0) {
        fprintf(stderr, "No mouse devices found\n");
        return 1;
    }

    struct pollfd poll_fds[mouse_count];
    for (int i = 0; i < mouse_count; i++) {
        poll_fds[i] = (struct pollfd){
            .fd     = mouse_event_fds[i],
            .events = POLLIN,
        };
    }

    while (1) {
        struct input_event ie;
        if (readEvent(poll_fds, mouse_count, &ie)) {
                printf("time %ld.%06ld\ttype %d\tcode %d\tvalue %d\n",
                  ie.time.tv_sec, ie.time.tv_usec, ie.type, ie.code, ie.value);
        }
    }

    for (int i = 0; i < mouse_count; i++) {
        if (close(mouse_event_fds[i]) == -1) perror("close");
    }

    return 0;
}
