/*
 * Wacom Intuos 5 Touch Medium PTH-650 tablet emulation.
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
#include "qom/object.h"
#include "qemu/timer.h"
#include "hw/qdev-properties.h"

/* Interface requests */
#define WACOM_GET_REPORT	0x01
#define WACOM_SET_REPORT	0x09

#define WAC_CMD_LED_CONTROL 0x20
#define WAC_CMD_SET_DATARATE 0x04
#define WAC_CMD_SET_SCANMODE_PENTOUCH 0x0d

#define WACOM_REPORT_PROXIMITY 5
#define WACOM_REPORT_PENABLED 2
#define WACOM_REPORT_INTUOS_PEN 16
#define WACOM_REPORT_WL 128
#define WACOM_REPORT_USB 192
#define WACOM_REPORT_VERSIONS 10

#define WACOM_REQUEST_GET_MODE 2
#define WACOM_REQUEST_GET_FIRST_TOOL_ID 5
#define WACOM_REQUEST_GET_VERSIONS 7

/* HID interface requests */
#define HID_GET_REPORT		0x01
#define HID_GET_IDLE		0x02
#define HID_SET_IDLE		0x0a
#define HID_SET_PROTOCOL    0x0b

/* HID descriptor types */
#define USB_DT_HID    0x21
#define USB_DT_REPORT 0x22
#define USB_DT_PHY    0x23

#define PEN_LEAVE_TIMEOUT 5000
#define PEN_PING_INTERVAL 200

#define TABLET_CLICK_PRESSURE 890
#define TABLET_POINTER_DOWN_MIN_PRESSURE 128
#define TABLET_MAX_PRESSURE ((1 << 11) - 1)

#define TABLET_RESOLUTION_X 44704
#define TABLET_RESOLUTION_Y 27940

#define TABLET_NAME_QEMU "QEMU Intuos 5 tablet"

typedef struct USBWacomState {
    USBDevice dev;
    USBEndpoint *intr;
    QEMUPutMouseEntry *eh_entry;
    USBDesc usb_desc_custom; /* If we customise product/vendor ids */
    int dx, dy, dz, buttons_state;
    int x, y, pressure;
    enum {
        WACOM_MODE_HID = 1,
        WACOM_MODE_WACOM = 2,
    } mode;
    uint8_t idle;
    uint16_t product_id, vendor_id;
    int64_t lastPacketTime;
    
    int64_t lastInputEventTime;
    bool penInProx;
    
    bool changedPen, changedProximity;
};

#define TYPE_USB_WACOM "usb-wacom-tablet-intuos-5"
OBJECT_DECLARE_SIMPLE_TYPE(USBWacomState, USB_WACOM)

enum {
    STR_SERIALNUMBER = 1,
    STR_MANUFACTURER,
    STR_PRODUCT
};

static const USBDescStrings desc_strings = {
    [STR_SERIALNUMBER]     = "QEMU:Intuos:5",
    [STR_MANUFACTURER]     = "Wacom Co.,Ltd.",
    [STR_PRODUCT]          = "Intuos5 touch M"
};

// Generated from https://github.com/linuxwacom/wacom-hid-descriptors/tree/master/Wacom%20Intuos5%20touch%20medium
// using hidrd-convert
static const uint8_t interface_1_hid_report_descriptor[] = {
    0x05, 0x01,         /*  Usage Page (Desktop),               */
    0x09, 0x02,         /*  Usage (Mouse),                      */
    0xA1, 0x01,         /*  Collection (Application),           */
    0x85, 0x01,         /*      Report ID (1),                  */
    0x09, 0x01,         /*      Usage (Pointer),                */
    0xA1, 0x00,         /*      Collection (Physical),          */
    0x05, 0x09,         /*          Usage Page (Button),        */
    0x19, 0x01,         /*          Usage Minimum (01h),        */
    0x29, 0x03,         /*          Usage Maximum (03h),        */
    0x15, 0x00,         /*          Logical Minimum (0),        */
    0x25, 0x01,         /*          Logical Maximum (1),        */
    0x95, 0x03,         /*          Report Count (3),           */
    0x75, 0x01,         /*          Report Size (1),            */
    0x81, 0x02,         /*          Input (Variable),           */
    0x95, 0x05,         /*          Report Count (5),           */
    0x81, 0x03,         /*          Input (Constant, Variable), */
    0x05, 0x01,         /*          Usage Page (Desktop),       */
    0x09, 0x30,         /*          Usage (X),                  */
    0x09, 0x31,         /*          Usage (Y),                  */
    0x09, 0x38,         /*          Usage (Wheel),              */
    0x15, 0x81,         /*          Logical Minimum (-127),     */
    0x25, 0x7F,         /*          Logical Maximum (127),      */
    0x75, 0x08,         /*          Report Size (8),            */
    0x95, 0x03,         /*          Report Count (3),           */
    0x81, 0x06,         /*          Input (Variable, Relative), */
    0xC0,               /*      End Collection,                 */
    0xC0,               /*  End Collection,                     */
    0x05, 0x0D,         /*  Usage Page (Digitizer),             */
    0x09, 0x01,         /*  Usage (Digitizer),                  */
    0xA1, 0x01,         /*  Collection (Application),           */
    0x85, 0x02,         /*      Report ID (2),                  */
    0x09, 0x00,         /*      Usage (00h),                    */
    0x75, 0x08,         /*      Report Size (8),                */
    0x96, 0x09, 0x00,   /*      Report Count (9),               */
    0x15, 0x00,         /*      Logical Minimum (0),            */
    0x26, 0xFF, 0x00,   /*      Logical Maximum (255),          */
    0x81, 0x02,         /*      Input (Variable),               */
    0x85, 0x03,         /*      Report ID (3),                  */
    0x09, 0x00,         /*      Usage (00h),                    */
    0x75, 0x08,         /*      Report Size (8),                */
    0x96, 0x09, 0x00,   /*      Report Count (9),               */
    0x15, 0x00,         /*      Logical Minimum (0),            */
    0x26, 0xFF, 0x00,   /*      Logical Maximum (255),          */
    0x81, 0x02,         /*      Input (Variable),               */
    0x85, 0xC0,         /*      Report ID (192),                */
    0x09, 0x00,         /*      Usage (00h),                    */
    0x75, 0x08,         /*      Report Size (8),                */
    0x96, 0x09, 0x00,   /*      Report Count (9),               */
    0x15, 0x00,         /*      Logical Minimum (0),            */
    0x26, 0xFF, 0x00,   /*      Logical Maximum (255),          */
    0x81, 0x02,         /*      Input (Variable),               */
    0x85, 0x02,         /*      Report ID (2),                  */
    0x09, 0x00,         /*      Usage (00h),                    */
    0x95, 0x01,         /*      Report Count (1),               */
    0xB1, 0x02,         /*      Feature (Variable),             */
    0x85, 0x03,         /*      Report ID (3),                  */
    0x09, 0x00,         /*      Usage (00h),                    */
    0x95, 0x09,         /*      Report Count (9),               */
    0xB1, 0x02,         /*      Feature (Variable),             */
    0x85, 0x05,         /*      Report ID (5),                  */
    0x09, 0x00,         /*      Usage (00h),                    */
    0x95, 0x08,         /*      Report Count (8),               */
    0xB1, 0x02,         /*      Feature (Variable),             */
    0x85, 0x07,         /*      Report ID (7),                  */
    0x09, 0x00,         /*      Usage (00h),                    */
    0x95, 0x0F,         /*      Report Count (15),              */
    0xB1, 0x02,         /*      Feature (Variable),             */
    0x85, 0x08,         /*      Report ID (8),                  */
    0x09, 0x00,         /*      Usage (00h),                    */
    0x95, 0x04,         /*      Report Count (4),               */
    0xB1, 0x02,         /*      Feature (Variable),             */
    0x85, 0x0A,         /*      Report ID (10),                 */
    0x09, 0x00,         /*      Usage (00h),                    */
    0x95, 0x02,         /*      Report Count (2),               */
    0xB1, 0x02,         /*      Feature (Variable),             */
    0x85, 0x0B,         /*      Report ID (11),                 */
    0x09, 0x00,         /*      Usage (00h),                    */
    0x95, 0x01,         /*      Report Count (1),               */
    0xB1, 0x02,         /*      Feature (Variable),             */
    0x85, 0x20,         /*      Report ID (32),                 */
    0x09, 0x00,         /*      Usage (00h),                    */
    0x95, 0x08,         /*      Report Count (8),               */
    0xB1, 0x02,         /*      Feature (Variable),             */
    0x85, 0x04,         /*      Report ID (4),                  */
    0x09, 0x00,         /*      Usage (00h),                    */
    0x95, 0x01,         /*      Report Count (1),               */
    0xB1, 0x02,         /*      Feature (Variable),             */
    0x85, 0x0D,         /*      Report ID (13),                 */
    0x09, 0x00,         /*      Usage (00h),                    */
    0x95, 0x01,         /*      Report Count (1),               */
    0xB1, 0x02,         /*      Feature (Variable),             */
    0x85, 0xCC,         /*      Report ID (204),                */
    0x09, 0x00,         /*      Usage (00h),                    */
    0x95, 0x02,         /*      Report Count (2),               */
    0xB1, 0x02,         /*      Feature (Variable),             */
    0x85, 0x30,         /*      Report ID (48),                 */
    0x09, 0x00,         /*      Usage (00h),                    */
    0x95, 0x1F,         /*      Report Count (31),              */
    0xB1, 0x02,         /*      Feature (Variable),             */
    0x85, 0x31,         /*      Report ID (49),                 */
    0x09, 0x00,         /*      Usage (00h),                    */
    0x95, 0x04,         /*      Report Count (4),               */
    0xB1, 0x02,         /*      Feature (Variable),             */
    0x85, 0x15,         /*      Report ID (21),                 */
    0x09, 0x00,         /*      Usage (00h),                    */
    0x95, 0x0A,         /*      Report Count (10),              */
    0xB1, 0x02,         /*      Feature (Variable),             */
    0x85, 0x14,         /*      Report ID (20),                 */
    0x09, 0x00,         /*      Usage (00h),                    */
    0x95, 0x0F,         /*      Report Count (15),              */
    0xB1, 0x02,         /*      Feature (Variable),             */
    0x85, 0x40,         /*      Report ID (64),                 */
    0x09, 0x00,         /*      Usage (00h),                    */
    0x95, 0x02,         /*      Report Count (2),               */
    0xB1, 0x02,         /*      Feature (Variable),             */
    0x85, 0xDD,         /*      Report ID (221),                */
    0x09, 0x00,         /*      Usage (00h),                    */
    0x95, 0x01,         /*      Report Count (1),               */
    0xB1, 0x02,         /*      Feature (Variable),             */
    0xC0                /*  End Collection                      */
};

static const uint8_t interface_2_hid_report_descriptor[] = {
    0x06, 0x00, 0xFF,   /*  Usage Page (FF00h),         */
    0x09, 0x01,         /*  Usage (01h),                */
    0xA1, 0x01,         /*  Collection (Application),   */
    0x85, 0x02,         /*      Report ID (2),          */
    0x09, 0x01,         /*      Usage (01h),            */
    0x15, 0x00,         /*      Logical Minimum (0),    */
    0x26, 0xFF, 0x00,   /*      Logical Maximum (255),  */
    0x75, 0x08,         /*      Report Size (8),        */
    0x95, 0x3F,         /*      Report Count (63),      */
    0x81, 0x02,         /*      Input (Variable),       */
    0xC0                /*  End Collection              */
};

static const USBDescIface wacom_ifaces[] = {
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
                    0x10, 0x01,    /*  u16 HID_class */
                    0x00,          /*  u8  country_code */
                    0x01,          /*  u8  num_descriptors */
                    USB_DT_REPORT, /*  u8  type: Report */
                    0xF3, 0,       /*  u16 len */
                },
            },
        },
        .eps = (USBDescEndpoint[]) {
            {
                .bEndpointAddress      = USB_DIR_IN | 0x03,
                .bmAttributes          = USB_ENDPOINT_XFER_INT,
                .wMaxPacketSize        = 16,
                .bInterval             = 1,
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
                    0x10, 0x01,    /*  u16 HID_class */
                    0x00,          /*  u8  country_code */
                    0x01,          /*  u8  num_descriptors */
                    USB_DT_REPORT, /*  u8  type: Report */
                    0x17, 0x0     /*  u16 len */
                },
            },
        },
        .eps = (USBDescEndpoint[]) {
            {
                .bEndpointAddress      = USB_DIR_IN | 0x02,
                .bmAttributes          = USB_ENDPOINT_XFER_INT,
                .wMaxPacketSize        = 64,
                .bInterval             = 2,
            },
        },
    }
};

static const USBDescDevice desc_device_wacom = {
    .bcdUSB                        = 0x0100,
    .bMaxPacketSize0               = 16,
    .bNumConfigurations            = 1,
    .confs = (USBDescConfig[]) {
        {
            .bNumInterfaces        = 2,
            .bConfigurationValue   = 1,
            .bmAttributes          = USB_CFG_ATT_ONE,
            .bMaxPower             = 249,
            .nif = 2,
            .ifs = wacom_ifaces
        },
    },
};

static const USBDescDevice desc_device_wacom2 = {
    .bcdUSB                        = 0x0200,
    .bMaxPacketSize0               = 16,
    .bNumConfigurations            = 1,
    .confs = (USBDescConfig[]) {
        {
            .bNumInterfaces        = 2,
            .bConfigurationValue   = 1,
            .bmAttributes          = USB_CFG_ATT_ONE,
            .bMaxPower             = 249,
            .nif = 2,
            .ifs = wacom_ifaces
        },
    }
};

static const USBDesc desc_wacom_default = {
    .id = {
        .idVendor          = 0x056a,
        .idProduct         = 0x0027,
        .bcdDevice         = 0x0107,
        .iManufacturer     = STR_MANUFACTURER,
        .iProduct          = STR_PRODUCT,
        .iSerialNumber     = STR_SERIALNUMBER,
    },
    .full = &desc_device_wacom,
    .high = &desc_device_wacom2,
    .str  = desc_strings,
};

static inline int int_clamp(int val, int vmin, int vmax)
{
    if (val < vmin)
        return vmin;
    else if (val > vmax)
        return vmax;
    else
        return val;
}

static void usb_wacom_event(void *opaque,
                            int x, int y, int dz, int buttons_state)
{
    USBWacomState *s = opaque;

    /* scale to tablet resolution */
    s->x = (x * TABLET_RESOLUTION_X / 0x7FFF);
    s->y = (y * TABLET_RESOLUTION_Y / 0x7FFF);
    s->dz += dz;
    s->pressure = int_clamp(s->pressure - dz * 128, TABLET_POINTER_DOWN_MIN_PRESSURE, TABLET_MAX_PRESSURE);
    s->buttons_state = buttons_state;
    
    s->changedPen = true;
    s->lastInputEventTime = qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL);
    
    if (!s->penInProx) {
        s->penInProx = true;
        s->changedProximity = true;
    }
    
    usb_wakeup(s->intr, 0);
}

#define WACOM_BUTTON_STYLUS_BUTTON_1 0x02
#define WACOM_BUTTON_STYLUS_BUTTON_2 0x04

#define WACOM_STYLUS_PROXIMITY 0x80
#define WACOM_STYLUS_READY 0x40
#define WACOM_STYLUS_IN_RANGE 0x20

#define WACOM_STYLUS_HAS_SERIAL 0x02
#define WACOM_STYLUS_NO_SERIAL  0x00

static int usb_wacom_poll(USBWacomState *s, uint8_t *buf, int len)
{
    int b;
    uint16_t pressure;
    uint8_t distance;
    uint8_t tiltX = 0 + 64, tiltY = 0 + 64; // Tilt centered
    
    if (len < 10)
        return 0;

    b = 0;
    
    if (s->buttons_state & MOUSE_EVENT_RBUTTON)
        b |= WACOM_BUTTON_STYLUS_BUTTON_1;
    
    if (s->buttons_state & MOUSE_EVENT_MBUTTON)
        b |= WACOM_BUTTON_STYLUS_BUTTON_2;

    if ((s->buttons_state & MOUSE_EVENT_LBUTTON) != 0) {
        pressure = s->pressure;
        distance = 0;
     } else {
        pressure = 0;
        distance = 10;
    }
    
    buf[0] = WACOM_REPORT_PENABLED;
    buf[1] = WACOM_STYLUS_PROXIMITY | WACOM_STYLUS_READY | WACOM_STYLUS_IN_RANGE | b | (pressure & 0x01);
    
    // Low bit of coords is stored in buf[9] to allow them to be 17-bit
    buf[2] = (s->x >> 9) & 0xFF;
    buf[3] = (s->x >> 1) & 0xFF;
    buf[4] = (s->y >> 9) & 0xFF;
    buf[5] = (s->y >> 1) & 0xFF;
    
    buf[6] = pressure >> 3;
    buf[7] = ((pressure & 0x6) << 5) | ((tiltX >> 1) & 0x7F);
    buf[8] = (tiltX << 7) | (tiltY & 0x7F); 
 
    buf[9] = (distance << 2) | ((s->x & 0x01) << 1) | (s->y & 0x01);
       
    return 10;
}

static int usb_wacom_prox_event(USBWacomState *s, uint8_t *buf, int len, bool inProx)
{
    uint8_t toolIndex = 0;
    uint32_t toolID = 0x802; /* Intuos4/5 13HD/24HD General Pen */
    uint32_t toolSerial = 0xFEEDC0DE;

    if (len < 9) // Because WACOM_REPORT_PROXIMITY only has an 8 byte payload (in our descriptor)
        return 0;
    
    buf[0] = WACOM_REPORT_PENABLED;
    buf[1] = WACOM_STYLUS_PROXIMITY | (inProx ? WACOM_STYLUS_READY | WACOM_STYLUS_HAS_SERIAL : 0) | (toolIndex & 0x01);

    buf[2] = toolID >> 4;
    buf[3] = (toolID << 4) | (toolSerial >> 28);
    buf[4] = toolSerial >> 20;
    buf[5] = toolSerial >> 12;
    buf[6] = toolSerial >> 4;
    buf[7] = (toolSerial << 4) | ((toolID >> 16) & 0x0F);
    buf[8] = (toolID >> 8) & 0xF0;
    
    return len;
}

static int usb_wacom_version_report(USBWacomState *s, uint8_t *buf, int len)
{
    if (len < 8)
        return 0;
    
    uint32_t penVersion = 0x121112; // i.e. 18.1.1.18[.0]
    uint16_t touchVersion = 0x1211; // i.e. 18.1.1[.0][.0] 

    buf[0] = WACOM_REPORT_VERSIONS;
    buf[1] = 0;

    buf[2] = 0;
    buf[3] = (penVersion >> 16) & 0xFF;
    buf[4] = (penVersion >> 8) & 0xFF;
    buf[5] = (penVersion) & 0xFF;
    buf[6] = (touchVersion >> 8) & 0xFF;
    buf[7] = (touchVersion) & 0xFF;
    buf[8] = 0;
    buf[9] = 0;

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

    // Start off with pen out of prox until we get some cursor events
    s->penInProx = false;
    s->changedPen = false;
    s->changedProximity = true;
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
                
            case WAC_CMD_LED_CONTROL:
                info_report(TYPE_USB_WACOM ": Discarding LED control message");
                break;

            case 0x04:
                switch (data[1]) {
                    // case 0x00: OEM report
                    case 0x01:
                        info_report(TYPE_USB_WACOM ": Discarding set Bluetooth address");
                        break;
                    default:
                        info_report(TYPE_USB_WACOM ": Discarding set report %x", data[1]);
                }

                s->changedPen = true;
                s->changedProximity = true;
                break;

            case WAC_CMD_SET_SCANMODE_PENTOUCH:
                info_report(TYPE_USB_WACOM ": Discarding set-scanmode message");

                s->changedPen = true;
                s->changedProximity = true;
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
            case WACOM_REQUEST_GET_FIRST_TOOL_ID:
                if (s->penInProx) {
                    p->actual_length = usb_wacom_prox_event(s, data, length, s->penInProx);
                } else {
                    p->actual_length = 0;
                }
            break;
            case WACOM_REQUEST_GET_VERSIONS:
                p->actual_length = usb_wacom_version_report(s, data, length);
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
        
            case 3:
                if (s->mode != WACOM_MODE_WACOM) {
                    p->status = USB_RET_NAK;
                    break;
                }
                
                currentTime = qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL);
                
                // If we haven't moved the pen in the while, move it out of proximity
                if (s->penInProx && !s->changedPen && currentTime - s->lastInputEventTime > PEN_LEAVE_TIMEOUT) {
                    s->penInProx = false;
                    s->changedProximity = true;
                }
                
                // Driver assumes pen has left if it doesn't get a ping every 1.5 seconds, so tickle it to keep it alive
                if (currentTime - s->lastPacketTime > PEN_PING_INTERVAL) {
                    if (s->penInProx) {
                        s->changedPen = true;
                    } else {
                        // Driver also doesn't like it if we go totally quiet when pen is out of prox
                        s->changedProximity = true;
                    }
                }
                
                if (!(s->changedPen || s->changedProximity)) {
                    p->status = USB_RET_NAK;
                    break;
                }

                s->lastPacketTime = currentTime;

                if (s->changedProximity) {
                    s->changedProximity = false;
                    len = usb_wacom_prox_event(s, buf, p->iov.size, s->penInProx);
                    usb_packet_copy(p, buf, len);
                } else if (s->changedPen) {
                    s->changedPen = false;
                    len = usb_wacom_poll(s, buf, p->iov.size);
                    usb_packet_copy(p, buf, len);
                }
                
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
    
    dev->usb_desc = 0;
}

static void usb_wacom_realize(USBDevice *dev, Error **errp)
{
    USBWacomState *s = USB_WACOM(dev);

    if (s->product_id != 0 || s->vendor_id != 0) {
        // Make a copy of the USB descriptor so we can customise the product ID
        memcpy((char*) &s->usb_desc_custom, (char*) &desc_wacom_default, sizeof(desc_wacom_default));

        s->usb_desc_custom.id.idProduct = s->product_id;
        s->usb_desc_custom.id.idVendor = s->vendor_id;

        dev->usb_desc = &s->usb_desc_custom;
    } else {
        dev->usb_desc = &desc_wacom_default;
    }

    usb_desc_init(dev);
    s->intr = usb_ep_get(dev, USB_TOKEN_IN, 3);
    s->eh_entry = 0;
    s->pressure = TABLET_CLICK_PRESSURE;
    s->lastPacketTime = 0;
    s->lastInputEventTime = 0;
    
    usb_wacom_set_tablet_mode(s, WACOM_MODE_HID);
}

static const VMStateDescription vmstate_usb_wacom = {
    .name = "usb-wacom-intuos-5",
    .unmigratable = 1,
};

static Property intuos_properties[] = {
    DEFINE_PROP_UINT16("productid", struct USBWacomState, product_id, 0),
    DEFINE_PROP_UINT16("vendorid", struct USBWacomState, vendor_id, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static void usb_wacom_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    USBDeviceClass *uc = USB_DEVICE_CLASS(klass);

    uc->product_desc   = TABLET_NAME_QEMU;
    uc->usb_desc       = &desc_wacom_default;
    uc->realize        = usb_wacom_realize;
    uc->handle_reset   = usb_wacom_handle_reset;
    uc->handle_control = usb_wacom_handle_control;
    uc->handle_data    = usb_wacom_handle_data;
    uc->unrealize      = usb_wacom_unrealize;
    uc->handle_attach  = usb_desc_attach;

    set_bit(DEVICE_CATEGORY_INPUT, dc->categories);
    dc->desc = TABLET_NAME_QEMU;
    dc->vmsd = &vmstate_usb_wacom;

    device_class_set_props(dc, intuos_properties);
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
    usb_legacy_register(TYPE_USB_WACOM, "wacom-tablet-intuos-5", NULL);
}

type_init(usb_wacom_register_types)
