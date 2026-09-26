/* SPDX-License-Identifier: MIT */
#ifndef UMAC_USB_TRANSFER_H
#define UMAC_USB_TRANSFER_H

#include <stdbool.h>

bool umac_usb_transfer_requested(void);
int umac_usb_transfer_run(void);

#endif
