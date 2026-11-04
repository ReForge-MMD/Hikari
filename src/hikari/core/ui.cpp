#include "hikari/core/ui.hpp"

#include <commctrl.h>
#include <commdlg.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

#include "hikari/core/backend.hpp"
#include "hikari/core/effect.hpp"
#include "hikari/core/log.hpp"
#include "hikari/core/mmd_host.hpp"

namespace hikari::core {
namespace {

constexpr UINT ID_MENU_ROOT = 0x4800;
constexpr UINT ID_MME_EFFECT_MAPPING = 40005;
constexpr UINT ID_MME_ENABLE_EFFECT = 40020;
constexpr UINT ID_MME_AUTO_RELOAD = 40001;
constexpr UINT ID_MME_AUTO_SAVE_MAPPING = 40022;
constexpr UINT ID_MME_RELOAD_ALL = 40004;
constexpr UINT ID_MME_LOG_WINDOW = 40002;
constexpr UINT ID_MME_ABOUT = 40003;

constexpr UINT ID_FILE_OPEN = 40009;
constexpr UINT ID_FILE_SAVE = 40010;
constexpr UINT ID_FILE_OPEN_MODEL = 40016;
constexpr UINT ID_FILE_SAVE_MODEL = 40017;
constexpr UINT ID_FILE_CLOSE = 40011;
constexpr UINT ID_EDIT_SET_EFFECT = 40007;
constexpr UINT ID_EDIT_REMOVE_EFFECT = 40012;
constexpr UINT ID_EDIT_HIDE_SHOW = 40013;
constexpr UINT ID_EDIT_RESET_DEFAULT = 40008;
constexpr UINT ID_TOOL_DISASSEMBLE = 40014;
constexpr UINT ID_TOOL_SKIP_VALIDATION = 40028;
constexpr UINT ID_TOOL_ADD_TAB = 40029;

constexpr UINT ID_TAB_CONTROL = 1002;
constexpr UINT ID_LISTVIEW = 1003;
constexpr UINT ID_CAPTION_LABEL = 1004;
constexpr UINT ID_SET_EFFECT_BTN = 1007;
constexpr UINT ID_REMOVE_EFFECT_BTN = 1008;
constexpr UINT ID_HIDE_SHOW_BTN = 1011;
constexpr UINT ID_STATUS_LABEL = 1012;
constexpr UINT WM_REFRESH_HOST_OBJECTS = WM_USER + 102;

struct UiRow {
    std::size_t object_id = 0;
    std::int32_t subset = -1;
    std::string filename;
    bool is_model_row = false;
    HikariEffect* effect = nullptr;
    std::string effect_path;
    bool enabled = true;
};

HWND g_manager = nullptr;
HWND g_parent = nullptr;
HWND g_list_view = nullptr;
HWND g_tab_control = nullptr;
HWND g_status = nullptr;
std::vector<UiRow> g_rows;
std::atomic<std::uint64_t> g_last_fingerprint{0};
std::atomic_bool g_menu_injected{false};

std::wstring widen(const std::string& text) {
    if (text.empty()) {
        return {};
    }
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                                         nullptr, 0);
    std::wstring wide(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), size);
    return wide;
}

std::string narrow(const std::wstring& text) {
    if (text.empty()) {
        return {};
    }
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                                         nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), size,
                        nullptr, nullptr);
    return out;
}

std::string open_file_dialog(HWND owner, const wchar_t* filter) {
    wchar_t path[MAX_PATH] = {};
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = owner;
    dialog.lpstrFilter = filter;
    dialog.lpstrFile = path;
    dialog.nMaxFile = MAX_PATH;
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (!GetOpenFileNameW(&dialog)) {
        return {};
    }
    return narrow(path);
}

std::string save_file_dialog(HWND owner, const wchar_t* filter, const wchar_t* extension) {
    wchar_t path[MAX_PATH] = {};
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = owner;
    dialog.lpstrFilter = filter;
    dialog.lpstrFile = path;
    dialog.nMaxFile = MAX_PATH;
    dialog.lpstrDefExt = extension;
    dialog.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    if (!GetSaveFileNameW(&dialog)) {
        return {};
    }
    return narrow(path);
}

void set_status(const std::string& text) {
    if (g_status != nullptr) {
        SetWindowTextW(g_status, widen(text).c_str());
    }
}

void refresh_list();

void refresh_tabs() {
    if (g_tab_control == nullptr) {
        return;
    }
    SendMessageW(g_tab_control, TCM_DELETEALLITEMS, 0, 0);
    std::lock_guard lock(g_assignment_mutex);
    for (std::size_t i = 0; i < g_assignment_registry.tabs.size(); ++i) {
        wchar_t text[128] = {};
        const std::wstring name = widen(g_assignment_registry.tabs[i].name);
        wcsncpy_s(text, name.c_str(), _TRUNCATE);
        TCITEMW item{};
        item.mask = TCIF_TEXT;
        item.pszText = text;
        SendMessageW(g_tab_control, TCM_INSERTITEMW, i, reinterpret_cast<LPARAM>(&item));
    }
    SendMessageW(g_tab_control, TCM_SETCURSEL, g_assignment_registry.current_tab_idx, 0);
}

std::size_t current_tab() {
    if (g_tab_control == nullptr) {
        return 0;
    }
    const LRESULT index = SendMessageW(g_tab_control, TCM_GETCURSEL, 0, 0);
    return index < 0 ? 0 : static_cast<std::size_t>(index);
}

void rebuild_rows() {
    g_rows.clear();
    std::lock_guard lock(g_assignment_mutex);
    auto& registry = g_assignment_registry;
    const std::size_t tab_index = current_tab();
    registry.current_tab_idx = tab_index;
    const auto host_objects = enumerate_host_objects();

    const auto add_model_rows = [&](std::size_t object_id, const std::string& filename,
                                    HikariEffect* model_effect, const std::string& model_path,
                                    bool enabled,
                                    const std::unordered_map<std::int32_t, ModelSubsetAssignment>& subsets) {
        UiRow row;
        row.object_id = object_id;
        row.filename = filename;
        row.is_model_row = true;
        row.effect = model_effect;
        row.effect_path = model_path;
        row.enabled = enabled;
        g_rows.push_back(std::move(row));
        std::vector<std::pair<std::int32_t, const ModelSubsetAssignment*>> sorted;
        for (const auto& [index, assignment] : subsets) {
            sorted.push_back({index, &assignment});
        }
        std::sort(sorted.begin(), sorted.end(),
                  [](const auto& a, const auto& b) { return a.first < b.first; });
        for (const auto& [index, assignment] : sorted) {
            UiRow subset_row;
            subset_row.object_id = object_id;
            subset_row.subset = index;
            subset_row.filename = filename;
            subset_row.effect = assignment->effect;
            subset_row.effect_path = assignment->effect_path;
            subset_row.enabled = assignment->enabled;
            g_rows.push_back(std::move(subset_row));
        }
    };

    if (tab_index == 0) {
        if (!registry.default_effect_path.empty() || registry.default_effect != nullptr) {
            UiRow row;
            row.filename = "(default)";
            row.effect = registry.default_effect;
            row.effect_path = registry.default_effect_path;
            g_rows.push_back(std::move(row));
        }
        for (const auto& object : host_objects) {
            const auto it = registry.models.find(object.id);
            if (it == registry.models.end()) {
                continue;
            }
            add_model_rows(object.id, object.filename, it->second.effect, it->second.effect_path,
                           it->second.enabled, it->second.subsets);
        }
        for (const auto& [id, model] : registry.models) {
            if (std::none_of(host_objects.begin(), host_objects.end(),
                             [&](const HostObject& object) { return object.id == id; })) {
                add_model_rows(id, model.filename, model.effect, model.effect_path, model.enabled,
                               model.subsets);
            }
        }
    } else if (tab_index < registry.tabs.size()) {
        const auto& tab = registry.tabs[tab_index];
        if (!tab.default_effect_path.empty() || tab.default_effect != nullptr) {
            UiRow row;
            row.filename = "(default)";
            row.effect = tab.default_effect;
            row.effect_path = tab.default_effect_path;
            g_rows.push_back(std::move(row));
        }
        for (const auto& object : host_objects) {
            const auto model = tab.model_overrides.find(object.id);
            std::unordered_map<std::int32_t, ModelSubsetAssignment> subsets;
            for (const auto& [packed, assignment] : tab.subset_overrides) {
                if (static_cast<std::size_t>(packed >> 32) == object.id) {
                    subsets[static_cast<std::int32_t>(packed & 0xFFFFFFFF)] = assignment;
                }
            }
            if (model != tab.model_overrides.end() || !subsets.empty()) {
                add_model_rows(object.id, object.filename,
                               model != tab.model_overrides.end() ? model->second.effect : nullptr,
                               model != tab.model_overrides.end() ? model->second.effect_path : "",
                               model != tab.model_overrides.end() ? model->second.enabled : true,
                               subsets);
            }
        }
    }
}

void refresh_list() {
    if (g_list_view == nullptr) {
        return;
    }
    rebuild_rows();
    SendMessageW(g_list_view, LVM_DELETEALLITEMS, 0, 0);

    const std::size_t tab_index = current_tab();
    for (std::size_t i = 0; i < g_rows.size(); ++i) {
        const auto& row = g_rows[i];
        std::wstring name = widen(row.filename);
        if (row.subset >= 0) {
            name = L"    Subset " + std::to_wstring(row.subset);
        }
        const std::wstring effect = row.effect_path.empty() ? L"(none)" : widen(row.effect_path);

        LVITEMW item{};
        item.mask = LVIF_TEXT | LVIF_PARAM;
        item.iItem = static_cast<int>(i);
        item.pszText = const_cast<wchar_t*>(name.c_str());
        item.lParam = static_cast<LPARAM>(i);
        const int index = static_cast<int>(SendMessageW(g_list_view, LVM_INSERTITEMW, 0,
                                                        reinterpret_cast<LPARAM>(&item)));
        LVITEMW sub{};
        sub.mask = LVIF_TEXT;
        sub.iItem = index;
        sub.iSubItem = 1;
        sub.pszText = const_cast<wchar_t*>(effect.c_str());
        SendMessageW(g_list_view, LVM_SETITEMW, 0, reinterpret_cast<LPARAM>(&sub));
        ListView_SetCheckState(g_list_view, index, row.enabled ? TRUE : FALSE);
    }
    refresh_tabs();
    SendMessageW(g_tab_control, TCM_SETCURSEL, tab_index, 0);

    std::lock_guard lock(g_assignment_mutex);
    const std::size_t safe_tab = std::min(tab_index, g_assignment_registry.tabs.size() - 1);
    const std::string caption =
        "[Hikari] MMEffect Mapping - " + g_assignment_registry.tabs[safe_tab].name;
    SetWindowTextW(g_manager, widen(caption).c_str());
}

int selected_row() {
    if (g_list_view == nullptr) {
        return -1;
    }
    return ListView_GetNextItem(g_list_view, -1, LVNI_SELECTED);
}

void apply_row_effect(int row_index, const std::string& path) {
    if (row_index < 0 || static_cast<std::size_t>(row_index) >= g_rows.size()) {
        return;
    }
    const UiRow& row = g_rows[static_cast<std::size_t>(row_index)];
    const std::size_t tab_index = current_tab();
    std::lock_guard lock(g_assignment_mutex);
    if (row.is_model_row) {
        g_assignment_registry.assign_tab_effect(tab_index, row.object_id, row.filename, -1, path);
        set_status(path.empty() ? "Removed model effect" : "Assigned effect to model");
    } else if (row.subset >= 0) {
        g_assignment_registry.assign_tab_effect(tab_index, row.object_id, row.filename, row.subset,
                                                path);
        set_status(path.empty() ? "Removed subset effect" : "Assigned effect to subset");
    } else {
        g_assignment_registry.assign_tab_effect(tab_index, std::nullopt, "", -1, path);
        set_status(path.empty() ? "Removed default effect" : "Assigned default effect");
    }
    refresh_list();
}

void set_check_state(int row_index, bool enabled) {
    if (row_index < 0 || static_cast<std::size_t>(row_index) >= g_rows.size()) {
        return;
    }
    const UiRow& row = g_rows[static_cast<std::size_t>(row_index)];
    std::lock_guard lock(g_assignment_mutex);
    const std::size_t tab_index = current_tab();
    if (row.is_model_row) {
        g_assignment_registry.set_tab_item_shown(tab_index, row.object_id, -1, enabled);
    } else if (row.subset >= 0) {
        g_assignment_registry.set_tab_item_shown(tab_index, row.object_id, row.subset, enabled);
    }
}

void refresh_log_window_content(HWND hwnd) {
    HWND edit = GetDlgItem(hwnd, 1);
    if (edit != nullptr) {
        SetWindowTextW(edit, widen(get_recent_logs()).c_str());
    }
}

LRESULT CALLBACK log_window_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case WM_SIZE: {
        HWND edit = GetDlgItem(hwnd, 1);
        if (edit != nullptr) {
            MoveWindow(edit, 0, 0, LOWORD(lparam), HIWORD(lparam), TRUE);
        }
        return 0;
    }
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    default:
        return DefWindowProcW(hwnd, message, wparam, lparam);
    }
}

void show_log_window(HWND owner) {
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = log_window_proc;
        wc.hInstance = reinterpret_cast<HINSTANCE>(GetModuleHandleW(nullptr));
        wc.lpszClassName = L"HikariLogWindow";
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        RegisterClassExW(&wc);
        registered = true;
    }
    HWND hwnd = CreateWindowExW(WS_EX_TOOLWINDOW, L"HikariLogWindow", L"Hikari Log",
                                WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT,
                                760, 480, owner, nullptr,
                                reinterpret_cast<HINSTANCE>(GetModuleHandleW(nullptr)), nullptr);
    if (hwnd == nullptr) {
        return;
    }
    CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT",
                    L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY |
                              ES_AUTOVSCROLL,
                    0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(static_cast<UINT_PTR>(1)),
                    reinterpret_cast<HINSTANCE>(GetModuleHandleW(nullptr)), nullptr);
    refresh_log_window_content(hwnd);
}

void show_about(HWND owner) {
    MessageBoxW(owner,
                L"Hikari (光)\n\nDirect3D 11 runtime and MikuMikuEffect (MME) host for MMD.\n"
                L"Ported to C++20 with Qt6 studio, vcpkg and ccache.\n\n"
                L"Shader Model 5.0, 64KB effect constant buffer, MRT and compute support.",
                L"About Hikari", MB_OK | MB_ICONINFORMATION);
}

void handle_command(HWND hwnd, UINT id) {
    switch (id) {
    case ID_MME_EFFECT_MAPPING:
        toggle_effects_manager(hwnd);
        break;
    case ID_MME_ENABLE_EFFECT:
        g_effects_enabled.store(!g_effects_enabled.load());
        set_status(g_effects_enabled.load() ? "Effects enabled" : "Effects disabled");
        break;
    case ID_MME_AUTO_RELOAD:
        g_auto_reload.store(!g_auto_reload.load());
        set_status(g_auto_reload.load() ? "Auto reload enabled" : "Auto reload disabled");
        break;
    case ID_MME_AUTO_SAVE_MAPPING:
        g_auto_save_mapping.store(!g_auto_save_mapping.load());
        set_status(g_auto_save_mapping.load() ? "Auto save enabled" : "Auto save disabled");
        break;
    case ID_MME_RELOAD_ALL: {
        std::lock_guard lock(g_assignment_mutex);
        g_assignment_registry.reload_all_effects();
        refresh_list();
        set_status("Reloaded all effects");
        break;
    }
    case ID_MME_LOG_WINDOW:
        show_log_window(hwnd);
        break;
    case ID_MME_ABOUT:
        show_about(hwnd);
        break;
    case ID_FILE_OPEN: {
        const std::string path = open_file_dialog(hwnd, L"Hikari Mapping (*.hmm)\0*.hmm\0");
        if (!path.empty()) {
            std::lock_guard lock(g_assignment_mutex);
            g_assignment_registry.load_hmm(path);
            refresh_list();
            set_status("Loaded mapping: " + path);
        }
        break;
    }
    case ID_FILE_SAVE: {
        const std::string path = save_file_dialog(hwnd, L"Hikari Mapping (*.hmm)\0*.hmm\0", L"hmm");
        if (!path.empty()) {
            std::lock_guard lock(g_assignment_mutex);
            g_assignment_registry.save_hmm(path);
            set_status("Saved mapping: " + path);
        }
        break;
    }
    case ID_FILE_OPEN_MODEL: {
        const std::string path = open_file_dialog(hwnd, L"Hikari Model Mapping (*.hmd)\0*.hmd\0");
        const int row = selected_row();
        if (!path.empty() && row >= 0) {
            std::lock_guard lock(g_assignment_mutex);
            g_assignment_registry.load_hmd(g_rows[static_cast<std::size_t>(row)].object_id, path);
            refresh_list();
            set_status("Loaded model mapping: " + path);
        }
        break;
    }
    case ID_FILE_SAVE_MODEL: {
        const int row = selected_row();
        if (row < 0) {
            set_status("Select a model first");
            break;
        }
        const std::string path = save_file_dialog(hwnd, L"Hikari Model Mapping (*.hmd)\0*.hmd\0",
                                                  L"hmd");
        if (!path.empty()) {
            std::lock_guard lock(g_assignment_mutex);
            g_assignment_registry.save_hmd(g_rows[static_cast<std::size_t>(row)].object_id, path);
            set_status("Saved model mapping: " + path);
        }
        break;
    }
    case ID_FILE_CLOSE:
        DestroyWindow(hwnd);
        break;
    case ID_EDIT_SET_EFFECT:
    case ID_SET_EFFECT_BTN: {
        const std::string path = open_file_dialog(
            hwnd, L"DirectX Effect (*.fx;*.fxsub;*.hlsl)\0*.fx;*.fxsub;*.hlsl\0");
        if (!path.empty()) {
            apply_row_effect(selected_row(), path);
        }
        break;
    }
    case ID_EDIT_REMOVE_EFFECT:
    case ID_REMOVE_EFFECT_BTN:
        apply_row_effect(selected_row(), "");
        break;
    case ID_EDIT_HIDE_SHOW:
    case ID_HIDE_SHOW_BTN: {
        const int row = selected_row();
        if (row >= 0) {
            const bool enabled = !g_rows[static_cast<std::size_t>(row)].enabled;
            set_check_state(row, enabled);
            ListView_SetCheckState(g_list_view, row, enabled ? TRUE : FALSE);
            refresh_list();
        }
        break;
    }
    case ID_EDIT_RESET_DEFAULT: {
        std::lock_guard lock(g_assignment_mutex);
        g_assignment_registry.assign_default_effect("");
        refresh_list();
        set_status("Reset default effect");
        break;
    }
    case ID_TOOL_DISASSEMBLE: {
        const int row = selected_row();
        if (row < 0 || g_rows[static_cast<std::size_t>(row)].effect == nullptr) {
            set_status("Select an assigned effect first");
            break;
        }
        try {
            const std::string listing =
                disassemble_hikari_effect(g_rows[static_cast<std::size_t>(row)].effect);
            log_msg(listing);
            show_log_window(hwnd);
            set_status("Effect disassembly written to hikari.log");
        } catch (const std::exception& error) {
            set_status(std::string("Disassembly failed: ") + error.what());
        }
        break;
    }
    case ID_TOOL_SKIP_VALIDATION:
        g_skip_device_validation.store(!g_skip_device_validation.load());
        set_status(g_skip_device_validation.load() ? "Device validation skipped"
                                                   : "Device validation enabled");
        break;
    case ID_TOOL_ADD_TAB: {
        std::lock_guard lock(g_assignment_mutex);
        g_assignment_registry.add_tab("Offscreen" + std::to_string(g_assignment_registry.tabs.size()),
                                      "", "");
        refresh_list();
        break;
    }
    default:
        break;
    }
}

LRESULT CALLBACK manager_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case WM_CREATE: {
        HINSTANCE instance = reinterpret_cast<HINSTANCE>(GetModuleHandleW(nullptr));
        g_tab_control = CreateWindowExW(0, L"SysTabControl32", L"",
                                        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS, 8, 8, 760, 24,
                                        hwnd,
                                        reinterpret_cast<HMENU>(static_cast<UINT_PTR>(ID_TAB_CONTROL)),
                                        instance, nullptr);
        g_list_view = CreateWindowExW(WS_EX_CLIENTEDGE, L"SysListView32", L"",
                                      WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SHOWSELALWAYS |
                                          LVS_SINGLESEL,
                                      8, 38, 760, 360, hwnd,
                                      reinterpret_cast<HMENU>(static_cast<UINT_PTR>(ID_LISTVIEW)),
                                      instance, nullptr);
        ListView_SetExtendedListViewStyle(g_list_view,
                                          LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES |
                                              LVS_EX_CHECKBOXES);
        LVCOLUMNW column{};
        column.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
        column.pszText = const_cast<wchar_t*>(L"Object");
        column.cx = 360;
        column.iSubItem = 0;
        ListView_InsertColumn(g_list_view, 0, &column);
        column.pszText = const_cast<wchar_t*>(L"Effect");
        column.cx = 380;
        column.iSubItem = 1;
        ListView_InsertColumn(g_list_view, 1, &column);

        HWND set_button = CreateWindowExW(0, L"BUTTON", L"Set Effect",
                                          WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 8, 406, 100, 28,
                                          hwnd,
                                          reinterpret_cast<HMENU>(static_cast<UINT_PTR>(ID_SET_EFFECT_BTN)),
                                          instance, nullptr);
        HWND remove_button = CreateWindowExW(
            0, L"BUTTON", L"Remove", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 116, 406, 100, 28, hwnd,
            reinterpret_cast<HMENU>(static_cast<UINT_PTR>(ID_REMOVE_EFFECT_BTN)), instance, nullptr);
        HWND hide_button = CreateWindowExW(
            0, L"BUTTON", L"Hide / Show", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 224, 406, 100, 28,
            hwnd, reinterpret_cast<HMENU>(static_cast<UINT_PTR>(ID_HIDE_SHOW_BTN)), instance,
            nullptr);
        g_status = CreateWindowExW(0, L"STATIC", L"Ready",
                                   WS_CHILD | WS_VISIBLE | SS_LEFT, 332, 412, 436, 20, hwnd,
                                   reinterpret_cast<HMENU>(static_cast<UINT_PTR>(ID_STATUS_LABEL)),
                                   instance, nullptr);
        (void)set_button;
        (void)remove_button;
        (void)hide_button;

        HMENU menu = CreateMenu();
        HMENU file = CreatePopupMenu();
        AppendMenuW(file, MF_STRING, ID_FILE_OPEN, L"Open Mapping (.hmm)...");
        AppendMenuW(file, MF_STRING, ID_FILE_SAVE, L"Save Mapping (.hmm)...");
        AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(file, MF_STRING, ID_FILE_OPEN_MODEL, L"Load Model Mapping (.hmd)...");
        AppendMenuW(file, MF_STRING, ID_FILE_SAVE_MODEL, L"Save Model Mapping (.hmd)...");
        AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(file, MF_STRING, ID_FILE_CLOSE, L"Close");
        AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(file), L"File");

        HMENU edit = CreatePopupMenu();
        AppendMenuW(edit, MF_STRING, ID_EDIT_SET_EFFECT, L"Set Effect...");
        AppendMenuW(edit, MF_STRING, ID_EDIT_REMOVE_EFFECT, L"Remove Effect");
        AppendMenuW(edit, MF_STRING, ID_EDIT_HIDE_SHOW, L"Hide / Show");
        AppendMenuW(edit, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(edit, MF_STRING, ID_EDIT_RESET_DEFAULT, L"Reset Default");
        AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(edit), L"Edit");

        HMENU tool = CreatePopupMenu();
        AppendMenuW(tool, MF_STRING, ID_TOOL_DISASSEMBLE, L"Disassemble Effect");
        AppendMenuW(tool, MF_STRING, ID_TOOL_SKIP_VALIDATION, L"Skip Device Validation");
        AppendMenuW(tool, MF_STRING, ID_TOOL_ADD_TAB, L"Add Offscreen Tab");
        AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(tool), L"Tool");
        SetMenu(hwnd, menu);

        refresh_tabs();
        refresh_list();
        return 0;
    }
    case WM_SIZE:
        if (g_list_view != nullptr) {
            MoveWindow(g_tab_control, 8, 8, LOWORD(lparam) - 16, 24, TRUE);
            MoveWindow(g_list_view, 8, 38, LOWORD(lparam) - 16, HIWORD(lparam) - 86, TRUE);
            MoveWindow(GetDlgItem(hwnd, ID_SET_EFFECT_BTN), 8, HIWORD(lparam) - 40, 100, 28, TRUE);
            MoveWindow(GetDlgItem(hwnd, ID_REMOVE_EFFECT_BTN), 116, HIWORD(lparam) - 40, 100, 28,
                       TRUE);
            MoveWindow(GetDlgItem(hwnd, ID_HIDE_SHOW_BTN), 224, HIWORD(lparam) - 40, 100, 28, TRUE);
            MoveWindow(g_status, 332, HIWORD(lparam) - 34, LOWORD(lparam) - 340, 20, TRUE);
        }
        return 0;
    case WM_COMMAND:
        handle_command(hwnd, LOWORD(wparam));
        return 0;
    case WM_NOTIFY: {
        const auto* header = reinterpret_cast<NMHDR*>(lparam);
        if (header == nullptr) {
            return 0;
        }
        if (header->idFrom == ID_TAB_CONTROL && header->code == TCN_SELCHANGE) {
            refresh_list();
        } else if (header->idFrom == ID_LISTVIEW) {
            if (header->code == LVN_ITEMCHANGED) {
                auto* changed = reinterpret_cast<NMLISTVIEW*>(lparam);
                if ((changed->uChanged & LVIF_STATE) != 0 &&
                    (changed->uNewState & LVIS_STATEIMAGEMASK) != 0) {
                    const bool checked = ListView_GetCheckState(g_list_view, changed->iItem) != 0;
                    set_check_state(changed->iItem, checked);
                }
            } else if (header->code == NM_DBLCLK) {
                handle_command(hwnd, ID_EDIT_SET_EFFECT);
            }
        }
        return 0;
    }
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        g_manager = nullptr;
        g_list_view = nullptr;
        g_tab_control = nullptr;
        g_status = nullptr;
        g_rows.clear();
        return 0;
    case WM_REFRESH_HOST_OBJECTS:
        refresh_list();
        return 0;
    default:
        return DefWindowProcW(hwnd, message, wparam, lparam);
    }
}

void register_manager_class() {
    static bool registered = false;
    if (registered) {
        return;
    }
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = manager_proc;
    wc.hInstance = reinterpret_cast<HINSTANCE>(GetModuleHandleW(nullptr));
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"HikariEffectsManager";
    wc.lpszMenuName = nullptr;
    RegisterClassExW(&wc);
    registered = true;
}

} // namespace

void init_ui(HWND parent) {
    g_parent = parent;
    if (parent == nullptr) {
        return;
    }
    HMENU menu = GetMenu(parent);
    if (menu == nullptr) {
        return;
    }

    HMENU root = CreatePopupMenu();
    AppendMenuW(root, MF_STRING, ID_MME_EFFECT_MAPPING, L"Effect Mapping...");
    AppendMenuW(root, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(root, MF_STRING | (g_effects_enabled.load() ? MF_CHECKED : 0), ID_MME_ENABLE_EFFECT,
                L"Enable Effects");
    AppendMenuW(root, MF_STRING | (g_auto_reload.load() ? MF_CHECKED : 0), ID_MME_AUTO_RELOAD,
                L"Auto Reload");
    AppendMenuW(root, MF_STRING | (g_auto_save_mapping.load() ? MF_CHECKED : 0),
                ID_MME_AUTO_SAVE_MAPPING, L"Auto Save Mapping");
    AppendMenuW(root, MF_STRING, ID_MME_RELOAD_ALL, L"Reload All Effects");
    AppendMenuW(root, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(root, MF_STRING, ID_MME_LOG_WINDOW, L"Log Window");
    AppendMenuW(root, MF_STRING, ID_MME_ABOUT, L"About Hikari");

    const int count = GetMenuItemCount(menu);
    AppendMenuW(menu, MF_POPUP | MF_RIGHTJUSTIFY, reinterpret_cast<UINT_PTR>(root), L"Hikari");
    (void)count;
    DrawMenuBar(parent);
    g_menu_injected.store(true);
    log_msg("[ui] Hikari menu injected into the MMD main menu");
}

void ensure_ui_injected() {
    if (g_parent != nullptr && !g_menu_injected.load() && GetMenu(g_parent) != nullptr) {
        init_ui(g_parent);
    }
}

void notify_host_objects_changed() {
    if (g_manager != nullptr) {
        PostMessageW(g_manager, WM_REFRESH_HOST_OBJECTS, 0, 0);
    }
}

void toggle_effects_manager(HWND parent) {
    if (g_manager != nullptr) {
        ShowWindow(g_manager, SW_SHOW);
        SetForegroundWindow(g_manager);
        refresh_list();
        return;
    }
    register_manager_class();
    g_manager = CreateWindowExW(WS_EX_TOOLWINDOW, L"HikariEffectsManager",
                                L"[Hikari] MMEffect Mapping", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                                CW_USEDEFAULT, CW_USEDEFAULT, 800, 520, parent, nullptr,
                                reinterpret_cast<HINSTANCE>(GetModuleHandleW(nullptr)), nullptr);
    if (g_manager != nullptr) {
        refresh_list();
    }
}

} // namespace hikari::core
