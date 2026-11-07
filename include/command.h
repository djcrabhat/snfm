#pragma once

#include <string>
#include <utility>
#include <vector>

using TemplateVariables = std::vector<std::pair<std::string, std::string>>;

// The variables send_file fills in for post_upload_command, e.g. "destination_name".
const std::vector<std::string>& PostUploadVariableNames();

// Replaces every "{name}" in template_string with the matching value from variables.
// Values go in exactly as they are and aren't scanned again.  Braces that aren't a
// known "{name}" - like a shell "{ ...; }" group - are left as literal text.
std::string ExpandTemplate(const std::string& template_string, const TemplateVariables& variables);

// Returns the "{identifier}" placeholders in template_string that aren't in known_names.
std::vector<std::string> FindUnknownPlaceholders(const std::string& template_string,
    const std::vector<std::string>& known_names);

// True if Windows would run program through cmd.exe, i.e. it's a .bat or .cmd file.
// Ignores case, and the trailing dots and spaces Windows drops from file names.
bool IsBatchFile(const std::string& program);

#ifdef _WIN32
std::wstring Utf8ToWide(const std::string& utf8);

// Joins arguments into a command line that the MSVC runtime (and CommandLineToArgvW)
// split back into exactly the same arguments.  The program name, arguments[0], must
// not contain a double quote - Windows file names can't anyway.
std::wstring BuildWindowsCommandLine(const std::vector<std::wstring>& arguments);
#endif

struct CommandResult
{
    // False if the program couldn't be started at all; error says why.
    bool started = false;
    std::string error;
    int exit_code = 0;
    // Unix only: the process was killed by a signal rather than exiting.
    bool signaled = false;
    int signal = 0;
};

// Runs arguments[0] with the rest as its arguments and waits for it to finish.  No
// shell is involved, so nothing in the arguments is interpreted.  The program is
// looked up on PATH the usual way for the OS.  On Windows, batch files are refused,
// because they're always run through cmd.exe, which would interpret the arguments.
CommandResult RunCommand(const std::vector<std::string>& arguments);

// Expands the variables into each part of command_template separately and runs the
// result, reporting anything that goes wrong on stdout.
void RunPostUploadCommand(const std::vector<std::string>& command_template,
    const TemplateVariables& variables);
