#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <winhttp.h>
#include <bcrypt.h>
#include <shellapi.h>
#include "launcher_update.h"
#include <array>
#include <fstream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace launcher::updates {
namespace {
struct Http { HINTERNET h{};Http(HINTERNET v):h(v){if(!h)throw std::runtime_error("Could not open HTTPS connection");}~Http(){WinHttpCloseHandle(h);}operator HINTERNET()const{return h;} };
std::wstring wide(const std::string& s){if(s.empty())return {};int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),int(s.size()),nullptr,0);if(!n)throw std::runtime_error("Invalid UTF-8 URL");std::wstring v(n,0);MultiByteToWideChar(CP_UTF8,0,s.data(),int(s.size()),v.data(),n);return v;}
std::filesystem::path module(){wchar_t p[32768];auto n=GetModuleFileNameW(nullptr,p,32768);if(!n || n>=32768)throw std::runtime_error("Cannot locate launcher");return std::filesystem::path(p);}
void fetch(const std::string& url,uint64_t limit,std::ostream& output) {
    auto full=wide(url);URL_COMPONENTS u{};u.dwStructSize=sizeof(u);u.dwHostNameLength=DWORD(-1);u.dwUrlPathLength=DWORD(-1);u.dwExtraInfoLength=DWORD(-1);
    if(!WinHttpCrackUrl(full.c_str(),0,0,&u) || u.nScheme!=INTERNET_SCHEME_HTTPS || u.nPort!=443)throw std::runtime_error("Update URL must use HTTPS");
    Http session(WinHttpOpen(L"BotWLauncher/0.1",WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0));
    WinHttpSetTimeouts(session,10000,10000,10000,30000);
    std::wstring host(u.lpszHostName,u.dwHostNameLength),path(u.lpszUrlPath,u.dwUrlPathLength);if(u.dwExtraInfoLength)path.append(u.lpszExtraInfo,u.dwExtraInfoLength);
    Http connection(WinHttpConnect(session,host.c_str(),443,0));
    Http request(WinHttpOpenRequest(connection,L"GET",path.c_str(),nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,WINHTTP_FLAG_SECURE));
    const wchar_t* headers=L"Accept: application/vnd.github+json\r\nX-GitHub-Api-Version: 2022-11-28\r\n";
    if(!WinHttpSendRequest(request,headers,DWORD(-1),nullptr,0,0,0) || !WinHttpReceiveResponse(request,nullptr))throw std::runtime_error("GitHub update request failed; check your connection");
    DWORD status=0,length=sizeof(status);if(!WinHttpQueryHeaders(request,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,nullptr,&status,&length,nullptr))throw std::runtime_error("Missing HTTP status");
    if(status!=200)throw std::runtime_error(status==403?"GitHub update checks are rate limited; try later":status==404?"Update repository or release asset not found":"GitHub returned HTTP "+std::to_string(status));
    std::array<char,32768> chunk{};uint64_t total=0;DWORD got=0;
    for(;;){if(!WinHttpReadData(request,chunk.data(),DWORD(chunk.size()),&got))throw std::runtime_error("Update download was interrupted");if(!got)break;total+=got;if(total>limit)throw std::runtime_error("Update response exceeds the allowed size");output.write(chunk.data(),got);if(!output)throw std::runtime_error("Cannot write update data");}
}
bool run(const std::filesystem::path& exe,std::wstring args) {
    std::wstring command=L"\""+exe.wstring()+L"\""+args;STARTUPINFOW si{sizeof(si)};PROCESS_INFORMATION pi{};
    if(!CreateProcessW(exe.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,exe.parent_path().c_str(),&si,&pi))return false;
    CloseHandle(pi.hThread);CloseHandle(pi.hProcess);return true;
}
void require_regular(const std::filesystem::path& path){auto s=std::filesystem::symlink_status(path);if(!std::filesystem::is_regular_file(s) || std::filesystem::is_symlink(s))throw std::runtime_error("Update file is missing or is a symbolic link");}
}
std::string sha256(const std::filesystem::path& path) {
    BCRYPT_ALG_HANDLE alg{};BCRYPT_HASH_HANDLE hash{};
    if(BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)throw std::runtime_error("Cannot initialize SHA-256");
    struct Cleanup{BCRYPT_ALG_HANDLE& a;BCRYPT_HASH_HANDLE& h;~Cleanup(){if(h)BCryptDestroyHash(h);if(a)BCryptCloseAlgorithmProvider(a,0);}} cleanup{alg,hash};
    if(BCryptCreateHash(alg,&hash,nullptr,0,nullptr,0,0)<0)throw std::runtime_error("Cannot create SHA-256 hash");
    std::ifstream in(path,std::ios::binary);if(!in)throw std::runtime_error("Cannot read downloaded launcher");std::array<unsigned char,32768> buf{};
    while(in){in.read(reinterpret_cast<char*>(buf.data()),buf.size());if(in.gcount() && BCryptHashData(hash,buf.data(),ULONG(in.gcount()),0)<0)throw std::runtime_error("SHA-256 calculation failed");}if(!in.eof())throw std::runtime_error("Could not read the complete update");
    std::array<unsigned char,32> digest{};if(BCryptFinishHash(hash,digest.data(),ULONG(digest.size()),0)<0)throw std::runtime_error("SHA-256 calculation failed");
    std::string out;for(auto c:digest){out+="0123456789abcdef"[c>>4];out+="0123456789abcdef"[c&15];}return out;
}
std::optional<Release> check_github(const std::string& repo,bool previews) {
    if(!valid_repository(repo))throw std::runtime_error("Update repository must be owner/repository");
    std::ostringstream response;fetch("https://api.github.com/repos/"+repo+"/releases?per_page=100",4*1024*1024,response);return select_release(response.str(),repo,previews);
}
std::filesystem::path stage(const Release& release,const std::filesystem::path& base) {
    // The metadata policy constrains URLs and hashes. Revalidate before using a cached result.
    if(!release.url.starts_with("https://github.com/") || release.size==0 || release.size>64*1024*1024 || release.sha256.size()!=64)throw std::runtime_error("Invalid update metadata");
    auto parent=base/L".launcher-updates";std::filesystem::create_directories(parent);
    if(std::filesystem::is_symlink(std::filesystem::symlink_status(parent)))throw std::runtime_error("Update staging folder is a symbolic link");
    auto dir=parent/(std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    if(!std::filesystem::create_directory(dir))throw std::runtime_error("Cannot create a unique update folder");
    auto target=dir/L"BotWLauncher.exe";
    {std::ofstream out(target,std::ios::binary);if(!out)throw std::runtime_error("Launcher folder is not writable");fetch(release.url,release.size,out);out.flush();if(!out)throw std::runtime_error("Cannot finish writing the update");}
    if(std::filesystem::file_size(target)!=release.size || sha256(target)!=release.sha256)throw std::runtime_error("Update verification failed; the current launcher was not changed");
    {std::ifstream in(target,std::ios::binary);char magic[2]{};in.read(magic,2);if(magic[0]!='M'||magic[1]!='Z')throw std::runtime_error("Downloaded asset is not a Windows executable");}
    std::filesystem::copy_file(module(),dir/L"ApplyUpdate.exe");
    {std::ofstream out(dir/L"sha256.txt",std::ios::binary);out<<release.sha256;if(!out)throw std::runtime_error("Cannot write update verification record");}
    return dir;
}
void launch_installer(const std::filesystem::path& staged,const std::filesystem::path& base) {
    if(std::filesystem::weakly_canonical(staged.parent_path())!=std::filesystem::weakly_canonical(base/L".launcher-updates"))throw std::runtime_error("Unexpected update folder");
    if(!run(staged/L"ApplyUpdate.exe",L" --apply-launcher-update "+std::to_wstring(GetCurrentProcessId())))throw std::runtime_error("Cannot start the launcher updater");
}
int apply_if_requested() {
    int argc=0;auto argv=CommandLineToArgvW(GetCommandLineW(),&argc);if(!argv)throw std::runtime_error("Cannot read command line");
    struct Free {LPWSTR* p;~Free(){LocalFree(p);}} free{argv};
    if(argc<2 || std::wstring_view(argv[1])!=L"--apply-launcher-update")return -1;
    try {
        if(argc!=3)throw std::runtime_error("Invalid updater arguments");wchar_t* end{};auto pid=wcstoul(argv[2],&end,10);if(!pid||*end||pid==GetCurrentProcessId())throw std::runtime_error("Invalid launcher process");
        auto helper=module(),dir=helper.parent_path();if(helper.filename()!=L"ApplyUpdate.exe" || dir.parent_path().filename()!=L".launcher-updates")throw std::runtime_error("Unexpected updater location");
        auto base=dir.parent_path().parent_path(),target=base/L"BotWLauncher.exe",download=dir/L"BotWLauncher.exe",backup=base/L"BotWLauncher.previous.exe";
        require_regular(download);require_regular(target);std::ifstream record(dir/L"sha256.txt");std::string expected;record>>expected;
        HANDLE parent=OpenProcess(SYNCHRONIZE,FALSE,DWORD(pid));if(parent){auto result=WaitForSingleObject(parent,60000);CloseHandle(parent);if(result!=WAIT_OBJECT_0)throw std::runtime_error("Launcher did not close; update was not installed");}else if(GetLastError()!=ERROR_INVALID_PARAMETER)throw std::runtime_error("Cannot verify the old launcher has closed");
        if(expected.size()!=64 || sha256(download)!=expected)throw std::runtime_error("Update hash changed before installation");
        if(!ReplaceFileW(target.c_str(),download.c_str(),backup.c_str(),0,nullptr,nullptr))throw std::runtime_error("Could not replace the launcher; the current version is preserved");
        if(!run(target,L"")){ReplaceFileW(target.c_str(),backup.c_str(),nullptr,0,nullptr,nullptr);throw std::runtime_error("New launcher could not start; attempted to restore the previous version");}
        return 0;
    } catch(const std::exception& e){MessageBoxW(nullptr,wide(e.what()).c_str(),L"Launcher update failed",MB_OK|MB_ICONERROR);return 1;}
}
}
