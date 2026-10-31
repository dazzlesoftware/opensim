// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/network/rest_forms.hpp"
#include <httplib.h>
#include <limits>
namespace opensim::network {
namespace {
std::optional<std::uint64_t> content_length(const httplib::Response& headers){
 if(!headers.has_header("Content-Length"))return {};auto text=headers.get_header_value("Content-Length");
 if(text.empty())return {};std::uint64_t value=0;constexpr auto max=static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
 for(char c:text){if(c<'0'||c>'9')return {};auto digit=static_cast<std::uint64_t>(c-'0');if(value>(max-digit)/10)return {};value=value*10+digit;}
 return value;
}
}
RestFormsRequest rest_forms_transport(std::optional<std::pair<std::string,std::string>> auth){
 return [auth](const std::string& url,const std::string& body){
  auto scheme=url.find("://");if(scheme==std::string::npos)throw std::invalid_argument("Invalid service URI");
  auto end=url.find_first_of("/?#",scheme+3);auto target=end==std::string::npos?std::string("/"):url.substr(end);
  if(target.starts_with('?'))target.insert(0,"/");if(auto fragment=target.find('#');fragment!=std::string::npos)target.resize(fragment);
  httplib::Client client(url.substr(0,end));if(!client.is_valid())throw std::invalid_argument("Invalid service URI");
  client.set_connection_timeout(30);client.set_read_timeout(30);client.set_write_timeout(30);client.set_max_timeout(30000);
  client.set_follow_location(true);client.set_keep_alive(true);
  client.set_decompress(false);
  httplib::Request request;request.method="POST";request.path=target;request.body=body;
  request.opensim_no_default_accept_headers=true;request.opensim_source_redirects=true;request.redirect_count_=10;
  request.headers={{"Connection","Keep-Alive"},{"Keep-Alive","timeout=30, max=10"}};
  if(auth)request.headers.insert(httplib::make_basic_authentication_header(auth->first,auth->second));
  if(!body.empty())request.headers.emplace("Content-Type","application/x-www-form-urlencoded");
  // send(Request) does not initialize the deadline origin as Post does.
  request.start_time_=std::chrono::steady_clock::now();
  bool discard=false,failed_status=false;
  request.opensim_response_headers_read=[&](httplib::Response& headers){
   // Like EnsureSuccessStatusCode, reject final HTTP errors before body reads.
   failed_status=headers.status<200||headers.status>=300;
   auto length=content_length(headers);discard=!length||*length==0;
   // HttpContentHeaders exposes the first wire Content-Length as a signed
   // Int64, including its absent/unparseable state. Later duplicate values do
   // not replace it. Framing uses transfer coding when it is present.
   headers.headers.erase("Content-Length");
   if(length&&!headers.has_header("Transfer-Encoding"))headers.headers.emplace("Content-Length",std::to_string(*length));
  };
  request.response_handler=[&](const httplib::Response&){
   // ResponseHeadersRead leaves absent/zero-length content unread. Returning
   // false cancels httplib's body phase; this intentional cancellation is not
   // a transport error. Redirects reach this callback only at the final reply.
   return !discard&&!failed_status;
  };
  auto response=client.send(request);
  if(failed_status)throw std::runtime_error("Service HTTP request failed");
  if(discard)return std::string{};
  if(!response||response->status<200||response->status>=300)throw std::runtime_error("Service HTTP request failed");
  return read_service_body(response->body);
 };
}
}
