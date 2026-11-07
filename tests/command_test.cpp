#include "command.h"
#include <gtest/gtest.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <random>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#else
#include <cstdlib>
#include <csignal>
#endif

namespace
{
// Strings that a shell, cmd.exe or the Windows command line parser would all like to
// do something with.  Every one of them has to come out the other side unchanged.
const std::vector<std::string> kAwkwardArguments = {
    "",
    " ",
    "plain",
    "two words",
    "  leading and trailing  ",
    "\"",
    "\"\"",
    "\"quoted\"",
    "say \"hi\" there",
    "\\",
    "\\\\",
    "trailing\\",
    "trailing\\\\",
    "\\\"",
    "a\\\\\"b",
    "\\\\server\\share\\",
    "C:\\My ROMs\\game.sfc",
    "C:\\My ROMs\\",
    "tab\there",
    "new\nline",
    "^&|<>()%!",
    "%PATH%",
    "!PATH!",
    "$HOME",
    "${HOME}",
    "`echo hi`",
    "$(echo hi)",
    "'single'",
    "it's",
    "; echo injected",
    "&& echo injected",
    "| echo injected",
    "*?[a]",
    "~",
    "-",
    "--flag=\"x y\"",
    "{destination_name}",
    "\xC3\xA9 \xE3\x83\x8B \xF0\x9F\x8E\xAE",
};

std::string PathToUtf8(const std::filesystem::path& path)
{
    const std::u8string utf8 = path.u8string();
    return std::string(utf8.begin(), utf8.end());
}

void SetEnv(const std::string& name, const std::string& value)
{
#ifdef _WIN32
    ::SetEnvironmentVariableW(Utf8ToWide(name).c_str(), Utf8ToWide(value).c_str());
#else
    ::setenv(name.c_str(), value.c_str(), 1);
#endif
}

void UnsetEnv(const std::string& name)
{
#ifdef _WIN32
    ::SetEnvironmentVariableW(Utf8ToWide(name).c_str(), nullptr);
#else
    ::unsetenv(name.c_str());
#endif
}
}

// ---------------------------------------------------------------------------
// ExpandTemplate / FindUnknownPlaceholders

TEST(ExpandTemplate, ReplacesKnownVariables)
{
    EXPECT_EQ(ExpandTemplate("{a}-{b}", { {"a", "1"}, {"b", "2"} }), "1-2");
    EXPECT_EQ(ExpandTemplate("{a}{a}", { {"a", "x"} }), "xx");
    EXPECT_EQ(ExpandTemplate("--name={a}!", { {"a", "x"} }), "--name=x!");
    EXPECT_EQ(ExpandTemplate("", { {"a", "x"} }), "");
    EXPECT_EQ(ExpandTemplate("no placeholders", { {"a", "x"} }), "no placeholders");
}

TEST(ExpandTemplate, InsertsValuesVerbatim)
{
    for (const auto& value : kAwkwardArguments)
    {
        EXPECT_EQ(ExpandTemplate("{a}", { {"a", value} }), value);
        EXPECT_EQ(ExpandTemplate("<{a}>", { {"a", value} }), "<" + value + ">");
    }
}

TEST(ExpandTemplate, DoesNotExpandInsideValues)
{
    EXPECT_EQ(ExpandTemplate("{a}", { {"a", "{b}"}, {"b", "oops"} }), "{b}");
    EXPECT_EQ(ExpandTemplate("{a}{b}", { {"a", "{"}, {"b", "b}"} }), "{b}");
}

TEST(ExpandTemplate, LeavesUnknownPlaceholdersAlone)
{
    EXPECT_EQ(ExpandTemplate("{nope}", { {"a", "x"} }), "{nope}");
    EXPECT_EQ(ExpandTemplate("{nope}{a}", { {"a", "x"} }), "{nope}x");
}

TEST(ExpandTemplate, LeavesOtherBracesAlone)
{
    const TemplateVariables variables = { {"a", "x"} };
    EXPECT_EQ(ExpandTemplate("{}", variables), "{}");
    EXPECT_EQ(ExpandTemplate("{", variables), "{");
    EXPECT_EQ(ExpandTemplate("}", variables), "}");
    EXPECT_EQ(ExpandTemplate("a{b", variables), "a{b");
    EXPECT_EQ(ExpandTemplate("{a", variables), "{a");
    EXPECT_EQ(ExpandTemplate("a}", variables), "a}");
    EXPECT_EQ(ExpandTemplate("{a b}", variables), "{a b}");
    EXPECT_EQ(ExpandTemplate("{a-b}", variables), "{a-b}");
    EXPECT_EQ(ExpandTemplate("{{a}}", variables), "{x}");
    EXPECT_EQ(ExpandTemplate("{ {a} }", variables), "{ x }");
    EXPECT_EQ(ExpandTemplate("${a}", variables), "$x");
}

TEST(ExpandTemplate, ExpandsInsideShellBraceGroups)
{
    EXPECT_EQ(ExpandTemplate("{ printf '%s\\n' \"{destination_name}\"; }", { {"destination_name", "WA2"} }),
        "{ printf '%s\\n' \"WA2\"; }");
}

TEST(FindUnknownPlaceholders, ReportsOnlyUnknownIdentifiers)
{
    const std::vector<std::string> known = { "a", "b_c" };
    EXPECT_TRUE(FindUnknownPlaceholders("{a} {b_c} plain", known).empty());
    EXPECT_TRUE(FindUnknownPlaceholders("{ echo {a}; } {} {a b} {a-b} {", known).empty());
    EXPECT_EQ(FindUnknownPlaceholders("{a} {nope}", known), std::vector<std::string>{ "nope" });
    EXPECT_EQ(FindUnknownPlaceholders("{x}{y}", known), (std::vector<std::string>{ "x", "y" }));
    EXPECT_EQ(FindUnknownPlaceholders("{{x}}", known), std::vector<std::string>{ "x" });
}

TEST(PostUploadVariableNames, AreUniqueIdentifiers)
{
    const auto& names = PostUploadVariableNames();
    for (const auto& name : names)
    {
        EXPECT_TRUE(FindUnknownPlaceholders("{" + name + "}", {}) == std::vector<std::string>{ name }) << name;
        EXPECT_EQ(std::count(names.begin(), names.end(), name), 1) << name;
    }
}

// ---------------------------------------------------------------------------
// IsBatchFile

TEST(IsBatchFile, SpotsBatchFiles)
{
    for (const char* program : { "x.bat", "x.cmd", "X.BAT", "x.Cmd", "C:/scripts/x.bat", "C:\\dir with spaces\\x.cmd",
        "x.bat.", "x.bat ", "x.bat. . ", ".bat" })
    {
        EXPECT_TRUE(IsBatchFile(program)) << program;
    }
    for (const char* program : { "x.exe", "x", "bat", "cmd", "x.batch", "x.bat.exe", "x_bat", "powershell", "" })
    {
        EXPECT_FALSE(IsBatchFile(program)) << program;
    }
}

// ---------------------------------------------------------------------------
// Windows command line quoting

#ifdef _WIN32
namespace
{
std::vector<std::wstring> ParseWithCommandLineToArgvW(const std::wstring& command_line)
{
    int argc = 0;
    LPWSTR* argv = ::CommandLineToArgvW(command_line.c_str(), &argc);
    std::vector<std::wstring> arguments(argv, argv + argc);
    ::LocalFree(argv);
    return arguments;
}

std::vector<std::wstring> ToWide(const std::vector<std::string>& arguments)
{
    std::vector<std::wstring> wide;
    for (const auto& argument : arguments)
    {
        wide.push_back(Utf8ToWide(argument));
    }
    return wide;
}
}

TEST(BuildWindowsCommandLine, RoundTripsEachAwkwardArgument)
{
    for (const auto& argument : kAwkwardArguments)
    {
        const auto arguments = ToWide({ "C:/Program Files/x.exe", argument });
        EXPECT_EQ(ParseWithCommandLineToArgvW(BuildWindowsCommandLine(arguments)), arguments) << argument;
        const auto surrounded = ToWide({ "x.exe", "before", argument, "after" });
        EXPECT_EQ(ParseWithCommandLineToArgvW(BuildWindowsCommandLine(surrounded)), surrounded) << argument;
    }
}

TEST(BuildWindowsCommandLine, RoundTripsAllAwkwardArgumentsTogether)
{
    std::vector<std::string> arguments = { "x.exe" };
    arguments.insert(arguments.end(), kAwkwardArguments.begin(), kAwkwardArguments.end());
    EXPECT_EQ(ParseWithCommandLineToArgvW(BuildWindowsCommandLine(ToWide(arguments))), ToWide(arguments));
}

TEST(BuildWindowsCommandLine, RoundTripsProgramNames)
{
    for (const char* program : { "x.exe", "C:\\Program Files\\x.exe", "C:\\dir\\", "C:/a b/c d.exe", "\\\\server\\share\\x.exe" })
    {
        const auto arguments = ToWide({ program, "arg" });
        EXPECT_EQ(ParseWithCommandLineToArgvW(BuildWindowsCommandLine(arguments)), arguments) << program;
    }
}

TEST(BuildWindowsCommandLine, RoundTripsRandomArguments)
{
    // Weighted towards the characters the quoting rules care about.
    const std::wstring alphabet = L"\\\\\\\\\"\"\"  \ta^&%!\x00e9\x30cb";
    std::mt19937 random(12345);
    std::uniform_int_distribution<size_t> pick(0, alphabet.size() - 1);
    std::uniform_int_distribution<int> length(0, 8);
    std::uniform_int_distribution<int> count(1, 4);
    for (int round = 0; round < 20000; ++round)
    {
        std::vector<std::wstring> arguments = { L"x.exe" };
        for (int i = count(random); i > 0; --i)
        {
            std::wstring argument;
            for (int j = length(random); j > 0; --j)
            {
                argument.push_back(alphabet[pick(random)]);
            }
            arguments.push_back(argument);
        }
        const std::wstring command_line = BuildWindowsCommandLine(arguments);
        ASSERT_EQ(ParseWithCommandLineToArgvW(command_line), arguments) << "command line: " << PathToUtf8(command_line);
    }
}
#endif

// ---------------------------------------------------------------------------
// RunCommand, end to end, using the echo_args helper

class RunCommandTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        const auto* test = ::testing::UnitTest::GetInstance()->current_test_info();
        out_path_ = std::filesystem::temp_directory_path() / (std::string("snfm_echo_args_") + test->name() + ".out");
        std::filesystem::remove(out_path_);
        SetEnv("ECHO_ARGS_OUT", PathToUtf8(out_path_));
    }

    void TearDown() override
    {
        UnsetEnv("ECHO_ARGS_OUT");
        UnsetEnv("ECHO_ARGS_EXIT_CODE");
        std::filesystem::remove(out_path_);
    }

    // What echo_args received, or nullopt if it didn't run.
    std::optional<std::vector<std::string>> EchoedArguments()
    {
        std::ifstream in(out_path_, std::ios::binary);
        if (!in)
        {
            return std::nullopt;
        }
        const std::string contents((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        in.close();
        std::filesystem::remove(out_path_);
        std::vector<std::string> arguments;
        size_t start = 0;
        for (size_t end; (end = contents.find('\0', start)) != std::string::npos; start = end + 1)
        {
            arguments.push_back(contents.substr(start, end - start));
        }
        return arguments;
    }

    // Runs echo_args with arguments and returns what it received.
    std::optional<std::vector<std::string>> Echo(const std::vector<std::string>& arguments)
    {
        std::vector<std::string> command = { ECHO_ARGS_PATH };
        command.insert(command.end(), arguments.begin(), arguments.end());
        const CommandResult result = RunCommand(command);
        EXPECT_TRUE(result.started) << result.error;
        EXPECT_EQ(result.exit_code, 0);
        return EchoedArguments();
    }

    std::filesystem::path out_path_;
};

TEST_F(RunCommandTest, PassesEachAwkwardArgumentThroughUnchanged)
{
    for (const auto& argument : kAwkwardArguments)
    {
        EXPECT_EQ(Echo({ argument }), std::vector<std::string>{ argument }) << argument;
        EXPECT_EQ(Echo({ "before", argument, "after" }), (std::vector<std::string>{ "before", argument, "after" })) << argument;
    }
}

TEST_F(RunCommandTest, PassesAllAwkwardArgumentsThroughTogether)
{
    EXPECT_EQ(Echo(kAwkwardArguments), kAwkwardArguments);
}

TEST_F(RunCommandTest, RunsWithNoArguments)
{
    EXPECT_EQ(Echo({}), std::vector<std::string>{});
}

TEST_F(RunCommandTest, ReportsExitCode)
{
    SetEnv("ECHO_ARGS_EXIT_CODE", "3");
    const CommandResult result = RunCommand({ ECHO_ARGS_PATH });
    EXPECT_TRUE(result.started) << result.error;
    EXPECT_FALSE(result.signaled);
    EXPECT_EQ(result.exit_code, 3);
}

TEST_F(RunCommandTest, RunsProgramWithSpacesInItsPath)
{
    const std::filesystem::path source = ECHO_ARGS_PATH;
    const std::filesystem::path directory = std::filesystem::temp_directory_path() / "snfm test dir with spaces";
    std::filesystem::create_directories(directory);
    const std::filesystem::path program = directory / ("echo args" + source.extension().string());
    std::filesystem::copy_file(source, program, std::filesystem::copy_options::overwrite_existing);
    std::filesystem::permissions(program, std::filesystem::perms::owner_all, std::filesystem::perm_options::add);

    const CommandResult result = RunCommand({ PathToUtf8(program), "two words" });
    EXPECT_TRUE(result.started) << result.error;
    EXPECT_EQ(result.exit_code, 0);
    EXPECT_EQ(EchoedArguments(), std::vector<std::string>{ "two words" });
    std::filesystem::remove_all(directory);
}

TEST_F(RunCommandTest, ReportsMissingProgram)
{
    const CommandResult result = RunCommand({ "snfm-there-is-no-such-program", "arg" });
    // Some older C libraries only find out in the child, which then exits with 127.
    EXPECT_TRUE(!result.started || result.exit_code == 127);
    if (!result.started)
    {
        EXPECT_FALSE(result.error.empty());
    }
}

TEST_F(RunCommandTest, RefusesEmptyCommand)
{
    EXPECT_FALSE(RunCommand({}).started);
    EXPECT_FALSE(RunCommand({ "" }).started);
}

TEST_F(RunCommandTest, PostUploadCommandExpandsEachArgumentSeparately)
{
    const TemplateVariables variables = {
        {"destination_name", "Wild Arms \"2\" & friends"},
        {"source_path", "C:\\My ROMs\\"},
        {"rule_name", ""},
    };
    RunPostUploadCommand({ ECHO_ARGS_PATH, "--pack={destination_name}", "{source_path}", "{rule_name}", "{ {rule_name} }" },
        variables);
    EXPECT_EQ(EchoedArguments(), (std::vector<std::string>{
        "--pack=Wild Arms \"2\" & friends", "C:\\My ROMs\\", "", "{  }" }));
}

TEST_F(RunCommandTest, PostUploadCommandReportsProblems)
{
    ::testing::internal::CaptureStdout();
    RunPostUploadCommand({ ECHO_ARGS_PATH }, {});
    std::string output = ::testing::internal::GetCapturedStdout();
    // Just the "Running post upload command" line - nothing to report.
    EXPECT_EQ(std::count(output.begin(), output.end(), '\n'), 1) << output;

    SetEnv("ECHO_ARGS_EXIT_CODE", "3");
    ::testing::internal::CaptureStdout();
    RunPostUploadCommand({ ECHO_ARGS_PATH }, {});
    output = ::testing::internal::GetCapturedStdout();
    EXPECT_NE(output.find("Post upload command exited with code 3"), std::string::npos) << output;

    ::testing::internal::CaptureStdout();
    RunPostUploadCommand({ "snfm-there-is-no-such-program" }, {});
    output = ::testing::internal::GetCapturedStdout();
    EXPECT_TRUE(output.find("Couldn't run the post upload command") != std::string::npos ||
        output.find("exited with code 127") != std::string::npos) << output;
}

#ifdef _WIN32
TEST_F(RunCommandTest, RefusesBatchFiles)
{
    // A batch file that would leave a marker behind if it ever ran.
    const std::filesystem::path directory = std::filesystem::temp_directory_path() / "snfm_batch_test";
    std::filesystem::create_directories(directory);
    const std::filesystem::path marker = directory / "ran.txt";
    const std::filesystem::path script = directory / "script.bat";
    std::ofstream(script) << "@echo ran > \"" << marker.string() << "\"\r\n";

    for (const std::string& program : { PathToUtf8(script), PathToUtf8(script) + ".", PathToUtf8(directory / "SCRIPT.BAT") })
    {
        const CommandResult result = RunCommand({ program, "&& echo injected" });
        EXPECT_FALSE(result.started) << program;
        EXPECT_NE(result.error.find("batch"), std::string::npos) << result.error;
    }
    EXPECT_FALSE(std::filesystem::exists(marker));
    std::filesystem::remove_all(directory);
}
#else
TEST_F(RunCommandTest, ReportsSignal)
{
    const CommandResult result = RunCommand({ "sh", "-c", "kill -KILL $$" });
    EXPECT_TRUE(result.started) << result.error;
    EXPECT_TRUE(result.signaled);
    EXPECT_EQ(result.signal, SIGKILL);
}

TEST_F(RunCommandTest, ReportsFailureToCollectTheResult)
{
    // With SIGCHLD ignored the child is reaped automatically, so waitpid fails with ECHILD.
    struct IgnoreSigchld
    {
        void (*previous)(int) = std::signal(SIGCHLD, SIG_IGN);
        ~IgnoreSigchld() { std::signal(SIGCHLD, previous); }
    } ignore;

    const CommandResult result = RunCommand({ "true" });
    EXPECT_TRUE(result.started);
    EXPECT_FALSE(result.error.empty());

    ::testing::internal::CaptureStdout();
    RunPostUploadCommand({ "true" }, {});
    const std::string output = ::testing::internal::GetCapturedStdout();
    EXPECT_NE(output.find("Couldn't get the result of the post upload command"), std::string::npos) << output;
}

TEST_F(RunCommandTest, ShellPositionalArgumentsAreNotInterpreted)
{
    // The pattern snfm_config_example.yaml recommends for using shell features.
    const std::string value = "$(echo injected); `echo injected` && echo 'injected' \"";
    const CommandResult result = RunCommand({ "sh", "-c", "\"$0\" \"$1\"", ECHO_ARGS_PATH, value });
    EXPECT_TRUE(result.started) << result.error;
    EXPECT_EQ(result.exit_code, 0);
    EXPECT_EQ(EchoedArguments(), std::vector<std::string>{ value });
}
#endif
