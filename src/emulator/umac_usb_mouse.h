#ifndef MCX_MAC_UMAC_USB_MOUSE_H_
#define MCX_MAC_UMAC_USB_MOUSE_H_

int umac_usb_mouse_init(void);
int umac_usb_mouse_bind_interface_one(void);
void umac_usb_mouse_poll(void);
void umac_usb_mouse_report(void);

#endif
