#define UNICODE
#define _UNICODE
#include <windows.h>
#include <shellapi.h>
#include <filesystem>
int WINAPI wWinMain(HINSTANCE,HINSTANCE,PWSTR,int){
    wchar_t path[32768];auto length=GetModuleFileNameW(nullptr,path,32768);
    if(!length||length>=32768)return 1;
    auto directory=std::filesystem::path(path).parent_path();auto launcher=directory/L"BotWLauncher.exe";
    auto result=ShellExecuteW(nullptr,L"open",launcher.c_str(),L"--play",directory.c_str(),SW_SHOWNORMAL);
    if(INT_PTR(result)<=32){MessageBoxW(nullptr,L"Keep this Play shortcut beside BotWLauncher.exe. Extract the complete ZIP first.",L"Breath of the Wild",MB_OK|MB_ICONERROR);return 1;}
    return 0;
}
