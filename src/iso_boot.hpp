#pragma once
#include <string>
#include "bcd_edit.hpp"

enum class image_kind {
	windows_iso,
	other_iso,
	windows_vhd
};

struct image_request {
	image_kind kind = image_kind::windows_iso;
	std::wstring description;
	std::wstring image_path;
	std::wstring loader_path;
	bool copy_install_files = false;
};

class iso_boot {
public:
	static bool add_image_entry(const image_request& request, std::wstring& message);
	static void remove_entry_files(const boot_entry& entry);
};
