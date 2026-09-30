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
	static bool run_bcdedit(const std::vector<std::wstring>& args, std::wstring& output);
	static bool run_program(const std::wstring& command_line, std::wstring& output);
	static bool rename_entry(const std::wstring& guid, const std::wstring& name, std::wstring& output);
	static bool set_default(const std::wstring& guid, std::wstring& output);
	static bool set_timeout(int seconds, std::wstring& output);
	static bool export_store(const std::wstring& file, std::wstring& output);
	static bool import_store(const std::wstring& file, std::wstring& output);
};
