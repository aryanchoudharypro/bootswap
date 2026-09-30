#pragma once
#include <string>
#include <vector>

struct boot_entry {
	std::wstring guid;
	std::wstring description;
	std::wstring path;
};

enum class boot_list {
	firmware,
	menu
};

class bcd_edit {
public:
	static bool is_uefi();
	static std::vector<boot_entry> get_boot_entries(boot_list list);
	static bool set_boot_order(boot_list list, const std::vector<boot_entry>& entries);
	static bool set_boot_next(boot_list list, const std::wstring& guid);
	static bool delete_entry(const std::wstring& guid);
	static bool create_object(const std::wstring& guid, unsigned long type, std::wstring& error);
	static bool set_string_element(const std::wstring& guid, unsigned long type, const std::wstring& value, std::wstring& error);
	static bool set_boolean_element(const std::wstring& guid, unsigned long type, bool value, std::wstring& error);
	static bool set_integer_element(const std::wstring& guid, unsigned long type, long value, std::wstring& error);
	static bool set_object_element(const std::wstring& guid, unsigned long type, const std::wstring& target, std::wstring& error);
	static bool set_partition_device(const std::wstring& guid, unsigned long type, const std::wstring& nt_path, std::wstring& error);
	static bool set_ramdisk_device(const std::wstring& guid, unsigned long type, const std::wstring& options_guid, const std::wstring& file_path, const std::wstring& parent_nt_path, std::wstring& error);
	static bool set_vhd_device(const std::wstring& guid, unsigned long type, const std::wstring& file_path, const std::wstring& parent_nt_path, std::wstring& error);
	static bool add_to_list(boot_list list, const std::wstring& guid, std::wstring& error);
	static bool rename_entry(const std::wstring& guid, const std::wstring& name, std::wstring& error);
	static bool set_default(const std::wstring& guid, std::wstring& error);
	static bool set_timeout(int seconds, std::wstring& error);
	static bool export_store(const std::wstring& file, std::wstring& error);
	static bool import_store(const std::wstring& file, std::wstring& error);
};
