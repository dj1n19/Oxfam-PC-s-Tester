#include "core/CrashNote.h"

#include <atomic>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <string>
#include <QDir>

#include <windows.h>

// Two ways out of a Windows program without a message:
// - a structured exception (access violation 0xC0000005, stack overflow...):
//   SetUnhandledExceptionFilter. The filter returns EXCEPTION_CONTINUE_SEARCH,
//   so Windows still records it in the Event Viewer (Application, ID 1000).
// - abort() (qFatal, uncaught C++ exception): the SIGABRT handler.
// Not caught: a "fast fail" (0xC0000409, the compiler's buffer overrun check)
// goes straight to Windows: only the Event Viewer has it.

namespace {

constexpr int kMaxFrames = 48;
std::wstring s_folder;            // set once at install, only read afterwards
DWORD s_mainThread = 0;
std::atomic_flag s_writing = ATOMIC_FLAG_INIT;   // two threads crashing: one note
char s_text[16384];               // static: a stack overflow leaves no stack for it

const char* exceptionName(DWORD code)
{
    switch (code) {
    case EXCEPTION_ACCESS_VIOLATION:     return "access violation (invalid memory access)";
    case EXCEPTION_STACK_OVERFLOW:       return "stack overflow";
    case EXCEPTION_ILLEGAL_INSTRUCTION:  return "illegal instruction";
    case EXCEPTION_INT_DIVIDE_BY_ZERO:   return "integer divide by zero";
    case EXCEPTION_IN_PAGE_ERROR:        return "in-page error (file on the USB stick unreadable?)";
    case EXCEPTION_PRIV_INSTRUCTION:     return "privileged instruction";
    case 0xE06D7363:                     return "C++ exception (MSVC)";
    default:                             return "other";
    }
}

// "Qt6Core.dll + 0x1a2b3c": with the module's exact version, the offset gives
// the function. No module = the address alone (code outside any DLL).
void appendWhere(std::size_t& used, const void* address)
{
    HMODULE module = nullptr;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           static_cast<LPCWSTR>(address), &module)) {
        wchar_t path[MAX_PATH];
        const DWORD length = GetModuleFileNameW(module, path, MAX_PATH);
        const wchar_t* name = path;
        for (DWORD i = 0; i < length; ++i) {
            if (path[i] == L'\\')
                name = path + i + 1;
        }
        char utf8[MAX_PATH * 3];
        if (length == 0 || WideCharToMultiByte(CP_UTF8, 0, name, -1, utf8, sizeof utf8, nullptr, nullptr) == 0)
            std::snprintf(utf8, sizeof utf8, "?");
        const auto offset = reinterpret_cast<std::uintptr_t>(address) - reinterpret_cast<std::uintptr_t>(module);
        crashAppend(s_text, sizeof s_text, used, "%s + 0x%llx", utf8, static_cast<unsigned long long>(offset));
    } else {
        crashAppend(s_text, sizeof s_text, used, "%p (no module)", address);
    }
}

void appendStack(std::size_t& used)
{
    void* frames[kMaxFrames];
    const USHORT count = CaptureStackBackTrace(0, kMaxFrames, frames, nullptr);
    crashAppend(s_text, sizeof s_text, used, "Call stack (most recent first, the crash handler included):\r\n");
    for (USHORT i = 0; i < count; ++i) {
        crashAppend(s_text, sizeof s_text, used, "  %2u  ", static_cast<unsigned>(i));
        appendWhere(used, frames[i]);
        crashAppend(s_text, sizeof s_text, used, "\r\n");
    }
}

void writeNote(std::size_t used)
{
    SYSTEMTIME now{};
    GetLocalTime(&now);
    wchar_t path[MAX_PATH * 2];
    _snwprintf_s(path, MAX_PATH * 2, _TRUNCATE, L"%ls\\%04u-%02u-%02u %02u-%02u-%02u CRASH.txt", s_folder.c_str(), now.wYear,
                 now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond);
    CreateDirectoryW(s_folder.c_str(), nullptr);   // fails harmlessly if it exists
    const HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return;
    DWORD written = 0;
    WriteFile(file, s_text, static_cast<DWORD>(used), &written, nullptr);
    CloseHandle(file);
}

void appendThread(std::size_t& used)
{
    crashAppend(s_text, sizeof s_text, used, "Thread: %s\r\n",
                GetCurrentThreadId() == s_mainThread ? "the main (window) thread" : "another thread (a test's worker)");
}

LONG WINAPI onException(EXCEPTION_POINTERS* info)
{
    if (s_writing.test_and_set())
        return EXCEPTION_CONTINUE_SEARCH;
    const EXCEPTION_RECORD& record = *info->ExceptionRecord;
    std::size_t used = 0;
    crashAppendHeader(s_text, sizeof s_text, used, "\r\n");
    crashAppend(s_text, sizeof s_text, used, "Exception: 0x%08lX (%s)\r\n", static_cast<unsigned long>(record.ExceptionCode),
                exceptionName(record.ExceptionCode));
    if (record.ExceptionCode == EXCEPTION_ACCESS_VIOLATION && record.NumberParameters >= 2) {
        const ULONG_PTR kind = record.ExceptionInformation[0];
        crashAppend(s_text, sizeof s_text, used, "Access: %s address 0x%llx\r\n",
                    kind == 0 ? "read at" : kind == 1 ? "write at" : "execute at",
                    static_cast<unsigned long long>(record.ExceptionInformation[1]));
    }
    crashAppend(s_text, sizeof s_text, used, "At: ");
    appendWhere(used, record.ExceptionAddress);
    crashAppend(s_text, sizeof s_text, used, "\r\n");
    appendThread(used);
    appendStack(used);
    writeNote(used);
    return EXCEPTION_CONTINUE_SEARCH;   // Windows goes on: Event Viewer, end of the program
}

void onAbort(int /*sig*/)
{
    if (s_writing.test_and_set())
        return;
    std::size_t used = 0;
    crashAppendHeader(s_text, sizeof s_text, used, "\r\n");
    crashAppend(s_text, sizeof s_text, used, "abort() called (Qt fatal error or uncaught C++ exception)\r\n");
    appendThread(used);
    appendStack(used);
    writeNote(used);
    // Returning lets abort() end the program as before.
}

} // namespace

void installCrashNote(const QString& logsFolder)
{
    installCrashNoteCommon();
    s_folder = QDir::toNativeSeparators(logsFolder).toStdWString();
    s_mainThread = GetCurrentThreadId();
    SetUnhandledExceptionFilter(onException);
    std::signal(SIGABRT, onAbort);
}
