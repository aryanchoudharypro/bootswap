#include "iso_boot.hpp"
#include <windows.h>
#include <objbase.h>
#include <string>
#include <vector>
#include <cwctype>
#include <cwchar>

static const wchar_t* FWBOOTMGR = L"{fwbootmgr}";
static const wchar_t* BOOTMGR = L"{bootmgr}";
static const wchar_t* ESP_ROOT_DIR = L"\\EFI\\Bootswap\\";

static std::wstring new_guid() {
	GUID guid;
	CoCreateGuid(&guid);
	wchar_t buffer[64] = {};
	StringFromGUID2(guid, buffer, 64);
	return buffer;
}

static std::wstring strip_braces(const std::wstring& guid) {
	if (guid.size() > 2 && guid.front() == L'{') {
		return guid.substr(1, guid.size() - 2);
	}
	return guid;
}

static bool file_exists(const std::wstring& path) {
	return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

static bool has_drive_letter(const std::wstring& path) {
	return path.size() > 3 && iswalpha(path[0]) && path[1] == L':' && path[2] == L'\\';
}

static bool has_unsafe_chars(const std::wstring& path) {
	return path.find_first_of(L"\"'$`%^&|<>") != std::wstring::npos;
}

static bool is_ascii(const std::wstring& text) {
	for (wchar_t ch : text) {
		if (ch > 127) {
			return false;
		}
	}
	return true;
}

static std::wstring drive_of(const std::wstring& path) {
	return path.substr(0, 2);
}

static std::wstring after_drive(const std::wstring& path) {
	return path.substr(2);
}

static std::wstring to_forward(std::wstring text) {
	for (auto& ch : text) {
		if (ch == L'\\') {
			ch = L'/';
		}
	}
	return text;
}

static std::string to_utf8(const std::wstring& text) {
	if (text.empty()) {
		return std::string();
	}
	int len = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
	std::string out(len, '\0');
	WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), &out[0], len, nullptr, nullptr);
	return out;
}

static bool make_dirs(const std::wstring& path) {
	for (size_t i = 3; i < path.size(); ++i) {
		if (path[i] == L'\\') {
			CreateDirectoryW(path.substr(0, i).c_str(), nullptr);
		}
	}
	CreateDirectoryW(path.c_str(), nullptr);
	DWORD attrs = GetFileAttributesW(path.c_str());
	return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY);
}

static bool write_file(const std::wstring& path, const std::string& data, bool append) {
	HANDLE file = CreateFileW(path.c_str(), append ? FILE_APPEND_DATA : GENERIC_WRITE, 0, nullptr, append ? OPEN_ALWAYS : CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (file == INVALID_HANDLE_VALUE) {
		return false;
	}
	DWORD written = 0;
	BOOL ok = WriteFile(file, data.data(), static_cast<DWORD>(data.size()), &written, nullptr);
	CloseHandle(file);
	return ok && written == data.size();
}

static bool bcd(const std::vector<std::wstring>& args, std::wstring& message) {
	std::wstring output;
	if (bcd_edit::run_bcdedit(args, output)) {
		return true;
	}
	message = L"The boot configuration tool reported an error: " + output;
	return false;
}

static void bcd_delete(const std::wstring& guid) {
	std::wstring ignored;
	bcd_edit::run_bcdedit({L"/delete", guid, L"/cleanup"}, ignored);
}

static std::wstring find_guid(const std::wstring& text) {
	size_t open = text.find(L'{');
	size_t close = text.find(L'}', open == std::wstring::npos ? 0 : open);
	if (open == std::wstring::npos || close == std::wstring::npos) {
		return std::wstring();
	}
	return text.substr(open, close - open + 1);
}

static std::wstring winload_for_setup() {
	return bcd_edit::is_uefi() ? L"\\windows\\system32\\boot\\winload.efi" : L"\\windows\\system32\\boot\\winload.exe";
}

static std::wstring winload_for_installed() {
	return bcd_edit::is_uefi() ? L"\\windows\\system32\\winload.efi" : L"\\windows\\system32\\winload.exe";
}

// Mounting the EFI system partition on a free drive letter.
static std::wstring mount_esp(std::wstring& message) {
	DWORD used = GetLogicalDrives();
	wchar_t letter = 0;
	for (wchar_t ch = L'Z'; ch >= L'D'; --ch) {
		if (!(used & (1u << (ch - L'A')))) {
			letter = ch;
			break;
		}
	}
	if (!letter) {
		message = L"No free drive letter is available to open the EFI system partition.";
		return std::wstring();
	}
	std::wstring drive(1, letter);
	drive += L':';
	wchar_t sys_dir[MAX_PATH] = {};
	GetSystemDirectoryW(sys_dir, MAX_PATH);
	std::wstring output;
	std::wstring cmd = L"\"" + std::wstring(sys_dir) + L"\\mountvol.exe\" " + drive + L" /S";
	if (!bcd_edit::run_program(cmd, output)) {
		message = L"Could not open the EFI system partition: " + output;
		return std::wstring();
	}
	return drive;
}

static void unmount_esp(const std::wstring& drive) {
	wchar_t sys_dir[MAX_PATH] = {};
	GetSystemDirectoryW(sys_dir, MAX_PATH);
	std::wstring output;
	bcd_edit::run_program(L"\"" + std::wstring(sys_dir) + L"\\mountvol.exe\" " + drive + L" /D", output);
}

static void remove_dir_tree(const std::wstring& dir) {
	WIN32_FIND_DATAW data;
	HANDLE find = FindFirstFileW((dir + L"\\*").c_str(), &data);
	if (find != INVALID_HANDLE_VALUE) {
		do {
			std::wstring name = data.cFileName;
			if (name == L"." || name == L"..") {
				continue;
			}
			std::wstring full = dir + L"\\" + name;
			if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
				remove_dir_tree(full);
			} else {
				SetFileAttributesW(full.c_str(), FILE_ATTRIBUTE_NORMAL);
				DeleteFileW(full.c_str());
			}
		} while (FindNextFileW(find, &data));
		FindClose(find);
	}
	RemoveDirectoryW(dir.c_str());
}

static std::wstring powershell_quote(const std::wstring& text) {
	return L"'" + text + L"'";
}

static bool run_powershell(const std::wstring& script, std::wstring& output) {
	wchar_t sys_dir[MAX_PATH] = {};
	GetSystemDirectoryW(sys_dir, MAX_PATH);
	std::wstring cmd = L"\"" + std::wstring(sys_dir) + L"\\WindowsPowerShell\\v1.0\\powershell.exe\" -NoProfile -NonInteractive -Command \"" + script + L"\"";
	return bcd_edit::run_program(cmd, output);
}

static std::wstring trim(std::wstring text) {
	while (!text.empty() && iswspace(text.back())) {
		text.pop_back();
	}
	size_t start = 0;
	while (start < text.size() && iswspace(text[start])) {
		++start;
	}
	return text.substr(start);
}

static bool add_windows_iso(const image_request& req, std::wstring& message) {
	const std::wstring iso = req.image_path;
	std::wstring output;
	bool was_attached = false;
	if (run_powershell(L"(Get-DiskImage -ImagePath " + powershell_quote(iso) + L").Attached", output)) {
		was_attached = trim(output) == L"True";
	}
	if (!run_powershell(L"(Mount-DiskImage -ImagePath " + powershell_quote(iso) + L" -PassThru | Get-Volume).DriveLetter", output)) {
		message = L"Windows could not open the ISO file: " + output;
		return false;
	}
	std::wstring letter = trim(output);
	if (letter.size() != 1) {
		if (!was_attached) {
			run_powershell(L"Dismount-DiskImage -ImagePath " + powershell_quote(iso), output);
		}
		message = L"Windows opened the ISO file but did not report a drive letter.";
		return false;
	}
	const std::wstring source = letter + L":";
	const std::wstring wim = source + L"\\sources\\boot.wim";
	const std::wstring sdi = source + L"\\boot\\boot.sdi";
	bool ok = false;
	std::wstring id = new_guid();
	std::wstring options_guid;
	std::wstring entry_guid;
	do {
		if (!file_exists(wim) || !file_exists(sdi)) {
			message = L"This ISO does not contain sources\\boot.wim and boot\\boot.sdi, so it is not a Windows setup or recovery image. Choose the other ISO type instead.";
			break;
		}
		const std::wstring drive = drive_of(iso);
		const std::wstring dir_rel = L"\\Bootswap\\" + strip_braces(id);
		const std::wstring dir = drive + dir_rel;
		if (!make_dirs(dir)) {
			message = L"Could not create the folder " + dir;
			break;
		}
		if (!CopyFileW(wim.c_str(), (dir + L"\\boot.wim").c_str(), FALSE)) {
			message = L"Could not copy boot.wim to " + dir;
			break;
		}
		if (!CopyFileW(sdi.c_str(), (dir + L"\\boot.sdi").c_str(), FALSE)) {
			message = L"Could not copy boot.sdi to " + dir;
			break;
		}
		if (req.copy_install_files) {
			std::wstring sources_dir = drive + L"\\sources";
			make_dirs(sources_dir);
			const wchar_t* names[] = {L"install.wim", L"install.esd", L"install.swm"};
			bool copied = false;
			for (const wchar_t* name : names) {
				std::wstring from = source + L"\\sources\\" + name;
				if (file_exists(from)) {
					std::wstring to = sources_dir + L"\\" + name;
					if (!file_exists(to) && !CopyFileW(from.c_str(), to.c_str(), TRUE)) {
						message = L"Could not copy " + std::wstring(name) + L" to " + sources_dir;
						copied = false;
						break;
					}
					copied = true;
				}
			}
			if (!copied) {
				if (message.empty()) {
					message = L"The ISO has no install.wim or install.esd to copy.";
				}
				break;
			}
		}
		options_guid = new_guid();
		const std::wstring ram_options = L"ramdisk=[" + drive + L"]" + dir_rel + L"\\boot.wim," + options_guid;
		if (!bcd({L"/create", options_guid, L"/d", L"Ramdisk options for " + req.description, L"/device"}, message)) {
			options_guid.clear();
			break;
		}
		if (!bcd({L"/set", options_guid, L"ramdisksdidevice", L"partition=" + drive}, message) ||
			!bcd({L"/set", options_guid, L"ramdisksdipath", dir_rel + L"\\boot.sdi"}, message)) {
			break;
		}
		entry_guid = new_guid();
		if (!bcd({L"/create", entry_guid, L"/d", req.description, L"/application", L"osloader"}, message)) {
			entry_guid.clear();
			break;
		}
		if (!bcd({L"/set", entry_guid, L"device", ram_options}, message) ||
			!bcd({L"/set", entry_guid, L"osdevice", ram_options}, message) ||
			!bcd({L"/set", entry_guid, L"path", winload_for_setup()}, message) ||
			!bcd({L"/set", entry_guid, L"systemroot", L"\\windows"}, message) ||
			!bcd({L"/set", entry_guid, L"winpe", L"yes"}, message) ||
			!bcd({L"/set", entry_guid, L"detecthal", L"yes"}, message) ||
			!bcd({L"/displayorder", entry_guid, L"/addlast"}, message)) {
			break;
		}
		ok = true;
	} while (false);
	if (!was_attached) {
		std::wstring ignored;
		run_powershell(L"Dismount-DiskImage -ImagePath " + powershell_quote(iso), ignored);
	}
	if (!ok) {
		if (!entry_guid.empty()) {
			bcd_delete(entry_guid);
		}
		if (!options_guid.empty()) {
			bcd_delete(options_guid);
		}
		return false;
	}
	message = L"Added \"" + req.description + L"\" to the Windows boot menu list.";
	if (!req.copy_install_files) {
		message += L" Windows Setup will not find install files unless the ISO contents are available on a drive.";
	}
	return true;
}

static bool add_windows_vhd(const image_request& req, std::wstring& message) {
	const std::wstring drive = drive_of(req.image_path);
	const std::wstring device = L"vhd=[" + drive + L"]" + after_drive(req.image_path);
	std::wstring guid = new_guid();
	if (!bcd({L"/create", guid, L"/d", req.description, L"/application", L"osloader"}, message)) {
		return false;
	}
	if (!bcd({L"/set", guid, L"device", device}, message) ||
		!bcd({L"/set", guid, L"osdevice", device}, message) ||
		!bcd({L"/set", guid, L"path", winload_for_installed()}, message) ||
		!bcd({L"/set", guid, L"systemroot", L"\\windows"}, message) ||
		!bcd({L"/set", guid, L"detecthal", L"yes"}, message) ||
		!bcd({L"/displayorder", guid, L"/addlast"}, message)) {
		bcd_delete(guid);
		return false;
	}
	message = L"Added \"" + req.description + L"\" to the Windows boot menu list. The virtual disk must contain an installed copy of Windows.";
	return true;
}

static std::string grub2_config(const std::wstring& iso_path) {
	const std::string iso = to_utf8(to_forward(after_drive(iso_path)));
	std::string cfg;
	cfg += "insmod part_gpt\ninsmod part_msdos\ninsmod fat\ninsmod ntfs\ninsmod exfat\ninsmod ext2\ninsmod iso9660\ninsmod loopback\ninsmod search\n";
	cfg += "set isofile=\"" + iso + "\"\n";
	cfg += "search --no-floppy --file --set=isodev \"${isofile}\"\n";
	cfg += "if [ -z \"${isodev}\" ]; then\n  echo \"Could not find the ISO file ${isofile}.\"\n  sleep 15\n  exit\nfi\n";
	cfg += "loopback loop ($isodev)${isofile}\n";
	cfg += "if [ -f (loop)/boot/grub/loopback.cfg ]; then\n  set iso_path=\"${isofile}\"\n  export iso_path\n  set root=(loop)\n  configfile /boot/grub/loopback.cfg\nfi\n";
	cfg += "if [ -f (loop)/EFI/BOOT/BOOTX64.EFI ]; then\n  chainloader (loop)/EFI/BOOT/BOOTX64.EFI\n  boot\nfi\n";
	cfg += "echo \"This ISO could not be started automatically.\"\nsleep 15\n";
	return cfg;
}

static bool add_other_iso_uefi(const image_request& req, std::wstring& message) {
	const size_t slash = req.loader_path.find_last_of(L'\\');
	const std::wstring loader_name = req.loader_path.substr(slash + 1);
	std::wstring esp = mount_esp(message);
	if (esp.empty()) {
		return false;
	}
	std::wstring id = strip_braces(new_guid());
	std::wstring rel_dir = std::wstring(ESP_ROOT_DIR) + id;
	std::wstring dir = esp + rel_dir;
	std::wstring entry_guid;
	bool ok = false;
	do {
		if (!make_dirs(dir)) {
			message = L"Could not create the folder " + rel_dir + L" on the EFI system partition.";
			break;
		}
		if (!CopyFileW(req.loader_path.c_str(), (dir + L"\\" + loader_name).c_str(), FALSE)) {
			message = L"Could not copy the GRUB image to the EFI system partition.";
			break;
		}
		if (!write_file(dir + L"\\grub.cfg", grub2_config(req.image_path), false)) {
			message = L"Could not write grub.cfg on the EFI system partition.";
			break;
		}
		std::wstring output;
		if (!bcd_edit::run_bcdedit({L"/copy", BOOTMGR, L"/d", req.description}, output)) {
			message = L"The boot configuration tool reported an error: " + output;
			break;
		}
		entry_guid = find_guid(output);
		if (entry_guid.empty()) {
			message = L"The boot entry was created but its identifier could not be read.";
			break;
		}
		if (!bcd({L"/set", entry_guid, L"path", rel_dir + L"\\" + loader_name}, message) ||
			!bcd({L"/set", FWBOOTMGR, L"displayorder", entry_guid, L"/addlast"}, message)) {
			break;
		}
		ok = true;
	} while (false);
	if (!ok) {
		if (!entry_guid.empty()) {
			bcd_delete(entry_guid);
		}
		remove_dir_tree(dir);
	}
	unmount_esp(esp);
	if (ok) {
		message = L"Added \"" + req.description + L"\" to the firmware boot list.";
	}
	return ok;
}

static std::string grub4dos_entry(const image_request& req) {
	std::wstring iso = to_forward(after_drive(req.image_path));
	std::wstring escaped;
	for (wchar_t ch : iso) {
		if (ch == L' ') {
			escaped += L'\\';
		}
		escaped += ch;
	}
	std::string p = to_utf8(escaped);
	std::string entry = "\r\ntitle " + to_utf8(req.description) + "\r\n";
	entry += "find --set-root --ignore-floppies " + p + "\r\n";
	entry += "map " + p + " (0xff) || map --mem " + p + " (0xff)\r\n";
	entry += "map --hook\r\n";
	entry += "chainloader (0xff)\r\n";
	return entry;
}

static bool add_other_iso_bios(const image_request& req, std::wstring& message) {
	if (!is_ascii(req.image_path) || !is_ascii(req.description)) {
		message = L"For legacy BIOS booting, the ISO path and description must use only plain English letters and symbols.";
		return false;
	}
	const size_t slash = req.loader_path.find_last_of(L'\\');
	const std::wstring loader_dir = req.loader_path.substr(0, slash);
	const std::wstring mbr = loader_dir + L"\\grldr.mbr";
	if (!file_exists(mbr)) {
		message = L"grldr.mbr was not found in the same folder as the GRUB4DOS loader file.";
		return false;
	}
	const std::wstring drive = drive_of(req.image_path);
	if (!file_exists(drive + L"\\grldr") && !CopyFileW(req.loader_path.c_str(), (drive + L"\\grldr").c_str(), TRUE)) {
		message = L"Could not copy grldr to " + drive + L"\\";
		return false;
	}
	if (!file_exists(drive + L"\\grldr.mbr") && !CopyFileW(mbr.c_str(), (drive + L"\\grldr.mbr").c_str(), TRUE)) {
		message = L"Could not copy grldr.mbr to " + drive + L"\\";
		return false;
	}
	if (!write_file(drive + L"\\menu.lst", grub4dos_entry(req), true)) {
		message = L"Could not update menu.lst on " + drive + L"\\";
		return false;
	}
	for (const auto& entry : bcd_edit::get_boot_entries(boot_list::menu)) {
		if (_wcsicmp(entry.path.c_str(), L"\\grldr.mbr") == 0) {
			message = L"Added \"" + req.description + L"\" to the existing GRUB4DOS menu. Choose that entry in the boot menu list to reach it.";
			return true;
		}
	}
	std::wstring guid = new_guid();
	if (!bcd({L"/create", guid, L"/d", L"GRUB4DOS ISO menu", L"/application", L"bootsector"}, message)) {
		return false;
	}
	if (!bcd({L"/set", guid, L"device", L"partition=" + drive}, message) ||
		!bcd({L"/set", guid, L"path", L"\\grldr.mbr"}, message) ||
		!bcd({L"/displayorder", guid, L"/addlast"}, message)) {
		bcd_delete(guid);
		return false;
	}
	message = L"Added \"GRUB4DOS ISO menu\" to the Windows boot menu list. It shows \"" + req.description + L"\" and any other ISO you add this way.";
	return true;
}

bool iso_boot::add_image_entry(const image_request& req, std::wstring& message) {
	message.clear();
	if (req.description.empty()) {
		message = L"Enter a name for the boot entry.";
		return false;
	}
	if (!has_drive_letter(req.image_path) || has_unsafe_chars(req.image_path)) {
		message = L"The image must be on a local drive with a drive letter, and its path must not contain any of these characters: quote marks, $, %, ^, &, |, <, >.";
		return false;
	}
	if (!file_exists(req.image_path)) {
		message = L"The image file does not exist.";
		return false;
	}
	if (req.kind == image_kind::windows_iso) {
		return add_windows_iso(req, message);
	}
	if (req.kind == image_kind::windows_vhd) {
		return add_windows_vhd(req, message);
	}
	if (req.loader_path.empty() || !file_exists(req.loader_path) || req.loader_path.find(L'\\') == std::wstring::npos) {
		message = bcd_edit::is_uefi() ? L"Choose the GRUB2 EFI file to use as the loader." : L"Choose the GRUB4DOS grldr file to use as the loader.";
		return false;
	}
	return bcd_edit::is_uefi() ? add_other_iso_uefi(req, message) : add_other_iso_bios(req, message);
}

void iso_boot::remove_entry_files(const boot_entry& entry) {
	const std::wstring prefix = ESP_ROOT_DIR;
	if (entry.path.size() <= prefix.size() || _wcsnicmp(entry.path.c_str(), prefix.c_str(), prefix.size()) != 0) {
		return;
	}
	size_t end = entry.path.find(L'\\', prefix.size());
	if (end == std::wstring::npos) {
		return;
	}
	std::wstring rel_dir = entry.path.substr(0, end);
	std::wstring ignored;
	std::wstring esp = mount_esp(ignored);
	if (esp.empty()) {
		return;
	}
	remove_dir_tree(esp + rel_dir);
	unmount_esp(esp);
}
