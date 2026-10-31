// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
// Ignore-case tailoring adapted from dotnet/runtime v8.0.31 pal_collation.c.
// Copyright (c) .NET Foundation and Contributors; third_party/dotnet_collation/LICENSE.
#include "opensim/framework/culture_prefix.hpp"
#include "opensim/framework/culture_sort.hpp"
#include "opensim/framework/text.hpp"
#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <iterator>
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
#include "../third_party/dotnet_collation/ignore_case_tables.inc"
struct Icu {
    using Upper=std::int32_t(*)(std::int32_t);
    Upper upper=nullptr;
    using Open=void*(*)(const char*,std::int32_t*);
    using Close=void(*)(void*);
    using Compare=std::int32_t(*)(const void*,const char16_t*,std::int32_t,const char16_t*,std::int32_t);
    using Search=void*(*)(const char16_t*,std::int32_t,const char16_t*,std::int32_t,const void*,void*,std::int32_t*);
    using First=std::int32_t(*)(void*,std::int32_t*);
    using Strength=void(*)(void*,std::int32_t);
    using Rules=const char16_t*(*)(const void*,std::int32_t*);
    using OpenRules=void*(*)(const char16_t*,std::int32_t,std::int32_t,std::int32_t,void*,std::int32_t*);
    Rules rules=nullptr;OpenRules open_rules=nullptr;
    Strength strength=nullptr;
    Open open=nullptr;Close close=nullptr,search_close=nullptr;Compare compare=nullptr;Search search=nullptr;First first=nullptr;
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
        module=dlopen("libicui18n.so",RTLD_LAZY|RTLD_LOCAL);for(int version=120;!module&&version>=40;--version)module=dlopen(("libicui18n.so."+std::to_string(version)).c_str(),RTLD_LAZY|RTLD_LOCAL);
#endif
        if(!module)return;std::string suffix;if(!symbol("ucol_open"))for(int version=120;version>=40;--version)if(symbol("ucol_open_"+std::to_string(version))){suffix="_"+std::to_string(version);break;}
        open=reinterpret_cast<Open>(symbol("ucol_open"+suffix));close=reinterpret_cast<Close>(symbol("ucol_close"+suffix));compare=reinterpret_cast<Compare>(symbol("ucol_strcoll"+suffix));search=reinterpret_cast<Search>(symbol("usearch_openFromCollator"+suffix));first=reinterpret_cast<First>(symbol("usearch_first"+suffix));search_close=reinterpret_cast<Close>(symbol("usearch_close"+suffix));
        strength=reinterpret_cast<Strength>(symbol("ucol_setStrength"+suffix));
        upper=reinterpret_cast<Upper>(symbol("u_toupper"+suffix));
        rules=reinterpret_cast<Rules>(symbol("ucol_getRules"+suffix));open_rules=reinterpret_cast<OpenRules>(symbol("ucol_openRules"+suffix));
    }
    ~Icu(){
#ifdef _WIN32
        if(module)FreeLibrary(module);
#else
        if(module)dlclose(module);
#endif
    }
};
Icu& icu(){static Icu value;return value;}
struct Collator {
    void* value=nullptr;
    Collator(bool ignore_case=false){auto& api=icu();if(!api.open||!api.close||!api.compare||!api.search||!api.first||!api.search_close||(ignore_case&&(!api.rules||!api.open_rules)))throw std::runtime_error("ICU is required for invariant collation");std::int32_t error=0;value=api.open("",&error);if(error>0||!value){if(value)api.close(value);throw std::runtime_error("Cannot create invariant ICU collator");}
        if(ignore_case){try{
            std::int32_t count=0;const auto base=api.rules(value,&count);std::u16string rules;if(count>0)rules.assign(base,base+count);
            for(char16_t h:hiragana){rules+=u'&';rules+=h;rules+=u'<';rules+=static_cast<char16_t>(h+0x60);}
            static_assert(std::size(width_lower)==std::size(width_higher));
            for(size_t i=0;i<std::size(width_lower);++i){const auto c=width_lower[i];rules+=u'&';if((c>=0x21&&c<=0x2f)||(c>=0x3a&&c<=0x40)||(c>=0x5b&&c<=0x60)||(c>=0x7b&&c<=0x7e))rules+=u'\\';rules+=c;rules+=u'<';rules+=width_higher[i];}
            for(char16_t c=0xff21;c<=0xff3a;++c){rules+=u'&';rules+=static_cast<char16_t>(c+0x20);rules+=u'=';rules+=c;}rules+=u"&a=a";
            error=0;auto tailored=api.open_rules(rules.data(),static_cast<int32_t>(rules.size()),-1,1,nullptr,&error);if(error>0||!tailored){if(tailored)api.close(tailored);throw std::runtime_error("Cannot tailor invariant ignore-case collation");}api.close(value);value=tailored;
        }catch(...){api.close(value);value=nullptr;throw;}}
    }
    ~Collator(){icu().close(value);}
};
bool printable(std::string_view value){return std::all_of(value.begin(),value.end(),[](unsigned char c){return c>=32&&c<127;});}
}
bool invariant_starts_with(std::string_view text,std::string_view prefix){
    if(prefix.empty())return true;
    if(printable(text)&&printable(prefix))return text.starts_with(prefix);
    thread_local Collator collator;auto& api=icu();auto source=utf16_replacing(text),pattern=utf16_replacing(prefix);std::int32_t error=0;
    if(api.compare(collator.value,pattern.data(),static_cast<std::int32_t>(pattern.size()),u"",0)==0)return true;if(text.empty())return false;
    auto search=api.search(pattern.data(),static_cast<std::int32_t>(pattern.size()),source.data(),static_cast<std::int32_t>(source.size()),collator.value,nullptr,&error);if(error>0||!search){if(search)api.search_close(search);throw std::runtime_error("Cannot create ICU prefix search");}
    struct Guard {Icu& api;void* search;~Guard(){api.search_close(search);}} guard{api,search};auto at=api.first(search,&error);if(error>0)throw std::runtime_error("ICU prefix search failed");return at>=0&&(at==0||api.compare(collator.value,source.data(),at,u"",0)==0);
}
bool invariant_equals_ignore_case(std::string_view left,std::string_view right){
    if(printable(left)&&printable(right)){if(left.size()!=right.size())return false;for(size_t i=0;i<left.size();++i){auto a=left[i],b=right[i];if(a>='A'&&a<='Z')a=static_cast<char>(a+32);if(b>='A'&&b<='Z')b=static_cast<char>(b+32);if(a!=b)return false;}return true;}
    thread_local Collator collator(true);auto a=utf16_replacing(left),b=utf16_replacing(right);return icu().compare(collator.value,a.data(),static_cast<int32_t>(a.size()),b.data(),static_cast<int32_t>(b.size()))==0;
}
bool ordinal_equals_ignore_case(std::string_view left,std::string_view right){
    auto a=utf16_replacing(left),b=utf16_replacing(right);if(a.size()!=b.size())return false;
    auto upper=[](std::int32_t c){if(c>='a'&&c<='z')return c-32;if(c<128||c==0x131||c==0x17f)return c;auto fn=icu().upper;if(!fn)throw std::runtime_error("ICU is required for ordinal case mapping");return fn(c);};
    auto next=[](const std::u16string& text,size_t& index){int32_t c=text[index++];if(c>=0xd800&&c<=0xdbff&&index<text.size()&&text[index]>=0xdc00&&text[index]<=0xdfff)c=0x10000+((c-0xd800)<<10)+(text[index++]-0xdc00);return c;};
    size_t i=0,j=0;while(i<a.size()&&j<b.size())if(upper(next(a,i))!=upper(next(b,j)))return false;return i==a.size()&&j==b.size();
}
int ordinal_compare_ignore_case(std::string_view left,std::string_view right){
 auto fold=[](std::string_view text){auto units=utf16_replacing(text);std::u16string result;for(size_t i=0;i<units.size();++i){int32_t c=units[i];if(c>=0xd800&&c<=0xdbff&&i+1<units.size()&&units[i+1]>=0xdc00&&units[i+1]<=0xdfff)c=0x10000+((c-0xd800)<<10)+(units[++i]-0xdc00);if(c>='a'&&c<='z')c-=32;else if(c>=128&&c!=0x131&&c!=0x17f){auto fn=icu().upper;if(!fn)throw std::runtime_error("ICU is required for ordinal case mapping");c=fn(c);}if(c<=0xffff)result+=static_cast<char16_t>(c);else{c-=0x10000;result+=static_cast<char16_t>(0xd800+(c>>10));result+=static_cast<char16_t>(0xdc00+(c&1023));}}return result;};
 auto a=fold(left),b=fold(right);return a<b?-1:a>b?1:0;
}
int invariant_compare(std::string_view left,std::string_view right){thread_local Collator collator;auto a=utf16_replacing(left),b=utf16_replacing(right);return icu().compare(collator.value,a.data(),static_cast<int32_t>(a.size()),b.data(),static_cast<int32_t>(b.size()));}
bool invariant_starts_with_ignore_case(std::string_view text,std::string_view prefix){
 if(prefix.empty())return true;if(printable(text)&&printable(prefix)){if(text.size()<prefix.size())return false;return invariant_equals_ignore_case(text.substr(0,prefix.size()),prefix);}
 thread_local Collator collator(true);auto& api=icu();auto source=utf16_replacing(text),pattern=utf16_replacing(prefix);int32_t error=0;if(api.compare(collator.value,pattern.data(),static_cast<int32_t>(pattern.size()),u"",0)==0)return true;if(source.empty())return false;auto search=api.search(pattern.data(),static_cast<int32_t>(pattern.size()),source.data(),static_cast<int32_t>(source.size()),collator.value,nullptr,&error);if(error>0||!search){if(search)api.search_close(search);throw std::runtime_error("Cannot create ICU prefix search");}struct Guard{Icu& api;void* value;~Guard(){api.search_close(value);}}guard{api,search};auto at=api.first(search,&error);if(error>0)throw std::runtime_error("ICU prefix search failed");return at>=0&&(at==0||api.compare(collator.value,source.data(),at,u"",0)==0);
}
}
