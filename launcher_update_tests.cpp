#include "launcher_update.h"
#include <cstdio>
#include <stdexcept>
using namespace launcher::updates;
#define CHECK(x) do { if(!(x))throw std::runtime_error(#x); } while(0)
std::string release(std::string tag="v0.3.0-preview.1",std::string extra="",std::string asset_extra="") {
    return R"({"tag_name":")"+tag+R"(","draft":false,"prerelease":true,"assets":[{"name":"BotWRecomp-Windows-x64.zip","state":"uploaded","size":1024,"browser_download_url":"https://github.com/test/launcher/releases/download/)"+tag+R"(/BotWRecomp-Windows-x64.zip","digest":"sha256:)"+std::string(64,'a')+R"(")"+asset_extra+R"(}])"+extra+"}";
}
int main(){try{
    CHECK(valid_repository("test/launcher"));
    for(auto bad:{"", "test", "../launcher", "test/launcher/extra", "test/a?x=y", "test/a\n", "/launcher", "test/"})CHECK(!valid_repository(bad));
    CHECK(newer("v0.1.0-preview.2","0.1.0-preview.1"));CHECK(newer("0.3.0",version));CHECK(newer("0.10.0","0.9.0"));
    CHECK(!newer(version,version));CHECK(!newer("0.1.0-preview.1","0.1.0"));CHECK(!newer("v01.2.0",version));CHECK(!newer("garbage",version));
    auto data="["+release()+"]";auto r=select_release(data,"test/launcher",true);CHECK(r && r->size==1024 && r->sha256==std::string(64,'a'));
    CHECK(!select_release(data,"test/launcher",false));CHECK(!select_release(data,"another/repo",true));
    auto changed=data;auto pos=changed.find("https://github.com/");changed.replace(pos,18,"https://evil.test/");CHECK(!select_release(changed,"test/launcher",true));
    for(auto field:{std::string("\"digest\""),std::string("\"name\""),std::string("\"state\"")}){changed=data;changed.replace(changed.find(field),field.size(),"\"unexpected\"");CHECK(!select_release(changed,"test/launcher",true));}
    changed=data;changed.replace(changed.find("\"draft\":false"),13,"\"draft\":true");CHECK(!select_release(changed,"test/launcher",true));
    CHECK(!select_release("[]","test/launcher",true));
    CHECK(select_release("["+release("v0.2.0-preview.3")+","+release("v0.2.0-preview.2")+"]","test/launcher",true)->tag=="v0.2.0-preview.3");
    changed="["+release("v0.3.0")+"]";auto p=changed.find("\"prerelease\":true");changed.replace(p,17,"\"prerelease\":false");CHECK(select_release(changed,"test/launcher",false));
    unsigned rejected=0;for(auto invalid:{"{}","not json"})try{select_release(invalid,"test/launcher",true);}catch(...){++rejected;}CHECK(rejected==2);
    puts("PASS release versions, preview channel, drafts, URL/repository pinning, digest and malformed metadata");return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL %s\n",e.what());return 1;}}
