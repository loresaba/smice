#include <errno.h>
#include <error.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <linux/input.h>
#include <sys/ioctl.h>

/* Absolute path to the file that contains a list of all registered 
 * input devices */
#define DEVICE_INPUT_PATH "/proc/bus/input/devices"

/* Absolute path to the file that contains real-time input events generated 
 * by the kernel's evdev (event device) subsystem */
#define DEVICE_EVENT_PATH "/dev/input/"

#define MAX_MICE 16          /* Maximum number of mice supported in parallel */
#define DEVICE_NAME_SIZE 128 /* Maximum number of character in a device name */
#define EVENT_BUFFER_SIZE 64 /* Maximum number of events int the buffer */

/* Represent an active device in the evdev. */
struct dev {
    char name[DEVICE_NAME_SIZE]; /* Name of the device */
    int evnum; /* Event handler number associated to the device */
};

static volatile int running = 1;

/* Catch the signal from the program and set 'running' to 0. */
void sigHandler(int signum) {
    (void)signum;
    running = 0;
}

/* Search for all the registered mouse devices in the system and fills the 
 * `devices` array for each of them.
 * The search ends when no more new mice are found or when 'max_mice' mice 
 * have been found.
 * Return the number of mice device found. */
int getMouseDevices(struct dev *devices, int max_mice) {
    FILE *fp = fopen(DEVICE_INPUT_PATH, "r");
    if (fp == NULL) {
        perror("Failed to open " DEVICE_INPUT_PATH);
        return 0;
    }

    /* Read the devices file to search for the following pattern:
     *   N: Name="..."
     *   H: Handlers=mouseX eventY
     * where X and Y are intergers that indicate the mouse and its 
     * associated event handler rispectively. */
    char line[512];
    int count = 0;
    char devname[DEVICE_NAME_SIZE];
    while (fgets(line, sizeof(line), fp) && count < max_mice) {
        if (strncmp(line, "N: Name=\"", 9) == 0) {
            strncpy(devname, line+9, sizeof(devname));
            devname[sizeof(devname)-1] = '\0'; 

            /* Erase terminating " or \n char */
            char *c;
            if      ((c = strchr(devname, '\"')) != NULL) *c = '\0';
            else if ((c = strchr(devname, '\n')) != NULL) *c = '\0';
        }

        if (strncmp(line, "H: Handlers=", 12) == 0 &&
            strstr(line, "mouse") != NULL) {
            char *evstr = strstr(line, "event");
            if (evstr != NULL) {
                struct dev *device = &devices[count];
                strncpy(device->name, devname, sizeof(device->name));
                device->name[sizeof(device->name)-1] = '\0';
                device->evnum = atoi(evstr+5);

                count++;
                devname[0] = '\0';
            }
        }
    }

    fclose(fp);
    return count;
}

/* Fill the 'poll_fds' array with the file descriptors of the 'count' event 
 * 'devices' from the 'file_path'.
 * Return 1 if all devices correctly get their file descriptor, 0 otherwise. 
 * It's up to the caller to close all the open file descriptors. */
int buildEventPoll(const char *file_path, struct dev *devices, int count, 
                   struct pollfd *poll_fds) 
{
    int ret = 1;
    for (int i = 0; i < count; i++) {
        char event_path[64];
        snprintf(event_path, sizeof(event_path), "%sevent%d", 
                 file_path, devices[i].evnum);

        int fd;
        if ((fd = open(event_path, O_RDONLY | O_NONBLOCK)) == -1) {
            error(0, errno, "Failed to open %s", event_path);
            ret = 0;
        }

        poll_fds[i] = (struct pollfd){
            .fd     = fd,
            .events = POLLIN,
        };
    }

    return ret;
}

/* Read as many pending events as possible from all ready file descriptors
 * and save them into 'iebuf' buffer.
 * 'max_events' is the maximum capacity of the array.
 * It waits for one of the 'count' file descriptors to become ready. 
 * Return the total number of events successfully read, or a negative value
 * on error. */
int readEvents(struct pollfd *poll_fds, int count, 
               struct input_event *iebuf, int max_events) 
{
    int poll_status = poll(poll_fds, count, -1);
    if (poll_status <= 0) return poll_status;

    int events_read = 0;
    for (int i = 0; i < count; i++) {
        if (events_read >= max_events) break; /* event buffer is full */

        if (poll_fds[i].revents & POLLIN) {
            /* Read a whole block of events in a single system call */
            int free_events = max_events - events_read;
            ssize_t bytes_read = read(poll_fds[i].fd, 
                                      &iebuf[events_read], 
                                      free_events*sizeof(struct input_event));
            if (bytes_read > 0) {
                events_read += (bytes_read / sizeof(struct input_event));
            } else if (bytes_read < 0) {
                /* Ignore EAGAIN/EWOULDBLOCK since we are in non-blocking 
                 * mode */
                if (errno != EAGAIN && errno != EWOULDBLOCK) {
                    perror("Error reading from input device");
                    return -1;
                }
            }
        }
    }

    return events_read;
}

int main(int argc, char **argv) {
    (void)argc;

    /* Record termination signals to gracefully quit the event loop. */
    if (signal(SIGINT, sigHandler) == SIG_ERR) {
        perror("Error registering SIGINT handler");
        return 1;
    }
    if (signal(SIGTERM, sigHandler) == SIG_ERR) {
        perror("Error registering SIGTERM handler");
        return 1;
    }

    struct dev mouse_devices[MAX_MICE];
    int mouse_count = getMouseDevices(mouse_devices, MAX_MICE);
    if (mouse_count == 0) {
        fprintf(stderr, "No mouse devices found\n");
        fprintf(stderr, "Please check /proc/bus/input/devices file to see "
                "if there are any mice available (if the cat hasn't eaten "
                "them all)\n");
        return 0;
    }

    struct pollfd poll_fds[mouse_count];
    if (!buildEventPoll(DEVICE_EVENT_PATH, mouse_devices, mouse_count, 
                        poll_fds)) {
        if (errno == EACCES) {
            fprintf(stderr, "Since %s files are proteced for security "
                    "reasons, run the program again with `sudo`\n", 
                    DEVICE_EVENT_PATH);
        } 
        return 1;
    }

    printf("%s is listening the following %d mice:\n", argv[0], mouse_count);
    for (int i = 0; i < mouse_count; i++) {
        printf("%s\n", mouse_devices[i].name);
    }
    printf("\n");
    printf("Press CTRL+C to safely quit the program\n");

    /* Grab control of the devices to hide them from the OS. */
    for (int i = 0; i < mouse_count; i++) {
        if (ioctl(poll_fds[i].fd, EVIOCGRAB, 1) == -1) perror("ioctl grab");
    }

    /* Event loop */
    struct input_event event_buffer[EVENT_BUFFER_SIZE];
    while (running) {
        int event_count = readEvents(poll_fds, mouse_count, 
                                     event_buffer, EVENT_BUFFER_SIZE);
        if (event_count < 0) break;

        for (int e = 0; e < event_count; e++) {
            struct input_event ie = event_buffer[e];
            printf("time %ld.%06ld\ttype %d\tcode %d\tvalue %d\n",
                  ie.time.tv_sec, ie.time.tv_usec, ie.type, ie.code, ie.value);
        }
    }

    /* Release control of the devices. */
    for (int i = 0; i < mouse_count; i++) {
        if (ioctl(poll_fds[i].fd, EVIOCGRAB, 0) == -1) perror("ioctl release");
    }

    printf("\n");
    printf("Input devices safely released\n");

    for (int i = 0; i < mouse_count; i++) {
        if (poll_fds[i].fd != -1) {
            if (close(poll_fds[i].fd) == -1) {
                error(0, errno, "Failed to close fd %d", poll_fds[i].fd);
            }
        }
    }

    return 0;
}
