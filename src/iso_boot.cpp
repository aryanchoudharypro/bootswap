#include "iso_boot.hpp"
#include <windows.h>
#include <objbase.h>
#include <winioctl.h>
#include <virtdisk.h>
#pragma comment(lib, "virtdisk.lib")
#include <string>
#include <vector>
#include <cwctype>
#include <cwchar>

static const unsigned long BCD_TYPE_OSLOADER = 0x10200003;
static const unsigned long BCD_TYPE_BOOTSECTOR = 0x10200008;
static const unsigned long BCD_TYPE_FIRMWARE_APP = 0x101fffff;
static const unsigned long BCD_APP_DEVICE = 0x11000001;
static const unsigned long BCD_APP_PATH = 0x12000002;
static const unsigned long BCD_DESCRIPTION = 0x12000004;
static const unsigned long BCD_OS_DEVICE = 0x21000001;
static const unsigned long BCD_SYSTEM_ROOT = 0x22000002;
static const unsigned long BCD_DETECT_HAL = 0x26000010;
static const unsigned long BCD_WINPE = 0x26000022;
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
	return path.find_first_of(L"\"$`") != std::wstring::npos;
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

static std::wstring winload_for_setup() {
	return bcd_edit::is_uefi() ? L"\\windows\\system32\\boot\\winload.efi" : L"\\windows\\system32\\boot\\winload.exe";
}

static std::wstring winload_for_installed() {
	return bcd_edit::is_uefi() ? L"\\windows\\system32\\winload.efi" : L"\\windows\\system32\\winload.exe";
}

static const GUID ESP_TYPE_GUID = {0xC12A7328, 0xF81F, 0x11D2, {0xBA, 0x4B, 0x00, 0xA0, 0xC9, 0x3E, 0xC9, 0x3B}};

static std::wstring nt_path_of_drive(const std::wstring& drive) {
	wchar_t target[MAX_PATH * 2] = {};
	if (!QueryDosDeviceW(drive.c_str(), target, MAX_PATH * 2)) {
		return std::wstring();
	}
	return target;
}

struct esp_mount {
	std::wstring drive;
	std::wstring nt_path;
};

// Finds the EFI system partition and gives it a temporary drive letter.
static bool mount_esp(esp_mount& mount, std::wstring& message) {
	wchar_t volume[MAX_PATH] = {};
	HANDLE find = FindFirstVolumeW(volume, MAX_PATH);
	std::wstring esp_volume;
	if (find != INVALID_HANDLE_VALUE) {
		do {
			std::wstring device = volume;
			if (!device.empty() && device.back() == L'\\') {
				device.pop_back();
			}
			HANDLE h = CreateFileW(device.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
			if (h == INVALID_HANDLE_VALUE) {
				continue;
			}
			PARTITION_INFORMATION_EX info = {};
			DWORD returned = 0;
			bool is_esp = DeviceIoControl(h, IOCTL_DISK_GET_PARTITION_INFO_EX, nullptr, 0, &info, sizeof(info), &returned, nullptr) &&
				info.PartitionStyle == PARTITION_STYLE_GPT && IsEqualGUID(info.Gpt.PartitionType, ESP_TYPE_GUID);
			CloseHandle(h);
			if (is_esp) {
				esp_volume = volume;
				break;
			}
		} while (FindNextVolumeW(find, volume, MAX_PATH));
		FindClose(find);
	}
	if (esp_volume.empty()) {
		message = L"The EFI system partition could not be found.";
		return false;
	}
	DWORD used = GetLogicalDrives();
	for (wchar_t ch = L'Z'; ch >= L'D'; --ch) {
		if (used & (1u << (ch - L'A'))) {
			continue;
		}
		std::wstring drive(1, ch);
		drive += L':';
		if (SetVolumeMountPointW((drive + L"\\").c_str(), esp_volume.c_str())) {
			mount.drive = drive;
			mount.nt_path = nt_path_of_drive(drive);
			if (mount.nt_path.empty()) {
				DeleteVolumeMountPointW((drive + L"\\").c_str());
				break;
			}
			return true;
		}
		break;
	}
	message = L"Could not open the EFI system partition. Make sure a drive letter is free and you are running as Administrator.";
	return false;
}

static void unmount_esp(const esp_mount& mount) {
	DeleteVolumeMountPointW((mount.drive + L"\\").c_str());
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

// Opens an ISO read-only and finds the drive letter Windows gives it.
struct mounted_iso {
	HANDLE handle = INVALID_HANDLE_VALUE;
	std::wstring drive;
};

static bool device_number_of(const std::wstring& path, DWORD& type, DWORD& number) {
	HANDLE h = CreateFileW(path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
	if (h == INVALID_HANDLE_VALUE) {
		return false;
	}
	STORAGE_DEVICE_NUMBER sdn = {};
	DWORD returned = 0;
	bool ok = DeviceIoControl(h, IOCTL_STORAGE_GET_DEVICE_NUMBER, nullptr, 0, &sdn, sizeof(sdn), &returned, nullptr) != 0;
	CloseHandle(h);
	if (ok) {
		type = sdn.DeviceType;
		number = sdn.DeviceNumber;
	}
	return ok;
}

static bool mount_iso(const std::wstring& iso, mounted_iso& mount, std::wstring& message) {
	VIRTUAL_STORAGE_TYPE storage_type = {VIRTUAL_STORAGE_TYPE_DEVICE_ISO, VIRTUAL_STORAGE_TYPE_VENDOR_MICROSOFT};
	if (OpenVirtualDisk(&storage_type, iso.c_str(), VIRTUAL_DISK_ACCESS_ATTACH_RO | VIRTUAL_DISK_ACCESS_GET_INFO, OPEN_VIRTUAL_DISK_FLAG_NONE, nullptr, &mount.handle) != ERROR_SUCCESS) {
		message = L"Windows could not open the ISO file.";
		return false;
	}
	if (AttachVirtualDisk(mount.handle, nullptr, ATTACH_VIRTUAL_DISK_FLAG_READ_ONLY, 0, nullptr, nullptr) != ERROR_SUCCESS) {
		message = L"Windows could not attach the ISO file. It may already be open in another program.";
		return false;
	}
	wchar_t physical[MAX_PATH] = {};
	ULONG size = sizeof(physical);
	if (GetVirtualDiskPhysicalPath(mount.handle, &size, physical) != ERROR_SUCCESS) {
		message = L"Windows could not locate the opened ISO file.";
		return false;
	}
	DWORD iso_type = 0;
	DWORD iso_number = 0;
	if (!device_number_of(physical, iso_type, iso_number)) {
		message = L"Windows could not identify the opened ISO file.";
		return false;
	}
	for (int attempt = 0; attempt < 50; ++attempt) {
		DWORD drives = GetLogicalDrives();
		for (wchar_t ch = L'A'; ch <= L'Z'; ++ch) {
			if (!(drives & (1u << (ch - L'A')))) {
				continue;
			}
			std::wstring drive(1, ch);
			drive += L':';
			if (GetDriveTypeW((drive + L"\\").c_str()) != DRIVE_CDROM) {
				continue;
			}
			DWORD type = 0;
			DWORD number = 0;
			if (device_number_of(L"\\\\.\\" + drive, type, number) && type == iso_type && number == iso_number) {
				mount.drive = drive;
				return true;
			}
		}
		Sleep(200);
	}
	message = L"The ISO file was opened but no drive letter appeared for it.";
	return false;
}

static void unmount_iso(mounted_iso& mount) {
	if (mount.handle != INVALID_HANDLE_VALUE) {
		CloseHandle(mount.handle);
		mount.handle = INVALID_HANDLE_VALUE;
	}
}

static bool add_windows_iso(const image_request& req, std::wstring& message) {
	const std::wstring iso = req.image_path;
	mounted_iso mount;
	if (!mount_iso(iso, mount, message)) {
		unmount_iso(mount);
		return false;
	}
	const std::wstring source = mount.drive;
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
		const std::wstring volume_nt = nt_path_of_drive(drive);
		if (volume_nt.empty()) {
			message = L"Could not identify the drive " + drive;
			break;
		}
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
		if (!bcd_edit::create_object(options_guid, 0x30000000, message)) {
			options_guid.clear();
			break;
		}
		if (!bcd_edit::set_string_element(options_guid, BCD_DESCRIPTION, L"Ramdisk options for " + req.description, message) ||
			!bcd_edit::set_partition_device(options_guid, 0x31000003, volume_nt, message) ||
			!bcd_edit::set_string_element(options_guid, 0x32000004, dir_rel + L"\\boot.sdi", message)) {
			break;
		}
		entry_guid = new_guid();
		if (!bcd_edit::create_object(entry_guid, BCD_TYPE_OSLOADER, message)) {
			entry_guid.clear();
			break;
		}
		const std::wstring wim_rel = dir_rel + L"\\boot.wim";
		if (!bcd_edit::set_string_element(entry_guid, BCD_DESCRIPTION, req.description, message) ||
			!bcd_edit::set_ramdisk_device(entry_guid, BCD_APP_DEVICE, options_guid, wim_rel, volume_nt, message) ||
			!bcd_edit::set_ramdisk_device(entry_guid, BCD_OS_DEVICE, options_guid, wim_rel, volume_nt, message) ||
			!bcd_edit::set_string_element(entry_guid, BCD_APP_PATH, winload_for_setup(), message) ||
			!bcd_edit::set_string_element(entry_guid, BCD_SYSTEM_ROOT, L"\\windows", message) ||
			!bcd_edit::set_boolean_element(entry_guid, BCD_WINPE, true, message) ||
			!bcd_edit::set_boolean_element(entry_guid, BCD_DETECT_HAL, true, message) ||
			!bcd_edit::add_to_list(boot_list::menu, entry_guid, message)) {
			break;
		}
		ok = true;
	} while (false);
	unmount_iso(mount);
	if (!ok) {
		if (!entry_guid.empty()) {
			bcd_edit::delete_entry(entry_guid);
		}
		if (!options_guid.empty()) {
			bcd_edit::delete_entry(options_guid);
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
	const std::wstring volume_nt = nt_path_of_drive(drive_of(req.image_path));
	if (volume_nt.empty()) {
		message = L"Could not identify the drive of the virtual disk.";
		return false;
	}
	const std::wstring rel = after_drive(req.image_path);
	std::wstring guid = new_guid();
	if (!bcd_edit::create_object(guid, BCD_TYPE_OSLOADER, message)) {
		return false;
	}
	if (!bcd_edit::set_string_element(guid, BCD_DESCRIPTION, req.description, message) ||
		!bcd_edit::set_vhd_device(guid, BCD_APP_DEVICE, rel, volume_nt, message) ||
		!bcd_edit::set_vhd_device(guid, BCD_OS_DEVICE, rel, volume_nt, message) ||
		!bcd_edit::set_string_element(guid, BCD_APP_PATH, winload_for_installed(), message) ||
		!bcd_edit::set_string_element(guid, BCD_SYSTEM_ROOT, L"\\windows", message) ||
		!bcd_edit::set_boolean_element(guid, BCD_DETECT_HAL, true, message) ||
		!bcd_edit::add_to_list(boot_list::menu, guid, message)) {
		bcd_edit::delete_entry(guid);
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
	esp_mount esp;
	if (!mount_esp(esp, message)) {
		return false;
	}
	std::wstring id = strip_braces(new_guid());
	std::wstring rel_dir = std::wstring(ESP_ROOT_DIR) + id;
	std::wstring dir = esp.drive + rel_dir;
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
		entry_guid = new_guid();
		if (!bcd_edit::create_object(entry_guid, BCD_TYPE_FIRMWARE_APP, message)) {
			entry_guid.clear();
			break;
		}
		if (!bcd_edit::set_string_element(entry_guid, BCD_DESCRIPTION, req.description, message) ||
			!bcd_edit::set_partition_device(entry_guid, BCD_APP_DEVICE, esp.nt_path, message) ||
			!bcd_edit::set_string_element(entry_guid, BCD_APP_PATH, rel_dir + L"\\" + loader_name, message) ||
			!bcd_edit::add_to_list(boot_list::firmware, entry_guid, message)) {
			break;
		}
		ok = true;
	} while (false);
	if (!ok) {
		if (!entry_guid.empty()) {
			bcd_edit::delete_entry(entry_guid);
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
	const std::wstring volume_nt = nt_path_of_drive(drive);
	if (volume_nt.empty()) {
		message = L"Could not identify the drive " + drive;
		return false;
	}
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
	if (!bcd_edit::create_object(guid, BCD_TYPE_BOOTSECTOR, message)) {
		return false;
	}
	if (!bcd_edit::set_string_element(guid, BCD_DESCRIPTION, L"GRUB4DOS ISO menu", message) ||
		!bcd_edit::set_partition_device(guid, BCD_APP_DEVICE, volume_nt, message) ||
		!bcd_edit::set_string_element(guid, BCD_APP_PATH, L"\\grldr.mbr", message) ||
		!bcd_edit::add_to_list(boot_list::menu, guid, message)) {
		bcd_edit::delete_entry(guid);
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
		message = L"The image must be on a local drive with a drive letter, and its path must not contain double quote marks, dollar signs or backticks.";
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
	esp_mount esp;
	if (!mount_esp(esp, ignored)) {
		return;
	}
	remove_dir_tree(esp.drive + rel_dir);
	unmount_esp(esp);
}
