#include "opensim/services/login_response.hpp"
#include <atomic>
#include <iostream>
#include <stdexcept>
#include <thread>
using namespace opensim;
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main(){try{
 auto db=std::make_shared<framework::TimeZoneDatabase>();auto utc=db->find("UTC");check(!utc->daylight_at(-62135596800ll)&&!utc->daylight_at(253402300799ll),"UTC covers DateTime boundaries");check(db->find("utc")==utc,"timezone cache ignores identifier case");db->clear();check(db->find("UTC")!=utc&&!utc->daylight_at(0),"cache clear preserves held timezone values");
 std::atomic<bool> okay=true;std::vector<std::thread> threads;for(int n=0;n<8;++n)threads.emplace_back([&]{for(int i=0;i<200;++i)if(db->find("UTC")->daylight_at(i))okay=false;});for(auto& t:threads)t.join();check(okay,"concurrent timezone reads");
 services::LoginResponseEnvironment env;env.server_uri=[](const auto&)->std::optional<std::string>{return "fixture";};std::int64_t instant=1768478400;int calls=0;services::bind_login_response_timezones(env,db,[&]{++calls;return instant;});auto pacific=env.find_timezone("America/Los_Angeles");check(calls==0,"finding a timezone does not evaluate the clock");check(!pacific()&&calls==1,"Pacific winter response DST");instant=1784116800;check(pacific()&&calls==2,"selected timezone evaluates current clock on each invocation");check(env.server_uri(services::GridRegion{})==std::optional<std::string>("fixture"),"timezone binding preserves networking");check(env.local_daylight()==db->local()->daylight_at(instant),"local response provider uses local OS timezone");bool threw=false;try{env.find_timezone("Bad/Zone");}catch(const std::exception&){threw=true;}check(threw,"invalid named timezone fails during selection");instant=253402300800ll;threw=false;try{pacific();}catch(const std::exception&){threw=true;}check(threw,"date evaluation errors remain outside timezone selection");
 std::cout<<"PASS: native timezone cache, concurrency and response binding\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what();return 1;}}
