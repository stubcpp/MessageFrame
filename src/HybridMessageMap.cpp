#include "messageframe/HybridMessageMap.hpp"
#include "messageframe/Value.hpp"
#include "messageframe/Structures.hpp"
#include <tsl/robin_map.h> 
#include <msgpack.hpp>
#include <algorithm> 
#include <vector>
#include <cstring>


namespace msgframe {
	
    // MapImpl (Implementation of pImpl for tsl::robin_map)
    // Uses a flat string as the only key, storing ParameterValue
    struct HybridMessageMap::MapImpl {
        tsl::robin_map<ParameterKey, ParameterValue, ParameterKeyHash, ParameterKeyEqual> map;
    };

    void HybridMessageMap::map_emplace_rvalue(std::string_view device, std::string_view param, ParameterValue&& val) {
        map_storage->map.emplace(ParameterKey{device, param}, std::move(val));
    }

    void HybridMessageMap::map_emplace_lvalue(std::string_view device, std::string_view param, const ParameterValue& val) {
        map_storage->map.emplace(ParameterKey{device, param}, val);
    }

    ParameterValue* HybridMessageMap::map_find_mutable(std::string_view device, std::string_view param) noexcept {
        // Make the current object (*this) const so that we can call the const version of find()
        const HybridMessageMap* const_this = this;
        const ParameterValue* const_res = const_this->find(device, param);

        // Unconst the result ONLY.
        return const_cast<ParameterValue*>(const_res);
    }

    // Instantiation for basic methods (with two key components device + param)
    template void msgframe::HybridMessageMap::add_impl<const msgframe::ParameterValue&>(std::string_view, std::string_view, const msgframe::ParameterValue&);
    template void msgframe::HybridMessageMap::add_impl<msgframe::ParameterValue>(std::string_view, std::string_view, msgframe::ParameterValue&&);
    template void msgframe::HybridMessageMap::set_impl<const msgframe::ParameterValue&>(std::string_view, std::string_view, const msgframe::ParameterValue&);
    template void msgframe::HybridMessageMap::set_impl<msgframe::ParameterValue>(std::string_view, std::string_view, msgframe::ParameterValue&&);
    template bool msgframe::HybridMessageMap::update_impl<const msgframe::ParameterValue&>(std::string_view, std::string_view, const msgframe::ParameterValue&);
    template bool msgframe::HybridMessageMap::update_impl<msgframe::ParameterValue>(std::string_view, std::string_view, msgframe::ParameterValue&&);

    HybridMessageMap::HybridMessageMap() : 
        is_vector_mode(true),
        map_storage(nullptr) {

        // Optimize the vector for the CPU L1 cache line 
        vector_storage.reserve(SMALL_CAPACITY);
    }

    HybridMessageMap::~HybridMessageMap() {}; 

    // Moving objects
    HybridMessageMap::HybridMessageMap(HybridMessageMap&& other) noexcept 
        : is_vector_mode(other.is_vector_mode),
          vector_storage(std::move(other.vector_storage)),
          map_storage(std::move(other.map_storage)) {}

    HybridMessageMap& HybridMessageMap::operator=(HybridMessageMap&& other) noexcept {
        if (this != &other) {
            is_vector_mode = other.is_vector_mode;
            vector_storage = std::move(other.vector_storage);
            map_storage = std::move(other.map_storage);
        }
        return *this;
    }

    // Zero-Allocation key collection via stack with Small String Optimization (SSO)
    void HybridMessageMap::add(std::string_view device, std::string_view param, const ParameterValue& val) {
        // Implementation for lvalue (copy only when pasting into the container)
        add_impl(device, param, val);
    }
        
    void HybridMessageMap::add(std::string_view device, std::string_view param, ParameterValue&& val) {
        // Implementation for rvalue (maximum Low-Latency path, 0 copies!)
        add_impl(device, param, std::move(val));
    }

    void HybridMessageMap::add_flat(std::string_view flat_key, const ParameterValue& val) {
        size_t dot_pos = flat_key.find('.');
        if (dot_pos == std::string_view::npos) {
            add_impl("", flat_key, val);
        } else {
            add_impl(flat_key.substr(0, dot_pos), flat_key.substr(dot_pos + 1), val);
        }
    }

    void HybridMessageMap::add_flat(std::string_view flat_key, ParameterValue&& val) {
        size_t dot_pos = flat_key.find('.');
        if (dot_pos == std::string_view::npos) {
            add_impl("", flat_key, std::move(val));
        } else {
            add_impl(flat_key.substr(0, dot_pos), flat_key.substr(dot_pos + 1), std::move(val));
        }
    }

    void HybridMessageMap::set(std::string_view device, std::string_view param, const ParameterValue& val) {
        set_impl(device, param, val);
    }

    void HybridMessageMap::set(std::string_view device, std::string_view param, ParameterValue&& val) {
        set_impl(device, param, std::move(val));
    }

    void HybridMessageMap::set_flat(std::string_view flat_key, const ParameterValue& val) {
        size_t dot_pos = flat_key.find('.');
        if (dot_pos == std::string_view::npos) {
            set_impl("", flat_key, val);
        } else {
            set_impl(flat_key.substr(0, dot_pos), flat_key.substr(dot_pos + 1), val);
        }
    }
    
    void HybridMessageMap::set_flat(std::string_view flat_key, ParameterValue&& val) {
        size_t dot_pos = flat_key.find('.');
        if (dot_pos == std::string_view::npos) {
            set_impl("", flat_key, std::move(val));
        } else {
            set_impl(flat_key.substr(0, dot_pos), flat_key.substr(dot_pos + 1), std::move(val));
        }
    }

    bool HybridMessageMap::update(std::string_view device, std::string_view param, const ParameterValue& val) {
        return update_impl(device, param, val);
    }

    bool HybridMessageMap::update(std::string_view device, std::string_view param, ParameterValue&& val) {
        return update_impl(device, param, std::move(val));
    }

    bool HybridMessageMap::update_flat(std::string_view flat_key, const ParameterValue& val) {
        size_t dot_pos = flat_key.find('.');
        if (dot_pos == std::string_view::npos) {
            return update_impl("", flat_key, val);
        } else {
            return update_impl(flat_key.substr(0, dot_pos), flat_key.substr(dot_pos + 1), val);
        }
    }

    bool HybridMessageMap::update_flat(std::string_view flat_key, ParameterValue&& val) {
        size_t dot_pos = flat_key.find('.');
        if (dot_pos == std::string_view::npos) {
            return update_impl("", flat_key, std::move(val));
        } else {
            return update_impl(flat_key.substr(0, dot_pos), flat_key.substr(dot_pos + 1), std::move(val));
        }
    }
    
    const ParameterValue* HybridMessageMap::find(std::string_view device, std::string_view param) const noexcept {
        if (is_vector_mode) {
            // Searching the vector without creating any temporary strings.
            auto it = std::find_if(vector_storage.begin(), vector_storage.end(),
                                   [device, param](const auto& pair) {
                                       std::string_view l_view(pair.first.full_key);
                                       if (l_view.size() != device.size() + 1 + param.size()) return false;
                                       if (l_view.compare(0, device.size(), device) != 0) return false;
                                       if (l_view[device.size()] != '.') return false;
                                       return l_view.substr(device.size() + 1) == param;
                                   });
            return (it != vector_storage.end()) ? &(it->second) : nullptr;
        } else {
            // Thanks to transparent comparators, we search by string_view pair!
            // No std::string is created or copied here.
            auto it = map_storage->map.find(std::make_pair(device, param));
            return (it != map_storage->map.end()) ? &(it->second) : nullptr;
        }
    }  

    const ParameterValue* HybridMessageMap::find_flat(std::string_view flat_key) const noexcept {
        if (is_vector_mode) {
            auto it = std::find_if(vector_storage.begin(), vector_storage.end(),
                                   [flat_key](const auto& pair) { return pair.first.full_key == flat_key; });
            return (it != vector_storage.end()) ? &(it->second) : nullptr;
        } else {
            auto it = map_storage->map.find(flat_key);
            return (it != map_storage->map.end()) ? &(it->second) : nullptr;
        }
    }

    void HybridMessageMap::clear() noexcept {
        vector_storage.clear();
        if (map_storage) {
            map_storage->map.clear();
        }
        is_vector_mode = true;
    }

    size_t HybridMessageMap::size() const noexcept {
        return is_vector_mode ? vector_storage.size() : map_storage->map.size();
    }

    void HybridMessageMap::convert_to_map() {
        is_vector_mode = false;
        
        if (!map_storage) {
            map_storage = std::make_unique<MapImpl>();
        }

		// Allocate buckets for future size, avoiding rehashing
        map_storage->map.reserve(SMALL_CAPACITY);
		
		// Move data from vector to map without copying values
        for (size_t i = 0; i < vector_storage.size(); ++i) {
            map_storage->map.emplace(std::move(vector_storage[i].first), std::move(vector_storage[i].second));
        }

        vector_storage.clear();
        vector_storage.shrink_to_fit();
    }

    void HybridMessageMap::iterate(ConstCallback callback, void* user_data) const {
        if (!callback) return;

        if (is_vector_mode) {
            // Flat vector traversal (perfect L1/L2 cache locality)
            for (const auto& pair : vector_storage) {
                callback(pair.first.full_key, pair.second, user_data);
            }
        } else if (map_storage) {
            // Traversal robin_map, passing individual string components of the key
            for (const auto& pair : map_storage->map) {
                callback(pair.first.full_key, pair.second, user_data);
            }
        }
    }

    // MessagePack parameter map serialization
    void HybridMessageMap::pack(void* packer_ptr) const {
        // Instead of a concrete sbuffer, we use an abstract template wrapper interface
        auto* pk = static_cast<msgpack::packer<VectorBuffer>*>(packer_ptr);

        pk->pack_map(size());

        if (is_vector_mode) {
            for (const auto& pair : vector_storage) {
                pk->pack(pair.first.full_key); // Instant whole string packing
                pair.second.pack(pk);
            }
        } else if (map_storage) {
            for (const auto& pair : map_storage->map) {
                pk->pack(pair.first.full_key);
                pair.second.pack(pk);
            }
        }
    }

    // MessagePack parameter map deserialization
    void HybridMessageMap::unpack(const void* object_ptr) {
        clear(); 

        const auto& obj = *static_cast<const msgpack::object*>(object_ptr);
        if (obj.type != msgpack::type::MAP) return;

        const size_t map_size = obj.via.map.size;

        if (map_size > SMALL_CAPACITY) {
            if (!map_storage) map_storage = std::make_unique<MapImpl>();
            // Reserve is needed for a map because robin_map can dump capacity,
            // or its reuse works differently than in vector
            map_storage->map.reserve(map_size);
            is_vector_mode = false;
        }
        else {
            is_vector_mode = true;
        }

        const auto* ptr = obj.via.map.ptr;
        for (size_t i = 0; i < map_size; ++i) {
            // Getting string_view WITHOUT ANY COPYING OR ALLOCATION
            std::string_view key_view = (ptr + i)->key.as<std::string_view>();

            ParameterValue val;
            val.unpack(&(ptr + i)->val);

            if (is_vector_mode) {
                // Construct ParameterKey IN PLACE directly from string_view.
                vector_storage.emplace_back(ParameterKey{ key_view }, std::move(val));
            }
            else {
                map_storage->map.emplace(ParameterKey{ key_view }, std::move(val));
            }
        }
    }

} // namespace msgframe
