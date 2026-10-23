#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define _WIN32_WINNT 0x0A00
#define WINVER 0x0A00
#define NTDDI_VERSION 0x0A000005
#include <windows.h>
#include <shellapi.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <shobjidl.h>
#include <shlobj.h>
#include <dxgi1_2.h>
#include <filesystem>
#include <string>
#include <vector>
#include <array>
#include <thread>
#include <memory>
#include "settings.h"
#include "launcher_update.h"
#include "launcher_licenses.h"

namespace {
constexpr COLORREF bg=RGB(13,22,26),panel=RGB(22,34,38),field=RGB(29,44,48),gold=RGB(214,190,133),ink=RGB(228,234,229),dim=RGB(150,169,166);
enum { RES=100,MODE,FPS,UPSCALE,RENDER,VOLUME,MUTE,VSYNC,GAME,UPDATE,BROWSE_GAME,BROWSE_UPDATE,PLAY,SAVE,LOGS,PREWARM,LANGUAGE,RELEASE_REPO,PRERELEASES,CHECK_RELEASE,APPLY_RELEASE,CREDITS,FULLSCREEN };
HWND root{},status{},volume_text{};std::array<HWND,120> controls{};
HANDLE preparation_process{},preparation_job{};bool direct_play=false;
HFONT font{},small{},heading{},title{},brand{};HBRUSH background{},fields{};
launcher::Settings settings;std::filesystem::path base,config;
std::wstring gpu=L"GPU information unavailable";unsigned vendor{};HANDLE game_process{};
float dpi=1.f;int origin_x=0,origin_y=0;
bool launcher_fullscreen=false;WINDOWPLACEMENT windowed_placement{sizeof(WINDOWPLACEMENT)};
constexpr DWORD windowed_style=WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN;
struct LayoutItem {HWND window;int x,y,w,h;bool small_font=false;};
std::vector<LayoutItem> layout_items;
constexpr UINT update_result_message=WM_APP+1,initial_check_message=WM_APP+2;
struct UpdateResult {std::optional<launcher::updates::Release> release;std::filesystem::path staged;std::string error,repo;bool install=false;};
std::thread update_worker;bool update_busy=false;
std::optional<launcher::updates::Release> available_release;std::string checked_repo;
int px(int x){return int(x*dpi);}
std::wstring wide(const std::string& s){if(s.empty())return {};int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),s.size(),nullptr,0);if(!n)throw std::runtime_error("Invalid UTF-8 in settings");std::wstring out(n,0);MultiByteToWideChar(CP_UTF8,0,s.data(),s.size(),out.data(),n);return out;}
std::string utf8(const std::wstring& s){if(s.empty())return {};int n=WideCharToMultiByte(CP_UTF8,0,s.data(),s.size(),nullptr,0,nullptr,nullptr);std::string out(n,0);WideCharToMultiByte(CP_UTF8,0,s.data(),s.size(),out.data(),n,nullptr,nullptr);return out;}
HFONT make_font(int size,int weight,const wchar_t* face=L"Segoe UI"){return CreateFontW(-px(size),0,0,0,weight,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,face);}
void text(HDC dc,const wchar_t* s,int x,int y,int w,int h,HFONT f,COLORREF color,UINT flags=DT_LEFT|DT_WORDBREAK){SelectObject(dc,f);SetTextColor(dc,color);SetBkMode(dc,TRANSPARENT);RECT r{origin_x+px(x),origin_y+px(y),origin_x+px(x+w),origin_y+px(y+h)};DrawTextW(dc,s,-1,&r,flags);}
void rect(HDC dc,int x,int y,int w,int h,COLORREF color){RECT r{origin_x+px(x),origin_y+px(y),origin_x+px(x+w),origin_y+px(y+h)};auto b=CreateSolidBrush(color);FillRect(dc,&r,b);DeleteObject(b);}
HWND widget(int id,const wchar_t* cls,const wchar_t* value,int x,int y,int w,int h,DWORD style=0){auto handle=CreateWindowExW(0,cls,value,WS_CHILD|WS_VISIBLE|WS_TABSTOP|style,px(x),px(y),px(w),px(h),root,reinterpret_cast<HMENU>(INT_PTR(id)),GetModuleHandleW(nullptr),nullptr);SendMessageW(handle,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);layout_items.push_back({handle,x,y,w,h});if(id>=100)controls[id-100]=handle;return handle;}
HWND control(int id){return controls[id-100];}
void label(const wchar_t* value,int x,int y,int w=300){auto h=CreateWindowExW(0,L"STATIC",value,WS_CHILD|WS_VISIBLE,px(x),px(y),px(w),px(22),root,nullptr,nullptr,nullptr);SendMessageW(h,WM_SETFONT,reinterpret_cast<WPARAM>(small),TRUE);layout_items.push_back({h,x,y,w,22,true});}
void combo(int id,int x,int y,int w,std::initializer_list<const wchar_t*> items,int chosen){auto h=widget(id,L"COMBOBOX",L"",x,y,w,260,CBS_DROPDOWNLIST|WS_VSCROLL);for(auto item:items)SendMessageW(h,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(item));SendMessageW(h,CB_SETCURSEL,chosen,0);}
int selected(int id){return int(SendMessageW(control(id),CB_GETCURSEL,0,0));}
std::wstring value(int id){int n=GetWindowTextLengthW(control(id));std::wstring s(n+1,0);GetWindowTextW(control(id),s.data(),n+1);s.resize(n);return s;}
void notice(const std::wstring& s){SetWindowTextW(status,s.c_str());}
void show_credits(){auto path=base/L"Launcher-CREDITS.txt";{std::ofstream out(path,std::ios::binary);out<<launcher::license_text;if(!out)throw std::runtime_error("Cannot write the credits file beside the launcher");}ShellExecuteW(root,L"open",path.c_str(),nullptr,nullptr,SW_SHOWNORMAL);}
void detect_gpu(){IDXGIFactory1* factory{};if(FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1),reinterpret_cast<void**>(&factory))))return;for(UINT i=0;;i++){IDXGIAdapter1* adapter{};if(factory->EnumAdapters1(i,&adapter)==DXGI_ERROR_NOT_FOUND)break;DXGI_ADAPTER_DESC1 d{};adapter->GetDesc1(&d);adapter->Release();if(!(d.Flags&DXGI_ADAPTER_FLAG_SOFTWARE)){gpu=d.Description;vendor=d.VendorId;break;}}factory->Release();}
void browse(int id){IFileOpenDialog* dialog{};if(FAILED(CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&dialog))))return;DWORD options{};dialog->GetOptions(&options);dialog->SetOptions(options|FOS_PICKFOLDERS|FOS_FORCEFILESYSTEM);dialog->SetTitle(id==GAME?L"Select BOTW base game folder":L"Select BOTW update folder");if(SUCCEEDED(dialog->Show(root))){IShellItem* item{};if(SUCCEEDED(dialog->GetResult(&item))){PWSTR path{};if(SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH,&path))){SetWindowTextW(control(id),path);CoTaskMemFree(path);}item->Release();}}dialog->Release();}
void update_limits(){bool high=selected(FPS)>=2;if(high)SendMessageW(control(VSYNC),BM_SETCHECK,BST_UNCHECKED,0);EnableWindow(control(VSYNC),!high);}
void update_upscaling(){
    InvalidateRect(root,nullptr,FALSE);
    if(selected(UPSCALE)!=3)return;
    static constexpr float scales[]={.5f,.75f,1.f,1.25f,1.5f,2.f,3.f};
    static constexpr int widths[]={1280,1600,1920,2560,3840};
    // Start near the Quality ratio using the existing internal-resolution choices.
    auto limit=widths[std::clamp(selected(RES),0,4)]/(1280.f*1.5f);int best=0;
    for(int i=0;i<7;i++)if(scales[i]<=limit)best=i;
    if(selected(RENDER)>best)SendMessageW(control(RENDER),CB_SETCURSEL,best,0);
    notice(L"FSR 3.1: animated-object motion and separate HUD. No frame generation.");
}
void collect(){
    static constexpr int widths[]={1280,1600,1920,2560,3840},heights[]={720,900,1080,1440,2160},caps[]={30,60,90,0};
    static constexpr float scales[]={0.5f,0.75f,1.f,1.25f,1.5f,2.f,3.f};
    const char* filters[]={"bicubic","linear","fsr1","fsr31"};
    settings.width=widths[std::clamp(selected(RES),0,4)];settings.height=heights[std::clamp(selected(RES),0,4)];
    settings.fps=caps[std::clamp(selected(FPS),0,3)];settings.scale=scales[std::clamp(selected(RENDER),0,6)];
    settings.upscaler=filters[std::clamp(selected(UPSCALE),0,3)];settings.fullscreen=selected(MODE)==1;
    settings.volume=int(SendMessageW(control(VOLUME),TBM_GETPOS,0,0));settings.mute=SendMessageW(control(MUTE),BM_GETCHECK,0,0)==BST_CHECKED;
    settings.prewarm=SendMessageW(control(PREWARM),BM_GETCHECK,0,0)==BST_CHECKED;
    static constexpr int languages[]={1,3,4};settings.language=languages[std::clamp(selected(LANGUAGE),0,2)];
    settings.prereleases=SendMessageW(control(PRERELEASES),BM_GETCHECK,0,0)==BST_CHECKED;settings.release_repo=utf8(value(RELEASE_REPO));
    settings.vsync=settings.fps!=0 && settings.fps<=60 && SendMessageW(control(VSYNC),BM_GETCHECK,0,0)==BST_CHECKED;settings.game=utf8(value(GAME));settings.update=utf8(value(UPDATE));
    settings.validate();
}
void update_controls(){bool busy=update_busy||preparation_process;EnableWindow(control(CHECK_RELEASE),!busy);EnableWindow(control(APPLY_RELEASE),!busy && available_release.has_value());EnableWindow(control(RELEASE_REPO),!busy);EnableWindow(control(PRERELEASES),!busy);}
void start_update(bool install){
    if(update_busy||preparation_process)return;collect();
    if(!launcher::updates::valid_repository(settings.release_repo))throw std::runtime_error("Update repository must be owner/repository");
    if(install && (!available_release || checked_repo!=settings.release_repo))throw std::runtime_error("Check this repository for updates first");
    launcher::save(config,settings);update_busy=true;update_controls();notice(install?L"Downloading and verifying portable update…":L"Checking GitHub releases…");
    if(update_worker.joinable())update_worker.join();auto repo=settings.release_repo;bool previews=settings.prereleases;auto release=available_release;
    update_worker=std::thread([install,repo,previews,release]{
        auto result=std::make_unique<UpdateResult>();result->install=install;result->repo=repo;
        try {if(install)result->staged=launcher::updates::stage(*release,base);else result->release=launcher::updates::check_github(repo,previews);}
        catch(const std::exception& e){result->error=e.what();}
        auto raw=result.release();if(!PostMessageW(root,update_result_message,0,reinterpret_cast<LPARAM>(raw)))delete raw;
    });
}
void save(){collect();launcher::save(config,settings);notice(L"Settings saved.");}
std::filesystem::path prepared_runtime(){
    auto manifest=base/L"support/runtime-id.txt";
    if(!std::filesystem::exists(manifest))return base/L"BotWWindowsHeadless.exe";
    std::ifstream input(manifest);std::string id;input>>id;
    if(id.size()!=64||id.find_first_not_of("0123456789abcdef")!=std::string::npos)throw std::runtime_error("Package manifest is damaged. Extract the complete ZIP again.");
    return std::filesystem::u8path(settings.user)/L"runtime"/id/L"BotWWindowsHeadless.exe";
}
void launch_saved(){
    auto runtime_settings=settings;auto runtime_config=config;
    if(std::filesystem::exists(base/L"support/runtime-id.txt")){
        auto paths=launcher::read(std::filesystem::u8path(settings.user)/L"prepared-dumps.ini");
        if(paths["source_game"]!=settings.game||paths["source_update"]!=settings.update)throw std::runtime_error("Game folders need preparation. Click Set up and Play.");
        runtime_settings.game=paths["game"];runtime_settings.update=paths["update"];
        runtime_config=std::filesystem::u8path(settings.user)/L"runtime-launch.ini";
    }
    auto game_root=std::filesystem::u8path(runtime_settings.game),update_root=std::filesystem::u8path(runtime_settings.update);
    if(!std::filesystem::exists(game_root/L"code/U-King.rpx"))throw std::runtime_error("Select the base game folder containing code/U-King.rpx.");
    if(!runtime_settings.update.empty() && !std::filesystem::exists(update_root/L"code/U-King.rpx"))throw std::runtime_error("Select the update folder containing code/U-King.rpx.");
    if(std::filesystem::exists(base/L"support/runtime-id.txt")){
        auto effective=runtime_settings.update.empty()?game_root:update_root;
        if(!std::filesystem::is_directory(game_root/L"content")||!std::filesystem::is_directory(effective/L"content"))throw std::runtime_error("Choose extracted dump folders containing code/ and content/.");
        if(launcher::updates::sha256(effective/L"code/U-King.rpx")!="ba58da5b95ce929e005d058ceb08b9b2788d1ab2bbc8a6c189bbadca0bb34d30")throw std::runtime_error("This preview requires your own Wii U USA v208 update. The selected executable is unsupported.");
    }
    if(settings.user.empty())throw std::runtime_error("The save-data folder is missing from windows-launch.ini.");
    launcher::save(config,settings);launcher::save(runtime_config,runtime_settings);auto exe=prepared_runtime();
    if(!std::filesystem::exists(exe))throw std::runtime_error("The native game executable was not found beside the launcher.");
    std::wstring command=L"\""+exe.wstring()+L"\" --config \""+runtime_config.wstring()+L"\"";STARTUPINFOW start{sizeof(start)};PROCESS_INFORMATION process{};
    if(!CreateProcessW(exe.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,base.c_str(),&start,&process))throw std::runtime_error("Windows could not start the game (error "+std::to_string(GetLastError())+").");
    CloseHandle(process.hThread);game_process=process.hProcess;
    if(root){EnableWindow(control(PLAY),FALSE);notice(L"Game running. Your settings will apply on the next launch.");}
}
void play(){
    if(game_process||preparation_process)return;
    collect();launcher::save(config,settings);
    if(!std::filesystem::exists(base/L"support/runtime-id.txt") && std::filesystem::exists(prepared_runtime())){launch_saved();return;}
    auto python=base/L"support/python/python.exe",script=base/L"support/prepare.py";
    if(!std::filesystem::exists(python)||!std::filesystem::exists(script))throw std::runtime_error("Extract the complete portable ZIP, including its support folder.");
    auto user=std::filesystem::u8path(settings.user);std::filesystem::create_directories(user);
    auto log=user/L"preparation.log";SECURITY_ATTRIBUTES security{sizeof(security),nullptr,TRUE};
    HANDLE output=CreateFileW(log.c_str(),GENERIC_WRITE,FILE_SHARE_READ,&security,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(output==INVALID_HANDLE_VALUE)throw std::runtime_error("Cannot create preparation.log in the save-data folder.");
    std::wstring command=L"\""+python.wstring()+L"\" -I -u \""+script.wstring()+L"\" --package \""+base.wstring()+L"\" --config \""+config.wstring()+L"\"";
    STARTUPINFOW start{sizeof(start)};start.dwFlags=STARTF_USESTDHANDLES;start.hStdOutput=start.hStdError=output;
    start.hStdInput=CreateFileW(L"NUL",GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,&security,OPEN_EXISTING,0,nullptr);
    preparation_job=CreateJobObjectW(nullptr,nullptr);JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if(!preparation_job||!SetInformationJobObject(preparation_job,JobObjectExtendedLimitInformation,&limits,sizeof(limits))){CloseHandle(output);CloseHandle(start.hStdInput);throw std::runtime_error("Cannot initialize the preparation worker.");}
    PROCESS_INFORMATION process{};bool created=CreateProcessW(python.c_str(),command.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW|CREATE_SUSPENDED,nullptr,base.c_str(),&start,&process);
    CloseHandle(output);CloseHandle(start.hStdInput);
    if(!created){CloseHandle(preparation_job);preparation_job=nullptr;throw std::runtime_error("Cannot start bundled preparation tools.");}
    if(!AssignProcessToJobObject(preparation_job,process.hProcess)){TerminateProcess(process.hProcess,1);CloseHandle(process.hProcess);CloseHandle(process.hThread);CloseHandle(preparation_job);preparation_job=nullptr;throw std::runtime_error("Cannot manage the preparation worker.");}
    preparation_process=process.hProcess;ResumeThread(process.hThread);CloseHandle(process.hThread);
    EnableWindow(control(PLAY),FALSE);EnableWindow(control(SAVE),FALSE);EnableWindow(control(CHECK_RELEASE),FALSE);EnableWindow(control(APPLY_RELEASE),FALSE);
    notice(L"Checking your dump and preparing install packages if needed. See Open logs for progress.");
}
void paint(){PAINTSTRUCT ps{};HDC dc=BeginPaint(root,&ps);RECT area{};GetClientRect(root,&area);FillRect(dc,&area,background);
    rect(dc,0,0,310,790,panel);rect(dc,28,52,40,3,gold);
    text(dc,L"THE LEGEND OF",29,85,255,28,small,gold);
    text(dc,L"ZELDA",24,112,275,75,brand,gold);
    text(dc,L"BREATH OF THE WILD",29,188,260,30,font,ink);
    text(dc,L"NATIVE WINDOWS",30,224,260,25,small,dim);
    rect(dc,29,355,252,1,RGB(54,76,77));text(dc,L"GRAPHICS DEVICE",29,372,260,25,small,gold);text(dc,gpu.c_str(),29,403,250,52,font,ink);
    text(dc,(L"Launcher v"+wide(launcher::updates::version)).c_str(),29,480,260,25,small,dim);
    text(dc,L"Launch settings",350,40,450,46,title,ink);text(dc,L"Graphics, performance and audio",352,88,650,28,font,dim);
    text(dc,vendor==0x10de?L"DLSS unavailable: temporal renderer integration is not implemented in this build.":L"DLSS unavailable on this GPU. NVIDIA RTX and temporal renderer integration are required.",352,451,650,37,small,dim);
    text(dc,L"90 FPS and Unlimited are experimental. Some cutscenes retain a 30 FPS limit.",352,628,650,40,small,dim);
    if(selected(UPSCALE)==3)text(dc,L"FSR 3.1: object motion + separate HUD. Effects may shimmer. No frame generation.",352,652,650,26,small,dim);
    EndPaint(root,&ps);
}
void rebuild_fonts(){
    auto old=std::array{font,small,heading,title,brand};
    font=make_font(16,FW_NORMAL);small=make_font(13,FW_NORMAL);heading=make_font(19,FW_SEMIBOLD);title=make_font(30,FW_SEMIBOLD);brand=make_font(62,FW_BOLD,L"Georgia");
    for(auto& item:layout_items)SendMessageW(item.window,WM_SETFONT,reinterpret_cast<WPARAM>(item.small_font?small:font),FALSE);
    for(auto f:old)if(f)DeleteObject(f);
}
void resize_layout(){
    if(layout_items.empty())return;
    RECT client{};GetClientRect(root,&client);if(!client.right||!client.bottom)return;
    float scale=std::min(client.right/1040.f,client.bottom/790.f);
    bool changed=std::abs(scale-dpi)>.001f;dpi=scale;
    origin_x=(client.right-px(1040))/2;origin_y=(client.bottom-px(790))/2;
    if(changed)rebuild_fonts();
    for(auto& item:layout_items)SetWindowPos(item.window,nullptr,origin_x+px(item.x),origin_y+px(item.y),px(item.w),px(item.h),SWP_NOZORDER|SWP_NOACTIVATE);
    RedrawWindow(root,nullptr,nullptr,RDW_INVALIDATE|RDW_ALLCHILDREN);
}
void toggle_fullscreen(){
    if(!launcher_fullscreen){
        windowed_placement.length=sizeof(windowed_placement);GetWindowPlacement(root,&windowed_placement);
        MONITORINFO monitor{sizeof(monitor)};
        if(!GetMonitorInfoW(MonitorFromWindow(root,MONITOR_DEFAULTTONEAREST),&monitor))return;
        launcher_fullscreen=true;SetWindowLongPtrW(root,GWL_STYLE,WS_POPUP|WS_VISIBLE|WS_CLIPCHILDREN);
        SetWindowPos(root,HWND_TOP,monitor.rcMonitor.left,monitor.rcMonitor.top,monitor.rcMonitor.right-monitor.rcMonitor.left,monitor.rcMonitor.bottom-monitor.rcMonitor.top,SWP_FRAMECHANGED|SWP_NOOWNERZORDER);
    }else{
        launcher_fullscreen=false;SetWindowLongPtrW(root,GWL_STYLE,windowed_style|WS_VISIBLE);
        SetWindowPlacement(root,&windowed_placement);
        SetWindowPos(root,nullptr,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE|SWP_FRAMECHANGED);
    }
    SetWindowTextW(control(FULLSCREEN),launcher_fullscreen?L"Exit fullscreen (F11)":L"Fullscreen (F11)");
    resize_layout();
}
void create_controls(){
    rebuild_fonts();
    background=CreateSolidBrush(bg);fields=CreateSolidBrush(field);
    widget(FULLSCREEN,L"BUTTON",L"Fullscreen (F11)",810,46,188,34,BS_OWNERDRAW);
    label(L"WINDOW RESOLUTION",352,132);
    int ri=settings.width==1600?1:settings.width==1920?2:settings.width==2560?3:settings.width==3840?4:0;
    combo(RES,350,158,310,{L"1280 × 720",L"1600 × 900",L"1920 × 1080",L"2560 × 1440",L"3840 × 2160"},ri);
    label(L"DISPLAY MODE",690,132);
    combo(MODE,688,158,310,{L"Windowed",L"Borderless fullscreen"},settings.fullscreen?1:0);
    label(L"FRAME RATE CAP",352,214);
    combo(FPS,350,240,310,{L"30 FPS",L"60 FPS",L"90 FPS",L"Unlimited FPS"},settings.fps==60?1:settings.fps==90?2:settings.fps==0?3:0);
    label(L"UPSCALING",690,214);
    combo(UPSCALE,688,240,310,{L"Bicubic",L"Bilinear",L"AMD FSR 1 · spatial (EASU)",L"AMD FSR 3.1 · experimental"},settings.upscaler=="fsr31"?3:settings.upscaler=="fsr1"?2:settings.upscaler=="linear"?1:0);
    label(L"GAME LANGUAGE · USA DATA",29,278,250);combo(LANGUAGE,29,304,252,{L"English",L"Français",L"Español"},settings.language==3?1:settings.language==4?2:0);
    label(L"GITHUB UPDATE REPOSITORY",29,522,260);widget(RELEASE_REPO,L"EDIT",wide(settings.release_repo).c_str(),29,549,252,29,ES_AUTOHSCROLL|WS_BORDER);
    auto preview=widget(PRERELEASES,L"BUTTON",L"Include pre-releases",29,591,252,28,BS_AUTOCHECKBOX);SendMessageW(preview,BM_SETCHECK,settings.prereleases?BST_CHECKED:BST_UNCHECKED,0);
    widget(CHECK_RELEASE,L"BUTTON",L"Check for updates",29,633,252,36,BS_OWNERDRAW);widget(APPLY_RELEASE,L"BUTTON",L"Update package",29,680,252,36,BS_OWNERDRAW);EnableWindow(control(APPLY_RELEASE),FALSE);
    widget(CREDITS,L"BUTTON",L"Credits / licenses",29,733,252,32,BS_OWNERDRAW);
    label(L"INTERNAL RENDER RESOLUTION",352,296);
    int scale_index=settings.scale<0.6?0:settings.scale<0.9?1:settings.scale<1.1?2:settings.scale<1.4?3:settings.scale<1.8?4:settings.scale<2.5?5:6;
    combo(RENDER,350,322,310,{L"640 × 360",L"960 × 540",L"1280 × 720 (original)",L"1600 × 900",L"1920 × 1080",L"2560 × 1440",L"3840 × 2160"},scale_index);
    label(L"AUDIO VOLUME",690,296);
    auto slider=widget(VOLUME,TRACKBAR_CLASSW,L"Volume",688,318,255,38,TBS_HORZ|TBS_NOTICKS);SendMessageW(slider,TBM_SETRANGE,TRUE,MAKELPARAM(0,100));SendMessageW(slider,TBM_SETPOS,TRUE,settings.volume);
    volume_text=widget(0,L"STATIC",(std::to_wstring(settings.volume)+L"%").c_str(),950,325,55,25);
    auto mute=widget(MUTE,L"BUTTON",L"Mute audio",688,382,210,28,BS_AUTOCHECKBOX);SendMessageW(mute,BM_SETCHECK,settings.mute?BST_CHECKED:BST_UNCHECKED,0);
    auto warm=widget(PREWARM,L"BUTTON",L"Prepare cached effects · slower startup",350,414,645,28,BS_AUTOCHECKBOX);SendMessageW(warm,BM_SETCHECK,settings.prewarm?BST_CHECKED:BST_UNCHECKED,0);
    auto sync=widget(VSYNC,L"BUTTON",L"VSync · monitor refresh limit",350,382,315,28,BS_AUTOCHECKBOX);SendMessageW(sync,BM_SETCHECK,settings.vsync?BST_CHECKED:BST_UNCHECKED,0);update_limits();
    label(L"BASE GAME FOLDER (DUMP / INSTALL PACKAGE)",352,497,600);widget(GAME,L"EDIT",wide(settings.game).c_str(),350,522,548,29,ES_AUTOHSCROLL|WS_BORDER);widget(BROWSE_GAME,L"BUTTON",L"Browse",912,520,86,32,BS_OWNERDRAW);
    label(L"USA v208 UPDATE (DUMP / INSTALL PACKAGE)",352,560,600);widget(UPDATE,L"EDIT",wide(settings.update).c_str(),350,585,548,29,ES_AUTOHSCROLL|WS_BORDER);widget(BROWSE_UPDATE,L"BUTTON",L"Browse",912,583,86,32,BS_OWNERDRAW);
    widget(PLAY,L"BUTTON",std::filesystem::exists(prepared_runtime())?L"PLAY GAME":L"SET UP AND PLAY",350,685,224,50,BS_OWNERDRAW);widget(SAVE,L"BUTTON",L"Save settings",592,685,192,50,BS_OWNERDRAW);widget(LOGS,L"BUTTON",L"Open logs",802,685,196,50,BS_OWNERDRAW);
    status=widget(0,L"STATIC",settings.game.empty()?L"First run: choose your dumped game + USA v208 update, then click Set up and Play.":L"Ready to play.",352,746,650,24);SendMessageW(status,WM_SETFONT,reinterpret_cast<WPARAM>(small),TRUE);layout_items.back().small_font=true;
}
LRESULT CALLBACK proc(HWND w,UINT msg,WPARAM wp,LPARAM lp){
    switch(msg){
    case WM_CREATE:root=w;create_controls();SetTimer(w,1,500,nullptr);if(direct_play||!settings.game.empty())PostMessageW(w,initial_check_message,0,0);return 0;
    case initial_check_message:try{if(direct_play){direct_play=false;play();}else start_update(false);}catch(const std::exception& e){notice(wide(e.what()));}return 0;
    case update_result_message:{std::unique_ptr<UpdateResult> result(reinterpret_cast<UpdateResult*>(lp));if(update_worker.joinable())update_worker.join();update_busy=false;
        if(!result->error.empty()){available_release.reset();notice(wide(result->error));}
        else if(result->install){try{launcher::updates::launch_installer(result->staged,base);DestroyWindow(w);return 0;}catch(const std::exception& e){notice(wide(e.what()));}}
        else {available_release=result->release;checked_repo=result->repo;SetWindowTextW(control(APPLY_RELEASE),available_release?(L"Update to "+wide(available_release->tag)).c_str():L"Update package");notice(available_release?L"A package update is available. Your settings and saves will be preserved.":L"No newer compatible portable release found.");}
        update_controls();return 0;}
    case WM_SIZE:if(wp!=SIZE_MINIMIZED)resize_layout();return 0;
    case WM_GETMINMAXINFO:if(!launcher_fullscreen){auto limits=reinterpret_cast<MINMAXINFO*>(lp);UINT window_dpi=GetDpiForWindow(w);if(!window_dpi)window_dpi=96;RECT minimum{0,0,MulDiv(780,window_dpi,96),MulDiv(593,window_dpi,96)};AdjustWindowRectExForDpi(&minimum,windowed_style,FALSE,0,window_dpi);limits->ptMinTrackSize={minimum.right-minimum.left,minimum.bottom-minimum.top};}return 0;
    case WM_DPICHANGED:{auto suggested=reinterpret_cast<RECT*>(lp);if(!launcher_fullscreen)SetWindowPos(w,nullptr,suggested->left,suggested->top,suggested->right-suggested->left,suggested->bottom-suggested->top,SWP_NOZORDER|SWP_NOACTIVATE);resize_layout();return 0;}
    case WM_CLOSE:if(update_busy){notice(L"Wait for the update operation to finish before closing.");return 0;}DestroyWindow(w);return 0;
    case WM_ERASEBKGND:return 1;
    case WM_PAINT:paint();return 0;
    case WM_CTLCOLORSTATIC:case WM_CTLCOLOREDIT:case WM_CTLCOLORLISTBOX:case WM_CTLCOLORBTN:{auto dc=reinterpret_cast<HDC>(wp);SetTextColor(dc,ink);SetBkColor(dc,msg==WM_CTLCOLOREDIT || msg==WM_CTLCOLORLISTBOX?field:bg);return reinterpret_cast<LRESULT>(msg==WM_CTLCOLOREDIT || msg==WM_CTLCOLORLISTBOX?fields:background);}
    case WM_HSCROLL:SetWindowTextW(volume_text,(std::to_wstring(SendMessageW(control(VOLUME),TBM_GETPOS,0,0))+L"%").c_str());return 0;
    case WM_DRAWITEM:{auto d=reinterpret_cast<DRAWITEMSTRUCT*>(lp);bool primary=d->CtlID==PLAY;auto b=CreateSolidBrush(primary?gold:field);FillRect(d->hDC,&d->rcItem,b);DeleteObject(b);SetBkMode(d->hDC,TRANSPARENT);SetTextColor(d->hDC,(d->itemState&ODS_DISABLED)?dim:primary?bg:ink);SelectObject(d->hDC,font);auto s=value(d->CtlID);DrawTextW(d->hDC,s.c_str(),-1,&d->rcItem,DT_CENTER|DT_VCENTER|DT_SINGLELINE);if(d->itemState&ODS_FOCUS)DrawFocusRect(d->hDC,&d->rcItem);return TRUE;}
    case WM_COMMAND:try {int id=LOWORD(wp);if(id==FPS && HIWORD(wp)==CBN_SELCHANGE)update_limits();if((id==UPSCALE || id==RES) && HIWORD(wp)==CBN_SELCHANGE)update_upscaling();if(HIWORD(wp)==BN_CLICKED){if(id==FULLSCREEN)toggle_fullscreen();else if(id==PLAY)play();else if(id==SAVE)save();else if(id==CHECK_RELEASE)start_update(false);else if(id==APPLY_RELEASE)start_update(true);else if(id==CREDITS)show_credits();else if(id==BROWSE_GAME)browse(GAME);else if(id==BROWSE_UPDATE)browse(UPDATE);else if(id==LOGS && !settings.user.empty())ShellExecuteW(w,L"open",std::filesystem::u8path(settings.user).c_str(),nullptr,nullptr,SW_SHOWNORMAL);}}catch(const std::exception& e){notice(wide(e.what()));MessageBoxW(w,wide(e.what()).c_str(),L"BOTW Launcher",MB_OK|MB_ICONERROR);}return 0;
    case WM_TIMER:
        if(preparation_process){
            std::ifstream in(std::filesystem::u8path(settings.user)/L"preparation-status.txt");std::string message;std::getline(in,message);if(!message.empty())notice(wide(message));
            if(WaitForSingleObject(preparation_process,0)==WAIT_OBJECT_0){
                DWORD code{};GetExitCodeProcess(preparation_process,&code);CloseHandle(preparation_process);preparation_process=nullptr;CloseHandle(preparation_job);preparation_job=nullptr;
                EnableWindow(control(PLAY),TRUE);EnableWindow(control(SAVE),TRUE);update_controls();
                if(!code){SetWindowTextW(control(PLAY),L"PLAY GAME");try{launch_saved();}catch(const std::exception& e){notice(wide(e.what()));}}else if(message.empty())notice(L"Preparation failed. Open logs for details; click Play to resume.");
            }
        }
        if(game_process && WaitForSingleObject(game_process,0)==WAIT_OBJECT_0){DWORD code{};GetExitCodeProcess(game_process,&code);CloseHandle(game_process);game_process=nullptr;EnableWindow(control(PLAY),TRUE);notice(code?L"Game exited with an error. Open logs for details.":L"Game closed. Ready to play.");}return 0;
    case WM_DESTROY:if(preparation_job){TerminateJobObject(preparation_job,1);CloseHandle(preparation_job);}if(preparation_process)CloseHandle(preparation_process);if(game_process)CloseHandle(game_process);for(auto f:{font,small,heading,title,brand})DeleteObject(f);DeleteObject(background);DeleteObject(fields);PostQuitMessage(0);return 0;
    }
    return DefWindowProcW(w,msg,wp,lp);
}
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int show){try{
    int applied=launcher::updates::apply_if_requested();if(applied>=0)return applied;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    dpi=GetDpiForSystem()/96.f;INITCOMMONCONTROLSEX common{sizeof(common),ICC_STANDARD_CLASSES|ICC_BAR_CLASSES};InitCommonControlsEx(&common);
    wchar_t module[32768];GetModuleFileNameW(nullptr,module,32768);base=std::filesystem::path(module).parent_path();config=base/L"windows-launch.ini";settings=launcher::load(config);if(settings.user.empty()){PWSTR folder{};if(SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData,0,nullptr,&folder))){settings.user=utf8((std::filesystem::path(folder)/L"BotWRecomp"/L"user").wstring());CoTaskMemFree(folder);}}detect_gpu();
    int argc{};auto arguments=CommandLineToArgvW(GetCommandLineW(),&argc);if(arguments){direct_play=argc==2&&std::wstring_view(arguments[1])==L"--play";LocalFree(arguments);}
    if(direct_play&&!settings.game.empty()&&std::filesystem::exists(prepared_runtime())){
        auto paths=launcher::read(std::filesystem::u8path(settings.user)/L"prepared-dumps.ini");
        if(!std::filesystem::exists(base/L"support/runtime-id.txt")||(paths["source_game"]==settings.game&&paths["source_update"]==settings.update)){
            launch_saved();CloseHandle(game_process);game_process=nullptr;CoUninitialize();return 0;
        }
    }
    WNDCLASSEXW wc{sizeof(wc)};wc.hInstance=instance;wc.lpfnWndProc=proc;wc.lpszClassName=L"BotWNativeLauncher";wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.hIcon=LoadIconW(nullptr,IDI_APPLICATION);RegisterClassExW(&wc);
    RECT size{0,0,px(1040),px(790)};AdjustWindowRectEx(&size,windowed_style,FALSE,0);
    root=CreateWindowExW(0,wc.lpszClassName,L"Breath of the Wild · Native Launcher",windowed_style,CW_USEDEFAULT,CW_USEDEFAULT,size.right-size.left,size.bottom-size.top,nullptr,nullptr,instance,nullptr);
    BOOL dark=TRUE;DwmSetWindowAttribute(root,20,&dark,sizeof(dark));ShowWindow(root,show);UpdateWindow(root);
    MSG message{};while(GetMessageW(&message,nullptr,0,0)>0){
        if(message.message==WM_KEYDOWN&&(message.wParam==VK_F11||(message.wParam==VK_ESCAPE&&launcher_fullscreen))){if(!(message.lParam&(1LL<<30)))toggle_fullscreen();continue;}
        if(!IsDialogMessageW(root,&message)){TranslateMessage(&message);DispatchMessageW(&message);}}
    if(update_worker.joinable())update_worker.join();CoUninitialize();return 0;
}catch(const std::exception& e){MessageBoxW(nullptr,wide(e.what()).c_str(),L"BOTW Launcher",MB_ICONERROR);return 1;}}
