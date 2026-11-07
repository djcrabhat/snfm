#include "command.h"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <iostream>
#include <string_view>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <cerrno>
#include <spawn.h>
#include <sys/wait.h>
extern char** environ;
#endif

const std::vector<std::string>& PostUploadVariableNames()
{
    static const std::vector<std::string> names = {
        "rule_name",
        "destination_name",
        "destination_path",
        "destination_rom_name",
        "destination_file",
        "source_path",
        "source_name",
    };
    return names;
}

namespace
{
bool IsPlaceholderName(std::string_view name)
{
    return !name.empty() && std::all_of(name.begin(), name.end(),
        [](unsigned char c) { return std::isalnum(c) || c == '_'; });
}

// Walks template_string, handing literal text to on_text and the name of each
// "{identifier}" to on_placeholder, which returns false to have it kept as literal text.
template <typename OnText, typename OnPlaceholder>
void ScanTemplate(std::string_view template_string, OnText on_text, OnPlaceholder on_placeholder)
{
    size_t pos = 0;
    while (pos < template_string.size())
    {
        const size_t open = template_string.find('{', pos);
        if (open == std::string_view::npos)
        {
            on_text(template_string.substr(pos));
            return;
        }
        on_text(template_string.substr(pos, open - pos));

        const size_t close = template_string.find('}', open);
        if (close != std::string_view::npos)
        {
            const std::string_view name = template_string.substr(open + 1, close - open - 1);
            if (IsPlaceholderName(name) && on_placeholder(name))
            {
                pos = close + 1;
                continue;
            }
        }
        // Not a placeholder of ours - keep the brace as literal text and carry on scanning
        // just after it, so that placeholders inside something like a shell brace group
        // ("{ echo {destination_name}; }") still get expanded.
        on_text("{");
        pos = open + 1;
    }
}
}

std::string ExpandTemplate(const std::string& template_string, const TemplateVariables& variables)
{
    std::string result;
    ScanTemplate(template_string,
        [&result](std::string_view text) { result.append(text); },
        [&result, &variables](std::string_view name)
        {
            auto variable = std::find_if(variables.begin(), variables.end(),
                [name](const auto& v) { return v.first == name; });
            if (variable == variables.end())
            {
                return false;
            }
            result.append(variable->second);
            return true;
        });
    return result;
}

std::vector<std::string> FindUnknownPlaceholders(const std::string& template_string,
    const std::vector<std::string>& known_names)
{
    std::vector<std::string> unknown;
    ScanTemplate(template_string,
        [](std::string_view) {},
        [&unknown, &known_names](std::string_view name)
        {
            if (std::find(known_names.begin(), known_names.end(), name) == known_names.end())
            {
                unknown.emplace_back(name);
            }
            return true;
        });
    return unknown;
}

bool IsBatchFile(const std::string& program)
{
    std::string name = program;
    while (!name.empty() && (name.back() == '.' || name.back() == ' '))
    {
        name.pop_back();
    }
    if (name.size() < 4)
    {
        return false;
    }
    std::string extension = name.substr(name.size() - 4);
    std::transform(extension.begin(), extension.end(), extension.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return extension == ".bat" || extension == ".cmd";
}

#ifdef _WIN32
std::wstring Utf8ToWide(const std::string& utf8)
{
    if (utf8.empty())
    {
        return {};
    }
    const int length = ::MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    std::wstring wide(length, L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), wide.data(), length);
    return wide;
}

namespace
{
// Quoting rules from "Parsing C++ command-line arguments" in the MSVC docs: backslashes
// are only special when they come right before a double quote, where each pair becomes
// one backslash and an odd one out escapes the quote.
void AppendQuotedArgument(std::wstring& command_line, const std::wstring& argument)
{
    if (!argument.empty() && argument.find_first_of(L" \t\n\v\"") == std::wstring::npos)
    {
        command_line += argument;
        return;
    }
    command_line.push_back(L'"');
    for (auto it = argument.begin();; ++it)
    {
        size_t backslashes = 0;
        while (it != argument.end() && *it == L'\\')
        {
            ++it;
            ++backslashes;
        }
        if (it == argument.end())
        {
            // Double them so the closing quote isn't escaped.
            command_line.append(backslashes * 2, L'\\');
            break;
        }
        if (*it == L'"')
        {
            command_line.append(backslashes * 2 + 1, L'\\');
        }
        else
        {
            command_line.append(backslashes, L'\\');
        }
        command_line.push_back(*it);
    }
    command_line.push_back(L'"');
}

std::string DescribeWindowsError(DWORD error)
{
    char* message = nullptr;
    const DWORD length = ::FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, error, 0, reinterpret_cast<char*>(&message), 0, nullptr);
    std::string description = length ? std::string(message, length) : "error " + std::to_string(error);
    ::LocalFree(message);
    while (!description.empty() && std::isspace(static_cast<unsigned char>(description.back())))
    {
        description.pop_back();
    }
    return description;
}
}

std::wstring BuildWindowsCommandLine(const std::vector<std::wstring>& arguments)
{
    std::wstring command_line;
    for (size_t i = 0; i < arguments.size(); ++i)
    {
        if (i == 0)
        {
            // The program name is parsed differently - no backslash escapes, it just runs
            // to the next quote - so quote it plainly.
            command_line = L"\"" + arguments[0] + L"\"";
        }
        else
        {
            command_line.push_back(L' ');
            AppendQuotedArgument(command_line, arguments[i]);
        }
    }
    return command_line;
}
#endif

CommandResult RunCommand(const std::vector<std::string>& arguments)
{
    CommandResult result;
    if (arguments.empty() || arguments[0].empty())
    {
        result.error = "no program to run";
        return result;
    }
#ifdef _WIN32
    if (IsBatchFile(arguments[0]))
    {
        result.error = "batch files (.bat/.cmd) can't be run safely, because Windows runs them through "
            "cmd.exe, which would interpret the arguments - use a PowerShell script or an .exe instead";
        return result;
    }
    if (arguments[0].find('"') != std::string::npos)
    {
        result.error = "the program name can't contain a double quote";
        return result;
    }

    std::vector<std::wstring> wide_arguments;
    wide_arguments.reserve(arguments.size());
    for (const auto& argument : arguments)
    {
        wide_arguments.push_back(Utf8ToWide(argument));
    }
    std::wstring command_line = BuildWindowsCommandLine(wide_arguments);

    STARTUPINFOW startup_info{};
    startup_info.cb = sizeof(startup_info);
    PROCESS_INFORMATION process_info{};
    if (!::CreateProcessW(nullptr, command_line.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr,
        &startup_info, &process_info))
    {
        result.error = DescribeWindowsError(::GetLastError());
        return result;
    }
    result.started = true;
    ::WaitForSingleObject(process_info.hProcess, INFINITE);
    DWORD exit_code = 0;
    ::GetExitCodeProcess(process_info.hProcess, &exit_code);
    result.exit_code = static_cast<int>(exit_code);
    ::CloseHandle(process_info.hThread);
    ::CloseHandle(process_info.hProcess);
#else
    std::vector<char*> argv;
    argv.reserve(arguments.size() + 1);
    for (const auto& argument : arguments)
    {
        argv.push_back(const_cast<char*>(argument.c_str()));
    }
    argv.push_back(nullptr);

    pid_t pid = 0;
    const int spawn_error = ::posix_spawnp(&pid, argv[0], nullptr, nullptr, argv.data(), environ);
    if (spawn_error != 0)
    {
        result.error = std::strerror(spawn_error);
        return result;
    }
    result.started = true;

    int status = 0;
    while (::waitpid(pid, &status, 0) == -1)
    {
        if (errno != EINTR)
        {
            result.error = std::strerror(errno);
            return result;
        }
    }
    if (WIFEXITED(status))
    {
        result.exit_code = WEXITSTATUS(status);
    }
    else if (WIFSIGNALED(status))
    {
        result.signaled = true;
        result.signal = WTERMSIG(status);
    }
#endif
    return result;
}

void RunPostUploadCommand(const std::vector<std::string>& command_template,
    const TemplateVariables& variables)
{
    std::vector<std::string> command;
    command.reserve(command_template.size());
    std::cout << "Running post upload command:";
    for (const auto& part : command_template)
    {
        command.push_back(ExpandTemplate(part, variables));
        std::cout << " [" << command.back() << "]";
    }
    std::cout << std::endl;

    const CommandResult result = RunCommand(command);
    if (!result.started)
    {
        std::cout << "Couldn't run the post upload command: " << result.error << std::endl;
    }
    else if (result.signaled)
    {
        std::cout << "Post upload command was killed by signal " << result.signal << std::endl;
    }
    else if (result.exit_code != 0)
    {
        std::cout << "Post upload command exited with code " << result.exit_code << std::endl;
    }
}
