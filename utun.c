#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <pwd.h>

#include <sys/ioctl.h>
#include <sys/sys_domain.h>
#include <sys/kern_control.h>
#include <net/if_utun.h>
#include <arpa/inet.h>

#define info(fmt, ...) printf(fmt "\n", ##__VA_ARGS__)
#define fatal(fmt, ...)                                                       \
    do {                                                                      \
        fprintf(stderr, "%s:%d: %s: " fmt "\n", __FILE__, __LINE__, __func__, \
                ##__VA_ARGS__);                                               \
        exit(EXIT_FAILURE);                                                   \
    } while (0)

#define IFACE_MTU 8500

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

    int nblock = 1;
    if ((r = ioctl(fd, FIONBIO, &nblock))) {
        fatal("failed to ioctl FIONBIO: %d", r);
    }

    socklen_t len = IFNAMSIZ;
    if ((r = getsockopt(fd, SYSPROTO_CONTROL, UTUN_OPT_IFNAME, name, &len))) {
        fatal("failed to get iface name");
    }

    return fd;
}

void setup(const char *addr, int mtu)
{
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        fatal("failed to get fd");
    }

    struct sockaddr_in iaddr = {
        .sin_len = sizeof(iaddr),
        .sin_family = AF_INET,
    };
    int r = inet_pton(AF_INET, addr, &iaddr.sin_addr);
    if (!r) {
        fatal("invalid address: %s", addr);
    }

    struct sockaddr_in imask = {
        .sin_len = sizeof(iaddr),
        .sin_family = AF_INET,
        .sin_addr.s_addr = htonl((uint32_t)-1) // 32,
    };

    struct ifaliasreq ifra = { 0 };
    strlcpy(ifra.ifra_name, name, sizeof(ifra.ifra_name));
    memcpy(&ifra.ifra_addr, &iaddr, sizeof(iaddr));
    memcpy(&ifra.ifra_broadaddr, &iaddr, sizeof(iaddr));
    memcpy(&ifra.ifra_mask, &imask, sizeof(imask));

    if ((r = ioctl(fd, SIOCAIFADDR, &ifra))) {
        fatal("failed to set address");
    }

    struct ifreq ifr = { .ifr_mtu = mtu };
    strlcpy(ifr.ifr_name, name, sizeof(ifr.ifr_name));
    if ((r = ioctl(fd, SIOCSIFMTU, &ifr))) {
        fatal("failed to set mtu");
    }

    close(fd);
}

void drop_root(const char *user)
{
    struct passwd *pw = getpwnam(user);
    if (!pw) {
        fatal("couldn't find %s", user);
    }

    if (setgroups(0, NULL) || setgid(pw->pw_gid) || setuid(pw->pw_uid)) {
        fatal("couldn't change to %s uid=%u gid=%u", user, pw->pw_uid,
              pw->pw_gid);
    }

    if (!setuid(0)) {
        fatal("failed to drop root");
    }
}

int main(int argc, char **argv)
{
    if (argc != 4) {
        fatal("invalid amount of arguments");
    }

    int fd = create();
    info("iface %s created", name);

    char *addr = *(++argv);
    setup(addr, IFACE_MTU);
    info("address %s, mtu %d set", addr, IFACE_MTU);

    char *user = *(++argv);
    drop_root(user);

    char fd_str[16];
    snprintf(fd_str, sizeof(fd_str), "%d", fd);
    char *path = *(++argv);
    char *eargv[] = { path, fd_str, NULL };

    info("exec %s", path);
    (void)execv(path, eargv);
    fatal("failed to exec: %s", strerror(errno));
}
