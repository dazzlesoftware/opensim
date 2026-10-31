// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/server/http_cookies.hpp"
#include "opensim/network/service_protocol.hpp"
#include "opensim/framework/text.hpp"
#include <chrono>
#include <cstdio>
#include <ctime>
#include <stdexcept>
namespace opensim::server {
namespace {
constexpr int64_t epoch=621355968000000000,maximum=3155378975999999999;
std::string require_name(const HttpName& name){if(!name||name->empty())throw std::invalid_argument("Null cookie id");return *name;}
HttpName require_value(HttpName value){if(!value)throw std::invalid_argument("Null cookie content");return value;}
const RequestCookie& require_cookie(const std::shared_ptr<RequestCookie>& cookie){if(!cookie)throw std::invalid_argument("Null request cookie");return *cookie;}
}
HttpDateTime HttpClock::utc_now()const{const auto ticks=std::chrono::duration_cast<std::chrono::duration<int64_t,std::ratio<1,10000000>>>(std::chrono::system_clock::now().time_since_epoch()).count();return {ticks+epoch,1};}
HttpDateTime HttpClock::local_now()const{
    auto result=utc_now();auto seconds=static_cast<std::time_t>((result.ticks-epoch)/10000000);std::tm local{};
#ifdef _WIN32
    localtime_s(&local,&seconds);
#else
    localtime_r(&seconds,&local);
#endif
    using namespace std::chrono;auto days=sys_days(year(local.tm_year+1900)/month(local.tm_mon+1)/day(local.tm_mday));result.ticks=epoch+duration_cast<std::chrono::seconds>(days.time_since_epoch()).count()*10000000+int64_t(local.tm_hour*3600+local.tm_min*60+local.tm_sec)*10000000+(result.ticks-epoch)%10000000;result.kind=2;return result;
}
HttpClock& http_clock(){static HttpClock value;return value;}
std::string http_rfc_date(HttpDateTime value){
    if(value.ticks<0||value.ticks>maximum)throw std::out_of_range("DateTime ticks");
    using namespace std::chrono;const auto ticks=value.ticks-epoch;const auto seconds=floor<std::chrono::seconds>(duration<int64_t,std::ratio<1,10000000>>(ticks));const auto days=floor<std::chrono::days>(seconds);const year_month_day calendar{sys_days(days)};const auto daytime=seconds-days;const auto hours=duration_cast<std::chrono::hours>(daytime);const auto minutes=duration_cast<std::chrono::minutes>(daytime-hours);const auto secs=duration_cast<std::chrono::seconds>(daytime-hours-minutes);
    const char* names[]={"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};const char* months[]={"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};char out[90];std::snprintf(out,sizeof(out),"%s, %02u %s %04d %02lld:%02lld:%02lld GMT",names[weekday(sys_days(days)).c_encoding()],unsigned(calendar.day()),months[unsigned(calendar.month())-1],int(calendar.year()),static_cast<long long>(hours.count()),static_cast<long long>(minutes.count()),static_cast<long long>(secs.count()));return out;
}
RequestCookie::RequestCookie(HttpName name,HttpName value):name_(require_name(name)),value_(require_value(std::move(value))){}
HttpName RequestCookie::to_string(HttpClock&)const{[[maybe_unused]]const auto lifetime=weak_from_this().lock();return network::service_url_encode(name_)+"="+network::service_url_encode(value_.value_or(""))+"; ";}
ResponseCookie::ResponseCookie(HttpName name,HttpName value,HttpDateTime expires):RequestCookie(std::move(name),std::move(value)){if(expires.ticks!=0){expires_=expires;persistent_=true;}}
ResponseCookie::ResponseCookie(HttpName name,HttpName value,HttpDateTime expires,HttpName path,HttpName domain):ResponseCookie(std::move(name),std::move(value),expires){domain_=std::move(domain);path_=std::move(path);}
ResponseCookie::ResponseCookie(const std::shared_ptr<RequestCookie>& cookie,HttpDateTime expires):ResponseCookie(require_cookie(cookie).name(),require_cookie(cookie).value(),expires){}
HttpName ResponseCookie::to_string(HttpClock& clock)const{
    [[maybe_unused]]const auto lifetime=weak_from_this().lock();
    auto out=RequestCookie::to_string(clock).value_or("");
    if(persistent_){const auto local=clock.local_now();const auto utc=clock.utc_now();const auto offset=local.ticks-utc.ticks;const auto converted=expires_.ticks-offset;out+="expires="+http_rfc_date({converted,expires_.kind})+";";}
    if(path_&&!path_->empty())out+="path="+*path_+"; ";if(domain_&&!domain_->empty())out+="domain="+*domain_+"; ";return out;
}
void ResponseCookies::add(std::shared_ptr<ResponseCookie> cookie){if(!cookie)throw std::invalid_argument("Null response cookie");if(framework::trim_utf8(cookie->name()).empty())throw std::invalid_argument("Cookie name must be specified");if(!cookie->value()||framework::trim_utf8(*cookie->value()).empty())throw std::invalid_argument("Cookie content must be specified");for(auto& [name,value]:items_)if(name==cookie->name()){value=std::move(cookie);return;}items_.emplace_back(cookie->name(),cookie);++version_;}
std::shared_ptr<ResponseCookie> ResponseCookies::get(const HttpName& key)const{if(!key)throw std::invalid_argument("Null dictionary key");for(const auto& [name,value]:items_)if(name==*key)return value;return {};}
void ResponseCookies::set(const HttpName& key,std::shared_ptr<ResponseCookie> cookie){if(!key)throw std::invalid_argument("Null dictionary key");for(auto& [name,value]:items_)if(name==*key){value=std::move(cookie);return;}add(std::move(cookie));}
void ResponseCookies::each(const std::function<void(std::shared_ptr<ResponseCookie>)>& callback)const{const auto version=version_;for(size_t i=0;i<items_.size();++i){auto value=items_[i].second;callback(value);if(version!=version_)throw std::runtime_error("Cookie collection changed during enumeration");}}
}
