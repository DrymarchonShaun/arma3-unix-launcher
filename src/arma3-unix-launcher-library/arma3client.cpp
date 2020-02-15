#include "arma3client.hpp"

#include <algorithm>
#include <filesystem>
#include <string>

#include <fmt/format.h>
#include <fmt/ostream.h>

#include <static_todo.hpp>

#include "cppfilter.hpp"
#include "filesystem_utils.hpp"
#include "std_utils.hpp"
#include "string_utils.hpp"

#include "exceptions/directory_not_found.hpp"
#include "exceptions/file_no_access.hpp"
#include "exceptions/file_not_found.hpp"
#include "exceptions/not_a_symlink.hpp"
#include "exceptions/syntax_error.hpp"

using namespace std;
using namespace std::filesystem;

using namespace StdUtils;
using namespace StringUtils;

namespace fs = FilesystemUtils;

namespace ARMA3
{
    Client::Client(std::filesystem::path const &arma_path, std::filesystem::path const &target_workshop_path)
    {
        path_ = arma_path;
        for (auto const &executable : Definitions::executable_names)
        {
            if (fs::Exists(path_ / executable))
            {
                path_executable_ = path_ / executable;
                break;
            }
        }
        if (path_executable_.empty())
            throw FileNotFoundException("arma3.exe");
        path_workshop_target_ = target_workshop_path;
    }

    void Client::CreateArmaCfg(vector<string> const &workshop_mod_ids, vector<path> const &custom_mods, path cfg_path)
    {
        if (cfg_path.empty())
            cfg_path = GetCfgPath();
        if (!fs::Exists(cfg_path))
        {
            if (!fs::Exists(cfg_path.parent_path()))
                fs::CreateDirectories(cfg_path.parent_path());
            StdUtils::CreateFile(cfg_path);
        }
        std::string existing_config = FileReadAllText(cfg_path);

        CppFilter cpp_filter{existing_config};
        auto stripped_config = cpp_filter.RemoveClass("class ModLauncherList");
        stripped_config += R"cpp(class ModLauncherList
{
)cpp";

        int mod_number = 1;
        for (auto const& mod_id : workshop_mod_ids)
            stripped_config += GenerateCfgCppForMod(GetPathWorkshop() / mod_id, mod_number++);
        for (auto const& mod_path : custom_mods)
            stripped_config += GenerateCfgCppForMod(mod_path, mod_number++);

        stripped_config += "};\n";

        FileWriteAllText(cfg_path, stripped_config);
    }

    bool Client::IsProton()
    {
        return path_executable_.filename() == "arma3_x64.exe";
    }

    void Client::Start(string const &arguments)
    {
        #ifdef __linux
        if (IsProton())
            StdUtils::StartBackgroundProcess("steam -applaunch 107410 -nolauncher " + arguments);
        else
            StdUtils::StartBackgroundProcess("steam -applaunch 107410 " + arguments);
        #else
        std::string launch_command = "open steam://run/107410//" + StringUtils::Replace(arguments, " ", "%20");
        StdUtils::StartBackgroundProcess(launch_command);
        #endif
    }

    std::vector<Mod> Client::GetHomeMods()
    {
        return GetModsFromDirectory(GetPath());
    }

    std::vector<Mod> Client::GetWorkshopMods()
    {
        return GetModsFromDirectory(GetPathWorkshop());
    }

    std::filesystem::path Client::GetCfgPath()
    {
        return std::filesystem::path(Definitions::home_directory)
               / Definitions::local_share_prefix
               / Definitions::bohemia_interactive_prefix
               / Definitions::game_config_path;
    }

    char Client::GetFakeDriveLetter()
    {
        return IsProton() ? 'Z' : 'C';
    }

    string Client::GenerateCfgCppForMod(path const &mod_path, int mod_index)
    {
        constexpr char const *mod_template =
            R"cpp(    class Mod{}
    {{
        dir="{}";
        name="{}";
        origin="GAME DIR";
        fullPath="{}";
    }};
)cpp";

        Mod mod(mod_path);
        path mod_path_absolute = trim(mod.path_.string(), "\"");
        path final_path = StringUtils::ToWindowsPath(mod_path_absolute, GetFakeDriveLetter());
        path dir = trim(mod_path_absolute.filename().string(), "\"");
        auto name = mod.GetValueOrReturnDefault(dir, "name", "dir", "tooltip", "name_read_failed");

        return fmt::format(mod_template, mod_index, dir.string(), name, final_path.string());
    }

    std::filesystem::path const &Client::GetPath()
    {
        return path_;
    }

    std::filesystem::path const &Client::GetPathExecutable()
    {
        return path_executable_;
    }

    std::filesystem::path const &Client::GetPathWorkshop()
    {
        return path_workshop_target_;
    }

    std::vector<Mod> Client::GetModsFromDirectory(path const &dir)
    {
        std::vector<Mod> ret;

        for (auto const &ent : fs::Ls(dir))
        {
            if (StdUtils::Contains(Definitions::exclusions, ent))
                continue;

            std::filesystem::path mod_dir = dir / ent;
            if (!fs::IsDirectory(mod_dir))
                continue;
            if (!StdUtils::Contains(fs::Ls(mod_dir, true), "addons"))
                continue;

            ret.emplace_back(mod_dir);
        }

        std::sort(ret.begin(), ret.end(), [](Mod const & m1, Mod const & m2)
        {
            return m1.GetValueOrReturnDefault("name", "dir", StringUtils::Lowercase(m1.path_)) < m2.GetValueOrReturnDefault("name", "dir", StringUtils::Lowercase(m2.path_));
        });

        return ret;
    }
}
