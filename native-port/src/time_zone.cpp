// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/framework/time_zone.hpp"
#include "opensim/framework/text.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <mutex>
#include <stdexcept>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif
namespace opensim::framework {
namespace {
std::string lower(std::string value){for(auto& c:value)if(c>='A'&&c<='Z')c+=32;return value;}
[[maybe_unused]] std::int64_t year_start(int year){using namespace std::chrono;return duration_cast<seconds>(sys_days{std::chrono::year(year)/1/1}.time_since_epoch()).count();}
int utc_year(std::int64_t seconds){using namespace std::chrono;return int(year_month_day{floor<days>(sys_seconds{std::chrono::seconds{seconds}})}.year());}
// ICU is used only for identifier conversion, never as a substitute for OS rule data.
struct Icu {
    using IanaToWindows=std::int32_t(*)(const char16_t*,std::int32_t,char16_t*,std::int32_t,std::int32_t*);
    using WindowsToIana=std::int32_t(*)(const char16_t*,std::int32_t,const char*,char16_t*,std::int32_t,std::int32_t*);
    IanaToWindows to_windows=nullptr;WindowsToIana to_iana=nullptr;
#ifdef _WIN32
    HMODULE module=nullptr;
    void* symbol(const std::string& name){return reinterpret_cast<void*>(GetProcAddress(module,name.c_str()));}
#else
    void* module=nullptr;
    void* symbol(const std::string& name){return dlsym(module,name.c_str());}
#endif
    Icu(){
#ifdef _WIN32
        module=LoadLibraryExW(L"icu.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
#elif defined(__APPLE__)
        module=dlopen("/usr/lib/libicucore.A.dylib",RTLD_LAZY|RTLD_LOCAL);
#else
        module=dlopen("libicui18n.so",RTLD_LAZY|RTLD_LOCAL);for(int v=120;!module&&v>=40;--v){auto name="libicui18n.so."+std::to_string(v);module=dlopen(name.c_str(),RTLD_LAZY|RTLD_LOCAL);}
#endif
        if(!module)return;std::string suffix;if(!symbol("ucal_getWindowsTimeZoneID")){for(int v=120;v>=40;--v)if(symbol("ucal_getWindowsTimeZoneID_"+std::to_string(v))){suffix="_"+std::to_string(v);break;}}
        to_windows=reinterpret_cast<IanaToWindows>(symbol("ucal_getWindowsTimeZoneID"+suffix));to_iana=reinterpret_cast<WindowsToIana>(symbol("ucal_getTimeZoneIDForWindowsID"+suffix));
    }
    ~Icu(){
#ifdef _WIN32
        if(module)FreeLibrary(module);
#else
        if(module)dlclose(module);
#endif
    }
    std::optional<std::string> convert(const std::string& id,bool windows){if(id.find_first_of("\\\n\r")!=std::string::npos)return {};auto input=utf16_replacing(id);std::array<char16_t,512> buffer{};std::int32_t error=0,n=0;if(windows){if(!to_windows)return {};n=to_windows(input.data(),static_cast<std::int32_t>(input.size()),buffer.data(),static_cast<std::int32_t>(buffer.size()),&error);}else{if(!to_iana)return {};n=to_iana(input.data(),static_cast<std::int32_t>(input.size()),nullptr,buffer.data(),static_cast<std::int32_t>(buffer.size()),&error);}if(error>0||n<=0||n>=static_cast<int>(buffer.size()))return {};return utf8(std::u16string_view(buffer.data(),n));}
};
Icu& icu(){static Icu value;return value;}
#ifdef _WIN32
std::optional<DYNAMIC_TIME_ZONE_INFORMATION> windows_zone(const std::string& name){auto chars=utf16_replacing(name);std::wstring id(chars.begin(),chars.end());for(DWORD index=0;;++index){DYNAMIC_TIME_ZONE_INFORMATION value{};auto error=EnumDynamicTimeZoneInformation(index,&value);if(error==ERROR_NO_MORE_ITEMS)return {};if(error!=ERROR_SUCCESS)return {};if(CompareStringOrdinal(id.data(),static_cast<int>(id.size()),value.TimeZoneKeyName,-1,TRUE)==CSTR_EQUAL){value.DynamicDaylightTimeDisabled=FALSE;return value;}}}
std::int64_t seconds_of(const SYSTEMTIME& value){using namespace std::chrono;return duration_cast<seconds>(sys_days{year(value.wYear)/value.wMonth/value.wDay}.time_since_epoch()).count()+value.wHour*3600+value.wMinute*60+value.wSecond;}
std::int64_t windows_transition(int y,SYSTEMTIME value){using namespace std::chrono;if(!value.wYear){auto first=sys_days{year(y)/value.wMonth/1};int dom=1+(value.wDayOfWeek-static_cast<int>(weekday{first}.c_encoding())+7)%7+7*(value.wDay-1);auto month_end=unsigned(year_month_day_last{year(y)/month_day_last{month(value.wMonth)}}.day());if(dom>static_cast<int>(month_end))dom-=7;value.wYear=static_cast<WORD>(y);value.wDay=static_cast<WORD>(dom);}return seconds_of(value)+(value.wMilliseconds?1:0);}
bool windows_boundary_daylight(DYNAMIC_TIME_ZONE_INFORMATION& value,std::int64_t seconds){int year=std::clamp(utc_year(seconds-std::int64_t(value.Bias+value.StandardBias)*60),1,9999);TIME_ZONE_INFORMATION rules{};if(!GetTimeZoneInformationForYear(static_cast<USHORT>(year),&value,&rules))throw std::runtime_error("Windows timezone year rules unavailable");if(!rules.DaylightDate.wMonth||rules.DaylightBias==rules.StandardBias)return false;auto begin=windows_transition(year,rules.DaylightDate)+std::int64_t(rules.Bias+rules.StandardBias)*60;auto end=windows_transition(year,rules.StandardDate)+std::int64_t(rules.Bias+rules.DaylightBias)*60;return begin<end?seconds>=begin&&seconds<end:seconds>=begin||seconds<end;}
std::function<bool(std::int64_t)> windows_daylight(DYNAMIC_TIME_ZONE_INFORMATION value){return [value](std::int64_t seconds){if(value.DynamicDaylightTimeDisabled)return false;auto rules=value;return windows_boundary_daylight(rules,seconds);};}

#else
struct TransitionRule{int kind=0,a=0,b=0,c=0,time=7200;char basis='w';};
struct PosixRule {
    int standard_offset=0,daylight_offset=0;bool daylight=false;TransitionRule start,end;
    std::int64_t transition(int year,const TransitionRule& rule,int previous_offset)const{using namespace std::chrono;int day=0;if(rule.kind=='M'){auto first=sys_days{std::chrono::year(year)/static_cast<unsigned>(rule.a)/1};auto first_weekday=weekday{first}.c_encoding();int dom=1+(rule.c-static_cast<int>(first_weekday)+7)%7+7*(rule.b-1);auto month_end=unsigned(year_month_day_last{std::chrono::year(year)/month_day_last{month(static_cast<unsigned>(rule.a))}}.day());if(dom>static_cast<int>(month_end))dom-=7;day=static_cast<int>((first-sys_days{std::chrono::year(year)/1/1}).count())+dom-1;}else{day=rule.a-(rule.kind=='J'?1:0);if(rule.kind=='J'&&std::chrono::year(year).is_leap()&&rule.a>=60)++day;}auto value=year_start(year)+std::int64_t(day)*86400+rule.time;return value-(rule.basis=='u'?0:rule.basis=='s'?standard_offset:previous_offset);}
    bool at(std::int64_t seconds)const{if(!daylight)return false;int year=utc_year(seconds);for(int y=year-1;y<=year+1;++y){auto begin=transition(y,start,standard_offset),finish=transition(y,end,daylight_offset);if(finish<begin)finish=transition(y+1,end,daylight_offset);if(seconds>=begin&&seconds<finish)return true;}return false;}
};
struct PosixParser {
    std::string_view text;std::size_t pos=0;
    [[noreturn]] void bad(){throw std::invalid_argument("Invalid POSIX timezone rule");}
    bool name(){auto start=pos;if(pos<text.size()&&text[pos]=='<'){auto end=text.find('>',++pos);if(end==std::string_view::npos||end==pos)bad();pos=end+1;return true;}while(pos<text.size()&&((text[pos]>='a'&&text[pos]<='z')||(text[pos]>='A'&&text[pos]<='Z')))++pos;return pos-start>=3;}
    int number(int maximum){auto start=pos;int n=0;while(pos<text.size()&&text[pos]>='0'&&text[pos]<='9'){n=n*10+(text[pos++]-'0');if(n>maximum)bad();}if(start==pos)bad();return n;}
    int offset(){int sign=1;if(pos<text.size()&&(text[pos]=='-'||text[pos]=='+')){if(text[pos]=='-')sign=-1;++pos;}int n=number(167)*3600;for(int scale:{60,1}){if(pos==text.size()||text[pos]!=':')break;++pos;n+=number(59)*scale;}return sign*n;}
    TransitionRule rule(){TransitionRule r;if(pos<text.size()&&text[pos]=='M'){r.kind='M';++pos;r.a=number(12);if(r.a<1||pos==text.size()||text[pos++]!='.')bad();r.b=number(5);if(r.b<1||pos==text.size()||text[pos++]!='.')bad();r.c=number(6);}else{r.kind=pos<text.size()&&text[pos]=='J'?'J':'N';if(r.kind=='J')++pos;r.a=number(r.kind=='J'?365:365);if(r.kind=='J'&&r.a<1)bad();}if(pos<text.size()&&text[pos]=='/'){++pos;r.time=offset();if(pos<text.size()&&(text[pos]=='w'||text[pos]=='s'||text[pos]=='u'||text[pos]=='g'||text[pos]=='z')){r.basis=text[pos++];if(r.basis=='g'||r.basis=='z')r.basis='u';}}return r;}
    PosixRule parse(){PosixRule r;if(!name())bad();r.standard_offset=-offset();if(pos==text.size())return r;if(!name())bad();r.daylight=true;r.daylight_offset=r.standard_offset+3600;if(pos<text.size()&&text[pos]!=',')r.daylight_offset=-offset();if(pos==text.size()||text[pos++]!=',')bad();r.start=rule();if(pos==text.size()||text[pos++]!=',')bad();r.end=rule();if(pos!=text.size())bad();return r;}
};
struct TzFile {
    struct Type{std::int32_t offset;bool daylight;};std::vector<std::int64_t> transitions;std::vector<std::uint8_t> indices;std::vector<Type> types;std::optional<PosixRule> future;
    bool at(std::int64_t utc)const{if(future&&(transitions.empty()||utc>transitions.back()))return future->at(utc);auto where=std::upper_bound(transitions.begin(),transitions.end(),utc);std::size_t type=0;if(where!=transitions.begin())type=indices[static_cast<std::size_t>(where-transitions.begin()-1)];else {for(std::size_t i=0;i<types.size();++i)if(!types[i].daylight){type=i;break;}}return types[type].daylight;}
    static std::shared_ptr<TzFile> read(const std::filesystem::path& path){
        std::ifstream file(path,std::ios::binary);if(!file)throw std::invalid_argument("Timezone file not found");file.seekg(0,std::ios::end);auto length=file.tellg();if(length<44||length>4*1024*1024)throw std::invalid_argument("Invalid timezone file size");file.seekg(0);std::vector<std::uint8_t> bytes(static_cast<std::size_t>(length));file.read(reinterpret_cast<char*>(bytes.data()),length);if(!file)throw std::invalid_argument("Timezone file read failed");
        auto u32=[&](std::size_t at){if(at>bytes.size()||bytes.size()-at<4)throw std::invalid_argument("Truncated timezone file");std::uint32_t n=0;for(int i=0;i<4;++i)n=(n<<8)|bytes[at+i];return n;};
        auto header=[&](std::size_t at){if(at>bytes.size()||bytes.size()-at<44||std::string_view(reinterpret_cast<char*>(bytes.data()+at),4)!="TZif")throw std::invalid_argument("Invalid timezone header");std::array<std::uint32_t,6> n{};for(int i=0;i<6;++i)n[i]=u32(at+20+i*4);if(n[4]==0||n[4]>256)throw std::invalid_argument("Invalid timezone type count");return n;};
        auto size=[](auto n,int width){return std::uint64_t(n[3])*(width+1)+std::uint64_t(n[4])*6+n[5]+std::uint64_t(n[2])*(width+4)+n[0]+n[1];};
        std::size_t base=0;int width=4;auto counts=header(base);if(bytes[4]=='2'||bytes[4]=='3'||bytes[4]=='4'){auto next=44+size(counts,4);if(next>bytes.size())throw std::invalid_argument("Truncated timezone block");base=static_cast<std::size_t>(next);width=8;counts=header(base);}auto end=base+44+size(counts,width);if(end>bytes.size())throw std::invalid_argument("Truncated timezone block");
        auto result=std::make_shared<TzFile>();std::size_t pos=base+44;for(std::uint32_t i=0;i<counts[3];++i){std::int64_t n=0;if(width==4){n=std::bit_cast<std::int32_t>(u32(pos));pos+=4;}else{std::uint64_t bits=0;for(int b=0;b<8;++b)bits=(bits<<8)|bytes[pos++];n=std::bit_cast<std::int64_t>(bits);}if(!result->transitions.empty()&&n<result->transitions.back())throw std::invalid_argument("Unsorted timezone transitions");result->transitions.push_back(n);}
        for(std::uint32_t i=0;i<counts[3];++i){auto index=bytes[pos++];if(index>=counts[4])throw std::invalid_argument("Invalid timezone transition type");result->indices.push_back(index);}for(std::uint32_t i=0;i<counts[4];++i){auto offset=std::bit_cast<std::int32_t>(u32(pos));result->types.push_back({offset,bytes[pos+4]!=0});pos+=6;}
        if(width==8&&end+1<bytes.size()&&bytes[end]=='\n'){auto last=std::find(bytes.begin()+static_cast<std::ptrdiff_t>(end+1),bytes.end(),static_cast<std::uint8_t>('\n'));if(last!=bytes.end()){std::string value(bytes.begin()+static_cast<std::ptrdiff_t>(end+1),last);if(!value.empty())try{result->future=PosixParser{value}.parse();}catch(const std::exception&){} }}return result;
    }
};
std::filesystem::path zone_directory(){const char* value=std::getenv("TZDIR");return value&&*value?std::filesystem::path(value):std::filesystem::path("/usr/share/zoneinfo");}
std::shared_ptr<TzFile> named_file(const std::string& id){auto path=std::filesystem::path(id);if(path.is_absolute())throw std::invalid_argument("Rooted timezone identifier");return TzFile::read(zone_directory()/path);}
#endif
}
bool TimeZone::daylight_at(std::int64_t utc_seconds)const{if(utc_seconds<-62135596800ll||utc_seconds>253402300799ll)throw std::out_of_range("Timezone date is outside DateTime range");return daylight_(utc_seconds);}
struct TimeZoneDatabase::State{std::mutex mutex;std::map<std::string,std::shared_ptr<TimeZone>> zones;std::shared_ptr<TimeZone> local;};
TimeZoneDatabase::TimeZoneDatabase():state_(std::make_unique<State>()){}
TimeZoneDatabase::~TimeZoneDatabase()=default;
std::shared_ptr<TimeZone> TimeZoneDatabase::find(const std::optional<std::string>& id){
    if(!id)throw std::invalid_argument("Null timezone identifier");if(id->empty()||utf16_replacing(*id).size()>255||id->find('\0')!=std::string::npos)throw std::invalid_argument("Invalid timezone identifier");std::lock_guard lock(state_->mutex);auto key=lower(*id);if(auto found=state_->zones.find(key);found!=state_->zones.end())return found->second;std::function<bool(std::int64_t)> daylight;
    if(key=="utc")daylight=[](auto){return false;};else{
#ifdef _WIN32
        auto found=windows_zone(*id);if(!found){auto alternative=icu().convert(*id,true);if(alternative)found=windows_zone(*alternative);}if(!found)throw std::invalid_argument("Timezone identifier not found");daylight=windows_daylight(*found);
#else
        std::shared_ptr<TzFile> data;try{data=named_file(*id);}catch(const std::exception&){auto alternative=icu().convert(*id,false);if(alternative)data=named_file(*alternative);else throw;}daylight=[data](auto seconds){return data->at(seconds);};
#endif
    }auto result=std::shared_ptr<TimeZone>(new TimeZone(*id,std::move(daylight)));state_->zones[key]=result;return result;
}
std::shared_ptr<TimeZone> TimeZoneDatabase::local(){std::lock_guard lock(state_->mutex);if(state_->local)return state_->local;std::function<bool(std::int64_t)> daylight;
#ifdef _WIN32
    DYNAMIC_TIME_ZONE_INFORMATION value{};if(GetDynamicTimeZoneInformation(&value)==TIME_ZONE_ID_INVALID)daylight=[](auto){return false;};else daylight=windows_daylight(value);
#else
    try{std::shared_ptr<TzFile> data;const char* value=std::getenv("TZ");if(!value)data=TzFile::read("/etc/localtime");else {std::string name(value);if(name.starts_with(':'))name.erase(0,1);if(name.empty())throw std::invalid_argument("Empty local timezone");auto path=std::filesystem::path(name);data=TzFile::read(path.is_absolute()?path:zone_directory()/path);}daylight=[data](auto seconds){return data->at(seconds);};}catch(const std::exception&){daylight=[](auto){return false;};}
#endif
    state_->local=std::shared_ptr<TimeZone>(new TimeZone("Local",std::move(daylight)));return state_->local;}
void TimeZoneDatabase::clear(){std::lock_guard lock(state_->mutex);state_->zones.clear();state_->local.reset();}
TimeZoneDatabase& time_zones(){static TimeZoneDatabase value;return value;}
}
