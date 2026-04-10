#include <windows.h>
#include <commctrl.h>
#include "bcd_edit.hpp"
#include "resource.hpp"
HWND h_listbox;
HWND h_last_focus = NULL;
std::vector<boot_entry> current_entries;

void load_entries(HWND hwnd) {
	current_entries = bcd_edit::get_boot_entries();
	SendMessageW(h_listbox, LB_RESETCONTENT, 0, 0);
	for (const auto& entry : current_entries) {
		std::wstring display_text = entry.description;
		if (!entry.path.empty()) {
			display_text += L" - " + entry.path;
		}
		SendMessageW(h_listbox, LB_ADDSTRING, 0, (LPARAM)display_text.c_str());
	}
	if (!current_entries.empty()) {
		SendMessageW(h_listbox, LB_SETCURSEL, 0, 0);
	}
}

void move_item(int direction) {
	int sel = SendMessageW(h_listbox, LB_GETCURSEL, 0, 0);
	if (sel == LB_ERR) {
		return;
	}
	int new_sel = sel + direction;
	if (new_sel < 0 || new_sel >= static_cast<int>(current_entries.size())) {
		return;
	}
	std::swap(current_entries[sel], current_entries[new_sel]);
	SendMessageW(h_listbox, LB_RESETCONTENT, 0, 0);
	for (const auto& entry : current_entries) {
		std::wstring display_text = entry.description;
		if (!entry.path.empty()) {
			display_text += L" - " + entry.path;
		}
		SendMessageW(h_listbox, LB_ADDSTRING, 0, (LPARAM)display_text.c_str());
	}
	SendMessageW(h_listbox, LB_SETCURSEL, new_sel, 0);
}

void delete_item(HWND hwnd) {
	int sel = SendMessageW(h_listbox, LB_GETCURSEL, 0, 0);
	if (sel == LB_ERR) {
		return;
	}
	if (MessageBoxW(hwnd, L"Are you sure you want to delete this boot entry?", L"Confirm Delete", MB_YESNO | MB_ICONWARNING) == IDYES) {
		if (bcd_edit::delete_entry(current_entries[sel].guid)) {
			load_entries(hwnd);
			MessageBoxW(hwnd, L"Entry deleted successfully.", L"Success", MB_OK | MB_ICONINFORMATION);
		} else {
			MessageBoxW(hwnd, L"Failed to delete entry. Please run as Administrator.", L"Error", MB_OK | MB_ICONERROR);
		}
	}
}

void apply_changes(HWND hwnd) {
	if (bcd_edit::set_boot_order(current_entries)) {
		MessageBoxW(hwnd, L"Boot order updated.", L"Success", MB_OK | MB_ICONINFORMATION);
	} else {
		MessageBoxW(hwnd, L"Failed to update boot order. Please run as Administrator.", L"Error", MB_OK | MB_ICONERROR);
	}
}

LRESULT CALLBACK window_proc(HWND hwnd, UINT u_msg, WPARAM w_param, LPARAM l_param) {
	switch (u_msg) {
		case WM_ACTIVATE: {
			if (LOWORD(w_param) == WA_INACTIVE) {
				h_last_focus = GetFocus();
			} else {
				if (h_last_focus != NULL) {
					SetFocus(h_last_focus);
				} else {
					SetFocus(h_listbox);
				}
			}
			return 0;
		}
		case WM_CREATE: {
			HWND h_label = CreateWindowExW(0, L"STATIC", L"Boot Entries:", WS_CHILD | WS_VISIBLE, 10, 10, 350, 20, hwnd, NULL, NULL, NULL);
			h_listbox = CreateWindowExW(0, L"LISTBOX", NULL, WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP | LBS_NOTIFY | LBS_HASSTRINGS, 10, 30, 350, 180, hwnd, (HMENU)ID_LISTBOX, NULL, NULL);
			CreateWindowExW(0, L"BUTTON", L"Up", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 370, 10, 120, 30, hwnd, (HMENU)ID_BTN_UP, NULL, NULL);
			CreateWindowExW(0, L"BUTTON", L"Down", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 370, 50, 120, 30, hwnd, (HMENU)ID_BTN_DOWN, NULL, NULL);
			CreateWindowExW(0, L"BUTTON", L"Delete", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 370, 90, 120, 30, hwnd, (HMENU)ID_BTN_DELETE, NULL, NULL);
			CreateWindowExW(0, L"BUTTON", L"Apply", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 370, 180, 120, 30, hwnd, (HMENU)ID_BTN_APPLY, NULL, NULL);
			HFONT h_font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
			SendMessageW(h_label, WM_SETFONT, (WPARAM)h_font, TRUE);
			SendMessageW(h_listbox, WM_SETFONT, (WPARAM)h_font, TRUE);
			SendMessageW(GetDlgItem(hwnd, ID_BTN_UP), WM_SETFONT, (WPARAM)h_font, TRUE);
			SendMessageW(GetDlgItem(hwnd, ID_BTN_DOWN), WM_SETFONT, (WPARAM)h_font, TRUE);
			SendMessageW(GetDlgItem(hwnd, ID_BTN_DELETE), WM_SETFONT, (WPARAM)h_font, TRUE);
			SendMessageW(GetDlgItem(hwnd, ID_BTN_APPLY), WM_SETFONT, (WPARAM)h_font, TRUE);
			load_entries(hwnd);
			return 0;
		}
		case WM_COMMAND: {
			int wm_id = LOWORD(w_param);
			if (wm_id == ID_BTN_UP || wm_id == ID_ACCEL_UP) {
				move_item(-1);
			} else if (wm_id == ID_BTN_DOWN || wm_id == ID_ACCEL_DOWN) {
				move_item(1);
			} else if (wm_id == ID_BTN_DELETE || wm_id == ID_ACCEL_DELETE) {
				delete_item(hwnd);
			} else if (wm_id == ID_BTN_APPLY) {
				apply_changes(hwnd);
			}
			return 0;
		}
		case WM_DESTROY: {
			PostQuitMessage(0);
			return 0;
		}
	}
	return DefWindowProcW(hwnd, u_msg, w_param, l_param);
}

int WINAPI WinMain(HINSTANCE h_inst, HINSTANCE h_prev, LPSTR cmd_line, int cmd_show) {
	CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
CoInitializeSecurity(
	NULL,
	-1,
	NULL,
	NULL,
	RPC_C_AUTHN_LEVEL_DEFAULT,
	RPC_C_IMP_LEVEL_IMPERSONATE,
	NULL,
	EOAC_NONE,
	NULL
);
	WNDCLASSEXW wc = {sizeof(WNDCLASSEXW)};
	wc.lpfnWndProc = window_proc;
	wc.hInstance = h_inst;
	wc.lpszClassName = L"BootSwapClass";
	wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);
	RegisterClassExW(&wc);
	HWND hwnd = CreateWindowExW(WS_EX_CONTROLPARENT, L"BootSwapClass", L"Boot Swap", WS_OVERLAPPEDWINDOW ^ WS_THICKFRAME ^ WS_MAXIMIZEBOX, CW_USEDEFAULT, CW_USEDEFAULT, 520, 260, NULL, NULL, h_inst, NULL);
	ShowWindow(hwnd, cmd_show);
	UpdateWindow(hwnd);
	SetFocus(h_listbox);
	ACCEL accel_table[] = {
		{ FALT | FVIRTKEY, VK_UP, ID_ACCEL_UP },
		{ FALT | FVIRTKEY, VK_DOWN, ID_ACCEL_DOWN },
		{ FVIRTKEY, VK_DELETE, ID_ACCEL_DELETE }
	};
	HACCEL h_accel = CreateAcceleratorTableW(accel_table, 3);
	MSG msg;
	while (GetMessageW(&msg, NULL, 0, 0)) {
		if (!TranslateAcceleratorW(hwnd, h_accel, &msg)) {
			if (!IsDialogMessageW(hwnd, &msg)) {
				TranslateMessage(&msg);
				DispatchMessageW(&msg);
			}
		}
	}
	CoUninitialize();
	return (int)msg.wParam;
}
