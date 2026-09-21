#pragma once

#include <stdbool.h>
#include <stdint.h>

bool autoboot_image_check(void);

int autoboot_read_pid(uint16_t *pid);

int autoboot_boot(void);

// Sends the same "reboot" vendor command `irecovery -n` sends to a device
// sitting in Recovery Mode. If Volume Down is held when the device reboots,
// it lands in DFU instead of continuing to boot normally.
int autoboot_send_recovery_reboot(void);
