#include "snfm.h"
#include "config.h"
#include "command.h"
#include <algorithm>
#include <filesystem>

// On Windows, std::filesystem treats narrow strings as the local code page, not UTF-8.
std::string ToUtf8(const std::u8string& utf8)
{
    return std::string(utf8.begin(), utf8.end());
}

std::filesystem::path PathFromUtf8(const std::string& utf8)
{
    return std::filesystem::path(std::u8string(utf8.begin(), utf8.end()));
}

void do_error(const std::string& e)
{
    std::cout << e << std::endl;
    std::cout << "(PRESS ENTER TO EXIT)" << std::endl;
    std::string s;
    std::getline(std::cin, s);
}


int main(int argc, char* argv[])
{
    auto maybePath = FindExistingConfigFile(std::filesystem::path(argv[0]).parent_path());
    Config config;
    if (maybePath) {
        try {
            config = LoadConfig(*maybePath);
        }
        catch (YAML::Exception e)
        {
            std::cout << "Error loading config: " << e.what() << std::endl;
            return 1;
        }
    }

    if (argc != 2)
    {
        do_error("A single argument - the name of the file you're trying to put on your SNES - is required");
        return 1;
    }

    std::filesystem::path rom_path = argv[1];
    std::cout << rom_path.string() << std::endl;

    std::string device_directory = config.default_rom_directory;
    std::string destination_rom_name = rom_path.filename().string();
    // The same name as UTF-8, for the post upload command.  destination_rom_name is in the
    // local code page on Windows when it comes from the ROM's file name.
    std::string destination_rom_name_utf8 = ToUtf8(rom_path.filename().u8string());
    std::string rule_name;
    std::string destination_name;

    auto maybe_rule = config.FindRuleForRom(rom_path.filename().string());
    if (maybe_rule)
    {
        auto rule = maybe_rule->get();
        rule_name = rule.name;
        if (!rule.destinations.empty())
        {
            RomDestination& destination = rule.destinations[0];
            if (rule.destinations.size() > 1)
            {
                std::cout << "Rule " << rule.name << " has multiple destinations possible, please choose the destination:" << std::endl;

                size_t longest_name = 0;
                for (const auto& destination : rule.destinations)
                {
                    longest_name = std::max(destination.config_name.size(), longest_name);
                }

                const int space_count = std::to_string(rule.destinations.size() + 1).size() + 1;
                for (int i = 0; i < rule.destinations.size(); i++)
                {
                    std::cout << " " << std::right << std::setw(space_count) << std::to_string(i);
                    std::cout << " - " << std::left << std::setw(longest_name) << rule.destinations[i].config_name;
                    std::cout << " - " << rule.destinations[i].path << "/" << rule.destinations[i].rom_name << std::endl;
                }
                while (true) {
                    std::cout << "Please choose your destination (defaults to 0):" << std::endl << "> ";
                    std::string input;
                    std::getline(std::cin, input);

                    if (input.empty())
                    {
                        break;
                    }
                    int pick;
                    try {
                        pick = std::stoi(input.c_str());
                    }
                    catch (std::exception const& ex) {
                        std::cout << "Not a number." << std::endl;
                        continue;
                    }
                    if (pick < 0 || pick >= static_cast<int>(rule.destinations.size()))
                    {
                        std::cout << "Choice must be between 0 and " << std::to_string(rule.destinations.size() - 1) << std::endl;
                        continue;
                    }
                    destination = rule.destinations[pick];
                    break;
                }

            }
            std::cout << "Using " << destination.config_name << std::endl;
            destination_name = destination.config_name;
            device_directory = destination.path;
            if (!destination.rom_name.empty()) {
                destination_rom_name = destination.rom_name;
                destination_rom_name_utf8 = destination.rom_name;
            }
        }
    }

    if (device_directory.empty() || device_directory[0] != '/')
    {
        device_directory = "/" + device_directory;
    }

    SNIConnection sni;

    auto device_filter =
        [](const DevicesResponse::Device& device) -> bool
    {
        auto caps = device.capabilities();
        return HasCapability(device, DeviceCapability::MakeDirectory) &&
            HasCapability(device, DeviceCapability::PutFile) &&
            HasCapability(device, DeviceCapability::BootFile);
    };
    std::cout << "Refreshing devices..." << std::endl;
    sni.refreshDevices(device_filter);
    auto uri = sni.getFirstDeviceUri();
    if (!uri)
    {
        do_error("Couldn't find any valid devices.");
        return 1;
    }

    sni.makeDirectory(*uri, device_directory, true);

    std::optional<std::filesystem::path> put_path = sni.putFile(*uri, rom_path, device_directory, destination_rom_name);
    if (!put_path)
    {
        do_error("Couldn't put the file, due to some error.");
        return 1;
    }
    sni.bootFile(*uri, *put_path);

    if (!config.post_upload_command.empty())
    {
        // Everything goes in as UTF-8, which is what RunPostUploadCommand expects.  The values
        // from the config already are; local paths need converting.
        const std::filesystem::path destination_file =
            PathFromUtf8(device_directory) / PathFromUtf8(destination_rom_name_utf8);
        RunPostUploadCommand(config.post_upload_command, {
            {"rule_name", rule_name},
            {"destination_name", destination_name},
            {"destination_path", device_directory},
            {"destination_rom_name", destination_rom_name_utf8},
            {"destination_file", ToUtf8(destination_file.generic_u8string())},
            {"source_path", ToUtf8(std::filesystem::absolute(rom_path).u8string())},
            {"source_name", ToUtf8(rom_path.filename().u8string())},
            });
    }

    return 0;
}
