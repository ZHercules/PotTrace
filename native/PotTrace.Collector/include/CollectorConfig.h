#pragma once
#include <windows.h>
#include <atomic>
#include <string>
#include <string_view>
#include <map>
#include <charconv>
#include <filesystem>
#include <fstream>
#include <memory>

namespace pottrace {
struct CapturePolicy {
 bool valid=false,enabled=false,paused=true;uint64_t revision=0;
 bool allowed() const noexcept{return valid&&enabled&&!paused;}
};
// The policy deliberately has a small, strict schema. Unknown/duplicate keys,
// missing members or malformed JSON fail closed instead of defaulting enabled.
inline CapturePolicy ParseCapturePolicy(std::string_view text) {
 CapturePolicy p;size_t i=0;std::map<std::string,std::string> fields;
 auto ws=[&]{while(i<text.size()&&(text[i]==' '||text[i]=='\r'||text[i]=='\n'||text[i]=='\t'))++i;};
 auto token=[&](char c){ws();if(i>=text.size()||text[i]!=c)return false;++i;return true;};
 if(!token('{'))return p;
 for(int n=0;n<3;++n){
  if(n&&!token(','))return p;if(!token('"'))return p;auto start=i;
  while(i<text.size()&&text[i]!='"'){if(text[i]=='\\'||static_cast<unsigned char>(text[i])<32)return p;++i;}
  if(i==text.size())return p;std::string key(text.substr(start,i-start));++i;
  if(!token(':'))return p;ws();start=i;while(i<text.size()&&text[i]!=','&&text[i]!='}'&&text[i]!=' '&&text[i]!='\r'&&text[i]!='\n'&&text[i]!='\t')++i;
  if(!fields.emplace(key,std::string(text.substr(start,i-start))).second)return p;
 }
 if(!token('}'))return p;ws();if(i!=text.size()||fields.size()!=3||!fields.count("Enabled")||!fields.count("RecordingPaused")||!fields.count("Revision"))return p;
 auto boolean=[](const std::string& value){return value=="true"||value=="false";};
 if(!boolean(fields["Enabled"])||!boolean(fields["RecordingPaused"]))return p;
 auto& revision=fields["Revision"];auto result=std::from_chars(revision.data(),revision.data()+revision.size(),p.revision);
 if(result.ec!=std::errc()||result.ptr!=revision.data()+revision.size()||p.revision==0||p.revision>INT64_MAX)return {};
 p.valid=true;p.enabled=fields["Enabled"]=="true";p.paused=fields["RecordingPaused"]=="true";return p;
}
inline bool IsAbsoluteLocalPath(std::wstring_view p){return p.size()>3&&((p[0]>=L'A'&&p[0]<=L'Z')||(p[0]>=L'a'&&p[0]<=L'z'))&&p[1]==L':'&&(p[2]==L'\\'||p[2]==L'/');}
class CaptureGate {
 std::atomic<bool> enabled{false},suppressed{false};std::atomic<uint64_t> change{0};
public:
 bool allowed() const noexcept{return enabled&&!suppressed;}
 uint64_t revision() const noexcept{return change.load();}
 void update(CapturePolicy policy,bool pluginEnabled) noexcept{bool next=policy.allowed()&&pluginEnabled&&!suppressed;if(enabled.exchange(next)!=next)++change;}
 void suppress() noexcept{suppressed=true;if(enabled.exchange(false))++change;}
};
struct CollectorConfig {bool valid=false;std::wstring bridge,policy,playerIni;bool registryProfile=false;};
inline std::wstring ReadCollectorIni(const std::wstring& file,const wchar_t* section,const wchar_t* key){wchar_t value[32768]{};GetPrivateProfileStringW(section,key,L"",value,32768,file.c_str());return value;}
inline CollectorConfig LoadCollectorConfig(const std::filesystem::path& modulePath){
 CollectorConfig c;auto file=(modulePath.parent_path()/L"pottrace-collector.ini").wstring();
 c.bridge=ReadCollectorIni(file,L"PotTrace",L"BridgePath");c.policy=ReadCollectorIni(file,L"PotTrace",L"PolicyPath");c.playerIni=ReadCollectorIni(file,L"PotTrace",L"PlayerIni");
 wchar_t exe[32768]{};GetModuleFileNameW(nullptr,exe,32768);auto expected=std::filesystem::path(exe);expected.replace_extension(L".ini");
 c.valid=IsAbsoluteLocalPath(c.bridge)&&IsAbsoluteLocalPath(c.policy)&&IsAbsoluteLocalPath(c.playerIni)&&_wcsicmp(expected.c_str(),c.playerIni.c_str())==0;
 c.registryProfile=ReadCollectorIni(file,L"PotTrace",L"ProfileBackend")==L"Registry";
 if(c.registryProfile){
  wchar_t registered[32768]{};DWORD size=sizeof(registered);
  auto result=RegGetValueW(HKEY_CURRENT_USER,L"Software\\DAUM\\PotPlayer64",L"ProgramPath",RRF_RT_REG_SZ,nullptr,registered,&size);
  c.valid=c.valid&&result==ERROR_SUCCESS&&_wcsicmp(registered,exe)==0&&GetFileAttributesW(expected.c_str())==INVALID_FILE_ATTRIBUTES&&GetLastError()==ERROR_FILE_NOT_FOUND;
 }
 return c;
}
inline CapturePolicy ReadCapturePolicy(const std::wstring& path){
 HANDLE handle=INVALID_HANDLE_VALUE;
 for(unsigned attempt=0;attempt<4;++attempt){handle=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);if(handle!=INVALID_HANDLE_VALUE)break;if(attempt<3)Sleep(1);}
 if(handle==INVALID_HANDLE_VALUE)return {};std::unique_ptr<void,decltype(&CloseHandle)> file(handle,CloseHandle);
 std::string data(4097,'\0');DWORD n=0;if(!ReadFile(handle,data.data(),static_cast<DWORD>(data.size()),&n,nullptr)||n>4096)return {};data.resize(n);return ParseCapturePolicy(data);
}
inline bool RegistryPluginEnabled(HKEY profile){
 constexpr wchar_t script[]=L"PlaybackStatistics - PotTrace.as";
 wchar_t selected[128]{};DWORD size=sizeof(selected);
 if(RegGetValueW(profile,L"ExtensionSection",L"PlaybackStatistics",RRF_RT_REG_SZ,nullptr,selected,&size)!=ERROR_SUCCESS||wcscmp(selected,script)!=0)return false;
 HKEY list=nullptr;if(RegOpenKeyExW(profile,L"ExtensionInfoList",0,KEY_QUERY_VALUE,&list)!=ERROR_SUCCESS)return false;
 std::unique_ptr<std::remove_pointer_t<HKEY>,decltype(&RegCloseKey)> owned(list,RegCloseKey);
 DWORD count=0;if(RegQueryInfoKeyW(list,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,&count,nullptr,nullptr,nullptr,nullptr)!=ERROR_SUCCESS||count>16384)return false;
 unsigned matches=0;bool enabled=false;
 wchar_t name[16384];
 for(DWORD i=0;i<count;++i){
  DWORD length=16384;
  if(RegEnumValueW(list,i,name,&length,nullptr,nullptr,nullptr,nullptr)!=ERROR_SUCCESS)return false;
  if(!length||wcsspn(name,L"0123456789")!=length)continue;
  wchar_t entry[128]{};size=sizeof(entry);
  if(RegGetValueW(list,nullptr,name,RRF_RT_REG_SZ,nullptr,entry,&size)!=ERROR_SUCCESS||wcscmp(entry,script)!=0)continue;
  ++matches;DWORD flag=0;size=sizeof(flag);auto key=std::wstring(name)+L"DN";
  enabled=RegGetValueW(list,nullptr,key.c_str(),RRF_RT_REG_DWORD,nullptr,&flag,&size)==ERROR_SUCCESS&&flag==1;
 }
 return matches==1&&enabled;
}
inline bool IsPlayerPluginEnabled(const CollectorConfig& c){
 if(c.valid&&c.registryProfile){
  HKEY profile=nullptr;if(RegOpenKeyExW(HKEY_CURRENT_USER,L"Software\\DAUM\\PotPlayerMini64",0,KEY_READ,&profile)!=ERROR_SUCCESS)return false;
  std::unique_ptr<std::remove_pointer_t<HKEY>,decltype(&RegCloseKey)> owned(profile,RegCloseKey);
  return GetFileAttributesW(c.playerIni.c_str())==INVALID_FILE_ATTRIBUTES&&GetLastError()==ERROR_FILE_NOT_FOUND&&RegistryPluginEnabled(profile);
 }
 if(!c.valid||ReadCollectorIni(c.playerIni,L"ExtensionSection",L"PlaybackStatistics")!=L"PlaybackStatistics - PotTrace.as")return false;
 wchar_t section[32768]{};auto length=GetPrivateProfileSectionW(L"ExtensionInfoList",section,32768,c.playerIni.c_str());if(!length||length>=32766)return false;
 std::map<std::wstring,std::wstring> fields;
 for(const wchar_t* p=section;*p;p+=wcslen(p)+1){std::wstring line(p);auto equals=line.find(L'=');if(equals!=std::wstring::npos)fields.emplace(line.substr(0,equals),line.substr(equals+1));}
 unsigned matches=0;bool enabled=false;
 for(const auto& [key,value]:fields)if(!key.empty()&&key.find_first_not_of(L"0123456789")==std::wstring::npos&&value==L"PlaybackStatistics - PotTrace.as"){
  ++matches;auto flag=fields.find(key+L"DN");enabled=flag!=fields.end()&&flag->second==L"1";
 }return matches==1&&enabled;
}
}
