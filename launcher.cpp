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
enum { RES=100,MODE,FPS,UPSCALE,RENDER,VOLUME,MUTE,VSYNC,GAME,UPDATE,BROWSE_GAME,BROWSE_UPDATE,PLAY,SAVE,LOGS,PREWARM,LANGUAGE,RELEASE_REPO,PRERELEASES,CHECK_RELEASE,APPLY_RELEASE,CREDITS };
HWND root{},status{},volume_text{};std::array<HWND,120> controls{};
HFONT font{},small{},heading{},title{},brand{};HBRUSH background{},fields{};
launcher::Settings settings;std::filesystem::path base,config;
std::wstring gpu=L"GPU information unavailable";unsigned vendor{};HANDLE game_process{};
float dpi=1.f;
constexpr UINT update_result_message=WM_APP+1,initial_check_message=WM_APP+2;
struct UpdateResult {std::optional<launcher::updates::Release> release;std::filesystem::path staged;std::string error,repo;bool install=false;};
std::thread update_worker;bool update_busy=false;
std::optional<launcher::updates::Release> available_release;std::string checked_repo;
int px(int x){return int(x*dpi);}
std::wstring wide(const std::string& s){if(s.empty())return {};int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),s.size(),nullptr,0);if(!n)throw std::runtime_error("Invalid UTF-8 in settings");std::wstring out(n,0);MultiByteToWideChar(CP_UTF8,0,s.data(),s.size(),out.data(),n);return out;}
std::string utf8(const std::wstring& s){if(s.empty())return {};int n=WideCharToMultiByte(CP_UTF8,0,s.data(),s.size(),nullptr,0,nullptr,nullptr);std::string out(n,0);WideCharToMultiByte(CP_UTF8,0,s.data(),s.size(),out.data(),n,nullptr,nullptr);return out;}
HFONT make_font(int size,int weight,const wchar_t* face=L"Segoe UI"){return CreateFontW(-px(size),0,0,0,weight,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,face);}
void text(HDC dc,const wchar_t* s,int x,int y,int w,int h,HFONT f,COLORREF color,UINT flags=DT_LEFT|DT_WORDBREAK){SelectObject(dc,f);SetTextColor(dc,color);SetBkMode(dc,TRANSPARENT);RECT r{px(x),px(y),px(x+w),px(y+h)};DrawTextW(dc,s,-1,&r,flags);}
void rect(HDC dc,int x,int y,int w,int h,COLORREF color){RECT r{px(x),px(y),px(x+w),px(y+h)};auto b=CreateSolidBrush(color);FillRect(dc,&r,b);DeleteObject(b);}
HWND widget(int id,const wchar_t* cls,const wchar_t* value,int x,int y,int w,int h,DWORD style=0){auto handle=CreateWindowExW(0,cls,value,WS_CHILD|WS_VISIBLE|WS_TABSTOP|style,px(x),px(y),px(w),px(h),root,reinterpret_cast<HMENU>(INT_PTR(id)),GetModuleHandleW(nullptr),nullptr);SendMessageW(handle,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);if(id>=100)controls[id-100]=handle;return handle;}
HWND control(int id){return controls[id-100];}
void label(const wchar_t* value,int x,int y,int w=300){auto h=CreateWindowExW(0,L"STATIC",value,WS_CHILD|WS_VISIBLE,px(x),px(y),px(w),px(22),root,nullptr,nullptr,nullptr);SendMessageW(h,WM_SETFONT,reinterpret_cast<WPARAM>(small),TRUE);}
void combo(int id,int x,int y,int w,std::initializer_list<const wchar_t*> items,int chosen){auto h=widget(id,L"COMBOBOX",L"",x,y,w,260,CBS_DROPDOWNLIST|WS_VSCROLL);for(auto item:items)SendMessageW(h,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(item));SendMessageW(h,CB_SETCURSEL,chosen,0);}
int selected(int id){return int(SendMessageW(control(id),CB_GETCURSEL,0,0));}
std::wstring value(int id){int n=GetWindowTextLengthW(control(id));std::wstring s(n+1,0);GetWindowTextW(control(id),s.data(),n+1);s.resize(n);return s;}
void notice(const std::wstring& s){SetWindowTextW(status,s.c_str());}
void show_credits(){auto path=base/L"Launcher-CREDITS.txt";{std::ofstream out(path,std::ios::binary);out<<launcher::license_text;if(!out)throw std::runtime_error("Cannot write the credits file beside the launcher");}ShellExecuteW(root,L"open",path.c_str(),nullptr,nullptr,SW_SHOWNORMAL);}
void detect_gpu(){IDXGIFactory1* factory{};if(FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1),reinterpret_cast<void**>(&factory))))return;for(UINT i=0;;i++){IDXGIAdapter1* adapter{};if(factory->EnumAdapters1(i,&adapter)==DXGI_ERROR_NOT_FOUND)break;DXGI_ADAPTER_DESC1 d{};adapter->GetDesc1(&d);adapter->Release();if(!(d.Flags&DXGI_ADAPTER_FLAG_SOFTWARE)){gpu=d.Description;vendor=d.VendorId;break;}}factory->Release();}
void browse(int id){IFileOpenDialog* dialog{};if(FAILED(CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&dialog))))return;DWORD options{};dialog->GetOptions(&options);dialog->SetOptions(options|FOS_PICKFOLDERS|FOS_FORCEFILESYSTEM);dialog->SetTitle(id==GAME?L"Select BOTW base game folder":L"Select BOTW update folder");if(SUCCEEDED(dialog->Show(root))){IShellItem* item{};if(SUCCEEDED(dialog->GetResult(&item))){PWSTR path{};if(SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH,&path))){SetWindowTextW(control(id),path);CoTaskMemFree(path);}item->Release();}}dialog->Release();}
void update_limits(){bool high=selected(FPS)>=2;if(high)SendMessageW(control(VSYNC),BM_SETCHECK,BST_UNCHECKED,0);EnableWindow(control(VSYNC),!high);}
void collect(){
    static constexpr int widths[]={1280,1600,1920,2560,3840},heights[]={720,900,1080,1440,2160},caps[]={30,60,90,0};
    static constexpr float scales[]={0.5f,0.75f,1.f,1.25f,1.5f,2.f,3.f};
    const char* filters[]={"bicubic","linear","fsr1"};
    settings.width=widths[std::clamp(selected(RES),0,4)];settings.height=heights[std::clamp(selected(RES),0,4)];
    settings.fps=caps[std::clamp(selected(FPS),0,3)];settings.scale=scales[std::clamp(selected(RENDER),0,6)];
    settings.upscaler=filters[std::clamp(selected(UPSCALE),0,2)];settings.fullscreen=selected(MODE)==1;
    settings.volume=int(SendMessageW(control(VOLUME),TBM_GETPOS,0,0));settings.mute=SendMessageW(control(MUTE),BM_GETCHECK,0,0)==BST_CHECKED;
    settings.prewarm=SendMessageW(control(PREWARM),BM_GETCHECK,0,0)==BST_CHECKED;
    static constexpr int languages[]={1,3,4};settings.language=languages[std::clamp(selected(LANGUAGE),0,2)];
    settings.prereleases=SendMessageW(control(PRERELEASES),BM_GETCHECK,0,0)==BST_CHECKED;settings.release_repo=utf8(value(RELEASE_REPO));
    settings.vsync=settings.fps!=0 && settings.fps<=60 && SendMessageW(control(VSYNC),BM_GETCHECK,0,0)==BST_CHECKED;settings.game=utf8(value(GAME));settings.update=utf8(value(UPDATE));
    settings.validate();
}
void update_controls(){EnableWindow(control(CHECK_RELEASE),!update_busy);EnableWindow(control(APPLY_RELEASE),!update_busy && available_release.has_value());EnableWindow(control(RELEASE_REPO),!update_busy);EnableWindow(control(PRERELEASES),!update_busy);}
void start_update(bool install){
    if(update_busy)return;collect();
    if(!launcher::updates::valid_repository(settings.release_repo))throw std::runtime_error("Update repository must be owner/repository");
    if(install && (!available_release || checked_repo!=settings.release_repo))throw std::runtime_error("Check this repository for updates first");
    launcher::save(config,settings);update_busy=true;update_controls();notice(install?L"Downloading and verifying launcher update…":L"Checking GitHub releases…");
    if(update_worker.joinable())update_worker.join();auto repo=settings.release_repo;bool previews=settings.prereleases;auto release=available_release;
    update_worker=std::thread([install,repo,previews,release]{
        auto result=std::make_unique<UpdateResult>();result->install=install;result->repo=repo;
        try {if(install)result->staged=launcher::updates::stage(*release,base);else result->release=launcher::updates::check_github(repo,previews);}
        catch(const std::exception& e){result->error=e.what();}
        auto raw=result.release();if(!PostMessageW(root,update_result_message,0,reinterpret_cast<LPARAM>(raw)))delete raw;
    });
}
void save(){collect();launcher::save(config,settings);notice(L"Settings saved.");}
void play(){
    if(game_process)return;
    collect();
    auto game_root=std::filesystem::u8path(settings.game),update_root=std::filesystem::u8path(settings.update);
    if(!std::filesystem::exists(game_root/L"code/U-King.rpx"))throw std::runtime_error("Select the base game folder containing code/U-King.rpx.");
    if(!settings.update.empty() && !std::filesystem::exists(update_root/L"code/U-King.rpx"))throw std::runtime_error("Select the update folder containing code/U-King.rpx.");
    if(settings.user.empty())throw std::runtime_error("The save-data folder is missing from windows-launch.ini.");
    launcher::save(config,settings);auto exe=base/L"BotWWindowsHeadless.exe";
    if(!std::filesystem::exists(exe))throw std::runtime_error("The native game executable was not found beside the launcher.");
    std::wstring command=L"\""+exe.wstring()+L"\"";STARTUPINFOW start{sizeof(start)};PROCESS_INFORMATION process{};
    if(!CreateProcessW(exe.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,base.c_str(),&start,&process))throw std::runtime_error("Windows could not start the game (error "+std::to_string(GetLastError())+").");
    CloseHandle(process.hThread);game_process=process.hProcess;EnableWindow(control(PLAY),FALSE);notice(L"Game running. Your settings will apply on the next launch.");
}
void paint(){PAINTSTRUCT ps{};HDC dc=BeginPaint(root,&ps);RECT area{};GetClientRect(root,&area);FillRect(dc,&area,background);
    rect(dc,0,0,310,780,panel);rect(dc,28,52,40,3,gold);
    text(dc,L"THE LEGEND OF",29,85,255,28,small,gold);
    text(dc,L"ZELDA",24,112,275,75,brand,gold);
    text(dc,L"BREATH OF THE WILD",29,188,260,30,font,ink);
    text(dc,L"NATIVE WINDOWS",30,224,260,25,small,dim);
    rect(dc,29,355,252,1,RGB(54,76,77));text(dc,L"GRAPHICS DEVICE",29,372,260,25,small,gold);text(dc,gpu.c_str(),29,403,250,52,font,ink);
    text(dc,(L"Launcher v"+wide(launcher::updates::version)).c_str(),29,480,260,25,small,dim);
    text(dc,L"Launch settings",350,40,650,46,title,ink);text(dc,L"Graphics, performance and audio",352,88,650,28,font,dim);
    text(dc,vendor==0x10de?L"DLSS unavailable: temporal renderer integration is not implemented in this build.":L"DLSS unavailable on this GPU. NVIDIA RTX and temporal renderer integration are required.",352,451,650,37,small,dim);
    text(dc,L"90 FPS and Unlimited are experimental. Some cutscenes retain a 30 FPS limit.",352,628,650,40,small,dim);
    EndPaint(root,&ps);
}
void create_controls(){
    font=make_font(16,FW_NORMAL);small=make_font(13,FW_NORMAL);heading=make_font(19,FW_SEMIBOLD);title=make_font(30,FW_SEMIBOLD);brand=make_font(62,FW_BOLD,L"Georgia");
    background=CreateSolidBrush(bg);fields=CreateSolidBrush(field);
    label(L"WINDOW RESOLUTION",352,132);
    int ri=settings.width==1600?1:settings.width==1920?2:settings.width==2560?3:settings.width==3840?4:0;
    combo(RES,350,158,310,{L"1280 × 720",L"1600 × 900",L"1920 × 1080",L"2560 × 1440",L"3840 × 2160"},ri);
    label(L"DISPLAY MODE",690,132);
    combo(MODE,688,158,310,{L"Windowed",L"Borderless fullscreen"},settings.fullscreen?1:0);
    label(L"FRAME RATE CAP",352,214);
    combo(FPS,350,240,310,{L"30 FPS",L"60 FPS",L"90 FPS",L"Unlimited FPS"},settings.fps==60?1:settings.fps==90?2:settings.fps==0?3:0);
    label(L"UPSCALING",690,214);
    combo(UPSCALE,688,240,310,{L"Bicubic",L"Bilinear",L"AMD FSR 1 · spatial (EASU)",L"AMD FSR 3.1 · unavailable"},settings.upscaler=="fsr1"?2:settings.upscaler=="linear"?1:0);
    label(L"GAME LANGUAGE · USA DATA",29,278,250);combo(LANGUAGE,29,304,252,{L"English",L"Français",L"Español"},settings.language==3?1:settings.language==4?2:0);
    label(L"GITHUB UPDATE REPOSITORY",29,522,260);widget(RELEASE_REPO,L"EDIT",wide(settings.release_repo).c_str(),29,549,252,29,ES_AUTOHSCROLL|WS_BORDER);
    auto preview=widget(PRERELEASES,L"BUTTON",L"Include pre-releases",29,591,252,28,BS_AUTOCHECKBOX);SendMessageW(preview,BM_SETCHECK,settings.prereleases?BST_CHECKED:BST_UNCHECKED,0);
    widget(CHECK_RELEASE,L"BUTTON",L"Check for updates",29,633,252,36,BS_OWNERDRAW);widget(APPLY_RELEASE,L"BUTTON",L"Update launcher",29,680,252,36,BS_OWNERDRAW);EnableWindow(control(APPLY_RELEASE),FALSE);
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
    label(L"BASE GAME FOLDER",352,497,500);widget(GAME,L"EDIT",wide(settings.game).c_str(),350,522,548,29,ES_AUTOHSCROLL|WS_BORDER);widget(BROWSE_GAME,L"BUTTON",L"Browse",912,520,86,32,BS_OWNERDRAW);
    label(L"UPDATE FOLDER",352,560,500);widget(UPDATE,L"EDIT",wide(settings.update).c_str(),350,585,548,29,ES_AUTOHSCROLL|WS_BORDER);widget(BROWSE_UPDATE,L"BUTTON",L"Browse",912,583,86,32,BS_OWNERDRAW);
    widget(PLAY,L"BUTTON",L"PLAY GAME",350,685,224,50,BS_OWNERDRAW);widget(SAVE,L"BUTTON",L"Save settings",592,685,192,50,BS_OWNERDRAW);widget(LOGS,L"BUTTON",L"Open logs",802,685,196,50,BS_OWNERDRAW);
    status=widget(0,L"STATIC",L"Ready to play.",352,746,650,24);SendMessageW(status,WM_SETFONT,reinterpret_cast<WPARAM>(small),TRUE);
}
LRESULT CALLBACK proc(HWND w,UINT msg,WPARAM wp,LPARAM lp){
    switch(msg){
    case WM_CREATE:root=w;create_controls();SetTimer(w,1,500,nullptr);PostMessageW(w,initial_check_message,0,0);return 0;
    case initial_check_message:try{start_update(false);}catch(const std::exception& e){notice(wide(e.what()));}return 0;
    case update_result_message:{std::unique_ptr<UpdateResult> result(reinterpret_cast<UpdateResult*>(lp));if(update_worker.joinable())update_worker.join();update_busy=false;
        if(!result->error.empty()){available_release.reset();notice(wide(result->error));}
        else if(result->install){try{launcher::updates::launch_installer(result->staged,base);DestroyWindow(w);return 0;}catch(const std::exception& e){notice(wide(e.what()));}}
        else {available_release=result->release;checked_repo=result->repo;SetWindowTextW(control(APPLY_RELEASE),available_release?(L"Update to "+wide(available_release->tag)).c_str():L"Update launcher");notice(available_release?L"A launcher update is available. Your game files and saves will be preserved.":L"No newer compatible launcher release found.");}
        update_controls();return 0;}
    case WM_CLOSE:if(update_busy){notice(L"Wait for the update operation to finish before closing.");return 0;}DestroyWindow(w);return 0;
    case WM_ERASEBKGND:return 1;
    case WM_PAINT:paint();return 0;
    case WM_CTLCOLORSTATIC:case WM_CTLCOLOREDIT:case WM_CTLCOLORLISTBOX:case WM_CTLCOLORBTN:{auto dc=reinterpret_cast<HDC>(wp);SetTextColor(dc,ink);SetBkColor(dc,msg==WM_CTLCOLOREDIT || msg==WM_CTLCOLORLISTBOX?field:bg);return reinterpret_cast<LRESULT>(msg==WM_CTLCOLOREDIT || msg==WM_CTLCOLORLISTBOX?fields:background);}
    case WM_HSCROLL:SetWindowTextW(volume_text,(std::to_wstring(SendMessageW(control(VOLUME),TBM_GETPOS,0,0))+L"%").c_str());return 0;
    case WM_DRAWITEM:{auto d=reinterpret_cast<DRAWITEMSTRUCT*>(lp);bool primary=d->CtlID==PLAY;auto b=CreateSolidBrush(primary?gold:field);FillRect(d->hDC,&d->rcItem,b);DeleteObject(b);SetBkMode(d->hDC,TRANSPARENT);SetTextColor(d->hDC,(d->itemState&ODS_DISABLED)?dim:primary?bg:ink);SelectObject(d->hDC,font);auto s=value(d->CtlID);DrawTextW(d->hDC,s.c_str(),-1,&d->rcItem,DT_CENTER|DT_VCENTER|DT_SINGLELINE);if(d->itemState&ODS_FOCUS)DrawFocusRect(d->hDC,&d->rcItem);return TRUE;}
    case WM_COMMAND:try {int id=LOWORD(wp);if(id==FPS && HIWORD(wp)==CBN_SELCHANGE)update_limits();if(id==UPSCALE && HIWORD(wp)==CBN_SELCHANGE && selected(UPSCALE)==3){SendMessageW(control(UPSCALE),CB_SETCURSEL,settings.upscaler=="fsr1"?2:settings.upscaler=="linear"?1:0,0);notice(L"FSR 3.1 is unavailable: game motion vectors, depth and jitter still need integration.");MessageBoxW(w,L"FSR 3.1 is not available in gameplay yet. The renderer still needs game motion vectors, depth, jitter and HUD separation. Your existing upscaler remains selected.",L"FSR 3.1 availability",MB_OK|MB_ICONINFORMATION);}if(HIWORD(wp)==BN_CLICKED){if(id==PLAY)play();else if(id==SAVE)save();else if(id==CHECK_RELEASE)start_update(false);else if(id==APPLY_RELEASE)start_update(true);else if(id==CREDITS)show_credits();else if(id==BROWSE_GAME)browse(GAME);else if(id==BROWSE_UPDATE)browse(UPDATE);else if(id==LOGS && !settings.user.empty())ShellExecuteW(w,L"open",std::filesystem::u8path(settings.user).c_str(),nullptr,nullptr,SW_SHOWNORMAL);}}catch(const std::exception& e){notice(wide(e.what()));MessageBoxW(w,wide(e.what()).c_str(),L"BOTW Launcher",MB_OK|MB_ICONERROR);}return 0;
    case WM_TIMER:if(game_process && WaitForSingleObject(game_process,0)==WAIT_OBJECT_0){DWORD code{};GetExitCodeProcess(game_process,&code);CloseHandle(game_process);game_process=nullptr;EnableWindow(control(PLAY),TRUE);notice(code?L"Game exited with an error. Open logs for details.":L"Game closed. Ready to play.");}return 0;
    case WM_DESTROY:if(game_process)CloseHandle(game_process);for(auto f:{font,small,heading,title,brand})DeleteObject(f);DeleteObject(background);DeleteObject(fields);PostQuitMessage(0);return 0;
    }
    return DefWindowProcW(w,msg,wp,lp);
}
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int show){try{
    int applied=launcher::updates::apply_if_requested();if(applied>=0)return applied;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    dpi=GetDpiForSystem()/96.f;INITCOMMONCONTROLSEX common{sizeof(common),ICC_STANDARD_CLASSES|ICC_BAR_CLASSES};InitCommonControlsEx(&common);
    wchar_t module[32768];GetModuleFileNameW(nullptr,module,32768);base=std::filesystem::path(module).parent_path();config=base/L"windows-launch.ini";settings=launcher::load(config);if(settings.user.empty()){PWSTR folder{};if(SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData,0,nullptr,&folder))){settings.user=utf8((std::filesystem::path(folder)/L"BotWRecomp"/L"user").wstring());CoTaskMemFree(folder);}}detect_gpu();
    WNDCLASSEXW wc{sizeof(wc)};wc.hInstance=instance;wc.lpfnWndProc=proc;wc.lpszClassName=L"BotWNativeLauncher";wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.hIcon=LoadIconW(nullptr,IDI_APPLICATION);RegisterClassExW(&wc);
    RECT size{0,0,px(1040),px(790)};AdjustWindowRectEx(&size,WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,FALSE,0);
    root=CreateWindowExW(0,wc.lpszClassName,L"Breath of the Wild · Native Launcher",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,CW_USEDEFAULT,CW_USEDEFAULT,size.right-size.left,size.bottom-size.top,nullptr,nullptr,instance,nullptr);
    BOOL dark=TRUE;DwmSetWindowAttribute(root,20,&dark,sizeof(dark));ShowWindow(root,show);UpdateWindow(root);
    MSG message{};while(GetMessageW(&message,nullptr,0,0)>0){if(!IsDialogMessageW(root,&message)){TranslateMessage(&message);DispatchMessageW(&message);}}
    if(update_worker.joinable())update_worker.join();CoUninitialize();return 0;
}catch(const std::exception& e){MessageBoxW(nullptr,wide(e.what()).c_str(),L"BOTW Launcher",MB_ICONERROR);return 1;}}
