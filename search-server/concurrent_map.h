#pragma once

#include <cstdlib>
#include <future>
#include <map>
#include <string>
#include <vector>

using namespace std::string_literals;

template <typename Key, typename Value>
class ConcurrentMap {

public:
    static_assert(std::is_integral_v<Key>, "ConcurrentMap supports only integer keys");

    struct Access {
        std::lock_guard<std::mutex> g;
        Value& ref_to_value;
    };

    explicit ConcurrentMap(size_t bucket_count)
        : bucket_count_(bucket_count),
          maps_(std::vector<std::map<Key, Value>>(bucket_count)),
          mtxs_(std::vector<std::mutex>(bucket_count)) {}

    Access operator[](const Key& key) {
        size_t indx = static_cast<uint64_t>(key) % bucket_count_;
        return {std::lock_guard<std::mutex>(mtxs_[indx]), maps_[indx][key]};
    }

    std::map<Key, Value> BuildOrdinaryMap() {
        std::map<Key, Value> _m;
        for(size_t i = 0; i < maps_.size(); ++i) {
            std::lock_guard g(mtxs_[i]);
            _m.insert(maps_[i].begin(), maps_[i].end());
        }
        return _m;
    }

private:
    size_t bucket_count_ = 1;
    std::vector<std::map<Key, Value>> maps_;
    std::vector<std::mutex> mtxs_;
};
