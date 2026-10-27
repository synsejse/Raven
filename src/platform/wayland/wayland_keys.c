/** Wayland xkb symbols use the same Raven key mapping as the X11 backend. */
#include "wayland_internal.h"
#include <xkbcommon/xkbcommon-keysyms.h>

RKey rvn_wl_translate_keysym(xkb_keysym_t symbol) {
	/* Latin letters and digits share their codes with ASCII */
	if (symbol >= XKB_KEY_a && symbol <= XKB_KEY_z) return (RKey)(R_KEY_A + (int)(symbol - XKB_KEY_a));
	if (symbol >= XKB_KEY_A && symbol <= XKB_KEY_Z) return (RKey)(R_KEY_A + (int)(symbol - XKB_KEY_A));
	if (symbol >= XKB_KEY_0 && symbol <= XKB_KEY_9) return (RKey)(R_KEY_0 + (int)(symbol - XKB_KEY_0));
	if (symbol >= XKB_KEY_F1 && symbol <= XKB_KEY_F12) return (RKey)(R_KEY_F1 + (int)(symbol - XKB_KEY_F1));
	if (symbol >= XKB_KEY_KP_0 && symbol <= XKB_KEY_KP_9) return (RKey)(R_KEY_KP_0 + (int)(symbol - XKB_KEY_KP_0));

	switch (symbol) {
		case XKB_KEY_space: return R_KEY_SPACE;
		case XKB_KEY_apostrophe: return R_KEY_APOSTROPHE;
		case XKB_KEY_comma: return R_KEY_COMMA;
		case XKB_KEY_minus: return R_KEY_MINUS;
		case XKB_KEY_period: return R_KEY_PERIOD;
		case XKB_KEY_slash: return R_KEY_SLASH;
		case XKB_KEY_semicolon: return R_KEY_SEMICOLON;
		case XKB_KEY_equal: return R_KEY_EQUAL;
		case XKB_KEY_bracketleft: return R_KEY_LEFT_BRACKET;
		case XKB_KEY_backslash: return R_KEY_BACKSLASH;
		case XKB_KEY_bracketright: return R_KEY_RIGHT_BRACKET;
		case XKB_KEY_grave: return R_KEY_GRAVE;

		case XKB_KEY_Escape: return R_KEY_ESCAPE;
		case XKB_KEY_Return: return R_KEY_ENTER;
		case XKB_KEY_Tab: return R_KEY_TAB;
		case XKB_KEY_ISO_Left_Tab: return R_KEY_TAB;
		case XKB_KEY_BackSpace: return R_KEY_BACKSPACE;
		case XKB_KEY_Insert: return R_KEY_INSERT;
		case XKB_KEY_Delete: return R_KEY_DELETE;
		case XKB_KEY_Right: return R_KEY_RIGHT;
		case XKB_KEY_Left: return R_KEY_LEFT;
		case XKB_KEY_Down: return R_KEY_DOWN;
		case XKB_KEY_Up: return R_KEY_UP;
		case XKB_KEY_Page_Up: return R_KEY_PAGE_UP;
		case XKB_KEY_Page_Down: return R_KEY_PAGE_DOWN;
		case XKB_KEY_Home: return R_KEY_HOME;
		case XKB_KEY_End: return R_KEY_END;
		case XKB_KEY_Caps_Lock: return R_KEY_CAPS_LOCK;
		case XKB_KEY_Print: return R_KEY_PRINT_SCREEN;
		case XKB_KEY_Pause: return R_KEY_PAUSE;
		case XKB_KEY_Menu: return R_KEY_MENU;

		/* keypad: the same keys with and without Num Lock */
		case XKB_KEY_KP_Insert: return R_KEY_KP_0;
		case XKB_KEY_KP_End: return R_KEY_KP_1;
		case XKB_KEY_KP_Down: return R_KEY_KP_2;
		case XKB_KEY_KP_Page_Down: return R_KEY_KP_3;
		case XKB_KEY_KP_Left: return R_KEY_KP_4;
		case XKB_KEY_KP_Begin: return R_KEY_KP_5;
		case XKB_KEY_KP_Right: return R_KEY_KP_6;
		case XKB_KEY_KP_Home: return R_KEY_KP_7;
		case XKB_KEY_KP_Up: return R_KEY_KP_8;
		case XKB_KEY_KP_Page_Up: return R_KEY_KP_9;
		case XKB_KEY_KP_Decimal: return R_KEY_KP_DECIMAL;
		case XKB_KEY_KP_Delete: return R_KEY_KP_DECIMAL;
		case XKB_KEY_KP_Divide: return R_KEY_KP_DIVIDE;
		case XKB_KEY_KP_Multiply: return R_KEY_KP_MULTIPLY;
		case XKB_KEY_KP_Subtract: return R_KEY_KP_SUBTRACT;
		case XKB_KEY_KP_Add: return R_KEY_KP_ADD;
		case XKB_KEY_KP_Enter: return R_KEY_KP_ENTER;

		case XKB_KEY_Shift_L: return R_KEY_LEFT_SHIFT;
		case XKB_KEY_Control_L: return R_KEY_LEFT_CTRL;
		case XKB_KEY_Alt_L: return R_KEY_LEFT_ALT;
		case XKB_KEY_Meta_L: return R_KEY_LEFT_ALT;
		case XKB_KEY_Super_L: return R_KEY_LEFT_SUPER;
		case XKB_KEY_Shift_R: return R_KEY_RIGHT_SHIFT;
		case XKB_KEY_Control_R: return R_KEY_RIGHT_CTRL;
		case XKB_KEY_Alt_R: return R_KEY_RIGHT_ALT;
		case XKB_KEY_Meta_R: return R_KEY_RIGHT_ALT;
		case XKB_KEY_ISO_Level3_Shift: return R_KEY_RIGHT_ALT;
		case XKB_KEY_Super_R: return R_KEY_RIGHT_SUPER;
		default: return R_KEY_UNKNOWN;
	}
}
