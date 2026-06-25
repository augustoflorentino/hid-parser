#define _GNU_SOURCE
#include <dirent.h>
#include <fcntl.h>
#include <linux/uhid.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/reboot.h>
#include <sys/syscall.h>
#include <unistd.h>

#define MAX_DEVICES 256

static FILE *out;

static void load_uhid(void)
{
    int fd = open("/uhid.ko.zst", O_RDONLY);

    if (fd < 0 || syscall(SYS_finit_module, fd, "", 4) != 0)
        fprintf(out, "!! uhid load failed\n");
    if (fd >= 0)
        close(fd);
}

static int create_device(const char *dir, const char *file)
{
    struct uhid_event ev;
    char path[512];
    int fd = open("/dev/uhid", O_RDWR | O_CLOEXEC);
    int in;
    ssize_t n;

    snprintf(path, sizeof(path), "%s/%s", dir, file);
    in = open(path, O_RDONLY);
    if (fd < 0 || in < 0)
        return -1;
    memset(&ev, 0, sizeof(ev));
    ev.type = UHID_CREATE2;
    snprintf((char *)ev.u.create2.name, sizeof(ev.u.create2.name), "%s", file);
    n = read(in, ev.u.create2.rd_data, sizeof(ev.u.create2.rd_data));
    close(in);
    ev.u.create2.rd_size = (unsigned short)(n > 0 ? n : 0);
    ev.u.create2.bus = 0x03;
    ev.u.create2.vendor = 0x1234;
    ev.u.create2.product = 0x5678;
    if (write(fd, &ev, sizeof(ev)) != (ssize_t)sizeof(ev)) {
        fprintf(out, "!! create failed %s\n", file);
        close(fd);
        return -1;
    }
    return fd;
}

static int hid_devices(void)
{
    DIR *d = opendir("/sys/bus/hid/devices");
    struct dirent *e;
    int n = 0;

    while (d != NULL && (e = readdir(d)) != NULL)
        n += e->d_name[0] != '.';
    if (d != NULL)
        closedir(d);
    return n;
}

static void drain(int *fds, int count)
{
    struct uhid_event ev;
    struct pollfd p[MAX_DEVICES];
    int i;
    int rounds;
    int seen = -1;
    int stable = 0;

    for (rounds = 0; rounds < 600 && stable < 15; rounds++) {
        int now = hid_devices();

        stable = now == seen ? stable + 1 : 0;
        seen = now;
        for (i = 0; i < count; i++) {
            p[i].fd = fds[i];
            p[i].events = POLLIN;
        }
        usleep(100000);
        if (poll(p, (nfds_t)count, 0) <= 0)
            continue;
        for (i = 0; i < count; i++)
            if (p[i].revents & POLLIN)
                read(fds[i], &ev, sizeof(ev));
    }
}

static int name_of(const char *dev, char *name, size_t name_len)
{
    char path[512];
    char line[512];
    FILE *f;

    snprintf(path, sizeof(path), "/sys/bus/hid/devices/%s/uevent", dev);
    if ((f = fopen(path, "r")) == NULL)
        return -1;
    while (fgets(line, sizeof(line), f) != NULL) {
        if (strncmp(line, "HID_NAME=", 9) == 0) {
            line[strcspn(line, "\n")] = 0;
            snprintf(name, name_len, "%s", line + 9);
            fclose(f);
            return 0;
        }
    }
    fclose(f);
    return -1;
}

static void dump_all(void)
{
    DIR *d = opendir("/sys/bus/hid/devices");
    struct dirent *e;
    char name[256];
    char path[512];
    char buf[4096];
    size_t n;
    FILE *f;

    while (d != NULL && (e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.' || name_of(e->d_name, name, sizeof(name)) != 0)
            continue;
        snprintf(path, sizeof(path), "/sys/kernel/debug/hid/%s/rdesc", e->d_name);
        if ((f = fopen(path, "r")) == NULL) {
            fprintf(out, "!! no rdesc for %s\n", name);
            continue;
        }
        fprintf(out, "@@ begin %s\n", name);
        while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
            fwrite(buf, 1, n, out);
        fprintf(out, "@@ end %s\n", name);
        fclose(f);
    }
    if (d != NULL)
        closedir(d);
}

int main(void)
{
    int fds[MAX_DEVICES];
    int count = 0;
    DIR *d;
    struct dirent *e;

    mount("devtmpfs", "/dev", "devtmpfs", 0, NULL);
    mount("proc", "/proc", "proc", 0, NULL);
    mount("sysfs", "/sys", "sysfs", 0, NULL);
    mount("debugfs", "/sys/kernel/debug", "debugfs", 0, NULL);
    out = fopen("/dev/ttyS1", "w");
    if (out == NULL)
        out = stderr;
    load_uhid();
    d = opendir("/descs");
    while (d != NULL && (e = readdir(d)) != NULL && count < MAX_DEVICES) {
        if (e->d_name[0] == '.')
            continue;
        if ((fds[count] = create_device("/descs", e->d_name)) >= 0)
            count++;
    }
    drain(fds, count);
    fprintf(out, "@@ registered %d\n", hid_devices());
    dump_all();
    fprintf(out, "@@ done %d\n", count);
    fflush(out);
    sync();
    reboot(RB_POWER_OFF);
    return 0;
}
