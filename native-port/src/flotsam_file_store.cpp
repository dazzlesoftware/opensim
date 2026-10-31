// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/services/flotsam_file_store.hpp"
#include "opensim/framework/asset_nrbf.hpp"
#include "opensim/framework/text.hpp"
#include <algorithm>
#include <thread>
#include <system_error>
#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#include <sys/file.h>
#include <sys/stat.h>
#endif
namespace opensim::services {
namespace {
using Path=std::filesystem::path;using Asset=std::shared_ptr<framework::AssetBase>;
std::string text(const Path& path){return framework::utf8(path.u16string());}
std::shared_ptr<FlotsamFileState> shared_state(){static auto state=std::make_shared<FlotsamFileState>();return state;}
void warning(const std::shared_ptr<FlotsamFileBackend>& backend,const std::string& message){if(backend->warn)backend->warn(message);}
std::string asset_id(const Asset& asset){if(!asset)throw std::invalid_argument("Null cache asset");return asset->id();}
bool io(FlotsamFileFailure kind){return kind==FlotsamFileFailure::Io||kind==FlotsamFileFailure::MissingFile||kind==FlotsamFileFailure::MissingDirectory;}
[[noreturn]]void error(const std::error_code& code){auto kind=(code==std::errc::permission_denied||code==std::errc::operation_not_permitted||code==std::errc::is_a_directory)?FlotsamFileFailure::Unauthorized:code==std::errc::no_such_file_or_directory?FlotsamFileFailure::MissingFile:FlotsamFileFailure::Io;throw FlotsamFileError(kind,code.message());}
#ifdef _WIN32
[[noreturn]]void win_error(){auto e=GetLastError();auto kind=e==ERROR_ACCESS_DENIED?FlotsamFileFailure::Unauthorized:e==ERROR_FILE_NOT_FOUND?FlotsamFileFailure::MissingFile:e==ERROR_PATH_NOT_FOUND?FlotsamFileFailure::MissingDirectory:FlotsamFileFailure::Io;throw FlotsamFileError(kind,std::system_category().message(static_cast<int>(e)));}
struct Handle {HANDLE h=INVALID_HANDLE_VALUE;~Handle(){if(h!=INVALID_HANDLE_VALUE)CloseHandle(h);}};
void delete_native_file(const Path& path){if(!DeleteFileW(path.c_str())){if(GetLastError()!=ERROR_FILE_NOT_FOUND)win_error();}}
void delete_native_tree(const Path& path,bool top=true){auto attributes=GetFileAttributesW(path.c_str());if(attributes==INVALID_FILE_ATTRIBUTES){if(!top&&(GetLastError()==ERROR_FILE_NOT_FOUND||GetLastError()==ERROR_PATH_NOT_FOUND))return;win_error();}if(!(attributes&FILE_ATTRIBUTE_DIRECTORY))throw FlotsamFileError(FlotsamFileFailure::Io,"Directory path is a file");
 if(!(attributes&FILE_ATTRIBUTE_REPARSE_POINT)){std::error_code ec;std::filesystem::directory_iterator it(path,ec),end;if(ec)error(ec);std::exception_ptr first;while(it!=end){auto child=it->path();try{auto attr=GetFileAttributesW(child.c_str());if(attr==INVALID_FILE_ATTRIBUTES){auto code=GetLastError();if(code!=ERROR_FILE_NOT_FOUND&&code!=ERROR_PATH_NOT_FOUND)win_error();}else if(attr&FILE_ATTRIBUTE_DIRECTORY)delete_native_tree(child,false);else delete_native_file(child);}catch(...){if(!first)first=std::current_exception();}it.increment(ec);if(ec){if(!first){try{error(ec);}catch(...){first=std::current_exception();}}break;}}if(first)std::rethrow_exception(first);}
 if(!RemoveDirectoryW(path.c_str())){if(!top&&(GetLastError()==ERROR_FILE_NOT_FOUND||GetLastError()==ERROR_PATH_NOT_FOUND))return;win_error();}
}
#else
struct Handle {int fd=-1;~Handle(){if(fd>=0)::close(fd);}};
#endif
void write_native(const Path& path,const Asset& asset){
#ifdef _WIN32
 Handle file{CreateFileW(path.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr)};if(file.h==INVALID_HANDLE_VALUE)win_error();
#else
 Handle file{::open(path.c_str(),O_CREAT|O_TRUNC|O_RDWR,0666)};if(file.fd<0)error({errno,std::generic_category()});if(::flock(file.fd,LOCK_EX|LOCK_NB)<0)error({errno,std::generic_category()});
#endif
 if(!asset)throw std::invalid_argument("Null asset serialization");auto bytes=framework::AssetNrbfCodec::encode(*asset);std::size_t offset=0;while(offset<bytes.size()){
#ifdef _WIN32
  DWORD count=0;auto length=static_cast<DWORD>(std::min<std::size_t>(bytes.size()-offset,INT32_MAX));if(!WriteFile(file.h,bytes.data()+offset,length,&count,nullptr))win_error();if(!count)throw FlotsamFileError(FlotsamFileFailure::Io,"Zero-byte file write");offset+=count;
#else
  auto count=::write(file.fd,bytes.data()+offset,bytes.size()-offset);if(count<0){if(errno==EINTR)continue;error({errno,std::generic_category()});}if(!count)throw FlotsamFileError(FlotsamFileFailure::Io,"Zero-byte file write");offset+=static_cast<std::size_t>(count);
#endif
 }
}
FlotsamFileReader read_native(const Path& path){
 auto file=std::make_shared<Handle>();
#ifdef _WIN32
 file->h=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);if(file->h==INVALID_HANDLE_VALUE)win_error();
#else
 file->fd=::open(path.c_str(),O_RDONLY);if(file->fd<0)error({errno,std::generic_category()});if(::flock(file->fd,LOCK_SH|LOCK_NB)<0)error({errno,std::generic_category()});
#endif
 FlotsamFileReader reader;reader.length=[file]()->std::uint64_t{
#ifdef _WIN32
 LARGE_INTEGER size;if(!GetFileSizeEx(file->h,&size))win_error();if(size.QuadPart<0)throw FlotsamFileError(FlotsamFileFailure::Io,"Negative file length");return static_cast<std::uint64_t>(size.QuadPart);
#else
 struct stat status;if(::fstat(file->fd,&status)<0)error({errno,std::generic_category()});if(S_ISDIR(status.st_mode))throw FlotsamFileError(FlotsamFileFailure::Unauthorized,"Cannot open a directory as a file");return static_cast<std::uint64_t>(status.st_size);
#endif
 };auto length=reader.length;reader.deserialize=[file,length]{auto size=length();if(size>SIZE_MAX)throw FlotsamFileError(FlotsamFileFailure::Io,"Asset cache file too large");std::vector<std::uint8_t> data(static_cast<std::size_t>(size));std::size_t offset=0;while(offset<data.size()){
#ifdef _WIN32
 DWORD count=0;auto chunk=static_cast<DWORD>(std::min<std::size_t>(data.size()-offset,INT32_MAX));if(!ReadFile(file->h,data.data()+offset,chunk,&count,nullptr))win_error();if(!count)break;offset+=count;
#else
 auto count=::read(file->fd,data.data()+offset,data.size()-offset);if(count<0){if(errno==EINTR)continue;error({errno,std::generic_category()});}if(!count)break;offset+=static_cast<std::size_t>(count);
#endif
 }data.resize(offset);Asset asset;if(!framework::AssetNrbfCodec::decode_file(data,asset))throw FlotsamFileError(FlotsamFileFailure::Serialization,"Invalid or unsupported AssetBase serialization");return asset;};reader.close=[file]{
#ifdef _WIN32
 if(file->h!=INVALID_HANDLE_VALUE){CloseHandle(file->h);file->h=INVALID_HANDLE_VALUE;}
#else
 if(file->fd>=0){::close(file->fd);file->fd=-1;}
#endif
 };return reader;
}
}
FlotsamFileStore::FlotsamFileStore(Options options,std::shared_ptr<FlotsamFileBackend> backend,std::shared_ptr<FlotsamFileState> state):options_(std::move(options)),backend_(std::move(backend)),state_(state?std::move(state):shared_state()){
 options_.tiers=std::clamp(options_.tiers,1,3);options_.tier_length=std::clamp(options_.tier_length,1,4);if(!backend_||(options_.update_access&&!backend_->touch_access)||!backend_->worker_available||!backend_->enqueue||!backend_->file_exists||!backend_->directory_exists||!backend_->random_file_name||!backend_->create_directory||!backend_->remove_file||!backend_->move_file||!backend_->write_asset||!backend_->open_read)throw std::invalid_argument("Incomplete Flotsam file backend");
}
std::u16string FlotsamFileStore::file_name_text(const std::string& id)const{auto value=framework::utf16_replacing(id);for(auto& c:value){
#ifdef _WIN32
 if(c<32||std::u16string_view(u"<>:\"/\\|?*").find(c)!=std::u16string_view::npos)c=u'_';
#else
 if(c==0||c==u'/')c=u'_';
#endif
 }auto length=static_cast<std::size_t>(options_.tiers*options_.tier_length);if(value.size()<length)throw std::out_of_range("Short cache ID tier span");std::u16string relative;for(int tier=0;tier<options_.tiers;++tier){relative+=value.substr(static_cast<std::size_t>(tier*options_.tier_length),static_cast<std::size_t>(options_.tier_length));relative+=static_cast<char16_t>(Path::preferred_separator);}relative+=value;
#ifdef _WIN32
 return (options_.directory/Path(relative)).u16string();
#else
 auto root=options_.directory.u16string();if(!root.empty()&&root.back()!=u'/')root+=u'/';return root+relative;
#endif
}
Path FlotsamFileStore::file_name(const std::string& id)const{auto value=file_name_text(id);
#ifdef _WIN32
 return Path(value);
#else
 auto encoded=framework::utf8(value);return Path(std::u8string(encoded.begin(),encoded.end()));
#endif
}
void FlotsamFileStore::cache(const std::string& id,Asset asset,bool replace){if(!backend_->worker_available())return;auto filename=file_name(id);try{{std::lock_guard lock(state_->mutex);if(!state_->writing.insert(filename.u16string()).second)return;}if(backend_->worker_available()){auto backend=backend_;auto state=state_;auto access=options_.update_access;backend_->enqueue([filename,asset,replace,access,backend,state]()mutable{try{write(filename,asset,replace,access,backend,state);asset.reset();std::this_thread::yield();}catch(...) {}});}}catch(const std::exception& e){warning(backend_,"[FLOTSAM ASSET CACHE]: Failed to update cache for asset "+asset_id(asset)+": "+e.what());}}
void FlotsamFileStore::write(const Path& filename,Asset asset,bool replace,bool access,const std::shared_ptr<FlotsamFileBackend>& b,const std::shared_ptr<FlotsamFileState>& state){struct Finish {std::shared_ptr<FlotsamFileState> state;Path file;~Finish(){try{std::lock_guard lock(state->mutex);state->writing.erase(file.u16string());}catch(...) {}}} finish{state,filename};auto exists=b->file_exists(filename);if(!replace&&exists){if(access&&b->touch_access)b->touch_access(filename);return;}auto directory=filename.parent_path();auto temp=directory/b->random_file_name();
 try{if(!b->directory_exists(directory))b->create_directory(directory);b->write_asset(temp,asset);if(b->note_access)b->note_access(filename,900000);}catch(const FlotsamFileError& e){if(io(e.kind)){warning(b,"[FLOTSAM ASSET CACHE]: Failed to write asset "+asset_id(asset)+" to temporary location "+text(temp)+" (final "+text(filename)+") on cache in "+text(directory)+": "+e.what());return;}if(e.kind!=FlotsamFileFailure::Unauthorized)throw;}
 try{if(exists)b->remove_file(filename);b->move_file(temp,filename);}catch(...){try{b->remove_file(temp);}catch(...) {}}
}
Asset FlotsamFileStore::get(const std::string& id){auto filename=file_name(id);{std::lock_guard lock(state_->mutex);if(state_->writing.contains(filename.u16string())){++state_->requests_in_progress;return {};}}Asset asset;try{auto reader=backend_->open_read(filename);std::exception_ptr failure;bool empty=false;try{if(reader.length()==0)empty=true;else{asset=reader.deserialize();std::lock_guard lock(state_->mutex);++state_->disk_hits;}}catch(...){failure=std::current_exception();}try{reader.close();}catch(...){failure=std::current_exception();}if(failure)std::rethrow_exception(failure);if(empty)return {};}catch(const FlotsamFileError& e){if(e.kind==FlotsamFileFailure::MissingFile||e.kind==FlotsamFileFailure::MissingDirectory)return asset;warning(backend_,"[FLOTSAM ASSET CACHE]: Failed to get file "+text(filename)+" for asset "+id+": "+e.what());if(e.kind==FlotsamFileFailure::Serialization)backend_->remove_file(filename);}catch(const std::exception& e){warning(backend_,"[FLOTSAM ASSET CACHE]: Failed to get file "+text(filename)+" for asset "+id+": "+e.what());}return asset;}
bool FlotsamFileStore::check(const std::string& id)const{try{return backend_->file_exists(file_name(id));}catch(...){return false;}}
bool FlotsamFileStore::writing(const std::string& id)const{auto file=file_name(id);std::lock_guard lock(state_->mutex);return state_->writing.contains(file.u16string());}
std::shared_ptr<FlotsamFileBackend> native_flotsam_file_backend(std::shared_ptr<framework::ObjectJobQueue> queue,std::function<void(const std::string&)> warn){auto b=std::make_shared<FlotsamFileBackend>();std::weak_ptr<framework::ObjectJobQueue> weak=queue;b->worker_available=[weak]{return !weak.expired();};b->enqueue=[weak](auto task){if(auto queue=weak.lock())queue->enqueue(std::move(task));};b->file_exists=[](const Path& path){std::error_code ec;auto status=std::filesystem::status(path,ec);return !ec&&std::filesystem::exists(status)&&!std::filesystem::is_directory(status);};b->directory_exists=[](const Path& path){std::error_code ec;return std::filesystem::is_directory(path,ec);};b->random_file_name=[](){auto s=framework::Uuid::random().str();s.erase(std::remove(s.begin(),s.end(),'-'),s.end());return Path(s.substr(0,8)+"."+s.substr(8,3));};b->create_directory=[](const Path& path){std::error_code ec;std::filesystem::create_directories(path,ec);if(ec)error(ec);};b->remove_file=[](const Path& path){
#ifdef _WIN32
 delete_native_file(path);
#else
 std::error_code ec;auto status=std::filesystem::status(path,ec);if(!ec&&std::filesystem::is_directory(status))throw FlotsamFileError(FlotsamFileFailure::Unauthorized,"Cannot delete a directory as a file");ec.clear();std::filesystem::remove(path,ec);if(ec&&ec!=std::errc::no_such_file_or_directory)error(ec);
#endif
 };b->move_file=[](const Path& source,const Path& target){
#ifdef _WIN32
 if(!MoveFileW(source.c_str(),target.c_str()))win_error();
#else
 if(::link(source.c_str(),target.c_str())<0)error({errno,std::generic_category()});if(::unlink(source.c_str())<0)error({errno,std::generic_category()});
#endif
 };b->list_directories=[](const Path& root){std::error_code ec;std::vector<Path> result;std::filesystem::directory_iterator it(root,ec),end;if(ec)error(ec);while(it!=end){auto directory=it->is_directory(ec);if(ec)error(ec);if(directory)result.push_back(it->path());it.increment(ec);if(ec)error(ec);}return result;};b->remove_directory_tree=[](const Path& path){
#ifdef _WIN32
 delete_native_tree(path);
#else
 std::error_code ec;std::filesystem::remove_all(path,ec);if(ec)error(ec);
#endif
 };b->write_asset=write_native;b->open_read=read_native;b->warn=std::move(warn);return b;}
}
