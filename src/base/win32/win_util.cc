// Copyright 2010-2021, Google Inc.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//     * Neither the name of Google Inc. nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include "base/win32/win_util.h"

// skip all unless _WIN32
#ifdef _WIN32

#include <aux_ulib.h>
#include <psapi.h>
#include <shellapi.h>
#include <stringapiset.h>
#include <winternl.h>

#define _ATL_NO_AUTOMATIC_NAMESPACE
#define _WTL_NO_AUTOMATIC_NAMESPACE
#include <atlbase.h>

#include <clocale>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

#include "base/logging.h"
#include "base/system_util.h"
#include "base/win32/scoped_handle.h"
#include "base/win32/wide_char.h"
#include "absl/base/call_once.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"

// Disable StrCat macro to use absl::StrCat.
#ifdef StrCat
#undef StrCat
#endif  // StrCat

namespace mozc {
namespace {

absl::once_flag g_aux_lib_initialized;

void CallAuxUlibInitialize() { ::AuxUlibInitialize(); }

bool EqualLuid(const LUID &L1, const LUID &L2) {
  return (L1.LowPart == L2.LowPart && L1.HighPart == L2.HighPart);
}

bool IsProcessSandboxedImpl() {
  bool is_restricted = false;
  if (!WinUtil::IsProcessRestricted(::GetCurrentProcess(), &is_restricted)) {
    return true;
  }
  if (is_restricted) {
    return true;
  }

  bool in_appcontainer = false;
  if (!WinUtil::IsProcessInAppContainer(::GetCurrentProcess(),
                                        &in_appcontainer)) {
    return true;
  }

  return in_appcontainer;
}

}  // namespace

HMODULE WinUtil::LoadSystemLibrary(const std::wstring &base_filename) {
  std::wstring fullpath = SystemUtil::GetSystemDir();
  fullpath += L"\\";
  fullpath += base_filename;

  const HMODULE module = ::LoadLibraryExW(fullpath.c_str(), nullptr,
                                          LOAD_WITH_ALTERED_SEARCH_PATH);
  if (nullptr == module) {
    const int last_error = ::GetLastError();
    DLOG(WARNING) << "LoadLibraryEx failed."
                  << " fullpath = " << fullpath.c_str()
                  << " error = " << last_error;
  }
  return module;
}

HMODULE WinUtil::LoadMozcLibrary(const std::wstring &base_filename) {
  std::wstring fullpath = win32::Utf8ToWide(SystemUtil::GetServerDirectory());
  fullpath += L"\\";
  fullpath += base_filename;

  const HMODULE module = ::LoadLibraryExW(fullpath.c_str(), nullptr,
                                          LOAD_WITH_ALTERED_SEARCH_PATH);
  if (nullptr == module) {
    const int last_error = ::GetLastError();
    DLOG(WARNING) << "LoadLibraryEx failed."
                  << " fullpath = " << fullpath.c_str()
                  << " error = " << last_error;
  }
  return module;
}

HMODULE WinUtil::GetSystemModuleHandle(const std::wstring &base_filename) {
  std::wstring fullpath = SystemUtil::GetSystemDir();
  fullpath += L"\\";
  fullpath += base_filename;

  HMODULE module = nullptr;
  if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                         fullpath.c_str(), &module) == FALSE) {
    const int last_error = ::GetLastError();
    DLOG(WARNING) << "GetModuleHandleExW failed."
                  << " fullpath = " << fullpath.c_str()
                  << " error = " << last_error;
  }
  return module;
}

HMODULE WinUtil::GetSystemModuleHandleAndIncrementRefCount(
    const std::wstring &base_filename) {
  std::wstring fullpath = SystemUtil::GetSystemDir();
  fullpath += L"\\";
  fullpath += base_filename;

  HMODULE module = nullptr;
  if (GetModuleHandleExW(0, fullpath.c_str(), &module) == FALSE) {
    const int last_error = ::GetLastError();
    DLOG(WARNING) << "GetModuleHandleExW failed."
                  << " fullpath = " << fullpath.c_str()
                  << " error = " << last_error;
  }
  return module;
}

bool WinUtil::IsDLLSynchronizationHeld(bool *lock_status) {
  absl::call_once(g_aux_lib_initialized, &CallAuxUlibInitialize);

  if (lock_status == nullptr) {
    return false;
  }

  BOOL synchronization_held = FALSE;
  const BOOL result = ::AuxUlibIsDLLSynchronizationHeld(&synchronization_held);
  if (!result) {
    const int error = ::GetLastError();
    DLOG(ERROR) << "AuxUlibIsDLLSynchronizationHeld failed. error = " << error;
    return false;
  }
  *lock_status = (synchronization_held != FALSE);
  return true;
}

uint32_t WinUtil::EncodeWindowHandle(HWND window_handle) {
  return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(window_handle));
}

HWND WinUtil::DecodeWindowHandle(uint32_t window_handle_value) {
  return reinterpret_cast<HWND>(static_cast<uintptr_t>(window_handle_value));
}

bool WinUtil::SystemEqualString(const std::wstring_view lhs,
                                const std::wstring_view rhs, bool ignore_case) {
  const int compare_result =
      ::CompareStringOrdinal(lhs.data(), lhs.size(), rhs.data(), rhs.size(),
                             (ignore_case ? TRUE : FALSE));

  return compare_result == CSTR_EQUAL;
}

bool WinUtil::IsServiceUser(HANDLE hToken, bool *is_service) {
  if (is_service == nullptr) {
    return false;
  }

  TOKEN_STATISTICS ts;
  DWORD dwSize = 0;
  // Use token logon LUID instead of user SID, for brevity and safety
  if (!::GetTokenInformation(hToken, TokenStatistics, (LPVOID)&ts, sizeof(ts),
                             &dwSize)) {
    return false;
  }

  // Compare LUID
  const LUID SystemLuid = SYSTEM_LUID;
  const LUID LocalServiceLuid = LOCALSERVICE_LUID;
  const LUID NetworkServiceLuid = NETWORKSERVICE_LUID;
  if (EqualLuid(SystemLuid, ts.AuthenticationId) ||
      EqualLuid(LocalServiceLuid, ts.AuthenticationId) ||
      EqualLuid(NetworkServiceLuid, ts.AuthenticationId)) {
    *is_service = true;
    return true;
  }

  // Not a service account
  *is_service = false;
  return true;
}

bool WinUtil::IsServiceProcess(bool *is_service) {
  if (is_service == nullptr) {
    return false;
  }

  // Session 0 is dedicated to services
  DWORD dwSessionId = 0;
  if (!::ProcessIdToSessionId(::GetCurrentProcessId(), &dwSessionId) ||
      (dwSessionId == 0)) {
    *is_service = true;
    return true;
  }

  // Get process token
  HANDLE hProcessToken = nullptr;
  if (!::OpenProcessToken(::GetCurrentProcess(),
                          TOKEN_QUERY | TOKEN_QUERY_SOURCE, &hProcessToken)) {
    return false;
  }

  ScopedHandle process_token(hProcessToken);

  // Process token is one for a service account.
  if (!IsServiceUser(process_token.get(), is_service)) {
    return false;
  }

  return true;
}

bool WinUtil::IsServiceThread(bool *is_service) {
  if (is_service == nullptr) {
    return false;
  }

  // Get thread token (if any)
  HANDLE hThreadToken = nullptr;
  if (!::OpenThreadToken(::GetCurrentThread(), TOKEN_QUERY, TRUE,
                         &hThreadToken) &&
      ERROR_NO_TOKEN != ::GetLastError()) {
    return false;
  }

  if (hThreadToken == nullptr) {
    // No thread token.
    *is_service = false;
    return true;
  }

  ScopedHandle thread_token(hThreadToken);

  // Check if the thread token (if any) is one for a service account.
  if (!IsServiceUser(thread_token.get(), is_service)) {
    return false;
  }
  return true;
}

bool WinUtil::IsServiceAccount(bool *is_service) {
  if (is_service == nullptr) {
    return false;
  }

  bool is_service_process = false;
  if (!WinUtil::IsServiceProcess(&is_service_process)) {
    DLOG(ERROR) << "WinUtil::IsServiceProcess failed.";
    return false;
  }

  if (is_service_process) {
    *is_service = true;
    return true;
  }

  // Process token is not one for service.
  // Check thread token just in case.
  bool is_service_thread = false;
  if (!WinUtil::IsServiceThread(&is_service_thread)) {
    DLOG(ERROR) << "WinUtil::IsServiceThread failed.";
    return false;
  }

  if (is_service_thread) {
    *is_service = true;
    return true;
  }

  *is_service = false;
  return true;
}

bool WinUtil::IsProcessImmersive(HANDLE process_handle, bool *is_immersive) {
  if (is_immersive == nullptr) {
    return false;
  }
  *is_immersive = false;
  // ImmersiveMode is supported only in Windows8 and later.
  if (!SystemUtil::IsWindows8OrLater()) {
    return true;
  }

  const HMODULE module = WinUtil::GetSystemModuleHandle(L"user32.dll");
  if (module == nullptr) {
    return false;
  }

  typedef BOOL(WINAPI * IsImmersiveProcessFunc)(HANDLE process);
  IsImmersiveProcessFunc is_immersive_process =
      reinterpret_cast<IsImmersiveProcessFunc>(
          ::GetProcAddress(module, "IsImmersiveProcess"));
  if (is_immersive_process == nullptr) {
    return false;
  }

  *is_immersive = !!is_immersive_process(process_handle);
  return true;
}

bool WinUtil::IsProcessRestricted(HANDLE process_handle, bool *is_restricted) {
  if (is_restricted == nullptr) {
    return false;
  }
  *is_restricted = false;

  HANDLE token = nullptr;
  if (!::OpenProcessToken(process_handle, TOKEN_QUERY, &token)) {
    return false;
  }

  ScopedHandle process_token(token);
  ::SetLastError(NOERROR);
  if (::IsTokenRestricted(process_token.get()) == FALSE) {
    const DWORD error = ::GetLastError();
    if (error != NOERROR) {
      return false;
    }
  } else {
    *is_restricted = true;
  }
  return true;
}

bool WinUtil::IsProcessInAppContainer(HANDLE process_handle,
                                      bool *in_appcontainer) {
  if (in_appcontainer == nullptr) {
    return false;
  }
  *in_appcontainer = false;

  // AppContainer is supported only in Windows8 and later.
  if (!SystemUtil::IsWindows8OrLater()) {
    return true;
  }

  HANDLE token = nullptr;
  if (!::OpenProcessToken(process_handle, TOKEN_QUERY | TOKEN_QUERY_SOURCE,
                          &token)) {
    return false;
  }

  // TokenIsAppContainer is defined only in Windows SDK 8.0 and later.
  ScopedHandle process_token(token);
  const TOKEN_INFORMATION_CLASS kTokenIsAppContainer =
      static_cast<TOKEN_INFORMATION_CLASS>(29);  // TokenIsAppContainer
#if defined(_WIN32_WINNT_WIN8)
  static_assert(kTokenIsAppContainer == TokenIsAppContainer,
                "Checking |kTokenIsAppContainer| has correct value.");
#endif  // _WIN32_WINNT_WIN8
  DWORD returned_size = 0;
  DWORD retval = 0;
  if (!GetTokenInformation(process_token.get(), kTokenIsAppContainer, &retval,
                           sizeof(retval), &returned_size)) {
    return false;
  }
  if (returned_size != sizeof(retval)) {
    return false;
  }

  *in_appcontainer = (retval != 0);
  return true;
}

bool WinUtil::GetFileSystemInfoFromPath(const std::wstring &path,
                                        BY_HANDLE_FILE_INFORMATION *info) {
  // no read access is required.
  ScopedHandle handle(::CreateFileW(
      path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
      nullptr, OPEN_EXISTING,
      FILE_FLAG_BACKUP_SEMANTICS | FILE_ATTRIBUTE_NORMAL, nullptr));

  // Caveats: handle.get() returns nullptr when it is initialized with
  //     INVALID_HANDLE_VALUE.
  if (handle.get() == nullptr) {
    return false;
  }
  return !!::GetFileInformationByHandle(handle.get(), info);
}

bool WinUtil::AreEqualFileSystemObject(const std::wstring &left_path,
                                       const std::wstring &right_path) {
  BY_HANDLE_FILE_INFORMATION left_info = {};
  if (!GetFileSystemInfoFromPath(left_path, &left_info)) {
    return false;
  }
  BY_HANDLE_FILE_INFORMATION right_info = {};
  if (!GetFileSystemInfoFromPath(right_path, &right_info)) {
    return false;
  }
  return (left_info.nFileIndexLow == right_info.nFileIndexLow) &&
         (left_info.nFileIndexHigh == right_info.nFileIndexHigh);
}

bool WinUtil::GetNtPath(const std::wstring &dos_path, std::wstring *nt_path) {
  if (nt_path == nullptr) {
    return false;
  }

  nt_path->clear();

  ScopedHandle file_handle(::CreateFileW(
      dos_path.c_str(), 0,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
      OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_ATTRIBUTE_NORMAL,
      nullptr));
  if (file_handle.get() == nullptr) {
    // Caveats: |file_handle.get()| becomes nullptr instead of
    // INVALID_HANDLE_VALUE when failure.
    return false;
  }

  constexpr size_t kMaxPath = 4096;
  std::unique_ptr<wchar_t[]> ntpath_buffer(new wchar_t[kMaxPath]);
  const DWORD copied_len_without_null = ::GetFinalPathNameByHandleW(
      file_handle.get(), ntpath_buffer.get(), kMaxPath,
      FILE_NAME_NORMALIZED | VOLUME_NAME_NT);
  if (copied_len_without_null == 0 || copied_len_without_null > kMaxPath) {
    const DWORD error = ::GetLastError();
    VLOG(1) << "GetFinalPathNameByHandleW() failed: " << error;
    return false;
  }

  nt_path->assign(ntpath_buffer.get(), copied_len_without_null);
  return true;
}

bool WinUtil::GetProcessInitialNtPath(DWORD pid, std::wstring *nt_path) {
  if (nt_path == nullptr) {
    return false;
  }
  nt_path->clear();

  ScopedHandle process_handle(
      ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid));

  if (process_handle.get() == nullptr) {
    VLOG(1) << "OpenProcess() failed: " << ::GetLastError();
    return false;
  }

  constexpr size_t kMaxPath = 4096;
  std::unique_ptr<wchar_t[]> ntpath_buffer(new wchar_t[kMaxPath]);
  const DWORD copied_len_without_null = ::GetProcessImageFileNameW(
      process_handle.get(), ntpath_buffer.get(), kMaxPath);
  if (copied_len_without_null == 0 || copied_len_without_null > kMaxPath) {
    const DWORD error = ::GetLastError();
    VLOG(1) << "GetProcessImageFileNameW() failed: " << error;
    return false;
  }

  nt_path->assign(ntpath_buffer.get(), copied_len_without_null);
  return true;
}

// SPI_GETTHREADLOCALINPUTSETTINGS is available on Windows 8 SDK and later.
#ifndef SPI_GETTHREADLOCALINPUTSETTINGS
#define SPI_GETTHREADLOCALINPUTSETTINGS 0x104E
#endif  // SPI_GETTHREADLOCALINPUTSETTINGS

bool WinUtil::IsPerUserInputSettingsEnabled() {
  if (!SystemUtil::IsWindows8OrLater()) {
    // Windows 7 and below does not support per-user input mode.
    return false;
  }
  BOOL is_thread_local = FALSE;
  if (::SystemParametersInfo(SPI_GETTHREADLOCALINPUTSETTINGS, 0,
                             reinterpret_cast<void *>(&is_thread_local),
                             0) == FALSE) {
    return false;
  }
  return !is_thread_local;
}

bool WinUtil::IsProcessSandboxed() {
  // Thread safety is not required.
  static bool sandboxed = IsProcessSandboxedImpl();
  return sandboxed;
}

bool WinUtil::ShellExecuteInSystemDir(const wchar_t *verb, const wchar_t *file,
                                      const wchar_t *parameters) {
  const auto result =
      static_cast<uint32_t>(reinterpret_cast<uintptr_t>(::ShellExecuteW(
          0, verb, file, parameters, SystemUtil::GetSystemDir(), SW_SHOW)));
  LOG_IF(ERROR, result <= 32)
      << "ShellExecute failed."
      << ", error:" << result << ", verb: " << verb << ", file: " << file
      << ", parameters: " << parameters;
  return result > 32;
}

}  // namespace mozc

#endif  // _WIN32
