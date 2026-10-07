#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <cstdlib>
#include <utility>
#include "bcd_edit.hpp"
#include "iso_boot.hpp"
#include "add_dialog.hpp"
#include "resource.hpp"
HWND h_listbox;
HWND h_combo;
HWND h_last_focus = NULL;
HINSTANCE h_instance = NULL;
std::vector<boot_entry> current_entries;

boot_list current_list() {
	return SendMessageW(h_combo, CB_GETCURSEL, 0, 0) == 0 ? boot_list::firmware : boot_list::menu;
}

void fill_listbox() {
	SendMessageW(h_listbox, LB_RESETCONTENT, 0, 0);
	for (const auto& entry : current_entries) {
		std::wstring display_text = entry.description;
		if (!entry.path.empty()) {
			display_text += L" - " + entry.path;
		}
		SendMessageW(h_listbox, LB_ADDSTRING, 0, (LPARAM)display_text.c_str());
	}
}

void load_entries(HWND hwnd) {
	current_entries = bcd_edit::get_boot_entries(current_list());
	fill_listbox();
	if (!current_entries.empty()) {
		SendMessageW(h_listbox, LB_SETCURSEL, 0, 0);
	}
}

int selected_index() {
	int sel = static_cast<int>(SendMessageW(h_listbox, LB_GETCURSEL, 0, 0));
	if (sel == LB_ERR || sel >= static_cast<int>(current_entries.size())) {
		return -1;
	}
	return sel;
}

void show_error(HWND hwnd, const wchar_t* text, const std::wstring& detail) {
	std::wstring message = text;
	if (!detail.empty()) {
		message += L"\n" + detail;
	}
	MessageBoxW(hwnd, message.c_str(), L"Error", MB_OK | MB_ICONERROR);
}

void move_item(int direction) {
	int sel = selected_index();
	if (sel < 0) {
		return;
	}
	int new_sel = sel + direction;
	if (new_sel < 0 || new_sel >= static_cast<int>(current_entries.size())) {
		return;
	}
	std::swap(current_entries[sel], current_entries[new_sel]);
	fill_listbox();
	SendMessageW(h_listbox, LB_SETCURSEL, new_sel, 0);
}

void delete_item(HWND hwnd) {
	int sel = selected_index();
	if (sel < 0) {
		return;
	}
	if (MessageBoxW(hwnd, L"Are you sure you want to delete this boot entry?", L"Confirm Delete", MB_YESNO | MB_ICONWARNING) == IDYES) {
		boot_entry entry = current_entries[sel];
		if (bcd_edit::delete_entry(entry.guid)) {
			iso_boot::remove_entry_files(entry);
			load_entries(hwnd);
			MessageBoxW(hwnd, L"Entry deleted successfully.", L"Success", MB_OK | MB_ICONINFORMATION);
		} else {
			MessageBoxW(hwnd, L"Failed to delete entry. Please run as Administrator.", L"Error", MB_OK | MB_ICONERROR);
		}
	}
}

void set_boot_next(HWND hwnd) {
	int sel = selected_index();
	if (sel < 0) {
		return;
	}
	if (bcd_edit::set_boot_next(current_list(), current_entries[sel].guid)) {
		MessageBoxW(hwnd, L"Boot next updated for the selected entry.", L"Success", MB_OK | MB_ICONINFORMATION);
	} else {
		MessageBoxW(hwnd, L"Failed to update boot next. Please run as Administrator.", L"Error", MB_OK | MB_ICONERROR);
	}
}

void apply_changes(HWND hwnd) {
	if (bcd_edit::set_boot_order(current_list(), current_entries)) {
		MessageBoxW(hwnd, L"Boot order updated.", L"Success", MB_OK | MB_ICONINFORMATION);
	} else {
		MessageBoxW(hwnd, L"Failed to update boot order. Please run as Administrator.", L"Error", MB_OK | MB_ICONERROR);
	}
}

void add_image(HWND hwnd) {
	if (show_add_image_dialog(hwnd, h_instance)) {
		// Windows ISO and VHD entries land in the boot menu list; GRUB entries land in the firmware list on UEFI.
		load_entries(hwnd);
	}
	SetFocus(h_listbox);
}

void rename_item(HWND hwnd) {
	int sel = selected_index();
	if (sel < 0) {
		return;
	}
	std::wstring name = current_entries[sel].description;
	if (!prompt_text(hwnd, h_instance, L"Rename boot entry", L"New name for the boot entry:", name) || name.empty()) {
		return;
	}
	std::wstring output;
	if (bcd_edit::rename_entry(current_entries[sel].guid, name, output)) {
		load_entries(hwnd);
		SendMessageW(h_listbox, LB_SETCURSEL, sel, 0);
		MessageBoxW(hwnd, L"Entry renamed.", L"Success", MB_OK | MB_ICONINFORMATION);
	} else {
		show_error(hwnd, L"Failed to rename the entry.", output);
	}
}

void set_default_item(HWND hwnd) {
	int sel = selected_index();
	if (sel < 0) {
		return;
	}
	std::wstring output;
	if (bcd_edit::set_default(current_entries[sel].guid, output)) {
		MessageBoxW(hwnd, L"The selected entry is now the default in the Windows boot menu.", L"Success", MB_OK | MB_ICONINFORMATION);
	} else {
		show_error(hwnd, L"Failed to set the default entry.", output);
	}
}

void set_timeout(HWND hwnd) {
	std::wstring text = L"30";
	if (!prompt_text(hwnd, h_instance, L"Boot menu timeout", L"Seconds to show the Windows boot menu before starting the default entry:", text)) {
		return;
	}
	wchar_t* end = nullptr;
	long seconds = wcstol(text.c_str(), &end, 10);
	if (text.empty() || *end != L'\0' || seconds < 0 || seconds > 999) {
		MessageBoxW(hwnd, L"Enter a whole number of seconds from 0 to 999.", L"Error", MB_OK | MB_ICONERROR);
		return;
	}
	std::wstring output;
	if (bcd_edit::set_timeout(static_cast<int>(seconds), output)) {
		MessageBoxW(hwnd, L"Timeout updated.", L"Success", MB_OK | MB_ICONINFORMATION);
	} else {
		show_error(hwnd, L"Failed to set the timeout.", output);
	}
}

void backup_store(HWND hwnd) {
	wchar_t file[MAX_PATH] = L"bcd-backup.bcd";
	OPENFILENAMEW ofn = {sizeof(ofn)};
	ofn.hwndOwner = hwnd;
	ofn.lpstrFilter = L"Boot configuration backup (*.bcd)\0*.bcd\0";
	ofn.lpstrFile = file;
	ofn.nMaxFile = MAX_PATH;
	ofn.lpstrDefExt = L"bcd";
	ofn.lpstrTitle = L"Save a backup of the boot configuration";
	ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
	if (!GetSaveFileNameW(&ofn)) {
		return;
	}
	std::wstring output;
	if (bcd_edit::export_store(file, output)) {
		MessageBoxW(hwnd, L"Backup saved.", L"Success", MB_OK | MB_ICONINFORMATION);
	} else {
		show_error(hwnd, L"Failed to save the backup.", output);
	}
}

void restore_store(HWND hwnd) {
	wchar_t file[MAX_PATH] = L"";
	OPENFILENAMEW ofn = {sizeof(ofn)};
	ofn.hwndOwner = hwnd;
	ofn.lpstrFilter = L"Boot configuration backup (*.bcd)\0*.bcd\0All files\0*.*\0";
	ofn.lpstrFile = file;
	ofn.nMaxFile = MAX_PATH;
	ofn.lpstrTitle = L"Choose a boot configuration backup to restore";
	ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
	if (!GetOpenFileNameW(&ofn)) {
		return;
	}
	if (MessageBoxW(hwnd, L"Restoring replaces your current boot configuration with the backup. Continue?", L"Confirm Restore", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES) {
		return;
	}
	std::wstring output;
	if (bcd_edit::import_store(file, output)) {
		load_entries(hwnd);
		MessageBoxW(hwnd, L"Boot configuration restored.", L"Success", MB_OK | MB_ICONINFORMATION);
	} else {
		show_error(hwnd, L"Failed to restore the backup.", output);
	}
}

HMENU build_menu() {
	HMENU bar = CreateMenu();
	HMENU file = CreatePopupMenu();
	AppendMenuW(file, MF_STRING, ID_MENU_BACKUP, L"&Back up boot configuration...");
	AppendMenuW(file, MF_STRING, ID_MENU_RESTORE, L"&Restore boot configuration...");
	AppendMenuW(file, MF_STRING, ID_MENU_EXIT, L"E&xit");
	HMENU entry = CreatePopupMenu();
	AppendMenuW(entry, MF_STRING, ID_BTN_ADD, L"&Add ISO or VHD entry...\tCtrl+A");
	AppendMenuW(entry, MF_STRING, ID_BTN_RENAME, L"&Rename...\tF2");
	AppendMenuW(entry, MF_STRING, ID_MENU_DEFAULT, L"Set as &default\tCtrl+D");
	AppendMenuW(entry, MF_STRING, ID_BTN_UP, L"Move &up\tAlt+Up");
	AppendMenuW(entry, MF_STRING, ID_BTN_DOWN, L"Move d&own\tAlt+Down");
	AppendMenuW(entry, MF_STRING, ID_BTN_BOOT_NEXT, L"Boot &next\tCtrl+N");
	AppendMenuW(entry, MF_STRING, ID_BTN_DELETE, L"De&lete\tDelete");
	HMENU settings = CreatePopupMenu();
	AppendMenuW(settings, MF_STRING, ID_MENU_TIMEOUT, L"Boot menu &timeout...");
	AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(file), L"&File");
	AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(entry), L"&Entry");
	AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(settings), L"&Settings");
	return bar;
}

HWND make_button(HWND parent, const wchar_t* text, int y, int id, HFONT font) {
	HWND button = CreateWindowExW(0, L"BUTTON", text, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 370, y, 120, 26, parent, (HMENU)(INT_PTR)id, NULL, NULL);
	SendMessageW(button, WM_SETFONT, (WPARAM)font, TRUE);
	return button;
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
			HFONT h_font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
			HWND h_list_label = CreateWindowExW(0, L"STATIC", L"Boot list:", WS_CHILD | WS_VISIBLE, 10, 10, 350, 20, hwnd, NULL, NULL, NULL);
			h_combo = CreateWindowExW(0, L"COMBOBOX", NULL, WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST, 10, 30, 350, 100, hwnd, (HMENU)ID_COMBO_LIST, NULL, NULL);
			SendMessageW(h_combo, CB_ADDSTRING, 0, (LPARAM)L"Firmware boot entries (UEFI)");
			SendMessageW(h_combo, CB_ADDSTRING, 0, (LPARAM)L"Windows boot menu entries");
			SendMessageW(h_combo, CB_SETCURSEL, bcd_edit::is_uefi() ? 0 : 1, 0);
			HWND h_label = CreateWindowExW(0, L"STATIC", L"Boot Entries:", WS_CHILD | WS_VISIBLE, 10, 62, 350, 20, hwnd, NULL, NULL, NULL);
			h_listbox = CreateWindowExW(0, L"LISTBOX", NULL, WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP | LBS_NOTIFY | LBS_HASSTRINGS, 10, 82, 350, 200, hwnd, (HMENU)ID_LISTBOX, NULL, NULL);
			make_button(hwnd, L"Up", 30, ID_BTN_UP, h_font);
			make_button(hwnd, L"Down", 60, ID_BTN_DOWN, h_font);
			make_button(hwnd, L"Add", 90, ID_BTN_ADD, h_font);
			make_button(hwnd, L"Rename", 120, ID_BTN_RENAME, h_font);
			make_button(hwnd, L"Delete", 150, ID_BTN_DELETE, h_font);
			make_button(hwnd, L"Boot Next", 180, ID_BTN_BOOT_NEXT, h_font);
			make_button(hwnd, L"Apply", 220, ID_BTN_APPLY, h_font);
			SendMessageW(h_list_label, WM_SETFONT, (WPARAM)h_font, TRUE);
			SendMessageW(h_combo, WM_SETFONT, (WPARAM)h_font, TRUE);
			SendMessageW(h_label, WM_SETFONT, (WPARAM)h_font, TRUE);
			SendMessageW(h_listbox, WM_SETFONT, (WPARAM)h_font, TRUE);
			load_entries(hwnd);
			return 0;
		}
		case WM_COMMAND: {
			int wm_id = LOWORD(w_param);
			if (wm_id == ID_COMBO_LIST) {
				if (HIWORD(w_param) == CBN_SELCHANGE) {
					load_entries(hwnd);
				}
			} else if (wm_id == ID_BTN_UP || wm_id == ID_ACCEL_UP) {
				move_item(-1);
			} else if (wm_id == ID_BTN_DOWN || wm_id == ID_ACCEL_DOWN) {
				move_item(1);
			} else if (wm_id == ID_BTN_DELETE || wm_id == ID_ACCEL_DELETE) {
				delete_item(hwnd);
			} else if (wm_id == ID_BTN_BOOT_NEXT || wm_id == ID_ACCEL_BOOT_NEXT) {
				set_boot_next(hwnd);
			} else if (wm_id == ID_BTN_APPLY) {
				apply_changes(hwnd);
			} else if (wm_id == ID_BTN_ADD || wm_id == ID_ACCEL_ADD) {
				add_image(hwnd);
			} else if (wm_id == ID_BTN_RENAME || wm_id == ID_ACCEL_RENAME) {
				rename_item(hwnd);
			} else if (wm_id == ID_MENU_DEFAULT || wm_id == ID_ACCEL_DEFAULT) {
				set_default_item(hwnd);
			} else if (wm_id == ID_MENU_TIMEOUT) {
				set_timeout(hwnd);
			} else if (wm_id == ID_MENU_BACKUP) {
				backup_store(hwnd);
			} else if (wm_id == ID_MENU_RESTORE) {
				restore_store(hwnd);
			} else if (wm_id == ID_MENU_EXIT) {
				DestroyWindow(hwnd);
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
	h_instance = h_inst;
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
	HWND hwnd = CreateWindowExW(WS_EX_CONTROLPARENT, L"BootSwapClass", L"Boot Swap", WS_OVERLAPPEDWINDOW ^ WS_THICKFRAME ^ WS_MAXIMIZEBOX, CW_USEDEFAULT, CW_USEDEFAULT, 520, 390, NULL, build_menu(), h_inst, NULL);
	ShowWindow(hwnd, cmd_show);
	UpdateWindow(hwnd);
	SetFocus(h_listbox);
	ACCEL accel_table[] = {
		{ FALT | FVIRTKEY, VK_UP, ID_ACCEL_UP },
		{ FALT | FVIRTKEY, VK_DOWN, ID_ACCEL_DOWN },
		{ FVIRTKEY, VK_DELETE, ID_ACCEL_DELETE },
		{ FCONTROL | FVIRTKEY, 'N', ID_ACCEL_BOOT_NEXT },
		{ FCONTROL | FVIRTKEY, 'A', ID_ACCEL_ADD },
		{ FVIRTKEY, VK_F2, ID_ACCEL_RENAME },
		{ FCONTROL | FVIRTKEY, 'D', ID_ACCEL_DEFAULT }
	};
	HACCEL h_accel = CreateAcceleratorTableW(accel_table, 7);
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
