/*
 * QEMU USB HID devices
 *
 * Copyright (c) 2005 Fabrice Bellard
 * Copyright (c) 2007 OpenMoko, Inc.  (andrew@openedhand.com)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#include "qemu/osdep.h"
#include "ui/console.h"
#include "hw/usb.h"
#include "hw/hw.h"
#include "migration/vmstate.h"
#include "desc.h"
#include "qapi/error.h"
#include "qemu/module.h"
#include "qemu/timer.h"
#include "hw/input/hid.h"
#include "hw/qdev-properties.h"
#include "hw/usb/savira-kotigalu-hid.h"
#include "hw/usb/char_to_scancodes_map.h"
#include "hw/usb/control_keys.h"
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <limits.h>
#include <unistd.h>
#include <poll.h>

/* HID interface requests */
#define GET_REPORT   0xa101
#define GET_IDLE     0xa102
#define GET_PROTOCOL 0xa103
#define SET_REPORT   0x2109
#define SET_IDLE     0x210a
#define SET_PROTOCOL 0x210b

/* HID descriptor types */
#define USB_DT_HID    0x21
#define USB_DT_REPORT 0x22
#define USB_DT_PHY    0x23

typedef struct USBHIDState {
    USBDevice dev;
    USBEndpoint *intr;
    HIDState hid;
    uint32_t usb_version;
    char *display;
    uint32_t head;
} USBHIDState;

#define TYPE_USB_HID "katigalu-usb-hid"
#define USB_HID(obj) OBJECT_CHECK(USBHIDState, (obj), TYPE_USB_HID)

enum {
    STR_MANUFACTURER = 1,
    STR_PRODUCT_KEYBOARD,
    STR_SERIAL_COMPAT,
    STR_CONFIG_KEYBOARD,
    STR_SERIAL_KEYBOARD,
};

static const USBDescStrings desc_strings = {
        [STR_MANUFACTURER]     = "Monash",
        [STR_PRODUCT_KEYBOARD] = "Savira Kotigalu",
        [STR_SERIAL_COMPAT]    = "42",
        [STR_CONFIG_KEYBOARD]  = "HID Keyboard",
        [STR_SERIAL_KEYBOARD]  = "68284",
};

static const USBDescIface desc_iface_keyboard = {
        .bInterfaceNumber              = 0,
        .bNumEndpoints                 = 1,
        .bInterfaceClass               = USB_CLASS_HID,
        .bInterfaceSubClass            = 0x01, /* boot */
        .bInterfaceProtocol            = 0x01, /* keyboard */
        .ndesc                         = 1,
        .descs = (USBDescOther[]) {
                {
                        /* HID descriptor */
                        .data = (uint8_t[]) {
                                0x09,          /*  u8  bLength */
                                USB_DT_HID,    /*  u8  bDescriptorType */
                                0x11, 0x01,    /*  u16 HID_class */
                                0x00,          /*  u8  country_code */
                                0x01,          /*  u8  num_descriptors */
                                USB_DT_REPORT, /*  u8  type: Report */
                                0x3f, 0,       /*  u16 len */
                        },
                },
        },
        .eps = (USBDescEndpoint[]) {
                {
                        .bEndpointAddress      = USB_DIR_IN | 0x01,
                        .bmAttributes          = USB_ENDPOINT_XFER_INT,
                        .wMaxPacketSize        = 8,
                        .bInterval             = 0x0a,
                },
        },
};

static const USBDescIface desc_iface_keyboard2 = {
        .bInterfaceNumber              = 0,
        .bNumEndpoints                 = 1,
        .bInterfaceClass               = USB_CLASS_HID,
        .bInterfaceSubClass            = 0x01, /* boot */
        .bInterfaceProtocol            = 0x01, /* keyboard */
        .ndesc                         = 1,
        .descs = (USBDescOther[]) {
                {
                        /* HID descriptor */
                        .data = (uint8_t[]) {
                                0x09,          /*  u8  bLength */
                                USB_DT_HID,    /*  u8  bDescriptorType */
                                0x11, 0x01,    /*  u16 HID_class */
                                0x00,          /*  u8  country_code */
                                0x01,          /*  u8  num_descriptors */
                                USB_DT_REPORT, /*  u8  type: Report */
                                0x3f, 0,       /*  u16 len */
                        },
                },
        },
        .eps = (USBDescEndpoint[]) {
                {
                        .bEndpointAddress      = USB_DIR_IN | 0x01,
                        .bmAttributes          = USB_ENDPOINT_XFER_INT,
                        .wMaxPacketSize        = 8,
                        .bInterval             = 7, /* 2 ^ (8-1) * 125 usecs = 8 ms */
                },
        },
};

static const USBDescDevice desc_device_keyboard = {
        .bcdUSB                        = 0x0100,
        .bMaxPacketSize0               = 8,
        .bNumConfigurations            = 1,
        .confs = (USBDescConfig[]) {
                {
                        .bNumInterfaces        = 1,
                        .bConfigurationValue   = 1,
                        .iConfiguration        = STR_CONFIG_KEYBOARD,
                        .bmAttributes          = USB_CFG_ATT_ONE | USB_CFG_ATT_WAKEUP,
                        .bMaxPower             = 50,
                        .nif = 1,
                        .ifs = &desc_iface_keyboard,
                },
        },
};

static const USBDescDevice desc_device_keyboard2 = {
        .bcdUSB                        = 0x0200,
        .bMaxPacketSize0               = 64,
        .bNumConfigurations            = 1,
        .confs = (USBDescConfig[]) {
                {
                        .bNumInterfaces        = 1,
                        .bConfigurationValue   = 1,
                        .iConfiguration        = STR_CONFIG_KEYBOARD,
                        .bmAttributes          = USB_CFG_ATT_ONE | USB_CFG_ATT_WAKEUP,
                        .bMaxPower             = 50,
                        .nif = 1,
                        .ifs = &desc_iface_keyboard2,
                },
        },
};

static const USBDescMSOS desc_msos_suspend = {
        .SelectiveSuspendEnabled = true,
};

static const USBDesc desc_keyboard = {
        .id = {
                .idVendor          = 0xdead,
                .idProduct         = 0xbeef,
                .bcdDevice         = 0,
                .iManufacturer     = STR_MANUFACTURER,
                .iProduct          = STR_PRODUCT_KEYBOARD,
                .iSerialNumber     = STR_SERIAL_KEYBOARD,
        },
        .full = &desc_device_keyboard,
        .str  = desc_strings,
        .msos = &desc_msos_suspend,
};

static const USBDesc desc_keyboard2 = {
        .id = {
                .idVendor          = 0xdead,
                .idProduct         = 0xbeef,
                .bcdDevice         = 0,
                .iManufacturer     = STR_MANUFACTURER,
                .iProduct          = STR_PRODUCT_KEYBOARD,
                .iSerialNumber     = STR_SERIAL_KEYBOARD,
        },
        .full = &desc_device_keyboard,
        .high = &desc_device_keyboard2,
        .str  = desc_strings,
        .msos = &desc_msos_suspend,
};

static const uint8_t qemu_keyboard_hid_report_descriptor[] = {
        0x05, 0x01,		/* Usage Page (Generic Desktop) */
        0x09, 0x06,		/* Usage (Keyboard) */
        0xa1, 0x01,		/* Collection (Application) */
        0x75, 0x01,		/*   Report Size (1) */
        0x95, 0x08,		/*   Report Count (8) */
        0x05, 0x07,		/*   Usage Page (Key Codes) */
        0x19, 0xe0,		/*   Usage Minimum (224) */
        0x29, 0xe7,		/*   Usage Maximum (231) */
        0x15, 0x00,		/*   Logical Minimum (0) */
        0x25, 0x01,		/*   Logical Maximum (1) */
        0x81, 0x02,		/*   Input (Data, Variable, Absolute) */
        0x95, 0x01,		/*   Report Count (1) */
        0x75, 0x08,		/*   Report Size (8) */
        0x81, 0x01,		/*   Input (Constant) */
        0x95, 0x05,		/*   Report Count (5) */
        0x75, 0x01,		/*   Report Size (1) */
        0x05, 0x08,		/*   Usage Page (LEDs) */
        0x19, 0x01,		/*   Usage Minimum (1) */
        0x29, 0x05,		/*   Usage Maximum (5) */
        0x91, 0x02,		/*   Output (Data, Variable, Absolute) */
        0x95, 0x01,		/*   Report Count (1) */
        0x75, 0x03,		/*   Report Size (3) */
        0x91, 0x01,		/*   Output (Constant) */
        0x95, 0x06,		/*   Report Count (6) */
        0x75, 0x08,		/*   Report Size (8) */
        0x15, 0x00,		/*   Logical Minimum (0) */
        0x25, 0xff,		/*   Logical Maximum (255) */
        0x05, 0x07,		/*   Usage Page (Key Codes) */
        0x19, 0x00,		/*   Usage Minimum (0) */
        0x29, 0xff,		/*   Usage Maximum (255) */
        0x81, 0x00,		/*   Input (Data, Array) */
        0xc0,		/* End Collection */
};

static data_buffer fifo_buffer = {0};

static int fifo_fd = 0;

static char fifo_path[PATH_MAX];

char control_input[5];

static void usb_hid_changed(HIDState *hs)
{
    USBHIDState *us = container_of(hs, USBHIDState, hid);

    usb_wakeup(us->intr, 0);
}

static void usb_hid_handle_reset(USBDevice *dev)
{
    USBHIDState *us = USB_HID(dev);

    hid_reset(&us->hid);
}

static void usb_hid_handle_control(USBDevice *dev, USBPacket *p,
                                   int request, int value, int index, int length, uint8_t *data)
{
    USBHIDState *us = USB_HID(dev);
    HIDState *hs = &us->hid;
    int ret;

    ret = usb_desc_handle_control(dev, p, request, value, index, length, data);
    if (ret >= 0) {
        return;
    }

    switch (request) {
        /* hid specific requests */
        case InterfaceRequest | USB_REQ_GET_DESCRIPTOR:
            switch (value >> 8) {
                case 0x22:
                    if (hs->kind == HID_KEYBOARD) {
                        memcpy(data, qemu_keyboard_hid_report_descriptor,
                               sizeof(qemu_keyboard_hid_report_descriptor));
                        p->actual_length = sizeof(qemu_keyboard_hid_report_descriptor);
                    }
                    break;
                default:
                    goto fail;
            }
            break;
        case GET_REPORT:
            if (hs->kind == HID_KEYBOARD) {
                p->actual_length = hid_keyboard_poll(hs, data, length);
            }
            break;
        case SET_REPORT:
            if (hs->kind == HID_KEYBOARD) {
                p->actual_length = hid_keyboard_write(hs, data, length);
            } else {
                goto fail;
            }
            break;
        case GET_PROTOCOL:
            if (hs->kind != HID_KEYBOARD) {
                goto fail;
            }
            data[0] = hs->protocol;
            p->actual_length = 1;
            break;
        case SET_PROTOCOL:
            if (hs->kind != HID_KEYBOARD) {
                goto fail;
            }
            hs->protocol = value;
            break;
        case GET_IDLE:
            data[0] = hs->idle;
            p->actual_length = 1;
            break;
        case SET_IDLE:
            hs->idle = (uint8_t) (value >> 8);
            hid_set_next_idle(hs);
            break;
        default:
        fail:
            p->status = USB_RET_STALL;
            break;
    }
}

static void usb_hid_handle_data(USBDevice *dev, USBPacket *p)
{
    USBHIDState *us = USB_HID(dev);
    HIDState *hs = &us->hid;
    uint8_t buf[p->iov.size];

    struct timeval current_time;
    struct timeval time_diff;
    gettimeofday(&current_time, NULL);
    static struct timeval last_simulated_packet_time = {0, 0};
    static int delay = 218182;

    int len = 0;
    switch (p->pid) {
        case USB_TOKEN_IN:
            if (p->ep->nr == 1) {
                if (hs->kind == HID_KEYBOARD) {
                    len = hid_keyboard_poll(hs, buf, p->iov.size);
                    timersub(&current_time, &last_simulated_packet_time, &time_diff);
                    if (time_diff.tv_sec > 1 || time_diff.tv_usec > delay) {
                        last_simulated_packet_time = current_time;
                        update_token(buf);
                    }
                    usb_packet_copy(p, buf, len);
                }
            } else {
                goto fail;
            }
            break;
        case USB_TOKEN_OUT:
        default:
        fail:
            p->status = USB_RET_STALL;
            break;
    }
}

static int update_token(uint8_t *token_buffer) {
    if (load_next_char(fifo_buffer) == 0) {
        decode_scancodes(fifo_buffer, token_buffer);
        return 1;
    } else {
        return 0;
    }
}

static void initialise_fifo_buffer(void) {
    fifo_buffer.index = 80;
    fifo_buffer.len = fifo_buffer.index;
    fifo_buffer.mem = malloc(fifo_buffer.len);
}

static int load_next_char(struct data_buffer buffer) {
    fifo_buffer.index++;

    if (fifo_buffer.index == fifo_buffer.len || fifo_buffer.mem[fifo_buffer.index] == '\0') {
        read_from_fifo();
    }

    if (fifo_buffer.mem[fifo_buffer.index] == '\0') {
        return 1;
    } else {
        return 0;
    }
}

static int get_control_input (struct data_buffer buffer) {
    int i = 0;
    load_next_char(buffer);

    switch ( fifo_buffer.mem[fifo_buffer.index] ) {
         case 'f':
             control_input[i++] = fifo_buffer.mem[fifo_buffer.index];
             load_next_char(buffer);
             control_input[i++] = fifo_buffer.mem[fifo_buffer.index];
             control_input[i] = '\0';
             break;
         case 'e':
             control_input[i++] = fifo_buffer.mem[fifo_buffer.index];
             load_next_char(buffer);
             if (fifo_buffer.mem[fifo_buffer.index] == 's' || fifo_buffer.mem[fifo_buffer.index] == 'n') {
                 control_input[i++] = fifo_buffer.mem[fifo_buffer.index];
                 load_next_char(buffer);
                 control_input[i++] = fifo_buffer.mem[fifo_buffer.index];
                 control_input[i] = '\0';
             }
             break;
         case 'd':
             control_input[i++] = fifo_buffer.mem[fifo_buffer.index];
             load_next_char(buffer);

             if (fifo_buffer.mem[fifo_buffer.index] == 'e' ) {
                 control_input[i++] = fifo_buffer.mem[fifo_buffer.index];
                 load_next_char(buffer);
                 control_input[i++] = fifo_buffer.mem[fifo_buffer.index];
                 control_input[i] = '\0';
             } else {
                 fifo_buffer.index--;
                 control_input[i] = '\0';
             }
             break;
         case 'p':
             control_input[i++] = fifo_buffer.mem[fifo_buffer.index];
             load_next_char(buffer);
             control_input[i++] = fifo_buffer.mem[fifo_buffer.index];
             control_input[i] = '\0';
             break;
         case 'b':
             control_input[i++] = fifo_buffer.mem[fifo_buffer.index];
             load_next_char(buffer);
             control_input[i++] = fifo_buffer.mem[fifo_buffer.index];
             control_input[i] = '\0';
             break;
         case 'c':
             control_input[i++] = fifo_buffer.mem[fifo_buffer.index];
             load_next_char(buffer);
             if ( fifo_buffer.mem[fifo_buffer.index] == 'r' ) {
                 control_input[i++] = fifo_buffer.mem[fifo_buffer.index];
                 control_input[i] = '\0';
             } else {
                 fifo_buffer.index--;
                 control_input[i] = '\0';
             }
             break;
         default:
             control_input[i++] = fifo_buffer.mem[fifo_buffer.index];
             control_input[i] = '\0';
             break;
     }
     return 0;
}


static int convert_to_hex(void) {
    char *temp;
    char *hexa_val;
    char *final_val;
    int hexa_equi_dec_val;
    int size = 8;
    int i;

    hexa_val = (char*)calloc(size, sizeof(char));
    temp = (char*)calloc(size, sizeof(char));

    for (i = 0; i < sizeof(control_input); i++) {
        if (control_input[i] != '\0') {
            sprintf(temp, "%02x", control_input[i]);
            strcat (hexa_val, temp);
        } else {
            break;
        }

    }

    final_val = (char*)calloc(size, sizeof(char));

    /* Add 0x to the beginning of the hex val */
    sprintf (final_val,"0x%s", hexa_val);
    /* convert into hexa string to decimal value for switch case */
    sscanf(final_val,"%x", &hexa_equi_dec_val);
    return hexa_equi_dec_val;
}


static void decode_scancodes(struct data_buffer buffer, uint8_t *token_buffer) {

    int val;

    if (fifo_buffer.mem[fifo_buffer.index] == '\\') {
        /* Sniff for control inputs like BACKSPACE, CTRL, SHIFT, ESC, UP, etc in the input text and grab it */
        get_control_input ( buffer );

        /* Get the decimal equivalent suitable for switch case */
        val = convert_to_hex();

        switch (val) {
            case BACKSLASH:
                decode_standard_code(buffer, token_buffer);
                break;
            case CTRL:
                add_modifier(KEY_MOD_LCTRL, token_buffer);
                break;
            case ALT:
                add_modifier(KEY_MOD_LALT, token_buffer);
                break;
            case SHIFT:
                add_modifier(KEY_MOD_LSHIFT, token_buffer);
                break;
            case META:
                add_modifier(KEY_MOD_LMETA, token_buffer);
                break;
            case TAB:
                set_key(KEY_TAB, token_buffer);
                break;
            case ESC:
                set_key(KEY_ESC, token_buffer);
                break;
            case UP:
                set_key(KEY_UP, token_buffer);
                break;
            case DOWN:
                set_key(KEY_DOWN, token_buffer);
                break;
            case LEFT:
                set_key(KEY_LEFT, token_buffer);
                break;
            case RIGHT:
                set_key(KEY_RIGHT, token_buffer);
                break;
            case INSERT:
                set_key(KEY_INSERT, token_buffer);
                break;
            case HOME:
                set_key(KEY_HOME, token_buffer);
                break;
            case PAGEUP:
                set_key(KEY_PAGEUP, token_buffer);
                break;
            case DELETE:
                set_key(KEY_DELETE, token_buffer);
                break;
            case END:
                set_key(KEY_END, token_buffer);
                break;
            case PAGEDOWN:
                set_key(KEY_PAGEDOWN, token_buffer);
                break;
            case BACKSPACE:
                set_key(KEY_BACKSPACE, token_buffer);
                break;
            case ENTER:
                set_key(KEY_KPENTER, token_buffer);
                break;
            case F1:
                set_key(KEY_F1, token_buffer);
                break;
            case F2:
                set_key(KEY_F2, token_buffer);
                break;
            case F3:
                set_key(KEY_F3, token_buffer);
                break;
            case F4:
                set_key(KEY_F4, token_buffer);
                break;
            case F5:
                set_key(KEY_F5, token_buffer);
                break;
            case F6:
                set_key(KEY_F6, token_buffer);
                break;
            case F7:
                set_key(KEY_F7, token_buffer);
                break;
            case F8:
                set_key(KEY_F8, token_buffer);
                break;
            case F9:
                set_key(KEY_F9, token_buffer);
                break;
            case F10:
                set_key(KEY_F10, token_buffer);
                break;
            case F11:
                set_key(KEY_F11, token_buffer);
                break;
            case F12:
                set_key(KEY_F12, token_buffer);
                break;
            default:
                break;
        }

        load_next_char(buffer);
        if (fifo_buffer.mem[fifo_buffer.index] == '+') {
            load_next_char(buffer);
            decode_scancodes(buffer, token_buffer);
        }
    } else {
        decode_standard_code(buffer, token_buffer);
    }
}

static void decode_standard_code(struct data_buffer buffer, uint8_t *token_buffer) {
    key_decoder scancode_instructions = char_to_scancodes[(int) fifo_buffer.mem[fifo_buffer.index]];
    add_modifier(scancode_instructions.mod, token_buffer);
    set_key(scancode_instructions.key, token_buffer);
}

static void add_modifier(uint8_t modifer, uint8_t *token_buffer) {
    token_buffer[0] |= modifer;
}

static void set_key (uint8_t key, uint8_t *token_buffer) {
    token_buffer[2] = key;
}

static int read_from_fifo(void) {
    memset(fifo_buffer.mem, 0, fifo_buffer.len);
    fifo_buffer.index = 0;
    return read(fifo_fd, fifo_buffer.mem, fifo_buffer.len);
}

static void initialise_fifo_fd(void) {
    const char * fifo_dir = "/tmp/kotigalu";
    const char * fifo_base = "/tmp/kotigalu/buffer_";
    pid_t processid;
    char processidstr[20];
    struct stat sb;

    /* Create the folder to keep fifo's if not exists */
    if ( stat(fifo_dir, &sb) != 0 ) {
        mkdir (fifo_dir, 0755);
    }
    processid = getpid();
    sprintf(processidstr, "%d", processid);
    strcat(fifo_path, fifo_base);
    strcat(fifo_path, processidstr);
    mkfifo(fifo_path, 0666);
    fifo_fd = open(fifo_path, O_RDONLY | O_NONBLOCK);
}

static void usb_hid_unrealize(USBDevice *dev, Error **errp)
{
    USBHIDState *us = USB_HID(dev);

    hid_free(&us->hid);

    close(fifo_fd);
    remove(fifo_path);

    free(fifo_buffer.mem);
}

static void usb_hid_initfn(USBDevice *dev, int kind,
                           const USBDesc *usb1, const USBDesc *usb2,
                           Error **errp)
{
    USBHIDState *us = USB_HID(dev);
    switch (us->usb_version) {
        case 1:
            dev->usb_desc = usb1;
            break;
        case 2:
            dev->usb_desc = usb2;
            break;
        default:
            dev->usb_desc = NULL;
    }
    if (!dev->usb_desc) {
        error_setg(errp, "Invalid usb version %d for usb hid device",
                   us->usb_version);
        return;
    }

    usb_desc_create_serial(dev);
    usb_desc_init(dev);
    us->intr = usb_ep_get(dev, USB_TOKEN_IN, 1);
    hid_init(&us->hid, kind, usb_hid_changed);
    if (us->display && us->hid.s) {
        qemu_input_handler_bind(us->hid.s, us->display, us->head, NULL);
    }
}

static void usb_keyboard_realize(USBDevice *dev, Error **errp)
{
    usb_hid_initfn(dev, HID_KEYBOARD, &desc_keyboard, &desc_keyboard2, errp);
    initialise_fifo_fd();
    initialise_fifo_buffer();
}

static const VMStateDescription vmstate_usb_kbd = {
        .name = "usb-kbd",
        .version_id = 1,
        .minimum_version_id = 1,
        .fields = (VMStateField[]) {
                VMSTATE_USB_DEVICE(dev, USBHIDState),
                VMSTATE_HID_KEYBOARD_DEVICE(hid, USBHIDState),
                VMSTATE_END_OF_LIST()
        }
};

static void usb_hid_class_initfn(ObjectClass *klass, void *data)
{
    USBDeviceClass *uc = USB_DEVICE_CLASS(klass);

    uc->handle_reset   = usb_hid_handle_reset;
    uc->handle_control = usb_hid_handle_control;
    uc->handle_data    = usb_hid_handle_data;
    uc->unrealize      = usb_hid_unrealize;
    uc->handle_attach  = usb_desc_attach;
}

static const TypeInfo usb_hid_type_info = {
        .name = TYPE_USB_HID,
        .parent = TYPE_USB_DEVICE,
        .instance_size = sizeof(USBHIDState),
        .abstract = true,
        .class_init = usb_hid_class_initfn,
};

static Property usb_keyboard_properties[] = {
        DEFINE_PROP_UINT32("usb_version", USBHIDState, usb_version, 2),
        DEFINE_PROP_STRING("display", USBHIDState, display),
        DEFINE_PROP_END_OF_LIST(),
};

static void usb_keyboard_class_initfn(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    USBDeviceClass *uc = USB_DEVICE_CLASS(klass);

    uc->realize        = usb_keyboard_realize;
    uc->product_desc   = "Savira Kotigalu";
    dc->vmsd = &vmstate_usb_kbd;
    dc->props = usb_keyboard_properties;
    set_bit(DEVICE_CATEGORY_INPUT, dc->categories);
}

static const TypeInfo usb_keyboard_info = {
        .name          = "usb-kbd",
        .parent        = TYPE_USB_HID,
        .class_init    = usb_keyboard_class_initfn,
};

static void usb_hid_register_types(void)
{
    type_register_static(&usb_hid_type_info);
    type_register_static(&usb_keyboard_info);
    usb_legacy_register("usb-kbd", "keyboard", NULL);
}

type_init(usb_hid_register_types)
