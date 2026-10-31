// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include <optional>
#include <cstdint>
#include <stdexcept>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>
namespace opensim::framework {
// .NET Dictionary enumeration walks entry slots; removed slots are reused LIFO.
// Keep that observable order for legacy service-url and seed arrays.
template<class K,class V>class OrderedDictionary {
public:
    using value_type=std::pair<const K,V>;
    template<bool Const>class Iterator {
        using Owner=std::conditional_t<Const,const OrderedDictionary,OrderedDictionary>;
        Owner* owner_;std::size_t index_;std::uint32_t generation_;
        void skip(){if(index_>owner_->slots_.size())index_=owner_->slots_.size();while(index_<owner_->slots_.size()&&!owner_->slots_[index_])++index_;}
    public:
        Iterator(Owner* owner,std::size_t index):owner_(owner),index_(index),generation_(owner->generation_){skip();}
        auto& operator*()const{return owner_->slots_.at(index_).value();}
        auto* operator->()const{return &**this;}
        Iterator& operator++(){if(generation_!=owner_->generation_)throw std::runtime_error("Dictionary changed during enumeration");++index_;skip();return *this;}
        bool operator==(const Iterator& other)const{
            if(owner_!=other.owner_||generation_!=other.generation_)return false;
            // A foreach end is cached before callbacks run. .NET permits
            // Clear during enumeration; both positions then denote the end.
            const auto size=owner_->slots_.size();return (index_>=size?size:index_)==(other.index_>=size?size:other.index_);
        }
    };
    auto begin(){return Iterator<false>(this,0);}auto end(){return Iterator<false>(this,slots_.size());}
    auto begin()const{return Iterator<true>(this,0);}auto end()const{return Iterator<true>(this,slots_.size());}
    auto find(const K& key){auto i=index_.find(key);return i==index_.end()?end():Iterator<false>(this,i->second);}
    auto find(const K& key)const{auto i=index_.find(key);return i==index_.end()?end():Iterator<true>(this,i->second);}
    bool contains(const K& key)const{return index_.contains(key);}
    std::size_t size()const{return index_.size();}bool empty()const{return size()==0;}
    V& operator[](const K& key){
        auto i=find(key);if(i!=end())return i->second;
        auto slot=free_.empty()?slots_.size():free_.back();index_.emplace(key,slot);
        try{
            if(free_.empty())slots_.push_back(value_type{key,V{}});
            else{slots_[slot].emplace(key,V{});free_.pop_back();}
        }catch(...){index_.erase(key);throw;}
        ++generation_;return slots_[slot]->second;
    }
    V& at(const K& key){auto i=find(key);if(i==end())throw std::out_of_range("Dictionary key");return i->second;}
    const V& at(const K& key)const{auto i=find(key);if(i==end())throw std::out_of_range("Dictionary key");return i->second;}
    bool erase(const K& key){auto i=index_.find(key);if(i==index_.end())return false;free_.push_back(i->second);slots_[i->second].reset();index_.erase(i);return true;}
    void clear(){slots_.clear();free_.clear();index_.clear();}
private:
    std::vector<std::optional<value_type>> slots_;std::vector<std::size_t> free_;
    std::unordered_map<K,std::size_t> index_;
    std::uint32_t generation_=0;
};
}
