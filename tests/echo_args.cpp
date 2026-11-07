// Test helper for RunCommand: writes each of its arguments (not the program name) as
// UTF-8, each followed by a NUL, to the file named by ECHO_ARGS_OUT, then exits with
// ECHO_ARGS_EXIT_CODE (0 if unset).
#ifdef _WIN32
// The plain CRT calls are fine for a test helper.
#define _CRT_SECURE_NO_WARNINGS
#endif
#include <cstdio>
#include <cstdlib>
#include <string>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

static std::string WideToUtf8(const wchar_t* wide)
{
    const int length = ::WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
    std::string utf8(length, '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, wide, -1, utf8.data(), length, nullptr, nullptr);
    utf8.pop_back(); // the terminator
    return utf8;
}

// wmain so the arguments come from the C runtime's own parsing, like any real program.
int wmain(int argc, wchar_t* argv[])
{
    const wchar_t* out_path = _wgetenv(L"ECHO_ARGS_OUT");
    FILE* out = out_path ? _wfopen(out_path, L"wb") : nullptr;
#else
int main(int argc, char* argv[])
{
    const char* out_path = std::getenv("ECHO_ARGS_OUT");
    FILE* out = out_path ? std::fopen(out_path, "wb") : nullptr;
#endif
    if (!out)
    {
        return 100;
    }
    for (int i = 1; i < argc; ++i)
    {
#ifdef _WIN32
        const std::string argument = WideToUtf8(argv[i]);
#else
        const std::string argument = argv[i];
#endif
        std::fwrite(argument.data(), 1, argument.size(), out);
        std::fputc('\0', out);
    }
    std::fclose(out);

    const char* exit_code = std::getenv("ECHO_ARGS_EXIT_CODE");
    return exit_code ? std::atoi(exit_code) : 0;
}
