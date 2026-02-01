#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/sys_domain.h>
#include <sys/kern_control.h>
#include <net/if_utun.h>

#define info(fmt, ...) printf(fmt "\n", ##__VA_ARGS__)
#define fatal(fmt, ...)                                                       \
    do {                                                                      \
        fprintf(stderr, "%s:%d: %s: " fmt "\n", __FILE__, __LINE__, __func__, \
                ##__VA_ARGS__);                                               \
        exit(EXIT_FAILURE);                                                   \
    } while (0);

static char name[IFNAMSIZ];

int create(void)
{
    int fd = socket(PF_SYSTEM, SOCK_DGRAM, SYSPROTO_CONTROL);
    if (fd < 0) {
        fatal("failed to get fd");
    }

    struct ctl_info ci = { 0 };
    strlcpy(ci.ctl_name, UTUN_CONTROL_NAME, sizeof(ci.ctl_name));
    int r = ioctl(fd, CTLIOCGINFO, &ci);
    if (r) {
        fatal("failed to ioctl CTLIOCGINFO: %d", r);
    }

    struct sockaddr_ctl sc = {
        .sc_len = sizeof(sc),
        .sc_id = ci.ctl_id,
        .sc_family = AF_SYSTEM,
        .ss_sysaddr = AF_SYS_CONTROL,
    };
    if ((r = connect(fd, (struct sockaddr *)&sc, sizeof(sc)))) {
        fatal("failed to connect: %d", r);
    }

    uint8_t nblock = 1;
    if ((r = ioctl(fd, FIONBIO, &nblock))) {
        fatal("failed to ioctl FIONBIO: %d", r);
    }

    socklen_t len = IFNAMSIZ;
    if ((r = getsockopt(fd, SYSPROTO_CONTROL, UTUN_OPT_IFNAME, name, &len))) {
        fatal("failed to get iface name");
    }

    return fd;
}

int main(void)
{
    int fd = create();
    info("created iface %s", name);
    getchar();

    return 0;
}
