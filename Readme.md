# Emulated Wacom tablets for QEMU

These devices simulate the basic functionality of a Wacom Bamboo or Wacom Intuos 5 graphics tablet 
in QEMU. I have built this as an alternative to the support that QEMU already has for simulating a Wacom "PenPartner" 
tablet, because this tablet model is positively ancient and isn't well-supported by guest drivers any more.

The goal of this project is to allow me to test out official Wacom drivers (macOS / Windows) for tablets that I don't
actually own, for my [Wacom Driver Fix](https://github.com/thenickdude/wacom-driver-fix) project that fixes bugs in 
Wacom's abandoned macOS drivers. 

The Intuos 5 model emulates a Intuos 5 Touch Medium PTH-650 (but with no touch support). The Bamboo tablet is a Bamboo 
Pen CTL-460.

The emulated devices work on Linux (with `input-wacom`), Windows 10 and macOS Catalina (both using Wacom's official drivers). 

There's probably nobody else in the world who will find this useful, but I'm posting it here just in case.

## Adding this to QEMU

Add the two drivers `dev-wacom-bamboo.c` and `dev-wacom-intuos-5.c` into QEMU's sourcecode at `/hw/usb`, alongside the 
`dev-wacom.c` driver that is already included with QEMU. Then edit `Makefile.objs` in that same directory to add the new 
drivers to the list of object files:

Before: 

```Makefile
common-obj-$(CONFIG_USB_TABLET_WACOM) += dev-wacom.o
```

After:

```Makefile
common-obj-$(CONFIG_USB_TABLET_WACOM) += dev-wacom.o dev-wacom-bamboo.o dev-wacom-intuos-5.o
```

Then build QEMU from source.

## Using the new tablet devices

You can attach one of the new tablets to your VM the same way as with the built-in `usb-wacom` device. The new tablets are named
`usb-wacom-tablet-intuos-5` and `usb-wacom-tablet-bamboo`, e.g.:

    qemu -device usb-wacom-tablet-intuos-5,id=wacom

I've dropped support for the tablet's fallback HID Mouse mode, which is normally used when no Wacom tablet drivers 
are loaded in the guest. Instead, QEMU will send your mouse events to its next input driver in the stack (likely the PS/2 
tablet device) until the guest's Wacom driver is loaded. 

(You can tell if you're using the proper Wacom device rather than the PS/2 fallback, because your scrollwheel will stop 
functioning, since I didn't add support for it)

You can override the vendorid and productid of the device like so (but the tablet may not provide the features that are 
expected of it by the guest's drivers, and fail to operate):

    qemu -device usb-wacom-tablet-bamboo,id=wacom,vendorid=0x056a,productid=0x0069
