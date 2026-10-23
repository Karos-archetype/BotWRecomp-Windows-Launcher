#include "settings.h"
#include <cstdio>
int main(){try{
    auto path=std::filesystem::temp_directory_path()/"botw-launcher-settings-test.ini";
    {std::ofstream f(path);f<<"game=C:/game with spaces\nupdate=C:/update\nuser=C:/private saves\nseconds=0\ndiagnostics=1\n";}
    auto s=launcher::load(path);if(s.fps!=30 || s.mute || s.game!="C:/game with spaces")throw std::runtime_error("Defaults/paths");
    for(auto cap:{30,60,90,0}){s.fps=cap;s.upscaler="fsr1";s.volume=35;s.width=1920;s.height=1080;s.scale=.75f;s.fullscreen=true;launcher::save(path,s);auto r=launcher::load(path);if(r.fps!=cap || r.upscaler!="fsr1" || r.scale!=.75f || r.volume!=35 || !r.fullscreen || launcher::read(path)["diagnostics"]!="1")throw std::runtime_error("Settings round trip/preserved config");}
    unsigned rejected=0;
    if(!launcher::Settings{}.prewarm)throw std::runtime_error("Effect preparation must default on");
    for(bool warm:{false,true}){s.prewarm=warm;launcher::save(path,s);if(launcher::load(path).prewarm!=warm)throw std::runtime_error("Effect preparation persistence");}
    for(int language:{1,3,4})for(bool previews:{false,true}){s.language=language;s.prereleases=previews;s.release_repo="test/launcher";launcher::save(path,s);auto r=launcher::load(path);if(r.language!=language||r.prereleases!=previews||r.release_repo!="test/launcher")throw std::runtime_error("Language/update settings persistence");}
    {auto bad=s;bad.language=99;bool failed=false;try{bad.validate();}catch(...){failed=true;}if(!failed)throw std::runtime_error("Invalid game language accepted");}
    for(auto key:{"fps","upscaler","scale","volume"}){auto bad=s;if(std::string(key)=="fps")bad.fps=45;else if(std::string(key)=="upscaler")bad.upscaler="dlss";else if(std::string(key)=="scale")bad.scale=0;else bad.volume=200;try{bad.validate();}catch(...){rejected++;}}
    if(rejected!=4)throw std::runtime_error("Invalid settings accepted");std::filesystem::remove(path);puts("PASS launcher paths, defaults, cap/upscaler persistence, config preservation and validation");return 0;
}catch(const std::exception& e){fprintf(stderr,"FAIL %s\n",e.what());return 1;}}
