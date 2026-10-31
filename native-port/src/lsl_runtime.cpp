// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/script/lsl_runtime.hpp"
#include "opensim/scene/script_control.hpp"
#include <bit>
#include <chrono>
#include <deque>
#include <limits>
#include <map>
#include <stdexcept>
#include <algorithm>
namespace opensim::script {
namespace {
enum class Kind {End,Name,Integer,String,Symbol};
enum class Type {Integer,String};
struct Token {Kind kind;std::string text;std::int32_t integer=0;std::size_t line=1,column=1;};
struct CompileFailure {LslDiagnostic diagnostic;};
[[noreturn]] void fail(const Token& t,std::string message){throw CompileFailure{{t.line,t.column,std::move(message)}};}
std::size_t utf16_length(std::string_view text){std::size_t n=0;for(unsigned char c:text)if((c&0xc0)!=0x80)n+=c>=0xf0?2:1;return n;}
class Lexer {
 std::string_view source;std::size_t pos=0,line=1,column=1;
 char take(){char c=source[pos++];if(c=='\n'){line++;column=1;}else column++;return c;}
public:
 explicit Lexer(std::string_view s):source(s){}
 Token next(){
  for(;;){while(pos<source.size()&&static_cast<unsigned char>(source[pos])<=32)take();
   if(source.substr(pos,2)=="//"){while(pos<source.size()&&take()!='\n'){}continue;}
   if(source.substr(pos,2)=="/*"){take();take();while(pos<source.size()&&source.substr(pos,2)!="*/")take();if(pos<source.size()){take();take();}continue;}break;}
  Token t{Kind::End,"",0,line,column};if(pos==source.size())return t;char c=take();
  if((c>='A'&&c<='Z')||(c>='a'&&c<='z')||c=='_'){
   t.kind=Kind::Name;t.text+=c;while(pos<source.size()){c=source[pos];if(!((c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='_'))break;t.text+=take();}if(t.text.size()>255)fail(t,"name too long, max 255");return t;}
  if(c>='0'&&c<='9'){
   t.kind=Kind::Integer;std::uint32_t value=c-'0',base=10;if(c=='0'&&pos<source.size()&&(source[pos]=='x'||source[pos]=='X')){take();base=16;value=0;}
   while(pos<source.size()){unsigned char d=source[pos];if((d=='x'||d=='X')&&value==0&&base==10){take();base=16;continue;}unsigned n=d>='0'&&d<='9'?d-'0':d>='a'&&d<='f'?d-'a'+10:d>='A'&&d<='F'?d-'A'+10:99;if(n>=base)break;take();auto v=std::uint64_t(value)*base+n;if(v>0xffffffffull)fail(t,"integer overflow");value=static_cast<std::uint32_t>(v);}
   t.integer=std::bit_cast<std::int32_t>(value);return t;}
  if(c=='"'){
   t.kind=Kind::String;auto begin=pos-1;bool escaped=false,closed=false;
   while(pos<source.size()){c=take();if(c=='\\'&&!escaped){escaped=true;continue;}if(c=='"'&&!escaped){closed=true;break;}if(escaped&&c=='n')t.text+='\n';else if(escaped&&c=='t')t.text+="    ";else t.text+=c;escaped=false;}
   if(!closed)fail(t,"unterminated string");if(utf16_length(source.substr(begin,pos-begin-1))>4096)fail(t,"string too long, max 4096");return t;}
  if(std::string_view("{}(),;+-").find(c)!=std::string_view::npos){t.kind=Kind::Symbol;t.text+=c;return t;}
  fail(t,"unsupported token in native LSL subset");
 }
};
struct Expression {
 enum class Op {Literal,Parameter,Negate,Add,Subtract,StringCast};
 Op op=Op::Literal;unsigned height=1;Type type=Type::Integer;LslValue value=std::int32_t(0);std::unique_ptr<Expression> left,right;
};
struct Instruction {bool owner=false;std::unique_ptr<Expression> channel,text;};
struct Handler {std::string parameter;std::vector<Instruction> instructions;};
std::int32_t wrap(std::uint32_t n){return std::bit_cast<std::int32_t>(n);}
LslValue evaluate(const Expression& e,const LslEvent& event){
 switch(e.op){
 case Expression::Op::Literal:return e.value;
 case Expression::Op::Parameter:return event.arguments.at(0);
 case Expression::Op::Negate:return wrap(0u-static_cast<std::uint32_t>(std::get<std::int32_t>(evaluate(*e.left,event))));
 case Expression::Op::StringCast:{auto v=evaluate(*e.left,event);if(auto i=std::get_if<std::int32_t>(&v))return std::to_string(*i);return v;}
 case Expression::Op::Add:case Expression::Op::Subtract:{auto a=evaluate(*e.left,event),b=evaluate(*e.right,event);if(e.type==Type::String)return std::get<std::string>(a)+std::get<std::string>(b);auto x=static_cast<std::uint32_t>(std::get<std::int32_t>(a)),y=static_cast<std::uint32_t>(std::get<std::int32_t>(b));return wrap(e.op==Expression::Op::Add?x+y:x-y);}}
 throw std::logic_error("invalid native LSL expression");
}
}
class LslProgram {public:std::map<std::string,Handler> handlers;std::uint64_t event_mask=0;};
namespace {
class Parser {
 Lexer lexer;Token token;Handler* handler=nullptr;std::size_t depth=0;
 void advance(){token=lexer.next();}
 bool is(std::string_view text)const{return token.text==text&&token.kind!=Kind::String;}
 void require(std::string_view text){if(!is(text))fail(token,"expected "+std::string(text));advance();}
 struct Depth {Parser& p;explicit Depth(Parser& v):p(v){if(++p.depth>128)fail(p.token,"native LSL nesting limit exceeded");}~Depth(){--p.depth;}};
 std::unique_ptr<Expression> unary(){Depth guard(*this);auto e=std::make_unique<Expression>();
  if(is("-")||is("+")){bool minus=is("-");advance();auto value=unary();if(value->type!=Type::Integer)fail(token,"unary +/- requires integer");if(!minus)return value;if(value->height>=128)fail(token,"native LSL expression depth exceeded");e->height=value->height+1;e->op=Expression::Op::Negate;e->left=std::move(value);return e;}
  if(is("(")){advance();if(is("string")){advance();require(")");e->op=Expression::Op::StringCast;e->type=Type::String;e->left=unary();if(e->left->height>=128)fail(token,"native LSL expression depth exceeded");e->height=e->left->height+1;return e;}auto value=expression();require(")");return value;}
  if(token.kind==Kind::Integer){e->value=token.integer;advance();return e;}
  if(token.kind==Kind::String){e->type=Type::String;e->value=token.text;advance();return e;}
  if(token.kind==Kind::Name&&handler&&!handler->parameter.empty()&&token.text==handler->parameter){e->op=Expression::Op::Parameter;advance();return e;}
  fail(token,"unsupported expression or undefined name");
 }
 std::unique_ptr<Expression> expression(){auto left=unary();std::size_t count=0;while(is("+")||is("-")){if(++count>128)fail(token,"native LSL expression limit exceeded");bool plus=is("+");auto at=token;advance();auto right=unary();if(left->type!=right->type||(!plus&&left->type!=Type::Integer))fail(at,"operator operand type mismatch");auto e=std::make_unique<Expression>();e->type=left->type;e->height=std::max(left->height,right->height)+1;if(e->height>128)fail(at,"native LSL expression depth exceeded");e->op=plus?Expression::Op::Add:Expression::Op::Subtract;e->left=std::move(left);e->right=std::move(right);left=std::move(e);}return left;}
 void block(){Depth guard(*this);require("{");while(!is("}")){if(token.kind==Kind::End)fail(token,"eof parsing event body");if(is("{")){block();continue;}if(is(";")){advance();continue;}
   Instruction ins;if(is("llSay"))ins.owner=false;else if(is("llOwnerSay"))ins.owner=true;else fail(token,"unsupported statement/API in native LSL subset");advance();require("(");
   if(!ins.owner){ins.channel=expression();if(ins.channel->type!=Type::Integer)fail(token,"llSay channel must be integer");require(",");}
   ins.text=expression();if(ins.text->type!=Type::String)fail(token,"chat text must be string");require(")");require(";");handler->instructions.push_back(std::move(ins));
  }require("}");}
public:
 explicit Parser(std::string_view s):lexer(s){advance();}
 std::shared_ptr<LslProgram> parse(){auto program=std::make_shared<LslProgram>();require("default");require("{");while(!is("}")){
  if(token.kind!=Kind::Name)fail(token,"expected event handler");auto event=token.text;auto at=token;if(event!="state_entry"&&event!="touch_start"&&event!="touch"&&event!="touch_end")fail(at,"unsupported event in native LSL subset");advance();if(program->handlers.contains(event))fail(at,"duplicate event handler");auto [it,inserted]=program->handlers.emplace(event,Handler{});handler=&it->second;require("(");
  if(event!="state_entry"){require("integer");if(token.kind!=Kind::Name)fail(token,"expected event parameter name");if(std::string_view(" default do else float for if integer list jump key return rotation state string vector while llSay llOwnerSay ").find(" "+token.text+" ")!=std::string_view::npos)fail(token,"unsupported/reserved event parameter name");handler->parameter=token.text;advance();}require(")");block();handler=nullptr;program->event_mask|=event=="state_entry"?1ull<<30:event=="touch_start"?1ull<<21:event=="touch"?1ull<<3:1ull<<29;
 }require("}");if(token.kind!=Kind::End)fail(token,"unsupported declaration after default state");program->handlers.try_emplace("state_entry");return program;}
};
// SDK StringToBytesNoTerm excludes one existing trailing NUL and truncates at
// a UTF-8 scalar boundary. Runtime strings use UTF-8; no byte splitting occurs.
std::string say_text(std::string text){if(auto zero=text.find(char(0));zero!=std::string::npos)text.resize(zero);if(text.size()<=1023)return text;std::size_t n=1023;while(n>0&&(static_cast<unsigned char>(text[n])&0xc0)==0x80)--n;text.resize(n);return text;}
std::int64_t ticks(){using Tick=std::chrono::duration<std::int64_t,std::ratio<1,10000000>>;return std::chrono::duration_cast<Tick>(std::chrono::steady_clock::now().time_since_epoch()).count();}
}
LslCompileResult compile_lsl(std::string_view source){try{return {Parser(source).parse(),{}};}catch(const CompileFailure& e){return {nullptr,{e.diagnostic}};}}
struct LslInstance::Impl {
 std::shared_ptr<const LslProgram> program;LslContext context;LslHost host;
 std::deque<LslEvent> queue;std::map<std::string,std::int64_t> counts;std::optional<LslEvent> active;std::size_t pc=0;
 scene::ScriptExecutionControl control;
 std::int64_t last_say_check=0,sleep_until=0;std::uint64_t say_count=0;std::string fault;
 std::int64_t now()const{return host.clock_ticks?host.clock_ticks():ticks();}
 void reset(bool constructing=false){queue.clear();counts.clear();active.reset();pc=0;sleep_until=0;fault.clear();control.pending_events=0;control.active_event=false;if(constructing)control.state=scene::ScriptExecutionState::Construct;else if(control.state!=scene::ScriptExecutionState::Suspended)control.state=scene::ScriptExecutionState::Idle;post({"state_entry",{}, {}});if(constructing)control.state=control.running?(queue.empty()?scene::ScriptExecutionState::Idle:scene::ScriptExecutionState::OnStartQueue):scene::ScriptExecutionState::Suspended;}
 void set_running(bool value){control.active_event=active.has_value();control.pending_events=queue.size();scene::ScriptExecutionQueues queues;queues.queue_yield=[]{};queues.queue_start=[]{};queues.remove_sleep=[]{};queues.clear_events=[this]{queue.clear();counts.clear();};scene::script_set_running(control,value,queues);}

 bool post(LslEvent event){control.pending_events=queue.size();auto gate=scene::stopped_script_event(control,program->handlers.contains(event.name),program->handlers.contains(event.name),event.name=="state_entry");if(gate==scene::StoppedScriptEvent::Reject)return false;if(gate==scene::StoppedScriptEvent::Ignore)return true;if(gate==scene::StoppedScriptEvent::QueueStateEntry){queue.push_front(std::move(event));control.pending_events=queue.size();return true;}auto expected=event.name=="state_entry"?0u:1u;if(event.arguments.size()!=expected)return false;if(expected&&!std::holds_alternative<std::int32_t>(event.arguments[0]))return false;if(counts[event.name]>=64)return false;++counts[event.name];if(event.name=="state_entry")queue.push_front(std::move(event));else queue.push_back(std::move(event));control.pending_events=queue.size();if(control.state==scene::ScriptExecutionState::Idle)control.state=scene::ScriptExecutionState::OnStartQueue;return true;}

 void execute(const Instruction& instruction){auto text=std::get<std::string>(evaluate(*instruction.text,*active));if(instruction.owner){if(context.owner==context.group)return;if(!host.chat)throw std::runtime_error("LSL host chat callback is required");host.chat({LslChatType::Owner,0,std::move(text),context});return;}
  auto channel=std::get<std::int32_t>(evaluate(*instruction.channel,*active));auto time=now();if(channel==0){if(time-last_say_check>10000000){last_say_check=time;say_count=0;}else ++say_count;}
  if(say_count>=11){float requested=2000.f*host.script_delay_factor;if(requested>=10&&requested<2147483648.f){auto delay=static_cast<std::int32_t>(requested);sleep_until=time+std::int64_t(delay)*10000;if(host.request_sleep)host.request_sleep(delay);}}
  if(host.console_channel&&channel==*host.console_channel){if(!host.console)throw std::runtime_error("LSL host console callback is required");host.console(text);return;}
  LslChat chat{LslChatType::Say,channel,say_text(std::move(text)),context};if(!host.chat)throw std::runtime_error("LSL host chat callback is required");host.chat(chat);if(host.listener)host.listener(chat);
 }
};
LslInstance::LslInstance(std::shared_ptr<const LslProgram> p,LslContext c,LslHost h):LslInstance(std::move(p),std::move(c),std::move(h),true){}
LslInstance::LslInstance(std::shared_ptr<const LslProgram> p,LslContext c,LslHost h,bool start_running):impl_(std::make_unique<Impl>()){
 if(!p)throw std::invalid_argument("null LSL program");impl_->program=std::move(p);impl_->context=std::move(c);impl_->host=std::move(h);impl_->last_say_check=impl_->now();impl_->control.running=start_running;impl_->reset(true);
}
LslInstance::~LslInstance()=default;LslInstance::LslInstance(LslInstance&&)noexcept=default;LslInstance& LslInstance::operator=(LslInstance&&)noexcept=default;
void LslInstance::reset(){impl_->reset();}
bool LslInstance::running()const{return impl_->control.running;}
void LslInstance::running(bool value){impl_->set_running(value);}
bool LslInstance::post_event(LslEvent event){return impl_->post(std::move(event));}
std::size_t LslInstance::queued_events()const{return impl_->queue.size();}
bool LslInstance::handles_event(std::string_view name)const{return impl_->program->handlers.contains(std::string(name));}
std::uint64_t LslInstance::event_mask()const{return impl_->program->event_mask;}
const LslEvent* LslInstance::current_event()const{return impl_->active?&*impl_->active:nullptr;}
void LslInstance::set_context(LslContext c){impl_->context=std::move(c);}
LslRunResult LslInstance::run(std::size_t budget){auto& s=*impl_;LslRunResult out{LslRunStatus::Idle,0,{}};
 auto paused=[&]{if(s.control.running)return false;s.control.state=scene::ScriptExecutionState::Suspended;s.control.active_event=s.active.has_value();s.control.pending_events=s.queue.size();return true;};
 if(paused())return out;if(!s.fault.empty())return {LslRunStatus::Faulted,0,s.fault};
 try{for(;;){if(paused())return out;if(s.now()<s.sleep_until){s.control.state=scene::ScriptExecutionState::OnSleepQueue;out.status=LslRunStatus::Sleeping;return out;}
   if(!s.active){if(s.queue.empty()){s.control.state=scene::ScriptExecutionState::Idle;s.control.active_event=false;s.control.pending_events=0;return out;}if(out.instructions>=budget){s.control.state=scene::ScriptExecutionState::OnStartQueue;out.status=LslRunStatus::BudgetExhausted;return out;}s.active=std::move(s.queue.front());s.queue.pop_front();--s.counts[s.active->name];s.control.pending_events=s.queue.size();s.control.active_event=true;s.pc=0;}
   auto& code=s.program->handlers.at(s.active->name).instructions;if(s.pc==code.size()){s.active.reset();s.control.active_event=false;continue;}if(out.instructions>=budget){s.control.state=scene::ScriptExecutionState::OnYieldQueue;out.status=LslRunStatus::BudgetExhausted;return out;}
   s.control.state=scene::ScriptExecutionState::Running;s.execute(code[s.pc]);++s.pc;++out.instructions;
  }}catch(const std::exception& e){s.fault=e.what();s.active.reset();s.pc=0;s.control.active_event=false;s.control.state=scene::ScriptExecutionState::OnSleepQueue;out.status=LslRunStatus::Faulted;out.error=s.fault;return out;}
}
}
