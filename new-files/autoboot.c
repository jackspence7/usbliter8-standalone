#include "autoboot.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "hardware/regs/addressmap.h"
#include "pico/stdlib.h"

#include "bus.h"
#include "log.h"
#include "usb.h"

/*
 * Standalone tethered boot: after the exploit succeeds, send the
 * patched iBoot (surrealra1n's boot/<model>/<version>/iBSS.boot)
 * the same way usbliter8ctl's "boot" command does, straight from
 * this Pico's flash. No computer needed.
 *
 * The image is written to flash by make_boot_uf2.py, which appends
 * it to the firmware UF2 at AUTOBOOT_FLASH_OFFSET:
 *
 *   +0x000  struct autoboot_header (padded to 0x100)
 *   +0x100  image bytes
 */

#define AUTOBOOT_FLASH_OFFSET   (0x100000)
#define AUTOBOOT_DATA_OFFSET    (0x100)
#define AUTOBOOT_MAGIC          "L8BOOT01"

struct autoboot_header {
    char     magic[8];
    uint32_t size;
    uint32_t crc32;
} __attribute__((packed));

#define HEADER  ((const struct autoboot_header *)(XIP_BASE + AUTOBOOT_FLASH_OFFSET))
#define IMAGE   ((const uint8_t *)(XIP_BASE + AUTOBOOT_FLASH_OFFSET + AUTOBOOT_DATA_OFFSET))

#define MAX_IMAGE_SIZE  (PICO_FLASH_SIZE_BYTES - AUTOBOOT_FLASH_OFFSET - AUTOBOOT_DATA_OFFSET)

/*
 * usbliter8ctl sends 0x800 per DFU_DNLOAD, but PIO USB's control pipe
 * keeps the OUT data length in a uint8_t. The ROM's DFU handler
 * appends whatever size it gets, so smaller chunks work the same.
 */
#define CHUNK_SIZE  (0xC0)

enum {
    DFU_DNLOAD    = 1,
    DFU_ABORT     = 4,
    CUSTOM_BOOT   = 8
};

// ======================== Image check ========================

static uint32_t crc32_table[256];

static uint32_t crc32(const uint8_t *buf, uint32_t len) {
    if (!crc32_table[1]) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++) {
                c = (c & 1) ? (0xEDB88320 ^ (c >> 1)) : (c >> 1);
            }
            crc32_table[i] = c;
        }
    }

    uint32_t crc = 0xFFFFFFFF;

    for (uint32_t i = 0; i < len; i++) {
        crc = crc32_table[(crc ^ buf[i]) & 0xFF] ^ (crc >> 8);
    }

    return crc ^ 0xFFFFFFFF;
}

bool autoboot_image_check(void) {
    const struct autoboot_header *h = HEADER;

    if (memcmp(h->magic, AUTOBOOT_MAGIC, sizeof(h->magic)) != 0) {
        INFO("autoboot: no boot image in flash, exploit only");
        return false;
    }

    if (h->size == 0 || h->size > MAX_IMAGE_SIZE) {
        INFO("autoboot: bad boot image size 0x%lx", (unsigned long)h->size);
        return false;
    }

    uint32_t crc = crc32(IMAGE, h->size);
    if (crc != h->crc32) {
        INFO("autoboot: boot image CRC mismatch (0x%08lx != 0x%08lx)",
             (unsigned long)crc, (unsigned long)h->crc32);
        return false;
    }

    INFO("autoboot: boot image OK, 0x%lx bytes, crc 0x%08lx",
         (unsigned long)h->size, (unsigned long)crc);

    return true;
}

// ======================== USB helpers (run on the USB core) ========================

struct setup_req {
    uint8_t  bmRequestType;
    uint8_t  bRequest;
    uint16_t wValue;
    uint16_t wIndex;
    uint16_t wLength;
} __attribute__((packed));

struct dev_desc_head {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint16_t bcdUSB;
    uint8_t  bDeviceClass;
    uint8_t  bDeviceSubClass;
    uint8_t  bDeviceProtocol;
    uint8_t  bMaxPacketSize;
    uint16_t idVendor;
    uint16_t idProduct;
} __attribute__((packed));

static int read_pid_internal(bus_t *b, void *ctx) {
    struct dev_desc_head desc = { 0 };

    struct setup_req req = {
        .bmRequestType = 0x80,
        .bRequest = 0x06,
        .wValue  = 0x0100,
        .wIndex  = 0x0000,
        .wLength = sizeof(desc)
    };

    if (bus_control_xfer(b, (uint8_t *)&req, (uint8_t *)&desc, sizeof(desc), true, 100) != 0) {
        return -1;
    }

    *(uint16_t *)ctx = (desc.idVendor == 0x5AC) ? desc.idProduct : 0;

    return 0;
}

/*
 * A Mac assigns an address and selects configuration 1 before
 * usbliter8ctl talks to the phone; do the same here.
 */
static int enumerate_internal(bus_t *b, __unused void *ctx) {
    struct setup_req set_address = { 0x00, 0x05, 1, 0, 0 };
    struct setup_req set_config  = { 0x00, 0x09, 1, 0, 0 };

    if (bus_control_xfer(b, (uint8_t *)&set_address, NULL, 0, false, 100) != 0) {
        return -1;
    }

    sleep_ms(10);

    bus_close_all(b);
    b->dev->address = 1;

    if (!bus_open_ep0(b, 64)) {
        return -1;
    }

    return bus_control_xfer(b, (uint8_t *)&set_config, NULL, 0, false, 100);
}

struct request_ctx {
    uint8_t         bRequest;
    const uint8_t  *src;
    uint16_t        len;
    uint32_t        timeout_ms;
};

static uint8_t chunk_buf[CHUNK_SIZE];

static int request_internal(bus_t *b, void *_ctx) {
    struct request_ctx *ctx = _ctx;

    struct setup_req req = {
        .bmRequestType = 0x21,
        .bRequest = ctx->bRequest,
        .wValue  = 0,
        .wIndex  = 0,
        .wLength = ctx->len
    };

    if (ctx->len) {
        memcpy(chunk_buf, ctx->src, ctx->len);
    }

    return bus_control_xfer(b, (uint8_t *)&req, ctx->len ? chunk_buf : NULL, ctx->len, false, ctx->timeout_ms);
}

/*
 * Recovery Mode's iBoot console accepts ASCII commands ("reboot", "go",
 * "bgcolor", ...) via a vendor control OUT transfer: bmRequestType 0x40,
 * bRequest 0, the command as a NUL-terminated string, wLength = strlen+1.
 * This is exactly what `irecovery -n` (libirecovery's irecv_reboot(), which
 * calls irecv_send_command_raw(client, "reboot", 0)) sends over a real USB
 * host - confirmed by disassembling the irecovery binary shipped in
 * surrealra1n/bin. If Volume Down is held on the phone when this lands,
 * the SecureROM comes back up in DFU instead of continuing to boot.
 */
static const char RECOVERY_REBOOT_CMD[] = "reboot";

static int recovery_reboot_internal(bus_t *b, __unused void *ctx) {
    static uint8_t cmd_buf[sizeof(RECOVERY_REBOOT_CMD)];
    memcpy(cmd_buf, RECOVERY_REBOOT_CMD, sizeof(cmd_buf));

    struct setup_req req = {
        .bmRequestType = 0x40,
        .bRequest = 0x00,
        .wValue  = 0,
        .wIndex  = 0,
        .wLength = sizeof(cmd_buf)
    };

    return bus_control_xfer(b, (uint8_t *)&req, cmd_buf, sizeof(cmd_buf), false, 1000);
}

// ======================== Public ========================

#define USB_TIMEOUT_US  (2 * 1000 * 1000)

int autoboot_read_pid(uint16_t *pid) {
    return usb_bus_execute(read_pid_internal, pid, USB_TIMEOUT_US);
}

int autoboot_send_recovery_reboot(void) {
    return usb_bus_execute(recovery_reboot_internal, NULL, USB_TIMEOUT_US);
}

static int send_request(uint8_t bRequest, const uint8_t *src, uint16_t len, uint32_t timeout_ms) {
    struct request_ctx ctx = { bRequest, src, len, timeout_ms };
    return usb_bus_execute(request_internal, &ctx, USB_TIMEOUT_US);
}

int autoboot_boot(void) {
    const uint32_t size = HEADER->size;

    INFO("autoboot: enumerating");

    if (usb_bus_execute(enumerate_internal, NULL, USB_TIMEOUT_US) != 0) {
        INFO("autoboot: enumeration failed, trying unconfigured at address 0");
        usb_bus_reset_open_ep0();
    }

    INFO("autoboot: sending boot image (0x%lx bytes)", (unsigned long)size);

    absolute_time_t start = get_absolute_time();

    for (uint32_t off = 0; off < size; off += CHUNK_SIZE) {
        uint16_t len = (size - off < CHUNK_SIZE) ? (size - off) : CHUNK_SIZE;

        if (send_request(DFU_DNLOAD, IMAGE + off, len, 1000) != 0) {
            INFO("\nautoboot: DFU_DNLOAD failed at 0x%lx", (unsigned long)off);
            return -1;
        }

        if ((off & 0xFFFF) < CHUNK_SIZE) {
            printf("\rsent - 0x%lx", (unsigned long)off);
        }
    }

    INFO("\rsent - 0x%lx", (unsigned long)size);

    // same tail as usbliter8ctl: empty DNLOAD, boot, abort
    if (send_request(DFU_DNLOAD, NULL, 0, 100) != 0) {
        INFO("autoboot: final DFU_DNLOAD failed");
        return -1;
    }

    if (send_request(CUSTOM_BOOT, NULL, 0, 100) != 0) {
        INFO("autoboot: CUSTOM_BOOT failed");
        return -1;
    }

    // the device leaves DFU here, so an error is expected sometimes
    send_request(DFU_ABORT, NULL, 0, 100);

    int64_t delta = absolute_time_diff_us(start, get_absolute_time());

    INFO("autoboot: took - %lldms", delta / 1000);
    INFO("autoboot: boot image sent, device should now boot");

    return 0;
}
