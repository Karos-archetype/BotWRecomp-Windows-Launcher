#include "launcher_update.h"
#include <boost/json/src.hpp>
#include <algorithm>
#include <array>
#include <charconv>
#include <regex>
#include <stdexcept>
#include <tuple>

namespace launcher::updates {
namespace {
using Version=std::array<uint32_t,5>;
std::optional<Version> parse(std::string_view text) {
    static const std::regex pattern(R"(^v?([0-9]+)\.([0-9]+)\.([0-9]+)(?:-(alpha|beta|preview|rc)\.([0-9]+))?$)");
    std::smatch m;std::string s(text);if(!std::regex_match(s,m,pattern))return {};
    Version v{};
    for(int i=0;i<3;i++) {auto str=m[i+1].str();if(str.size()>1 && str[0]=='0')return {};auto [p,e]=std::from_chars(str.data(),str.data()+str.size(),v[i]);if(e!=std::errc{})return {};}
    v[3]=4;
    if(m[4].matched) {const std::string names[]={"alpha","beta","preview","rc"};v[3]=uint32_t(std::find(std::begin(names),std::end(names),m[4].str())-std::begin(names));auto str=m[5].str();if(str.size()>1 && str[0]=='0')return {};auto [p,e]=std::from_chars(str.data(),str.data()+str.size(),v[4]);if(e!=std::errc{})return {};}
    return v;
}
bool hex_digest(std::string_view s){return s.size()==64 && std::all_of(s.begin(),s.end(),[](char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f');});}
std::string string(const boost::json::object& o,const char* key){auto p=o.if_contains(key);return p&&p->is_string()?std::string(p->as_string()):std::string{};}
bool flag(const boost::json::object& o,const char* key){auto p=o.if_contains(key);return p&&p->is_bool()&&p->as_bool();}
}
bool valid_repository(std::string_view repo) {
    auto slash=repo.find('/');if(slash==0 || slash==repo.npos || slash+1==repo.size() || repo.size()>200)return false;
    return std::all_of(repo.begin(),repo.end(),[](char c){return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'||c=='_'||c=='.'||c=='/';})
        && repo.find('/',slash+1)==repo.npos && repo.find("..") == repo.npos;
}
bool newer(std::string_view candidate,std::string_view current){auto a=parse(candidate),b=parse(current);return a&&b&&*a>*b;}
std::optional<Release> select_release(std::string_view json,std::string_view repo,bool previews,std::string_view current) {
    if(!valid_repository(repo))throw std::runtime_error("Update repository must be owner/repository");
    boost::system::error_code error;auto root=boost::json::parse(json,error);
    if(error || !root.is_array())throw std::runtime_error("GitHub returned invalid release information");
    std::optional<Release> best;
    for(const auto& value:root.as_array()) {
        if(!value.is_object())continue;const auto& r=value.as_object();auto tag=string(r,"tag_name");auto v=parse(tag);
        if(flag(r,"draft") || !v || (!previews && (flag(r,"prerelease") || (*v)[3]!=4)) || !newer(tag,best?best->tag:std::string(current)))continue;
        auto assets=r.if_contains("assets");if(!assets || !assets->is_array())continue;
        for(const auto& asset:assets->as_array()) {
            if(!asset.is_object())continue;const auto& a=asset.as_object();
            if(string(a,"name")!=asset_name || string(a,"state")!="uploaded")continue;
            auto url=string(a,"browser_download_url"),digest=string(a,"digest");
            auto prefix="https://github.com/"+std::string(repo)+"/releases/download/"+tag+"/";
            if(url!=prefix+asset_name || !digest.starts_with("sha256:") || !hex_digest(std::string_view(digest).substr(7)))continue;
            auto size=a.if_contains("size");if(!size || !size->is_int64() || size->as_int64()<=0 || size->as_int64()>1536LL*1024*1024)continue;
            best=Release{tag,url,digest.substr(7),uint64_t(size->as_int64())};break;
        }
    }
    return best;
}
}
