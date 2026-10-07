#include "add_dialog.hpp"
#include "iso_boot.hpp"
#include <commdlg.h>
#include <string>
#pragma comment(lib, "comdlg32.lib")

namespace {
const int ID_TYPE = 301;
const int ID_NAME = 302;
const int ID_IMAGE = 303;
const int ID_BROWSE_IMAGE = 304;
const int ID_LOADER = 305;
const int ID_BROWSE_LOADER = 306;
const int ID_COPY_INSTALL = 307;
const int ID_STATUS = 308;
const UINT WM_WORK_DONE = WM_APP + 1;

struct dialog_state {
	HWND hwnd = NULL;
	HWND type = NULL;
	HWND name = NULL;
	HWND image = NULL;
	HWND browse_image = NULL;
	HWND loader = NULL;
	HWND browse_loader = NULL;
	HWND copy_install = NULL;
	HWND status = NULL;
	HWND ok = NULL;
	HWND cancel = NULL;
	image_request request;
	std::wstring message;
	bool success = false;
	bool working = false;
	bool finished = false;
	bool added = false;
};

std::wstring get_text(HWND control) {
	int len = GetWindowTextLengthW(control);
	std::wstring text(len, L'\0');
	if (len > 0) {
		GetWindowTextW(control, &text[0], len + 1);
	}
	return text;
}

HWND make_label(HWND parent, const wchar_t* text, int x, int y, int w) {
	HWND h = CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE, x, y, w, 18, parent, NULL, NULL, NULL);
	SendMessageW(h, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
	return h;
}

HWND make_control(HWND parent, const wchar_t* cls, const wchar_t* text, DWORD style, int x, int y, int w, int h, int id) {
	HWND c = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style, x, y, w, h, parent, (HMENU)(INT_PTR)id, NULL, NULL);
	SendMessageW(c, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
	return c;
}

bool browse(HWND owner, std::wstring& path, const wchar_t* filter, const wchar_t* title) {
	wchar_t buffer[MAX_PATH * 2] = {};
	OPENFILENAMEW ofn = {sizeof(ofn)};
	ofn.hwndOwner = owner;
	ofn.lpstrFilter = filter;
	ofn.lpstrFile = buffer;
	ofn.nMaxFile = MAX_PATH * 2;
	ofn.lpstrTitle = title;
	ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
	if (GetOpenFileNameW(&ofn)) {
		path = buffer;
		return true;
	}
	return false;
}

void update_enabled(dialog_state* s) {
	int sel = static_cast<int>(SendMessageW(s->type, CB_GETCURSEL, 0, 0));
	bool other = sel == 1;
	bool windows_iso = sel == 0;
	EnableWindow(s->loader, other && !s->working);
	EnableWindow(s->browse_loader, other && !s->working);
	EnableWindow(s->copy_install, windows_iso && !s->working);
}

void set_working(dialog_state* s, bool working) {
	s->working = working;
	EnableWindow(s->type, !working);
	EnableWindow(s->name, !working);
	EnableWindow(s->image, !working);
	EnableWindow(s->browse_image, !working);
	EnableWindow(s->ok, !working);
	update_enabled(s);
	SetWindowTextW(s->status, working ? L"Working. Please wait, this can take a few minutes for large images." : L"");
}

DWORD WINAPI worker(LPVOID param) {
	dialog_state* s = static_cast<dialog_state*>(param);
	CoInitializeEx(NULL, COINIT_MULTITHREADED);
	s->success = iso_boot::add_image_entry(s->request, s->message);
	CoUninitialize();
	PostMessageW(s->hwnd, WM_WORK_DONE, 0, 0);
	return 0;
}

void start_add(dialog_state* s) {
	int sel = static_cast<int>(SendMessageW(s->type, CB_GETCURSEL, 0, 0));
	s->request.kind = sel == 0 ? image_kind::windows_iso : (sel == 1 ? image_kind::other_iso : image_kind::windows_vhd);
	s->request.description = get_text(s->name);
	s->request.image_path = get_text(s->image);
	s->request.loader_path = get_text(s->loader);
	s->request.copy_install_files = SendMessageW(s->copy_install, BM_GETCHECK, 0, 0) == BST_CHECKED;
	set_working(s, true);
	HANDLE thread = CreateThread(NULL, 0, worker, s, 0, NULL);
	if (thread) {
		CloseHandle(thread);
	} else {
		s->message = L"Could not start the background task.";
		s->success = false;
		PostMessageW(s->hwnd, WM_WORK_DONE, 0, 0);
	}
}

LRESULT CALLBACK dialog_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
	dialog_state* s = reinterpret_cast<dialog_state*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
	switch (msg) {
		case WM_NCCREATE: {
			CREATESTRUCTW* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
			SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
			return DefWindowProcW(hwnd, msg, wp, lp);
		}
		case WM_CREATE: {
			s = reinterpret_cast<dialog_state*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
			s->hwnd = hwnd;
			make_label(hwnd, L"Type of image:", 10, 10, 450);
			s->type = make_control(hwnd, L"COMBOBOX", NULL, CBS_DROPDOWNLIST | WS_TABSTOP | WS_VSCROLL, 10, 30, 450, 120, ID_TYPE);
			SendMessageW(s->type, CB_ADDSTRING, 0, (LPARAM)L"Windows setup or recovery ISO");
			SendMessageW(s->type, CB_ADDSTRING, 0, (LPARAM)L"Linux or other ISO, started through GRUB");
			SendMessageW(s->type, CB_ADDSTRING, 0, (LPARAM)L"Windows installed in a VHD or VHDX file");
			SendMessageW(s->type, CB_SETCURSEL, 0, 0);
			make_label(hwnd, L"Name shown in the boot menu:", 10, 60, 450);
			s->name = make_control(hwnd, L"EDIT", NULL, WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL, 10, 80, 450, 22, ID_NAME);
			make_label(hwnd, L"Image file path:", 10, 110, 450);
			s->image = make_control(hwnd, L"EDIT", NULL, WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL, 10, 130, 300, 22, ID_IMAGE);
			s->browse_image = make_control(hwnd, L"BUTTON", L"Browse for image", WS_TABSTOP | BS_PUSHBUTTON, 320, 128, 140, 26, ID_BROWSE_IMAGE);
			make_label(hwnd, L"Loader file, GRUB2 EFI or GRUB4DOS grldr, for Linux or other ISO only:", 10, 160, 450);
			s->loader = make_control(hwnd, L"EDIT", NULL, WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL, 10, 180, 300, 22, ID_LOADER);
			s->browse_loader = make_control(hwnd, L"BUTTON", L"Browse for loader", WS_TABSTOP | BS_PUSHBUTTON, 320, 178, 140, 26, ID_BROWSE_LOADER);
			s->copy_install = make_control(hwnd, L"BUTTON", L"Copy install files so Windows Setup can find them", WS_TABSTOP | BS_AUTOCHECKBOX, 10, 212, 450, 20, ID_COPY_INSTALL);
			s->status = make_control(hwnd, L"STATIC", L"", 0, 10, 240, 450, 34, ID_STATUS);
			s->ok = make_control(hwnd, L"BUTTON", L"Add entry", WS_TABSTOP | BS_DEFPUSHBUTTON, 270, 280, 90, 28, IDOK);
			s->cancel = make_control(hwnd, L"BUTTON", L"Cancel", WS_TABSTOP | BS_PUSHBUTTON, 370, 280, 90, 28, IDCANCEL);
			update_enabled(s);
			return 0;
		}
		case WM_COMMAND: {
			int id = LOWORD(wp);
			if (id == ID_TYPE && HIWORD(wp) == CBN_SELCHANGE) {
				update_enabled(s);
			} else if (id == ID_BROWSE_IMAGE) {
				std::wstring path;
				int sel = static_cast<int>(SendMessageW(s->type, CB_GETCURSEL, 0, 0));
				bool ok = sel == 2
					? browse(hwnd, path, L"Virtual disks (*.vhd;*.vhdx)\0*.vhd;*.vhdx\0All files\0*.*\0", L"Choose a virtual disk file")
					: browse(hwnd, path, L"ISO images (*.iso)\0*.iso\0All files\0*.*\0", L"Choose an ISO file");
				if (ok) {
					SetWindowTextW(s->image, path.c_str());
					if (get_text(s->name).empty()) {
						size_t slash = path.find_last_of(L'\\');
						std::wstring base = path.substr(slash + 1);
						size_t dot = base.find_last_of(L'.');
						SetWindowTextW(s->name, base.substr(0, dot).c_str());
					}
				}
			} else if (id == ID_BROWSE_LOADER) {
				std::wstring path;
				if (browse(hwnd, path, L"Loader files\0*.efi;grldr\0All files\0*.*\0", L"Choose the GRUB loader file")) {
					SetWindowTextW(s->loader, path.c_str());
				}
			} else if (id == IDOK) {
				if (!s->working) {
					start_add(s);
				}
			} else if (id == IDCANCEL) {
				if (!s->working) {
					s->finished = true;
					DestroyWindow(hwnd);
				}
			}
			return 0;
		}
		case WM_WORK_DONE: {
			set_working(s, false);
			MessageBoxW(hwnd, s->message.c_str(), s->success ? L"Entry added" : L"Could not add entry", MB_OK | (s->success ? MB_ICONINFORMATION : MB_ICONERROR));
			if (s->success) {
				s->added = true;
				s->finished = true;
				DestroyWindow(hwnd);
			} else {
				SetFocus(s->name);
			}
			return 0;
		}
		case WM_CLOSE: {
			if (s && !s->working) {
				s->finished = true;
				DestroyWindow(hwnd);
			}
			return 0;
		}
	}
	return DefWindowProcW(hwnd, msg, wp, lp);
}
}

bool show_add_image_dialog(HWND owner, HINSTANCE instance) {
	WNDCLASSEXW wc = {sizeof(wc)};
	wc.lpfnWndProc = dialog_proc;
	wc.hInstance = instance;
	wc.lpszClassName = L"BootSwapAddClass";
	wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);
	RegisterClassExW(&wc);
	dialog_state state;
	HWND hwnd = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT, L"BootSwapAddClass", L"Add ISO or VHD boot entry", WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT, 490, 360, owner, NULL, instance, &state);
	if (!hwnd) {
		return false;
	}
	EnableWindow(owner, FALSE);
	SetFocus(state.type);
	MSG msg;
	while (!state.finished && GetMessageW(&msg, NULL, 0, 0)) {
		if (!IsDialogMessageW(hwnd, &msg)) {
			TranslateMessage(&msg);
			DispatchMessageW(&msg);
		}
	}
	EnableWindow(owner, TRUE);
	SetForegroundWindow(owner);
	return state.added;
}

namespace {
struct prompt_state {
	std::wstring label;
	std::wstring text;
	HWND edit = NULL;
	bool accepted = false;
	bool finished = false;
};

LRESULT CALLBACK prompt_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
	prompt_state* s = reinterpret_cast<prompt_state*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
	switch (msg) {
		case WM_NCCREATE: {
			SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams));
			return DefWindowProcW(hwnd, msg, wp, lp);
		}
		case WM_CREATE: {
			s = reinterpret_cast<prompt_state*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
			make_label(hwnd, s->label.c_str(), 10, 10, 360);
			s->edit = make_control(hwnd, L"EDIT", s->text.c_str(), WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL, 10, 32, 360, 22, ID_NAME);
			make_control(hwnd, L"BUTTON", L"OK", WS_TABSTOP | BS_DEFPUSHBUTTON, 200, 68, 80, 28, IDOK);
			make_control(hwnd, L"BUTTON", L"Cancel", WS_TABSTOP | BS_PUSHBUTTON, 290, 68, 80, 28, IDCANCEL);
			return 0;
		}
		case WM_COMMAND: {
			if (LOWORD(wp) == IDOK) {
				s->text = get_text(s->edit);
				s->accepted = true;
				s->finished = true;
				DestroyWindow(hwnd);
			} else if (LOWORD(wp) == IDCANCEL) {
				s->finished = true;
				DestroyWindow(hwnd);
			}
			return 0;
		}
		case WM_CLOSE: {
			s->finished = true;
			DestroyWindow(hwnd);
			return 0;
		}
	}
	return DefWindowProcW(hwnd, msg, wp, lp);
}
}

bool prompt_text(HWND owner, HINSTANCE instance, const wchar_t* title, const wchar_t* label, std::wstring& text) {
	WNDCLASSEXW wc = {sizeof(wc)};
	wc.lpfnWndProc = prompt_proc;
	wc.hInstance = instance;
	wc.lpszClassName = L"BootSwapPromptClass";
	wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);
	RegisterClassExW(&wc);
	prompt_state state;
	state.label = label;
	state.text = text;
	HWND hwnd = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT, L"BootSwapPromptClass", title, WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT, 400, 150, owner, NULL, instance, &state);
	if (!hwnd) {
		return false;
	}
	EnableWindow(owner, FALSE);
	SetFocus(state.edit);
	SendMessageW(state.edit, EM_SETSEL, 0, -1);
	MSG msg;
	while (!state.finished && GetMessageW(&msg, NULL, 0, 0)) {
		if (!IsDialogMessageW(hwnd, &msg)) {
			TranslateMessage(&msg);
			DispatchMessageW(&msg);
		}
	}
	EnableWindow(owner, TRUE);
	SetForegroundWindow(owner);
	if (state.accepted) {
		text = state.text;
	}
	return state.accepted;
}
