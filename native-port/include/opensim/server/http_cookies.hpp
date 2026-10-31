// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>
namespace opensim::server {
using HttpName=std::optional<std::string>;
struct HttpDateTime {std::int64_t ticks=0;std::int32_t kind=0;bool operator==(const HttpDateTime& other)const{return ticks==other.ticks;}};
class HttpClock {
public:
    virtual ~HttpClock()=default;
    virtual HttpDateTime local_now()const;
    virtual HttpDateTime utc_now()const;
};
HttpClock& http_clock();
std::string http_rfc_date(HttpDateTime value);
class RequestCookie:public std::enable_shared_from_this<RequestCookie> {
public:
    RequestCookie(HttpName name,HttpName value);
    virtual ~RequestCookie()=default;
    const std::string& name()const{return name_;}
    HttpName value()const{return value_;}
    void value(HttpName value){value_=std::move(value);}
    virtual HttpName to_string(HttpClock& clock=http_clock())const;
private:
    const std::string name_;
    HttpName value_;
};
class ResponseCookie:public RequestCookie {
public:
    ResponseCookie(HttpName name,HttpName value,HttpDateTime expires);
    ResponseCookie(HttpName name,HttpName value,HttpDateTime expires,HttpName path,HttpName domain);
    ResponseCookie(const std::shared_ptr<RequestCookie>& cookie,HttpDateTime expires);
    HttpDateTime expires()const{return expires_;}
    void expires(HttpDateTime value){expires_=value;persistent_=value.ticks!=0;}
    HttpName path()const{return path_;}
    void path(HttpName value){path_=!value||value->empty()?HttpName("/"):std::move(value);}
    HttpName to_string(HttpClock& clock=http_clock())const override;
private:
    bool persistent_=false;
    HttpDateTime expires_;
    HttpName path_="/",domain_;
};
class ResponseCookies {
public:
    void add(std::shared_ptr<ResponseCookie> cookie);
    void add(const std::shared_ptr<RequestCookie>& cookie,HttpDateTime expires){add(std::make_shared<ResponseCookie>(cookie,expires));}
    std::size_t count()const{return items_.size();}
    std::shared_ptr<ResponseCookie> get(const HttpName& key)const;
    void set(const HttpName& key,std::shared_ptr<ResponseCookie> cookie);
    void clear(){items_.clear();}
    void each(const std::function<void(std::shared_ptr<ResponseCookie>)>& callback)const;
private:
    std::vector<std::pair<std::string,std::shared_ptr<ResponseCookie>>> items_;
    std::uint64_t version_=0;
};
}
