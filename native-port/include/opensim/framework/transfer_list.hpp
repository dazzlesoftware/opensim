// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include <stdexcept>
#include <vector>
namespace opensim::framework {
// List operations needed by agent transfer callbacks. Callers retain the list
// while enumerating; structural/indexer mutations invalidate enumeration.
template<class T>class TransferList {
    std::vector<T> values_;
    std::size_t version_=0;
public:
    std::size_t size()const{return values_.size();}
    bool empty()const{return values_.empty();}
    const T& get(std::size_t index)const{return values_.at(index);}
    void push_back(T value){values_.push_back(std::move(value));++version_;}
    void set(std::size_t index,T value){values_.at(index)=std::move(value);++version_;}
    void clear(){values_.clear();++version_;}
    class Iterator {
        const TransferList* owner_;
        std::size_t index_,version_;
        void check()const{if(owner_->version_!=version_)throw std::runtime_error("Transfer list changed during enumeration");}
    public:
        Iterator(const TransferList* owner,std::size_t index):owner_(owner),index_(index),version_(owner->version_){}
        const T& operator*()const{check();return owner_->values_.at(index_);}
        Iterator& operator++(){check();++index_;return *this;}
        bool operator==(const Iterator& other)const{return owner_==other.owner_&&index_==other.index_;}
    };
    Iterator begin()const{return {this,0};}Iterator end()const{return {this,values_.size()};}
};
}
