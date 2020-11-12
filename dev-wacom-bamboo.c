/*
 * Wacom Bamboo CTL-460 tablet emulation.
 *
 * Copyright (c) 2020 Nicholas Sherlock <n.sherlock@gmail.com>
 * 
 * Copyright (c) 2006 Openedhand Ltd.
 * Author: Andrzej Zaborowski <balrog@zabor.org>
 *
 * Based on hw/usb-hid.c:
 * Copyright (c) 2005 Fabrice Bellard
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
#include "migration/vmstate.h"
#include "qemu/module.h"
#include "desc.h"
#include "qemu/timer.h"

/* Interface requests */
#define WACOM_GET_REPORT	0x01
#define WACOM_SET_REPORT	0x09

#define WACOM_REQUEST_GET_MODE 2

/* HID interface requests */
#define HID_GET_REPORT		0x01
#define HID_GET_IDLE		0x02
#define HID_GET_PROTOCOL	0x03
#define HID_SET_IDLE		0x0a
#define HID_SET_PROTOCOL	0x0b

/* HID descriptor types */
#define USB_DT_HID    0x21
#define USB_DT_REPORT 0x22
#define USB_DT_PHY    0x23

#define TABLET_RESOLUTION_X 14720
#define TABLET_RESOLUTION_Y 9200

#define TABLET_NAME_QEMU "QEMU Bamboo tablet"

typedef struct USBWacomState {
    USBDevice dev;
    USBEndpoint *intr;
    QEMUPutMouseEntry *eh_entry;
    int dx, dy, dz, buttons_state;
    int x, y;
    enum {
        WACOM_MODE_HID = 1,
        WACOM_MODE_WACOM = 2,
    } mode;
    uint8_t idle;
    int64_t lastPacketTime;
    bool changedPen;
} USBWacomState;

#define TYPE_USB_WACOM "usb-wacom-tablet-bamboo"
#define USB_WACOM(obj) OBJECT_CHECK(USBWacomState, (obj), TYPE_USB_WACOM)

enum {
    STR_MANUFACTURER = 1,
    STR_PRODUCT
};

static const USBDescStrings desc_strings = {
    [STR_MANUFACTURER]     = "Wacom Co.,Ltd.",
    [STR_PRODUCT]          = "CTL-460"
};

// Generated from CTL-460 dump by https://eleccelerator.com/usbdescreqparser/
static const uint8_t interface_1_hid_report_descriptor[] = {
    0x05, 0x01,        // Usage Page (Generic Desktop Ctrls)
    0x09, 0x02,        // Usage (Mouse)
    0xA1, 0x01,        // Collection (Application)
    0x85, 0x01,        //   Report ID (1)
    0x09, 0x01,        //   Usage (Pointer)
    0xA1, 0x00,        //   Collection (Physical)
    0x05, 0x09,        //     Usage Page (Button)
    0x19, 0x01,        //     Usage Minimum (0x01)
    0x29, 0x05,        //     Usage Maximum (0x05)
    0x15, 0x00,        //     Logical Minimum (0)
    0x25, 0x01,        //     Logical Maximum (1)
    0x95, 0x05,        //     Report Count (5)
    0x75, 0x01,        //     Report Size (1)
    0x81, 0x02,        //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0x95, 0x01,        //     Report Count (1)
    0x75, 0x03,        //     Report Size (3)
    0x81, 0x01,        //     Input (Const,Array,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0x05, 0x01,        //     Usage Page (Generic Desktop Ctrls)
    0x09, 0x30,        //     Usage (X)
    0x09, 0x31,        //     Usage (Y)
    0x15, 0x81,        //     Logical Minimum (-127)
    0x25, 0x7F,        //     Logical Maximum (127)
    0x75, 0x08,        //     Report Size (8)
    0x95, 0x02,        //     Report Count (2)
    0x81, 0x06,        //     Input (Data,Var,Rel,No Wrap,Linear,Preferred State,No Null Position)
    0xC0,              //   End Collection
    0xC0,              // End Collection
    0x05, 0x0D,        // Usage Page (Digitizer)
    0x09, 0x01,        // Usage (Digitizer)
    0xA1, 0x01,        // Collection (Application)
    0x85, 0x02,        //   Report ID (2)
    0xA1, 0x00,        //   Collection (Physical)
    0x06, 0x00, 0xFF,  //     Usage Page (Vendor Defined 0xFF00)
    0x09, 0x01,        //     Usage (0x01)
    0x15, 0x00,        //     Logical Minimum (0)
    0x26, 0xFF, 0x00,  //     Logical Maximum (255)
    0x75, 0x08,        //     Report Size (8)
    0x95, 0x08,        //     Report Count (8)
    0x81, 0x02,        //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0xC0,              //   End Collection
    0x09, 0x01,        //   Usage (0x01)
    0x85, 0x02,        //   Report ID (2)
    0x95, 0x01,        //   Report Count (1)
    0xB1, 0x02,        //   Feature (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
    0x09, 0x01,        //   Usage (0x01)
    0x85, 0x03,        //   Report ID (3)
    0x95, 0x01,        //   Report Count (1)
    0xB1, 0x02,        //   Feature (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
    0x09, 0x01,        //   Usage (0x01)
    0x85, 0x04,        //   Report ID (4)
    0x95, 0x01,        //   Report Count (1)
    0xB1, 0x02,        //   Feature (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
    0x09, 0x01,        //   Usage (0x01)
    0x85, 0x05,        //   Report ID (5)
    0x95, 0x01,        //   Report Count (1)
    0xB1, 0x02,        //   Feature (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
    0x09, 0x01,        //   Usage (0x01)
    0x85, 0x10,        //   Report ID (16)
    0x95, 0x02,        //   Report Count (2)
    0xB1, 0x02,        //   Feature (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
    0x09, 0x01,        //   Usage (0x01)
    0x85, 0x11,        //   Report ID (17)
    0x95, 0x10,        //   Report Count (16)
    0xB1, 0x02,        //   Feature (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
    0x09, 0x01,        //   Usage (0x01)
    0x85, 0x13,        //   Report ID (19)
    0x95, 0x01,        //   Report Count (1)
    0xB1, 0x02,        //   Feature (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
    0x09, 0x01,        //   Usage (0x01)
    0x85, 0x20,        //   Report ID (32)
    0x95, 0x01,        //   Report Count (1)
    0xB1, 0x02,        //   Feature (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
    0x09, 0x01,        //   Usage (0x01)
    0x85, 0x21,        //   Report ID (33)
    0x95, 0x01,        //   Report Count (1)
    0xB1, 0x02,        //   Feature (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
    0x09, 0x01,        //   Usage (0x01)
    0x85, 0x06,        //   Report ID (6)
    0x95, 0x01,        //   Report Count (1)
    0xB1, 0x02,        //   Feature (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
    0x09, 0x01,        //   Usage (0x01)
    0x85, 0x07,        //   Report ID (7)
    0x95, 0x01,        //   Report Count (1)
    0xB1, 0x02,        //   Feature (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
    0x09, 0x01,        //   Usage (0x01)
    0x85, 0x14,        //   Report ID (20)
    0x95, 0x01,        //   Report Count (1)
    0xB1, 0x02,        //   Feature (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
    0xC0               // End Collection
};

static const uint8_t interface_2_hid_report_descriptor[] = {
    0x06, 0x00, 0xFF,  // Usage Page (Vendor Defined 0xFF00)
    0x09, 0x01,        // Usage (0x01)
    0xA1, 0x01,        // Collection (Application)
    0x85, 0x02,        //   Report ID (2)
    0x05, 0x0D,        //   Usage Page (Digitizer)
    0x09, 0x22,        //   Usage (Finger)
    0xA1, 0x00,        //   Collection (Physical)
    0x06, 0x00, 0xFF,  //     Usage Page (Vendor Defined 0xFF00)
    0x09, 0x01,        //     Usage (0x01)
    0x15, 0x00,        //     Logical Minimum (0)
    0x26, 0xFF, 0x00,  //     Logical Maximum (255)
    0x75, 0x08,        //     Report Size (8)
    0x95, 0x02,        //     Report Count (2)
    0x81, 0x02,        //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0x05, 0x01,        //     Usage Page (Generic Desktop Ctrls)
    0x09, 0x30,        //     Usage (X)
    0x35, 0x00,        //     Physical Minimum (0)
    0x46, 0xE0, 0x2E,  //     Physical Maximum (12000)
    0x26, 0xE0, 0x01,  //     Logical Maximum (480)
    0x75, 0x10,        //     Report Size (16)
    0x95, 0x01,        //     Report Count (1)
    0x81, 0x02,        //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0x09, 0x31,        //     Usage (Y)
    0x46, 0x40, 0x1F,  //     Physical Maximum (8000)
    0x26, 0x40, 0x01,  //     Logical Maximum (320)
    0x81, 0x02,        //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0x06, 0x00, 0xFF,  //     Usage Page (Vendor Defined 0xFF00)
    0x09, 0x01,        //     Usage (0x01)
    0x26, 0xFF, 0x00,  //     Logical Maximum (255)
    0x75, 0x08,        //     Report Size (8)
    0x95, 0x0D,        //     Report Count (13)
    0x81, 0x02,        //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0xC0,              //   End Collection
    0xC0               // End Collection
};

static const USBDescDevice desc_device_wacom = {
    .bcdUSB                        = 0x0200,
    .bMaxPacketSize0               = 64,
    .bNumConfigurations            = 1,
    .confs = (USBDescConfig[]) {
        {
            .bNumInterfaces        = 2,
            .bConfigurationValue   = 1,
            .bmAttributes          = USB_CFG_ATT_ONE,
            .bMaxPower             = 49,
            .nif = 2,
            .ifs = (USBDescIface[]) {
                {
                    .bInterfaceNumber              = 0,
                    .bNumEndpoints                 = 1,
                    .bInterfaceClass               = USB_CLASS_HID,
                    .bInterfaceSubClass            = 0x01, /* boot */
                    .bInterfaceProtocol            = 0x02, /* mouse */
                    .ndesc                         = 1,
                    .descs = (USBDescOther[]) {
                        {
                            /* HID descriptor */
                            .data = (uint8_t[]) {
                                0x09,          /*  u8  bLength */
                                USB_DT_HID,    /*  u8  bDescriptorType */
                                0x00, 0x01,    /*  u16 HID_class */
                                0x00,          /*  u8  country_code */
                                0x01,          /*  u8  num_descriptors */
                                USB_DT_REPORT, /*  u8  type: Report */
                                0xb0, 0,       /*  u16 len */
                            },
                        },
                    },
                    .eps = (USBDescEndpoint[]) {
                        {
                            .bEndpointAddress      = USB_DIR_IN | 0x01,
                            .bmAttributes          = USB_ENDPOINT_XFER_INT,
                            .wMaxPacketSize        = 9,
                            .bInterval             = 4,
                        },
                    },
                },
                {
                    .bInterfaceNumber              = 1,
                    .bNumEndpoints                 = 1,
                    .bInterfaceClass               = USB_CLASS_HID,
                    .bInterfaceSubClass            = 0,
                    .bInterfaceProtocol            = 0,
                    .ndesc                         = 1,
                    .descs = (USBDescOther[]) {
                        {
                            /* HID descriptor */
                            .data = (uint8_t[]) {
                                0x09,          /*  u8  bLength */
                                USB_DT_HID,    /*  u8  bDescriptorType */
                                0x00, 0x01,    /*  u16 HID_class */
                                0x00,          /*  u8  country_code */
                                0x01,          /*  u8  num_descriptors */
                                USB_DT_REPORT, /*  u8  type: Report */
                                0x4B, 0,       /*  u16 len */
                            },
                        },
                    },
                    .eps = (USBDescEndpoint[]) {
                        {
                            .bEndpointAddress      = USB_DIR_IN | 0x02,
                            .bmAttributes          = USB_ENDPOINT_XFER_INT,
                            .wMaxPacketSize        = 64,
                            .bInterval             = 4,
                        },
                    },
                }
            }
        },
    },
};

static const USBDesc desc_wacom = {
    .id = {
        .idVendor          = 0x056a,
        .idProduct         = 0x00d4,
        .bcdDevice         = 0x0106,
        .iManufacturer     = STR_MANUFACTURER,
        .iProduct          = STR_PRODUCT,
        .iSerialNumber     = 0,
    },
    .full = &desc_device_wacom,
    .str  = desc_strings,
};

static void usb_wacom_event(void *opaque,
                            int x, int y, int dz, int buttons_state)
{
    USBWacomState *s = opaque;

    /* scale to tablet resolution */
    s->x = (x * TABLET_RESOLUTION_X / 0x7FFF);
    s->y = (y * TABLET_RESOLUTION_Y / 0x7FFF);
    s->dz += dz;
    s->buttons_state = buttons_state;
    s->changedPen = true;
    usb_wakeup(s->intr, 0);
}

static inline int int_clamp(int val, int vmin, int vmax)
{
    if (val < vmin)
        return vmin;
    else if (val > vmax)
        return vmax;
    else
        return val;
}

#define WACOM_REPORT_PENABLED 2
#define WACOM_REPORT_INTUOS_PEN 16
#define WACOM_REPORT_USB 192

#define WACOM_STATUS_RANGE 0x80
#define WACOM_STATUS_PROXIMITY 0x40
#define WACOM_STATUS_READY 0x20

#define WACOM_BUTTON_PEN 0x01
#define WACOM_BUTTON_1 0x02
#define WACOM_BUTTON_2 0x04
#define WACOM_BUTTON_RUBBER 0x08

static int usb_wacom_poll(USBWacomState *s, uint8_t *buf, int len)
{
    int b;
    uint16_t pressure;

    b = WACOM_STATUS_READY | WACOM_STATUS_PROXIMITY | WACOM_STATUS_RANGE;
    
    if (s->buttons_state & MOUSE_EVENT_LBUTTON)
        b |= WACOM_BUTTON_PEN;
    if (s->buttons_state & MOUSE_EVENT_MBUTTON)
        b |= WACOM_BUTTON_1;
    if (s->buttons_state & MOUSE_EVENT_RBUTTON)
        b |= WACOM_BUTTON_2;

    if (len < 9)
        return 0;

    buf[0] = WACOM_REPORT_PENABLED;
    buf[1] = b;
    
    buf[2] = s->x & 0xff;
    buf[3] = s->x >> 8;
    buf[4] = s->y & 0xff;
    buf[5] = s->y >> 8;
    
    if (b & (WACOM_BUTTON_PEN | WACOM_BUTTON_RUBBER)) {
        pressure = 512;
     } else {
        pressure = 0;
    }
    
    buf[6] = pressure & 0xff;
    buf[7] = pressure >> 8;
    
    buf[8] = 0; // Range

    return len;
}

static void usb_wacom_set_tablet_mode(USBWacomState *s, int mode)
{
    if (s->eh_entry) {
        qemu_remove_mouse_event_handler(s->eh_entry);
    }

    s->mode = mode;

    switch (mode) {
        case WACOM_MODE_WACOM:
            s->eh_entry = qemu_add_mouse_event_handler(usb_wacom_event, s, 1, TABLET_NAME_QEMU);
            break;
        case WACOM_MODE_HID:
        default:
            s->eh_entry = 0;
    }

    if (s->eh_entry) {
        qemu_activate_mouse_event_handler(s->eh_entry);
    }

    // Resend all our state for the new mode
    s->changedPen = true;
}

static void usb_wacom_handle_reset(USBDevice *dev)
{
    USBWacomState *s = (USBWacomState *) dev;

    s->dx = 0;
    s->dy = 0;
    s->dz = 0;
    s->x = 0;
    s->y = 0;
    s->buttons_state = 0;
    usb_wacom_set_tablet_mode(s, WACOM_MODE_HID);
}

static void usb_wacom_handle_control(USBDevice *dev, USBPacket *p,
       int request, int value, int index, int length, uint8_t *data)
{
    USBWacomState *s = (USBWacomState *) dev;
    int ret;

    ret = usb_desc_handle_control(dev, p, request, value, index, length, data);
    if (ret >= 0) {
        return;
    }

    switch (request) {
    case ClassInterfaceOutRequest | WACOM_SET_REPORT:
        switch (data[0]) {
            case WACOM_MODE_HID:
            case WACOM_MODE_WACOM:
                info_report(TYPE_USB_WACOM ": Set tablet mode %d", data[0]);

                usb_wacom_set_tablet_mode(s, data[0]);
                break;

            default:
                warn_report(TYPE_USB_WACOM ": Ignoring unsupported Wacom command %02x", data[0]);
        }
        break;
    case ClassInterfaceOutRequest | WACOM_GET_REPORT:
        info_report(TYPE_USB_WACOM ": Get class interface out report %x %x", data[0], value);

        data[0] = 0;
        data[1] = s->mode;
        p->actual_length = 2;
        break;
    case ClassInterfaceOutRequest | HID_SET_PROTOCOL:
        warn_report(TYPE_USB_WACOM ": Ignoring attempt to switch between boot and report protocols");
        break;
    case InterfaceRequest | USB_REQ_GET_DESCRIPTOR:
        switch (value >> 8) {
            case USB_DT_REPORT:
                switch (index) {
                case 0:
                    memcpy(data, interface_1_hid_report_descriptor,
                            sizeof(interface_1_hid_report_descriptor));
                    p->actual_length = sizeof(interface_1_hid_report_descriptor);
                    break;
                case 1:
                    memcpy(data, interface_2_hid_report_descriptor,
                            sizeof(interface_2_hid_report_descriptor));
                    p->actual_length = sizeof(interface_2_hid_report_descriptor);
                    break;
                default:
                    goto fail;
                }
                break;
            
            default:
                goto fail;
         }
         break;
    case DeviceRequest | USB_REQ_GET_DESCRIPTOR:
        info_report(TYPE_USB_WACOM ": Get device HID descriptor 0x%04x index 0x%04x", value, index);

        switch (value >> 8)  {
            case USB_DT_HID:
                memcpy(data, desc_device_wacom.confs[0].ifs[(value & 0xFF) >= 1 ? 1 : 0].descs[0].data, 9);
                p->actual_length = 9;
                break;

            case USB_DT_DEVICE_QUALIFIER:
                // We don't need to support this because we only support running at one USB speed
                goto fail;

            default:
                warn_report(TYPE_USB_WACOM ": Rejecting request for unknown device descriptor 0x%04x index 0x%02x", value, index);

                goto fail;
        }
        break;
    case EndpointOutRequest | USB_REQ_CLEAR_FEATURE:
        if (value != 0x00)
            warn_report(TYPE_USB_WACOM ": Unknown CLEAR_FEATURE request type %x for endpoint %x", value, index & 0x0F);

        p->actual_length = 0;
        break;
    case ClassInterfaceRequest | HID_GET_REPORT:
        info_report(TYPE_USB_WACOM ": Get class interface report %x %x", value, index);

        switch (value & 0xFF) {
            case WACOM_REQUEST_GET_MODE:
                data[0] = 0;
                data[1] = s->mode;
                p->actual_length = 2;
                break;
            default:
                if (s->mode == WACOM_MODE_WACOM)
                    p->actual_length = usb_wacom_poll(s, data, length);
        }
        break;
    case ClassInterfaceRequest | HID_GET_IDLE:
        info_report(TYPE_USB_WACOM ": Get idle");
        
        data[0] = s->idle;
        p->actual_length = 1;
        break;
    case ClassInterfaceOutRequest | HID_SET_IDLE:
        s->idle = (uint8_t) (value >> 8);
        break;
    default:
        warn_report(TYPE_USB_WACOM ": Rejecting unsupported control request %x value %x index %x", request, value, index);
    fail:
        p->status = USB_RET_STALL;
    }
}

static void usb_wacom_handle_data(USBDevice *dev, USBPacket *p)
{
    USBWacomState *s = (USBWacomState *) dev;
    uint8_t buf[p->iov.size];
    int len = 0;
    int64_t currentTime;

    switch (p->pid) {
    case USB_TOKEN_IN:
        switch (p->ep->nr) {
            case 2:
                p->status = USB_RET_NAK;
                break;
                
            case 1:
                if (s->mode != WACOM_MODE_WACOM) {
                    p->status = USB_RET_NAK;
                    break;
                }
                
                currentTime = qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL);
    
                // Driver assumes pen has left if it doesn't get a ping every 1.5 seconds, so tickle it to keep it alive
                if (currentTime - s->lastPacketTime > 100) {
                    s->changedPen = true;
                }
                
                if (!s->changedPen) {
                    p->status = USB_RET_NAK;
                    break;
                }
                
                s->lastPacketTime = currentTime;

                s->changedPen = false;
                len = usb_wacom_poll(s, buf, p->iov.size);
                usb_packet_copy(p, buf, len);
                break;
            default:
                goto fail;
        }
        break;
    case USB_TOKEN_OUT:
    default:
    fail:
        p->status = USB_RET_STALL;
    }
}

static void usb_wacom_unrealize(USBDevice *dev)
{
    USBWacomState *s = (USBWacomState *) dev;

    if (s->eh_entry) {
        qemu_remove_mouse_event_handler(s->eh_entry);
        s->eh_entry = 0;
    }
}

static void usb_wacom_realize(USBDevice *dev, Error **errp)
{
    USBWacomState *s = USB_WACOM(dev);
    usb_desc_init(dev);
    s->dev.speed = USB_SPEED_FULL;
    s->intr = usb_ep_get(dev, USB_TOKEN_IN, 1);
    s->eh_entry = 0;
    s->lastPacketTime = 0;
    
    usb_wacom_set_tablet_mode(s, WACOM_MODE_HID);
}

static const VMStateDescription vmstate_usb_wacom = {
    .name = "usb-wacom-bamboo",
    .unmigratable = 1,
};

static void usb_wacom_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    USBDeviceClass *uc = USB_DEVICE_CLASS(klass);

    uc->product_desc   = TABLET_NAME_QEMU;
    uc->usb_desc       = &desc_wacom;
    uc->realize        = usb_wacom_realize;
    uc->handle_reset   = usb_wacom_handle_reset;
    uc->handle_control = usb_wacom_handle_control;
    uc->handle_data    = usb_wacom_handle_data;
    uc->unrealize      = usb_wacom_unrealize;
    set_bit(DEVICE_CATEGORY_INPUT, dc->categories);
    dc->desc = TABLET_NAME_QEMU;
    dc->vmsd = &vmstate_usb_wacom;
}

static const TypeInfo wacom_info = {
    .name          = TYPE_USB_WACOM,
    .parent        = TYPE_USB_DEVICE,
    .instance_size = sizeof(USBWacomState),
    .class_init    = usb_wacom_class_init,
};

static void usb_wacom_register_types(void)
{
    type_register_static(&wacom_info);
    usb_legacy_register(TYPE_USB_WACOM, "wacom-tablet-bamboo", NULL);
}

type_init(usb_wacom_register_types)
