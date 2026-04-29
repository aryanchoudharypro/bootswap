#pragma once
#include <string>
#include <vector>

struct boot_entry {
	std::wstring guid;
	std::wstring description;
	std::wstring path;
};

class bcd_edit {
public:
	static std::vector<boot_entry> get_boot_entries();
	static bool set_boot_order(const std::vector<boot_entry>& entries);
	static bool set_boot_next(const std::wstring& guid);
	static bool delete_entry(const std::wstring& guid);
};
