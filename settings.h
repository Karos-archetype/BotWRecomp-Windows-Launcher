#pragma once
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <stdexcept>
#include <string>

namespace launcher {
struct Settings {
    std::string game,update,user;
    int width=1280,height=720,fps=30,volume=80;
    float scale=1.f;
    bool mute=false,fullscreen=false,vsync=true;
    bool prewarm=true;
    int language=1;
    bool prereleases=true;
    std::string release_repo="Karos-archetype/BotWRecomp-Windows-Launcher";
    std::string upscaler="bicubic";
    void validate() const {
        if(width<640 || width>7680 || height<360 || height>4320)throw std::runtime_error("Invalid display resolution");
        if(fps!=0 && fps!=30 && fps!=60 && fps!=90)throw std::runtime_error("FPS cap must be 30, 60, 90 or 0 (Unlimited)");
        if(!(scale>=0.5f && scale<=3.f))throw std::runtime_error("Render scale must be 0.5 to 3.0");
        if(volume<0 || volume>100)throw std::runtime_error("Volume must be 0 to 100");
        if(language!=1 && language!=3 && language!=4)throw std::runtime_error("Game language must be English, French or Spanish for this USA build");
        if(upscaler!="bicubic" && upscaler!="linear" && upscaler!="fsr1" && upscaler!="fsr31")throw std::runtime_error("Unsupported upscaler; DLSS is unavailable in this build");
        if(upscaler=="fsr31" && (1280.f*scale>width || 720.f*scale>height))throw std::runtime_error("FSR 3.1 needs an internal render resolution at or below the window resolution");
    }
};
inline std::map<std::string,std::string> read(const std::filesystem::path& path) {
    std::map<std::string,std::string> values;std::ifstream file(path);
    for(std::string line;std::getline(file,line);) {
        if(!line.empty() && line.back()=='\r')line.pop_back();
        auto split=line.find('=');if(split!=std::string::npos)values[line.substr(0,split)]=line.substr(split+1);
    }
    return values;
}
inline Settings load(const std::filesystem::path& path) {
    Settings s;
    for(auto& [key,v]:read(path)) {
        if(key=="game")s.game=v;else if(key=="update")s.update=v;else if(key=="user")s.user=v;
        else if(key=="width")s.width=std::stoi(v);else if(key=="height")s.height=std::stoi(v);
        else if(key=="fps")s.fps=std::stoi(v);else if(key=="volume")s.volume=std::stoi(v);
        else if(key=="scale")s.scale=std::stof(v);else if(key=="mute")s.mute=v=="1";
        else if(key=="fullscreen")s.fullscreen=v=="1";else if(key=="vsync")s.vsync=v=="1";
        else if(key=="upscaler")s.upscaler=v;
        else if(key=="prewarm")s.prewarm=v=="1";
        else if(key=="language")s.language=std::stoi(v);
        else if(key=="prereleases")s.prereleases=v=="1";
        else if(key=="release_repo")s.release_repo=v;
    }
    s.validate();return s;
}
inline void save(const std::filesystem::path& path,const Settings& s) {
    s.validate();auto v=read(path);
    v["prewarm"]=s.prewarm?"1":"0";
    v["language"]=std::to_string(s.language);v["prereleases"]=s.prereleases?"1":"0";v["release_repo"]=s.release_repo;
    v["game"]=s.game;v["update"]=s.update;v["user"]=s.user;
    v["width"]=std::to_string(s.width);v["height"]=std::to_string(s.height);v["fps"]=std::to_string(s.fps);
    v["volume"]=std::to_string(s.volume);v["scale"]=std::to_string(s.scale);v["mute"]=s.mute?"1":"0";
    v["fullscreen"]=s.fullscreen?"1":"0";v["vsync"]=s.vsync?"1":"0";v["upscaler"]=s.upscaler;
    for(auto& [key,value]:v)if(value.find_first_of("\r\n")!=std::string::npos)throw std::runtime_error("Settings cannot contain newlines");
    std::ofstream out(path,std::ios::binary|std::ios::trunc);if(!out)throw std::runtime_error("Cannot save launcher settings");
    for(auto& [key,value]:v)out<<key<<'='<<value<<'\n';
    if(!out)throw std::runtime_error("Writing launcher settings failed");
}
}
