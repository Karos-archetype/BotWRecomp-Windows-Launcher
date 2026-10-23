#pragma once
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace launcher::updates {
inline constexpr char version[]="0.2.0-preview.1";
inline constexpr char repository[]="Karos-archetype/BotWRecomp-Windows-Launcher";
inline constexpr char asset_name[]="BotWRecomp-Windows-x64.zip";
struct Release { std::string tag,url,sha256; uint64_t size{}; };
bool valid_repository(std::string_view repo);
bool newer(std::string_view candidate,std::string_view current);
std::optional<Release> select_release(std::string_view json,std::string_view repo,bool previews,
                                    std::string_view current=version);
std::optional<Release> check_github(const std::string& repo,bool previews);
std::filesystem::path stage(const Release&,const std::filesystem::path& base);
std::string sha256(const std::filesystem::path&);
void launch_installer(const std::filesystem::path& staged,const std::filesystem::path& base);
// Return -1 for a normal launch, otherwise execute the local update helper and return its exit code.
int apply_if_requested();
}
