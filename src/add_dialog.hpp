#pragma once
#include <windows.h>
#include <string>

// Shows a modal dialog for adding an ISO or VHD boot entry. Returns true if an entry was added.
bool show_add_image_dialog(HWND owner, HINSTANCE instance);

// Shows a modal single-line text prompt. Returns true if the user accepted; text is updated.
bool prompt_text(HWND owner, HINSTANCE instance, const wchar_t* title, const wchar_t* label, std::wstring& text);
