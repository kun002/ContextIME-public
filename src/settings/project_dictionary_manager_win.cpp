#include "contextime/project_indexer_server.h"
#include "contextime/project_management_client.h"
#include "contextime/project_management_protocol.h"

#if !defined(_WIN32)
#error project_dictionary_manager_win.cpp is Windows-only
#endif

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commctrl.h>

#include <algorithm>
#include <cstdlib>
#include <cstdint>
#include <cwchar>
#include <iterator>
#include <limits>
#include <string>
#include <vector>

namespace {

constexpr wchar_t kWindowClass[] =
    L"ContextIME.ProjectDictionaryManager.Window.v1";
constexpr wchar_t kWindowTitle[] =
    L"ContextIME \u9879\u76ee\u8bcd\u5e93";
constexpr std::uint16_t kEntryPageSize = 100;
constexpr std::uint16_t kProjectPageSize = 512;
constexpr std::size_t kMaximumDisplayedProjects = 8192;
constexpr std::uint32_t kCallTimeoutMs = 5000;

enum ControlId : int {
  kProjectList = 1001,
  kEntryList = 1002,
  kRefresh = 1003,
  kToggleEnabled = 1004,
  kRemove = 1005,
  kPrevious = 1006,
  kNext = 1007,
  kStatus = 1008,
  kProjectLabel = 1009,
  kEntryLabel = 1010,
  kTermInput = 1011,
  kAddTerm = 1012,
  kRemoveEntry = 1013,
};

struct AppState {
  HWND window = nullptr;
  HWND project_list = nullptr;
  HWND entry_list = nullptr;
  HWND refresh = nullptr;
  HWND toggle_enabled = nullptr;
  HWND remove = nullptr;
  HWND previous = nullptr;
  HWND next = nullptr;
  HWND status = nullptr;
  HWND project_label = nullptr;
  HWND entry_label = nullptr;
  HWND term_input = nullptr;
  HWND add_term = nullptr;
  HWND remove_entry = nullptr;
  std::vector<std::string> project_ids;
  std::vector<contextime::ProjectDictionaryEntry> page_entries;
  std::string selected_project;
  std::uint32_t current_cursor = 0;
  std::uint32_t next_cursor =
      contextime::management_protocol::kNoNextCursor;
  std::uint32_t total_entries = 0;
  bool selected_enabled = false;
  int selected_entry = -1;
  LONG request_sequence = 0;
};

std::wstring WidenAscii(const char* value) {
  std::wstring result;
  if (value == nullptr) return result;
  while (*value != '\0') {
    result.push_back(static_cast<unsigned char>(*value));
    ++value;
  }
  return result;
}

std::wstring FromUtf8(const std::string& value) {
  if (value.empty()) return {};
  const int required = MultiByteToWideChar(
      CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
      static_cast<int>(value.size()), nullptr, 0);
  if (required <= 0) return L"<invalid UTF-8>";
  std::wstring result(static_cast<std::size_t>(required), L'\0');
  if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                          static_cast<int>(value.size()), result.data(),
                          required) != required) {
    return L"<invalid UTF-8>";
  }
  return result;
}

bool ToUtf8(const std::wstring& value, std::string& encoded) {
  encoded.clear();
  if (value.empty()) return true;
  const int required = WideCharToMultiByte(
      CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
      static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
  if (required <= 0) return false;
  std::string result(static_cast<std::size_t>(required), '\0');
  if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
                          static_cast<int>(value.size()), result.data(),
                          required, nullptr,
                          nullptr) != required) {
    return false;
  }
  encoded = std::move(result);
  return true;
}

std::wstring FormatTimestamp(std::uint64_t unix_ms) {
  constexpr std::uint64_t kWindowsToUnixEpochTicks =
      116444736000000000ull;
  constexpr std::uint64_t kTicksPerMillisecond = 10000ull;
  if (unix_ms >
      ((std::numeric_limits<std::uint64_t>::max)() -
       kWindowsToUnixEpochTicks) /
          kTicksPerMillisecond) {
    return std::to_wstring(unix_ms);
  }
  ULARGE_INTEGER ticks{};
  ticks.QuadPart =
      kWindowsToUnixEpochTicks + unix_ms * kTicksPerMillisecond;
  FILETIME utc{};
  utc.dwLowDateTime = ticks.LowPart;
  utc.dwHighDateTime = ticks.HighPart;
  FILETIME local{};
  SYSTEMTIME time{};
  if (!FileTimeToLocalFileTime(&utc, &local) ||
      !FileTimeToSystemTime(&local, &time)) {
    return std::to_wstring(unix_ms);
  }
  wchar_t buffer[32]{};
  swprintf_s(buffer, L"%04u-%02u-%02u %02u:%02u:%02u",
             static_cast<unsigned int>(time.wYear),
             static_cast<unsigned int>(time.wMonth),
             static_cast<unsigned int>(time.wDay),
             static_cast<unsigned int>(time.wHour),
             static_cast<unsigned int>(time.wMinute),
             static_cast<unsigned int>(time.wSecond));
  return buffer;
}

void SetStatus(AppState& state, const std::wstring& value) {
  SetWindowTextW(state.status, value.c_str());
}

std::wstring ResponseError(
    const contextime::management_protocol::ManagementResponse& response) {
  std::wstring message = L"Context Service: ";
  message += WidenAscii(
      contextime::management_protocol::ToString(response.status));
  if (response.status ==
      contextime::management_protocol::ResponseStatus::StoreError) {
    message += L" / ";
    message += WidenAscii(contextime::ToString(response.store_status));
  }
  return message;
}

bool Call(
    AppState& state,
    contextime::management_protocol::ManagementRequest request,
    contextime::management_protocol::ManagementResponse& response) {
  request.request_id = static_cast<std::uint32_t>(
      InterlockedIncrement(&state.request_sequence));
  const auto status = contextime::CallProjectManagement(
      contextime::kProjectIndexerPipeName, kCallTimeoutMs, request, response);
  if (status != contextime::ProjectManagementCallStatus::Ok) {
    std::wstring message = L"Context Service: ";
    message += WidenAscii(contextime::ToString(status));
    SetStatus(state, message);
    return false;
  }
  if (response.status !=
      contextime::management_protocol::ResponseStatus::Ok) {
    SetStatus(state, ResponseError(response));
    return false;
  }
  return true;
}

void SetButtons(AppState& state) {
  const bool selected = !state.selected_project.empty();
  EnableWindow(state.toggle_enabled, selected);
  EnableWindow(state.remove, selected);
  EnableWindow(state.add_term, selected);
  EnableWindow(state.remove_entry, selected && state.selected_entry >= 0);
  EnableWindow(state.previous, selected && state.current_cursor > 0);
  EnableWindow(
      state.next,
      selected &&
          state.next_cursor !=
              contextime::management_protocol::kNoNextCursor);
  SetWindowTextW(state.toggle_enabled,
                 state.selected_enabled ? L"\u7981\u7528"
                                        : L"\u542f\u7528");
}

void ClearEntries(AppState& state) {
  ListView_DeleteAllItems(state.entry_list);
  state.page_entries.clear();
  state.selected_entry = -1;
  state.current_cursor = 0;
  state.next_cursor = contextime::management_protocol::kNoNextCursor;
  state.total_entries = 0;
  state.selected_enabled = false;
  SetButtons(state);
}

void PopulateEntries(
    AppState& state,
    const contextime::management_protocol::ManagementResponse& response) {
  ListView_DeleteAllItems(state.entry_list);
  state.page_entries = response.entries;
  state.selected_entry = -1;
  for (std::size_t index = 0; index < response.entries.size(); ++index) {
    const auto& entry = response.entries[index];
    std::wstring symbol = FromUtf8(entry.symbol);
    LVITEMW item{};
    item.mask = LVIF_TEXT;
    item.iItem = static_cast<int>(index);
    item.pszText = symbol.data();
    const int row = ListView_InsertItem(state.entry_list, &item);
    const std::wstring type = WidenAscii(contextime::ToString(
        entry.symbol_type));
    const std::wstring source = WidenAscii(contextime::ToString(entry.source));
    const std::wstring frequency = std::to_wstring(entry.frequency);
    const std::wstring timestamp = FormatTimestamp(entry.last_seen_ms);
    ListView_SetItemText(state.entry_list, row, 1,
                         const_cast<wchar_t*>(type.c_str()));
    ListView_SetItemText(state.entry_list, row, 2,
                         const_cast<wchar_t*>(source.c_str()));
    ListView_SetItemText(state.entry_list, row, 3,
                         const_cast<wchar_t*>(frequency.c_str()));
    ListView_SetItemText(state.entry_list, row, 4,
                         const_cast<wchar_t*>(timestamp.c_str()));
  }
}

bool LoadEntryPage(AppState& state, std::uint32_t cursor) {
  if (state.selected_project.empty()) {
    ClearEntries(state);
    return false;
  }
  contextime::management_protocol::ManagementRequest request;
  request.operation =
      contextime::management_protocol::Operation::ViewProject;
  request.page_size = kEntryPageSize;
  request.cursor = cursor;
  if (!contextime::management_protocol::ProjectIdFromString(
          state.selected_project, request.project_id)) {
    SetStatus(state, L"\u9879\u76ee ID \u65e0\u6548");
    ClearEntries(state);
    return false;
  }
  contextime::management_protocol::ManagementResponse response;
  if (!Call(state, request, response)) {
    if (response.status ==
        contextime::management_protocol::ResponseStatus::NotFound) {
      state.selected_project.clear();
      ClearEntries(state);
    }
    return false;
  }
  state.current_cursor = cursor;
  state.next_cursor = response.next_cursor;
  state.total_entries = response.total_count;
  state.selected_enabled = response.enabled;
  PopulateEntries(state, response);
  SetButtons(state);
  std::wstring message = response.enabled ? L"\u5df2\u542f\u7528"
                                          : L"\u5df2\u7981\u7528";
  message += L"  |  ";
  message += std::to_wstring(response.total_count);
  message += L" \u4e2a\u8bcd\u6761";
  if (!response.entries.empty()) {
    message += L"  |  ";
    message += std::to_wstring(cursor + 1);
    message += L"-";
    message += std::to_wstring(cursor + response.entries.size());
  }
  SetStatus(state, message);
  return true;
}

void SelectProject(AppState& state, int index) {
  if (index < 0 ||
      static_cast<std::size_t>(index) >= state.project_ids.size()) {
    state.selected_project.clear();
    ClearEntries(state);
    return;
  }
  state.selected_project = state.project_ids[static_cast<std::size_t>(index)];
  LoadEntryPage(state, 0);
}

void RefreshProjects(AppState& state) {
  const std::string previous = state.selected_project;
  state.project_ids.clear();
  SendMessageW(state.project_list, LB_RESETCONTENT, 0, 0);
  state.selected_project.clear();
  ClearEntries(state);

  std::uint32_t cursor = 0;
  std::uint32_t total = 0;
  bool truncated = false;
  while (state.project_ids.size() < kMaximumDisplayedProjects) {
    contextime::management_protocol::ManagementRequest request;
    request.operation =
        contextime::management_protocol::Operation::ListProjects;
    request.page_size = kProjectPageSize;
    request.cursor = cursor;
    contextime::management_protocol::ManagementResponse response;
    if (!Call(state, request, response)) return;
    total = response.total_count;
    for (const auto& project_id : response.projects) {
      state.project_ids.push_back(
          contextime::project_protocol::ProjectIdToString(project_id));
    }
    if (response.next_cursor ==
        contextime::management_protocol::kNoNextCursor) {
      break;
    }
    if (response.next_cursor <= cursor) {
      SetStatus(state, L"Context Service: PROTOCOL_ERROR");
      return;
    }
    cursor = response.next_cursor;
  }
  truncated = state.project_ids.size() < total;

  int selected_index = -1;
  for (std::size_t index = 0; index < state.project_ids.size(); ++index) {
    const std::wstring text = WidenAscii(state.project_ids[index].c_str());
    SendMessageW(state.project_list, LB_ADDSTRING, 0,
                 reinterpret_cast<LPARAM>(text.c_str()));
    if (state.project_ids[index] == previous) {
      selected_index = static_cast<int>(index);
    }
  }
  if (selected_index < 0 && !state.project_ids.empty()) selected_index = 0;
  if (selected_index >= 0) {
    SendMessageW(state.project_list, LB_SETCURSEL,
                 static_cast<WPARAM>(selected_index), 0);
    SelectProject(state, selected_index);
  } else {
    SetStatus(state, L"\u5c1a\u65e0\u9879\u76ee\u8bcd\u5e93");
  }
  if (truncated) {
    SetStatus(state,
              L"\u9879\u76ee\u8fc7\u591a\uff0c\u4ec5\u663e\u793a\u524d 8192 \u4e2a");
  }
}

void ToggleProject(AppState& state) {
  if (state.selected_project.empty()) return;
  contextime::management_protocol::ManagementRequest request;
  request.operation =
      contextime::management_protocol::Operation::SetEnabled;
  request.enabled = !state.selected_enabled;
  if (!contextime::management_protocol::ProjectIdFromString(
          state.selected_project, request.project_id)) {
    SetStatus(state, L"\u9879\u76ee ID \u65e0\u6548");
    return;
  }
  contextime::management_protocol::ManagementResponse response;
  if (Call(state, request, response)) {
    LoadEntryPage(state, state.current_cursor);
  }
}

void RemoveProject(AppState& state) {
  if (state.selected_project.empty()) return;
  const int answer = MessageBoxW(
      state.window,
      L"\u5220\u9664\u540e\u4e0d\u53ef\u6062\u590d\u3002\u662f\u5426\u5220\u9664\u9009\u4e2d\u7684\u9879\u76ee\u8bcd\u5e93\uff1f",
      L"ContextIME", MB_ICONWARNING | MB_YESNO | MB_DEFBUTTON2);
  if (answer != IDYES) return;
  contextime::management_protocol::ManagementRequest request;
  request.operation =
      contextime::management_protocol::Operation::RemoveProject;
  if (!contextime::management_protocol::ProjectIdFromString(
          state.selected_project, request.project_id)) {
    SetStatus(state, L"\u9879\u76ee ID \u65e0\u6548");
    return;
  }
  contextime::management_protocol::ManagementResponse response;
  if (Call(state, request, response)) RefreshProjects(state);
}

std::wstring ReadTermInput(AppState& state) {
  const int length = GetWindowTextLengthW(state.term_input);
  if (length <= 0) return {};
  std::wstring text(static_cast<std::size_t>(length) + 1, L'\0');
  const int copied =
      GetWindowTextW(state.term_input, text.data(), length + 1);
  text.resize(static_cast<std::size_t>(copied > 0 ? copied : 0));
  while (!text.empty() && text.front() == L' ') text.erase(text.begin());
  while (!text.empty() && text.back() == L' ') text.pop_back();
  return text;
}

void AddTerm(AppState& state) {
  if (state.selected_project.empty()) return;
  const std::wstring text = ReadTermInput(state);
  if (text.empty()) {
    SetStatus(state,
              L"\u8bf7\u8f93\u5165\u8981\u6dfb\u52a0\u7684\u672f\u8bed");
    return;
  }
  std::string encoded;
  if (!ToUtf8(text, encoded)) {
    SetStatus(state, L"\u672f\u8bed\u7f16\u7801\u65e0\u6548");
    return;
  }
  if (encoded.size() > contextime::kMaximumProjectSymbolBytes) {
    SetStatus(state,
              L"\u672f\u8bed\u8fc7\u957f\uff1a\u6700\u591a 128 \u4e2a UTF-8 \u5b57\u8282");
    return;
  }
  contextime::management_protocol::ManagementRequest request;
  request.operation =
      contextime::management_protocol::Operation::UpsertTerm;
  request.entry.symbol = std::move(encoded);
  request.entry.symbol_type = contextime::ProjectSymbolType::Term;
  request.entry.source = contextime::ProjectSymbolSource::Manual;
  request.entry.frequency = 1;
  if (!contextime::management_protocol::ProjectIdFromString(
          state.selected_project, request.project_id)) {
    SetStatus(state, L"\u9879\u76ee ID \u65e0\u6548");
    return;
  }
  contextime::management_protocol::ManagementResponse response;
  if (!Call(state, request, response)) return;
  SetWindowTextW(state.term_input, L"");
  std::wstring message = L"\u5df2\u6dfb\u52a0\u672f\u8bed: ";
  message += text;
  SetStatus(state, message);
  LoadEntryPage(state, state.current_cursor);
}

void RemoveSelectedEntry(AppState& state) {
  if (state.selected_project.empty()) return;
  if (state.selected_entry < 0 ||
      static_cast<std::size_t>(state.selected_entry) >=
          state.page_entries.size()) {
    SetStatus(state,
              L"\u8bf7\u5148\u9009\u62e9\u8981\u5220\u9664\u7684\u8bcd\u6761");
    return;
  }
  const auto entry =
      state.page_entries[static_cast<std::size_t>(state.selected_entry)];
  const int answer = MessageBoxW(
      state.window,
      L"\u662f\u5426\u5220\u9664\u9009\u4e2d\u7684\u8bcd\u6761\uff1f",
      L"ContextIME", MB_ICONWARNING | MB_YESNO | MB_DEFBUTTON2);
  if (answer != IDYES) return;
  contextime::management_protocol::ManagementRequest request;
  request.operation =
      contextime::management_protocol::Operation::RemoveEntry;
  request.entry = entry;
  if (!contextime::management_protocol::ProjectIdFromString(
          state.selected_project, request.project_id)) {
    SetStatus(state, L"\u9879\u76ee ID \u65e0\u6548");
    return;
  }
  contextime::management_protocol::ManagementResponse response;
  if (!Call(state, request, response)) return;
  std::wstring message = L"\u5df2\u5220\u9664\u8bcd\u6761: ";
  message += FromUtf8(entry.symbol);
  SetStatus(state, message);
  LoadEntryPage(state, state.current_cursor);
}

void SetControlFont(HWND control) {
  SendMessageW(control, WM_SETFONT,
               reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)),
               TRUE);
}

HWND CreateControl(DWORD extended_style, const wchar_t* class_name,
                   const wchar_t* text, DWORD style, int id,
                   AppState& state) {
  HWND control = CreateWindowExW(
      extended_style, class_name, text, WS_CHILD | WS_VISIBLE | style, 0, 0,
      0, 0, state.window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
      GetModuleHandleW(nullptr), nullptr);
  if (control != nullptr) SetControlFont(control);
  return control;
}

bool CreateControls(AppState& state) {
  state.project_label = CreateControl(0, L"STATIC",
      L"\u9879\u76ee\uff08\u533f\u540d ID\uff09", 0, kProjectLabel, state);
  state.entry_label = CreateControl(0, L"STATIC", L"\u8bcd\u6761", 0,
                                    kEntryLabel, state);
  state.project_list = CreateControl(
      WS_EX_CLIENTEDGE, L"LISTBOX", L"",
      LBS_NOTIFY | LBS_NOINTEGRALHEIGHT | WS_VSCROLL | WS_TABSTOP,
      kProjectList, state);
  state.entry_list = CreateControl(
      WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
      LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS | WS_TABSTOP,
      kEntryList, state);
  state.refresh = CreateControl(0, L"BUTTON", L"\u5237\u65b0", WS_TABSTOP,
                                kRefresh, state);
  state.toggle_enabled = CreateControl(0, L"BUTTON", L"\u7981\u7528",
                                       WS_TABSTOP, kToggleEnabled, state);
  state.remove = CreateControl(0, L"BUTTON", L"\u5220\u9664", WS_TABSTOP,
                               kRemove, state);
  state.previous = CreateControl(0, L"BUTTON", L"\u4e0a\u4e00\u9875",
                                 WS_TABSTOP, kPrevious, state);
  state.next = CreateControl(0, L"BUTTON", L"\u4e0b\u4e00\u9875",
                             WS_TABSTOP, kNext, state);
  state.term_input = CreateControl(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                   ES_AUTOHSCROLL | WS_TABSTOP, kTermInput,
                                   state);
  state.add_term = CreateControl(0, L"BUTTON", L"\u6dfb\u52a0\u672f\u8bed",
                                 WS_TABSTOP, kAddTerm, state);
  state.remove_entry = CreateControl(0, L"BUTTON",
                                     L"\u5220\u9664\u8bcd\u6761", WS_TABSTOP,
                                     kRemoveEntry, state);
  state.status = CreateControl(0, L"STATIC", L"", SS_LEFT, kStatus, state);
  if (state.project_label == nullptr || state.entry_label == nullptr ||
      state.project_list == nullptr || state.entry_list == nullptr ||
      state.refresh == nullptr || state.toggle_enabled == nullptr ||
      state.remove == nullptr || state.previous == nullptr ||
      state.next == nullptr || state.status == nullptr ||
      state.term_input == nullptr || state.add_term == nullptr ||
      state.remove_entry == nullptr) {
    return false;
  }
  ListView_SetExtendedListViewStyle(
      state.entry_list,
      LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
  struct Column {
    const wchar_t* title;
    int width;
  };
  const Column columns[] = {
      {L"\u540d\u79f0", 230}, {L"\u7c7b\u578b", 90},
      {L"\u6765\u6e90", 130}, {L"\u9891\u6b21", 70},
      {L"\u6700\u540e\u66f4\u65b0", 150},
  };
  for (int index = 0; index < static_cast<int>(std::size(columns)); ++index) {
    LVCOLUMNW column{};
    column.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
    column.pszText = const_cast<wchar_t*>(columns[index].title);
    column.cx = columns[index].width;
    column.iSubItem = index;
    ListView_InsertColumn(state.entry_list, index, &column);
  }
  SetButtons(state);
  return true;
}

void LayoutControls(AppState& state, int width, int height) {
  constexpr int margin = 10;
  constexpr int label_height = 20;
  constexpr int button_height = 28;
  constexpr int button_width = 86;
  constexpr int gap = 8;
  constexpr int status_height = 20;
  const int left_width = (std::min)(320, (std::max)(230, width / 3));
  const int content_top = margin + label_height;
  const int button_top = (std::max)(content_top, height - 68);
  const int content_height = (std::max)(0, button_top - content_top - gap);
  MoveWindow(state.project_label, margin, margin, left_width - margin,
             label_height, TRUE);
  MoveWindow(state.entry_label, left_width + gap, margin,
             (std::max)(0, width - left_width - gap - margin), label_height,
             TRUE);
  MoveWindow(state.project_list, margin, content_top, left_width - margin,
             content_height, TRUE);
  MoveWindow(state.entry_list, left_width + gap, content_top,
             (std::max)(0, width - left_width - gap - margin), content_height,
             TRUE);
  int x = margin;
  MoveWindow(state.term_input, x, button_top, 180, button_height, TRUE);
  x += 180 + gap;
  for (HWND button : {state.add_term, state.remove_entry, state.refresh,
                      state.toggle_enabled, state.remove, state.previous,
                      state.next}) {
    MoveWindow(button, x, button_top, button_width, button_height, TRUE);
    x += button_width + gap;
  }
  MoveWindow(state.status, margin, height - status_height - 7,
             (std::max)(0, width - margin * 2), status_height, TRUE);
}

LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM w_param,
                                 LPARAM l_param) {
  auto* state = reinterpret_cast<AppState*>(
      GetWindowLongPtrW(window, GWLP_USERDATA));
  if (message == WM_NCCREATE) {
    const auto* create = reinterpret_cast<const CREATESTRUCTW*>(l_param);
    state = static_cast<AppState*>(create->lpCreateParams);
    state->window = window;
    SetWindowLongPtrW(window, GWLP_USERDATA,
                      reinterpret_cast<LONG_PTR>(state));
  }
  if (state == nullptr) return DefWindowProcW(window, message, w_param, l_param);

  switch (message) {
    case WM_CREATE:
      if (!CreateControls(*state)) return -1;
      PostMessageW(window, WM_COMMAND, MAKEWPARAM(kRefresh, BN_CLICKED),
                   reinterpret_cast<LPARAM>(state->refresh));
      return 0;
    case WM_SIZE:
      LayoutControls(*state, LOWORD(l_param), HIWORD(l_param));
      return 0;
    case WM_GETMINMAXINFO: {
      auto* limits = reinterpret_cast<MINMAXINFO*>(l_param);
      limits->ptMinTrackSize.x = 940;
      limits->ptMinTrackSize.y = 440;
      return 0;
    }
    case WM_COMMAND: {
      const int id = LOWORD(w_param);
      const int notification = HIWORD(w_param);
      if (id == kRefresh && notification == BN_CLICKED) {
        RefreshProjects(*state);
      } else if (id == kToggleEnabled && notification == BN_CLICKED) {
        ToggleProject(*state);
      } else if (id == kRemove && notification == BN_CLICKED) {
        RemoveProject(*state);
      } else if (id == kAddTerm && notification == BN_CLICKED) {
        AddTerm(*state);
      } else if (id == kRemoveEntry && notification == BN_CLICKED) {
        RemoveSelectedEntry(*state);
      } else if (id == kPrevious && notification == BN_CLICKED) {
        const std::uint32_t previous =
            state->current_cursor > kEntryPageSize
                ? state->current_cursor - kEntryPageSize
                : 0;
        LoadEntryPage(*state, previous);
      } else if (id == kNext && notification == BN_CLICKED &&
                 state->next_cursor !=
                     contextime::management_protocol::kNoNextCursor) {
        LoadEntryPage(*state, state->next_cursor);
      } else if (id == kProjectList && notification == LBN_SELCHANGE) {
        SelectProject(*state, static_cast<int>(SendMessageW(
                                  state->project_list, LB_GETCURSEL, 0, 0)));
      }
      return 0;
    }
    case WM_NOTIFY: {
      const auto* header = reinterpret_cast<const NMHDR*>(l_param);
      if (header != nullptr && header->idFrom == kEntryList &&
          header->code == LVN_ITEMCHANGED) {
        const auto* changed = reinterpret_cast<const NMLISTVIEW*>(l_param);
        if ((changed->uChanged & LVIF_STATE) != 0) {
          if ((changed->uNewState & LVIS_SELECTED) != 0) {
            state->selected_entry = changed->iItem;
          } else if ((changed->uOldState & LVIS_SELECTED) != 0 &&
                     state->selected_entry == changed->iItem) {
            state->selected_entry = -1;
          }
          SetButtons(*state);
        }
      }
      return 0;
    }
    case WM_DESTROY:
      PostQuitMessage(0);
      return 0;
    default:
      return DefWindowProcW(window, message, w_param, l_param);
  }
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show_command) {
  INITCOMMONCONTROLSEX controls{};
  controls.dwSize = sizeof(controls);
  controls.dwICC = ICC_LISTVIEW_CLASSES;
  if (!InitCommonControlsEx(&controls)) return EXIT_FAILURE;
  SetProcessDPIAware();

  WNDCLASSEXW window_class{};
  window_class.cbSize = sizeof(window_class);
  window_class.style = CS_HREDRAW | CS_VREDRAW;
  window_class.lpfnWndProc = WindowProcedure;
  window_class.hInstance = instance;
  window_class.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
  window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  window_class.hbrBackground =
      reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
  window_class.lpszClassName = kWindowClass;
  window_class.hIconSm = LoadIconW(nullptr, IDI_APPLICATION);
  if (RegisterClassExW(&window_class) == 0) return EXIT_FAILURE;

  AppState state;
  HWND window = CreateWindowExW(
      0, kWindowClass, kWindowTitle, WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
      CW_USEDEFAULT, CW_USEDEFAULT, 1000, 620, nullptr, nullptr, instance,
      &state);
  if (window == nullptr) return EXIT_FAILURE;
  ShowWindow(window, show_command);
  UpdateWindow(window);

  MSG message{};
  while (GetMessageW(&message, nullptr, 0, 0) > 0) {
    if (!IsDialogMessageW(window, &message)) {
      TranslateMessage(&message);
      DispatchMessageW(&message);
    }
  }
  return static_cast<int>(message.wParam);
}
