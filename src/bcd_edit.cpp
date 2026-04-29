#include "bcd_edit.hpp"
#include <windows.h>
#include <wbemidl.h>
#include <wrl/client.h>
#include <comdef.h>
#pragma comment(lib, "wbemuuid.lib")
#pragma comment(lib, "comsuppw.lib")
using Microsoft::WRL::ComPtr;
static const ULONG BCD_FW_DISPLAYORDER = 0x24000001;
static const ULONG BCD_FW_BOOTSEQUENCE = 0x24000002;
static const ULONG BCD_APPLICATION_PATH = 0x12000002;
static const ULONG BCD_DESCRIPTION = 0x12000004;

static ComPtr<IWbemServices> wmi_connect() {
	ComPtr<IWbemLocator> locator;
	if (FAILED(CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&locator)))) {
		return nullptr;
	}
	ComPtr<IWbemServices> services;
	if (FAILED(locator->ConnectServer(_bstr_t(L"ROOT\\WMI"), nullptr, nullptr, nullptr, 0, nullptr, nullptr, &services))) {
		return nullptr;
	}
	CoSetProxyBlanket(services.Get(), RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr, RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE);
	return services;
}

static ComPtr<IWbemClassObject> get_bcd_element(IWbemServices* services, const std::wstring& obj_path, ULONG element_type) {
	ComPtr<IWbemClassObject> bcd_object_class;
	if (FAILED(services->GetObject(_bstr_t(L"BcdObject"), 0, nullptr, &bcd_object_class, nullptr))) {
		return nullptr;
	}
	ComPtr<IWbemClassObject> in_def;
	if (FAILED(bcd_object_class->GetMethod(L"GetElement", 0, &in_def, nullptr))) {
		return nullptr;
	}
	ComPtr<IWbemClassObject> in_params;
	if (FAILED(in_def->SpawnInstance(0, &in_params))) {
		return nullptr;
	}
	_variant_t type_var(static_cast<long>(element_type));
	if (FAILED(in_params->Put(L"Type", 0, &type_var, 0))) {
		return nullptr;
	}
	ComPtr<IWbemClassObject> out_params;
	if (FAILED(services->ExecMethod(_bstr_t(obj_path.c_str()), _bstr_t(L"GetElement"), 0, nullptr, in_params.Get(), &out_params, nullptr))) {
		return nullptr;
	}
	_variant_t elem_var;
	if (FAILED(out_params->Get(L"Element", 0, &elem_var, nullptr, nullptr))) {
		return nullptr;
	}
	if (elem_var.vt != VT_UNKNOWN || !elem_var.punkVal) {
		return nullptr;
	}
	ComPtr<IWbemClassObject> element;
	if (FAILED(elem_var.punkVal->QueryInterface(IID_PPV_ARGS(&element)))) {
		return nullptr;
	}
	return element;
}

static bool set_bcd_object_list_element(IWbemServices* services, const std::wstring& obj_path, ULONG element_type, const std::vector<std::wstring>& ids) {
	ComPtr<IWbemClassObject> bcd_object_class;
	if (FAILED(services->GetObject(_bstr_t(L"BcdObject"), 0, nullptr, &bcd_object_class, nullptr))) {
		return false;
	}
	ComPtr<IWbemClassObject> in_def;
	if (FAILED(bcd_object_class->GetMethod(L"SetObjectListElement", 0, &in_def, nullptr))) {
		return false;
	}
	ComPtr<IWbemClassObject> in_params;
	if (FAILED(in_def->SpawnInstance(0, &in_params))) {
		return false;
	}
	_variant_t type_var(static_cast<long>(element_type));
	if (FAILED(in_params->Put(L"Type", 0, &type_var, 0))) {
		return false;
	}
	SAFEARRAY* sa = SafeArrayCreateVector(VT_BSTR, 0, static_cast<ULONG>(ids.size()));
	if (!sa) {
		return false;
	}
	for (size_t i = 0; i < ids.size(); ++i) {
		long index = static_cast<long>(i);
		BSTR bstr = SysAllocString(ids[i].c_str());
		SafeArrayPutElement(sa, &index, bstr);
		SysFreeString(bstr);
	}
	_variant_t ids_var;
	ids_var.vt = VT_ARRAY | VT_BSTR;
	ids_var.parray = sa;
	if (FAILED(in_params->Put(L"Ids", 0, &ids_var, 0))) {
		return false;
	}
	ComPtr<IWbemClassObject> out_params;
	HRESULT hr = services->ExecMethod(_bstr_t(obj_path.c_str()), _bstr_t(L"SetObjectListElement"), 0, nullptr, in_params.Get(), &out_params, nullptr);
	if (SUCCEEDED(hr) && out_params) {
		_variant_t ret_val;
		if (SUCCEEDED(out_params->Get(L"ReturnValue", 0, &ret_val, nullptr, nullptr))) {
			if (ret_val.vt == VT_BOOL) {
				return ret_val.boolVal != VARIANT_FALSE;
			}
		}
		return true;
	}
	return false;
}

static bool delete_bcd_object(IWbemServices* services, const std::wstring& guid) {
	ComPtr<IWbemClassObject> bcd_store_class;
	if (FAILED(services->GetObject(_bstr_t(L"BcdStore"), 0, nullptr, &bcd_store_class, nullptr))) {
		return false;
	}
	ComPtr<IWbemClassObject> in_def;
	if (FAILED(bcd_store_class->GetMethod(L"DeleteObject", 0, &in_def, nullptr))) {
		return false;
	}
	ComPtr<IWbemClassObject> in_params;
	if (FAILED(in_def->SpawnInstance(0, &in_params))) {
		return false;
	}
	_variant_t id_var(guid.c_str());
	if (FAILED(in_params->Put(L"Id", 0, &id_var, 0))) {
		return false;
	}
	std::wstring store_path = L"BcdStore.FilePath=\"\"";
	ComPtr<IWbemClassObject> out_params;
	HRESULT hr = services->ExecMethod(_bstr_t(store_path.c_str()), _bstr_t(L"DeleteObject"), 0, nullptr, in_params.Get(), &out_params, nullptr);
	if (SUCCEEDED(hr) && out_params) {
		_variant_t ret_val;
		if (SUCCEEDED(out_params->Get(L"ReturnValue", 0, &ret_val, nullptr, nullptr))) {
			if (ret_val.vt == VT_BOOL) {
				return ret_val.boolVal != VARIANT_FALSE;
			}
		}
		return true;
	}
	return false;
}

std::vector<boot_entry> bcd_edit::get_boot_entries() {
	std::vector<boot_entry> entries;
	auto services = wmi_connect();
	if (!services) {
		return entries;
	}
	const std::wstring fwbootmgr_path = L"BcdObject.Id=\"{a5a30fa2-3d06-4e9f-b5f4-a01df9d1fcba}\",StoreFilePath=\"\"";
	auto order_elem = get_bcd_element(services.Get(), fwbootmgr_path, BCD_FW_DISPLAYORDER);
	if (!order_elem) {
		return entries;
	}
	_variant_t ids_var;
	if (FAILED(order_elem->Get(L"Ids", 0, &ids_var, nullptr, nullptr)) || !(ids_var.vt & VT_ARRAY)) {
		return entries;
	}
	SAFEARRAY* sa = ids_var.parray;
	LONG lb = 0, ub = 0;
	SafeArrayGetLBound(sa, 1, &lb);
	SafeArrayGetUBound(sa, 1, &ub);
	for (LONG i = lb; i <= ub; i++) {
		BSTR guid_bstr = nullptr;
		if (FAILED(SafeArrayGetElement(sa, &i, &guid_bstr)) || !guid_bstr) {
			continue;
		}
		std::wstring guid(guid_bstr, SysStringLen(guid_bstr));
		SysFreeString(guid_bstr);
		boot_entry entry;
		entry.guid = guid;
		entry.description = guid;
		const std::wstring obj_path = L"BcdObject.Id=\"" + guid + L"\",StoreFilePath=\"\"";
		auto desc_elem = get_bcd_element(services.Get(), obj_path, BCD_DESCRIPTION);
		if (desc_elem) {
			_variant_t str_var;
			if (SUCCEEDED(desc_elem->Get(L"String", 0, &str_var, nullptr, nullptr)) && str_var.vt == VT_BSTR && str_var.bstrVal) {
				entry.description = str_var.bstrVal;
			}
		}
		auto path_elem = get_bcd_element(services.Get(), obj_path, BCD_APPLICATION_PATH);
		if (path_elem) {
			_variant_t str_var;
			if (SUCCEEDED(path_elem->Get(L"String", 0, &str_var, nullptr, nullptr)) && str_var.vt == VT_BSTR && str_var.bstrVal) {
				entry.path = str_var.bstrVal;
			}
		}
		entries.push_back(std::move(entry));
	}
	return entries;
}

bool bcd_edit::set_boot_order(const std::vector<boot_entry>& entries) {
	auto services = wmi_connect();
	if (!services) {
		return false;
	}
	std::vector<std::wstring> ids;
	for (const auto& entry : entries) {
		ids.push_back(entry.guid);
	}
	const std::wstring fwbootmgr_path = L"BcdObject.Id=\"{a5a30fa2-3d06-4e9f-b5f4-a01df9d1fcba}\",StoreFilePath=\"\"";
	return set_bcd_object_list_element(services.Get(), fwbootmgr_path, BCD_FW_DISPLAYORDER, ids);
}

bool bcd_edit::set_boot_next(const std::wstring& guid) {
	auto services = wmi_connect();
	if (!services) {
		return false;
	}
	std::vector<std::wstring> ids;
	ids.push_back(guid);
	const std::wstring fwbootmgr_path = L"BcdObject.Id=\"{a5a30fa2-3d06-4e9f-b5f4-a01df9d1fcba}\",StoreFilePath=\"\"";
	return set_bcd_object_list_element(services.Get(), fwbootmgr_path, BCD_FW_BOOTSEQUENCE, ids);
}

bool bcd_edit::delete_entry(const std::wstring& guid) {
	auto services = wmi_connect();
	if (!services) {
		return false;
	}
	return delete_bcd_object(services.Get(), guid);
}
