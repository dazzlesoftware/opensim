// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/services/flotsam_cache_files.hpp"
namespace opensim::services {
std::shared_ptr<FlotsamMemoryCache::FileOperations> flotsam_cache_files(std::shared_ptr<FlotsamFileStore> store,std::shared_ptr<FlotsamFileBackend> backend,std::function<void(const std::string&)> debug,std::function<void(const std::string&)> warn){
 if(!store||!backend||!backend->remove_file||!backend->directory_exists||!backend->list_directories||!backend->remove_directory_tree)throw std::invalid_argument("Incomplete composed Flotsam file operations");
 auto files=std::make_shared<FlotsamMemoryCache::FileOperations>();
 files->cache=[store](auto& id,auto& asset,bool replace){store->cache(id,asset,replace);};
 files->read=[store](auto& id){return store->get(id);};files->check=[store](auto& id){return store->check(id);};
 files->touch=[store,backend](auto& id){auto name=store->file_name(id);if(backend->touch_access)backend->touch_access(name);};
 files->expire=[store,backend](auto& id){backend->remove_file(store->file_name(id));};
 files->clear=[store,backend]{auto root=store->directory();if(backend->directory_exists(root)){auto directories=backend->list_directories(root);auto absolute_root=std::filesystem::absolute(root).lexically_normal();if(absolute_root.filename().empty())absolute_root=absolute_root.parent_path();for(auto& directory:directories){try{auto target=std::filesystem::absolute(directory).lexically_normal();if(target.parent_path()!=absolute_root)throw std::invalid_argument("Cache cleanup target is not an immediate child directory");backend->remove_directory_tree(directory);}catch(...) {}}}};
 files->debug=std::move(debug);files->warn=std::move(warn);return files;
}
}
