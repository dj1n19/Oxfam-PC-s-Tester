#include "core/CrashNote.h"

#include <csignal>
#include <ctime>
#include <QFile>

#include <execinfo.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {

constexpr int kMaxFrames = 64;
char s_folder[4096] = "";
pid_t s_mainThread = 0;
char s_text[8192];

const char* signalName(int sig)
{
    switch (sig) {
    case SIGSEGV: return "SIGSEGV, invalid memory access";
    case SIGBUS:  return "SIGBUS, invalid memory access";
    case SIGFPE:  return "SIGFPE, arithmetic error";
    case SIGILL:  return "SIGILL, illegal instruction";
    case SIGABRT: return "SIGABRT, abort(): Qt fatal error or uncaught exception";
    default:      return "unknown signal";
    }
}

void onSignal(int sig, siginfo_t* info, void* /*context*/)
{
    std::size_t used = 0;
    crashAppendHeader(s_text, sizeof s_text, used, "\n");
    crashAppend(s_text, sizeof s_text, used, "Signal: %d (%s), address %p\n", sig, signalName(sig), info->si_addr);
    crashAppend(s_text, sizeof s_text, used, "Thread: %s\n",
                gettid() == s_mainThread ? "the main (window) thread" : "another thread (a test's worker)");

    const time_t now = time(nullptr);
    tm local{};
    localtime_r(&now, &local);
    char path[4200];
    std::snprintf(path, sizeof path, "%s/%04d-%02d-%02d %02d-%02d-%02d CRASH.txt", s_folder, local.tm_year + 1900,
                  local.tm_mon + 1, local.tm_mday, local.tm_hour, local.tm_min, local.tm_sec);
    mkdir(s_folder, 0755);   // fails harmlessly if it exists
    const int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0) {
        if (write(fd, s_text, used) >= 0) {
            static const char title[] = "Call stack (most recent first):\n";
            if (write(fd, title, sizeof title - 1) >= 0) {
                void* frames[kMaxFrames];
                // Writes each frame straight to the file, no allocation.
                backtrace_symbols_fd(frames, backtrace(frames, kMaxFrames), fd);
            }
        }
        close(fd);
    }
    // SA_RESETHAND put the default action back: crash as before (core dump).
    raise(sig);
}

} // namespace

void installCrashNote(const QString& logsFolder)
{
    installCrashNoteCommon();
    const QByteArray folder = QFile::encodeName(logsFolder);
    std::snprintf(s_folder, sizeof s_folder, "%s", folder.constData());
    s_mainThread = gettid();
    // The first backtrace() loads libgcc (allocates): do it now, not in the crash.
    void* frames[1];
    backtrace(frames, 1);

    struct sigaction action{};
    action.sa_sigaction = onSignal;
    action.sa_flags = SA_SIGINFO | SA_RESETHAND;
    sigemptyset(&action.sa_mask);
    for (const int sig : {SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGABRT})
        sigaction(sig, &action, nullptr);
}
