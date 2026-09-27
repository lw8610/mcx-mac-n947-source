/* SPDX-License-Identifier: MIT */
#ifndef MCX_MAC_UMAC_USB_HID_KEYS_H_
#define MCX_MAC_UMAC_USB_HID_KEYS_H_

#include <stdint.h>

/* Macintosh virtual key codes from Apple's HIToolbox Events.h. That header
 * documents these as the physical key codes originally published in Inside
 * Macintosh Volume V. Keep a local enum because the embedded target does not
 * ship the macOS SDK headers. */
enum umac_virtual_key {
	UMAC_VK_A = 0x00, UMAC_VK_S = 0x01, UMAC_VK_D = 0x02,
	UMAC_VK_F = 0x03, UMAC_VK_H = 0x04, UMAC_VK_G = 0x05,
	UMAC_VK_Z = 0x06, UMAC_VK_X = 0x07, UMAC_VK_C = 0x08,
	UMAC_VK_V = 0x09, UMAC_VK_AngleBracket = 0x0a,
	UMAC_VK_B = 0x0b, UMAC_VK_Q = 0x0c, UMAC_VK_W = 0x0d,
	UMAC_VK_E = 0x0e, UMAC_VK_R = 0x0f, UMAC_VK_Y = 0x10,
	UMAC_VK_T = 0x11, UMAC_VK_1 = 0x12, UMAC_VK_2 = 0x13,
	UMAC_VK_3 = 0x14, UMAC_VK_4 = 0x15, UMAC_VK_6 = 0x16,
	UMAC_VK_5 = 0x17, UMAC_VK_Equal = 0x18, UMAC_VK_9 = 0x19,
	UMAC_VK_7 = 0x1a, UMAC_VK_Minus = 0x1b, UMAC_VK_8 = 0x1c,
	UMAC_VK_0 = 0x1d, UMAC_VK_RightBracket = 0x1e,
	UMAC_VK_O = 0x1f, UMAC_VK_U = 0x20, UMAC_VK_LeftBracket = 0x21,
	UMAC_VK_I = 0x22, UMAC_VK_P = 0x23, UMAC_VK_Return = 0x24,
	UMAC_VK_L = 0x25, UMAC_VK_J = 0x26, UMAC_VK_SingleQuote = 0x27,
	UMAC_VK_K = 0x28, UMAC_VK_SemiColon = 0x29,
	UMAC_VK_BackSlash = 0x2a, UMAC_VK_Comma = 0x2b,
	UMAC_VK_Slash = 0x2c, UMAC_VK_N = 0x2d, UMAC_VK_M = 0x2e,
	UMAC_VK_Period = 0x2f, UMAC_VK_Tab = 0x30, UMAC_VK_Space = 0x31,
	UMAC_VK_Grave = 0x32, UMAC_VK_BackSpace = 0x33,
	UMAC_VK_Escape = 0x35, UMAC_VK_Command = 0x37,
	UMAC_VK_Shift = 0x38, UMAC_VK_CapsLock = 0x39,
	UMAC_VK_Option = 0x3a, UMAC_VK_Control = 0x3b,
	UMAC_VK_Decimal = 0x41, UMAC_VK_KPMultiply = 0x43,
	UMAC_VK_KPAdd = 0x45, UMAC_VK_Clear = 0x47,
	UMAC_VK_KPDivide = 0x4b, UMAC_VK_Enter = 0x4c,
	UMAC_VK_KPSubtract = 0x4e, UMAC_VK_KPEqual = 0x51,
	UMAC_VK_KP0 = 0x52, UMAC_VK_KP1 = 0x53, UMAC_VK_KP2 = 0x54,
	UMAC_VK_KP3 = 0x55, UMAC_VK_KP4 = 0x56, UMAC_VK_KP5 = 0x57,
	UMAC_VK_KP6 = 0x58, UMAC_VK_KP7 = 0x59, UMAC_VK_KP8 = 0x5b,
	UMAC_VK_KP9 = 0x5c, UMAC_VK_F5 = 0x60, UMAC_VK_F6 = 0x61,
	UMAC_VK_F7 = 0x62, UMAC_VK_F3 = 0x63, UMAC_VK_F8 = 0x64,
	UMAC_VK_F9 = 0x65, UMAC_VK_F11 = 0x67, UMAC_VK_Print = 0x69,
	UMAC_VK_ScrollLock = 0x6b, UMAC_VK_F10 = 0x6d,
	UMAC_VK_F12 = 0x6f, UMAC_VK_Pause = 0x71, UMAC_VK_Help = 0x72,
	UMAC_VK_Home = 0x73, UMAC_VK_PageUp = 0x74,
	UMAC_VK_ForwardDel = 0x75, UMAC_VK_F4 = 0x76, UMAC_VK_End = 0x77,
	UMAC_VK_F2 = 0x78, UMAC_VK_PageDown = 0x79, UMAC_VK_F1 = 0x7a,
	UMAC_VK_Left = 0x7b, UMAC_VK_Right = 0x7c,
	UMAC_VK_Down = 0x7d, UMAC_VK_Up = 0x7e,
	UMAC_VK_None = 0xff,
};

/* USB HID Keyboard/Keypad usage IDs map to physical Macintosh keys. */
static inline uint8_t umac_usb_hid_usage_to_mac(uint8_t usage)
{
	static const uint8_t letters[] = {
		UMAC_VK_A, UMAC_VK_B, UMAC_VK_C, UMAC_VK_D, UMAC_VK_E, UMAC_VK_F, UMAC_VK_G,
		UMAC_VK_H, UMAC_VK_I, UMAC_VK_J, UMAC_VK_K, UMAC_VK_L, UMAC_VK_M, UMAC_VK_N,
		UMAC_VK_O, UMAC_VK_P, UMAC_VK_Q, UMAC_VK_R, UMAC_VK_S, UMAC_VK_T, UMAC_VK_U,
		UMAC_VK_V, UMAC_VK_W, UMAC_VK_X, UMAC_VK_Y, UMAC_VK_Z,
	};
	static const uint8_t digits[] = {
		UMAC_VK_1, UMAC_VK_2, UMAC_VK_3, UMAC_VK_4, UMAC_VK_5,
		UMAC_VK_6, UMAC_VK_7, UMAC_VK_8, UMAC_VK_9, UMAC_VK_0,
	};

	if (usage >= 0x04U && usage <= 0x1dU) {
		return letters[usage - 0x04U];
	}
	if (usage >= 0x1eU && usage <= 0x27U) {
		return digits[usage - 0x1eU];
	}
	switch (usage) {
	case 0x28: return UMAC_VK_Return;
	case 0x29: return UMAC_VK_Escape;
	case 0x2a: return UMAC_VK_BackSpace;
	case 0x2b: return UMAC_VK_Tab;
	case 0x2c: return UMAC_VK_Space;
	case 0x2d: return UMAC_VK_Minus;
	case 0x2e: return UMAC_VK_Equal;
	case 0x2f: return UMAC_VK_LeftBracket;
	case 0x30: return UMAC_VK_RightBracket;
	case 0x31: return UMAC_VK_BackSlash;
	case 0x32: return UMAC_VK_AngleBracket;
	case 0x33: return UMAC_VK_SemiColon;
	case 0x34: return UMAC_VK_SingleQuote;
	case 0x35: return UMAC_VK_Grave;
	case 0x36: return UMAC_VK_Comma;
	case 0x37: return UMAC_VK_Period;
	case 0x38: return UMAC_VK_Slash;
	case 0x39: return UMAC_VK_CapsLock;
	case 0x3a: return UMAC_VK_F1;
	case 0x3b: return UMAC_VK_F2;
	case 0x3c: return UMAC_VK_F3;
	case 0x3d: return UMAC_VK_F4;
	case 0x3e: return UMAC_VK_F5;
	case 0x3f: return UMAC_VK_F6;
	case 0x40: return UMAC_VK_F7;
	case 0x41: return UMAC_VK_F8;
	case 0x42: return UMAC_VK_F9;
	case 0x43: return UMAC_VK_F10;
	case 0x44: return UMAC_VK_F11;
	case 0x45: return UMAC_VK_F12;
	case 0x46: return UMAC_VK_Print;
	case 0x47: return UMAC_VK_ScrollLock;
	case 0x48: return UMAC_VK_Pause;
	case 0x49: return UMAC_VK_Help;
	case 0x4a: return UMAC_VK_Home;
	case 0x4b: return UMAC_VK_PageUp;
	case 0x4c: return UMAC_VK_ForwardDel;
	case 0x4d: return UMAC_VK_End;
	case 0x4e: return UMAC_VK_PageDown;
	case 0x4f: return UMAC_VK_Right;
	case 0x50: return UMAC_VK_Left;
	case 0x51: return UMAC_VK_Down;
	case 0x52: return UMAC_VK_Up;
	case 0x53: return UMAC_VK_Clear;
	case 0x54: return UMAC_VK_KPDivide;
	case 0x55: return UMAC_VK_KPMultiply;
	case 0x56: return UMAC_VK_KPSubtract;
	case 0x57: return UMAC_VK_KPAdd;
	case 0x58: return UMAC_VK_Enter;
	case 0x59: return UMAC_VK_KP1;
	case 0x5a: return UMAC_VK_KP2;
	case 0x5b: return UMAC_VK_KP3;
	case 0x5c: return UMAC_VK_KP4;
	case 0x5d: return UMAC_VK_KP5;
	case 0x5e: return UMAC_VK_KP6;
	case 0x5f: return UMAC_VK_KP7;
	case 0x60: return UMAC_VK_KP8;
	case 0x61: return UMAC_VK_KP9;
	case 0x62: return UMAC_VK_KP0;
	case 0x63: return UMAC_VK_Decimal;
	case 0x67: return UMAC_VK_KPEqual;
	default: return UMAC_VK_None;
	}
}

static inline uint8_t umac_usb_hid_modifier_to_mac(uint8_t bit)
{
	switch (bit) {
	case 0: case 4: return UMAC_VK_Control;
	case 1: case 5: return UMAC_VK_Shift;
	case 2: case 6: return UMAC_VK_Option;
	case 3: case 7: return UMAC_VK_Command;
	default: return UMAC_VK_None;
	}
}

#endif
