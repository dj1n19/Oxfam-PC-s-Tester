#include "core/CrashNote.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <QByteArray>
#include <QtGlobal>

namespace {

// Fixed buffers, filled in advance: the crash handler only reads them.
char s_context[256] = "";
char s_fatal[1024] = "";
QtMessageHandler s_previousHandler = nullptr;

void copyTo(char* target, std::size_t size, const QByteArray& text)
{
    const std::size_t n = std::min(size - 1, static_cast<std::size_t>(text.size()));
    std::memcpy(target, text.constData(), n);
    target[n] = '\0';
}

// qFatal() (e.g. "QThread: Destroyed while thread is still running") calls
// the message handler, then abort(): keep its text for the note.
void onQtMessage(QtMsgType type, const QMessageLogContext& context, const QString& message)
{
    if (type == QtFatalMsg)
        copyTo(s_fatal, sizeof s_fatal, "Qt fatal: " + message.toUtf8());
    if (s_previousHandler)
        s_previousHandler(type, context, message);
}

// An uncaught C++ exception (also one thrown in a QtConcurrent worker, then
// rethrown by QFuture::result() in a slot) ends in std::terminate().
void onTerminate()
{
    QByteArray text = "Uncaught C++ exception";
    if (const std::exception_ptr e = std::current_exception()) {
        try {
            std::rethrow_exception(e);
        } catch (const std::exception& ex) {
            text += QByteArray(": ") + ex.what();
        } catch (...) {
            text += " (not a std::exception)";
        }
    }
    copyTo(s_fatal, sizeof s_fatal, text);
    std::abort();   // -> the SIGABRT handler writes the note
}

} // namespace

void setCrashContext(const QString& text)
{
    copyTo(s_context, sizeof s_context, text.toUtf8());
}

void installCrashNoteCommon()
{
    s_previousHandler = qInstallMessageHandler(onQtMessage);
    std::set_terminate(onTerminate);
}

void crashAppend(char* buffer, std::size_t size, std::size_t& used, const char* format, ...)
{
    if (used + 1 >= size)
        return;
    va_list args;
    va_start(args, format);
    const int n = std::vsnprintf(buffer + used, size - used, format, args);
    va_end(args);
    if (n > 0)
        used = std::min(size - 1, used + static_cast<std::size_t>(n));
}

void crashAppendHeader(char* buffer, std::size_t size, std::size_t& used, const char* newline)
{
    crashAppend(buffer, size, used, "Lazy Djin's Tester %s stopped by a crash.%s", OX_APP_VERSION, newline);
    crashAppend(buffer, size, used, "Test running: %s%s", s_context[0] ? s_context : "none", newline);
    if (s_fatal[0])
        crashAppend(buffer, size, used, "%s%s", s_fatal, newline);
}
