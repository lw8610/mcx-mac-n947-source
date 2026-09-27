/* SPDX-License-Identifier: MIT */
#include <assert.h>

#include "emulator/umac_usb_hid_keys.h"

int main(void)
{
	assert(umac_usb_hid_usage_to_mac(0x04) == UMAC_VK_A);
	assert(umac_usb_hid_usage_to_mac(0x1d) == UMAC_VK_Z);
	assert(umac_usb_hid_usage_to_mac(0x1e) == UMAC_VK_1);
	assert(umac_usb_hid_usage_to_mac(0x27) == UMAC_VK_0);
	assert(umac_usb_hid_usage_to_mac(0x28) == UMAC_VK_Return);
	assert(umac_usb_hid_usage_to_mac(0x39) == UMAC_VK_CapsLock);
	assert(umac_usb_hid_usage_to_mac(0x50) == UMAC_VK_Left);
	assert(umac_usb_hid_usage_to_mac(0x63) == UMAC_VK_Decimal);
	assert(umac_usb_hid_usage_to_mac(0xff) == UMAC_VK_None);
	assert(umac_usb_hid_modifier_to_mac(0) == UMAC_VK_Control);
	assert(umac_usb_hid_modifier_to_mac(1) == UMAC_VK_Shift);
	assert(umac_usb_hid_modifier_to_mac(2) == UMAC_VK_Option);
	assert(umac_usb_hid_modifier_to_mac(3) == UMAC_VK_Command);
	assert(umac_usb_hid_modifier_to_mac(7) == UMAC_VK_Command);
	return 0;
}
