// sleepkey.c: see sleepkey.h. A uinput device with one key, KEY_POWER. bananad reads every input device (it watches
// /dev/input for new ones), so a press and release on it is a short press of the power button to bananad: its whole
// careful sleep (the game paused, Wi-Fi down, the battery check's alarm) and its wake.
#include "sleepkey.h"
#include "plat/plat.h"
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <sys/ioctl.h>
#include <linux/input.h>
#include <linux/uinput.h>

static int fd = -1;

void sleepkey_init(void) {
    if (fd >= 0) return;
    fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) { plat_log("sleep key: no /dev/uinput: the menu's Sleep isn't available"); return; }
    ioctl(fd, UI_SET_EVBIT, EV_KEY);
    ioctl(fd, UI_SET_KEYBIT, KEY_POWER);
    struct uinput_setup us; memset(&us, 0, sizeof us);
    us.id.bustype = BUS_VIRTUAL; us.id.vendor = 0x1d50; us.id.product = 0xd51;
    strcpy(us.name, "DSi Menu sleep key");
    if (ioctl(fd, UI_DEV_SETUP, &us) || ioctl(fd, UI_DEV_CREATE)) { plat_log("sleep key: uinput refused"); close(fd); fd = -1; }
}

static void emit(int type, int code, int value) {
    struct input_event e; memset(&e, 0, sizeof e);
    e.type = (unsigned short)type; e.code = (unsigned short)code; e.value = value;
    if (write(fd, &e, sizeof e) != sizeof e) {}
}

int sleepkey_tap(void) {
    if (fd < 0) return -1;
    emit(EV_KEY, KEY_POWER, 1); emit(EV_SYN, SYN_REPORT, 0);
    usleep(60000);
    emit(EV_KEY, KEY_POWER, 0); emit(EV_SYN, SYN_REPORT, 0);
    return 0;
}

void sleepkey_close(void) {
    if (fd < 0) return;
    ioctl(fd, UI_DEV_DESTROY); close(fd); fd = -1;
}
