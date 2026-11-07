#include "config.h"
#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>

namespace
{
// Writes yaml to a temporary config file and loads it.
Config LoadConfigFromString(const std::string& yaml)
{
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "snfm_config_test.yaml";
    std::ofstream(path, std::ios::binary) << yaml;
    struct Cleanup
    {
        std::filesystem::path path;
        ~Cleanup() { std::filesystem::remove(path); }
    } cleanup{ path };
    return LoadConfig(path);
}

std::string LoadErrorFor(const std::string& yaml)
{
    try
    {
        LoadConfigFromString(yaml);
    }
    catch (const YAML::Exception& e)
    {
        return e.what();
    }
    ADD_FAILURE() << "expected an error loading:\n" << yaml;
    return {};
}
}

TEST(PostUploadCommandConfig, IsEmptyByDefault)
{
    EXPECT_TRUE(LoadConfigFromString("default_rom_directory: x\n").post_upload_command.empty());
}

TEST(PostUploadCommandConfig, LoadsFlowList)
{
    const Config config = LoadConfigFromString(
        R"(post_upload_command: ["C:/tools/obs-msu.exe", "--pack", "{destination_name}", "--rom={source_path}"])");
    EXPECT_EQ(config.post_upload_command, (std::vector<std::string>{
        "C:/tools/obs-msu.exe", "--pack", "{destination_name}", "--rom={source_path}" }));
}

TEST(PostUploadCommandConfig, LoadsBlockListWithShellSyntax)
{
    const Config config = LoadConfigFromString(
        "post_upload_command:\n"
        "  - sh\n"
        "  - -c\n"
        "  - '{ printf \"%s\\n\" \"$1\"; } > \"$HOME/msu.txt\"'\n"
        "  - sh\n"
        "  - \"{destination_name}\"\n");
    EXPECT_EQ(config.post_upload_command, (std::vector<std::string>{
        "sh", "-c", "{ printf \"%s\\n\" \"$1\"; } > \"$HOME/msu.txt\"", "sh", "{destination_name}" }));
}

TEST(PostUploadCommandConfig, LoadsTheDocumentedExamples)
{
    // Keep these in step with snfm_config_example.yaml.
    EXPECT_EQ(LoadConfigFromString(
        R"(post_upload_command: ["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", "C:/scripts/msu.ps1", "{destination_name}"])")
        .post_upload_command.size(), 7u);
    EXPECT_EQ(LoadConfigFromString(
        R"(post_upload_command: ["sh", "-c", "printf '%s\n' \"$1\" > \"$HOME/msu.txt\"", "sh", "{destination_name}"])")
        .post_upload_command,
        (std::vector<std::string>{ "sh", "-c", "printf '%s\n' \"$1\" > \"$HOME/msu.txt\"", "sh", "{destination_name}" }));
}

TEST(PostUploadCommandConfig, AcceptsEveryVariable)
{
    EXPECT_EQ(LoadConfigFromString(
        R"(post_upload_command: ["x", "{rule_name}{destination_name}{destination_path}{destination_rom_name}", "{destination_file}{source_path}{source_name}"])")
        .post_upload_command.size(), 3u);
}

TEST(PostUploadCommandConfig, RejectsPlainString)
{
    EXPECT_NE(LoadErrorFor("post_upload_command: 'echo {destination_name}'\n").find("must be a list"), std::string::npos);
}

TEST(PostUploadCommandConfig, RejectsUnquotedPlaceholderReadAsMapping)
{
    // Unquoted, YAML reads {destination_name} as a mapping rather than a string.
    EXPECT_NE(LoadErrorFor("post_upload_command: {destination_name}\n").find("must be a list"), std::string::npos);
    EXPECT_FALSE(LoadErrorFor("post_upload_command: [x, {destination_name}]\n").empty());
}

TEST(PostUploadCommandConfig, RejectsUnknownVariable)
{
    EXPECT_NE(LoadErrorFor(R"(post_upload_command: ["x", "--pack={destinaton_name}"])").find("{destinaton_name}"),
        std::string::npos);
}

TEST(PostUploadCommandConfig, RejectsEmptyList)
{
    EXPECT_NE(LoadErrorFor("post_upload_command: []\n").find("at least a program"), std::string::npos);
}

TEST(PostUploadCommandConfig, RejectsNestedList)
{
    EXPECT_FALSE(LoadErrorFor("post_upload_command: [[x]]\n").empty());
}

TEST(ExampleConfig, Loads)
{
    EXPECT_NO_THROW(LoadConfig(EXAMPLE_CONFIG_PATH));
}
