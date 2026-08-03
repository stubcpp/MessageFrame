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

    HybridMessageMap::HybridMessageMap() : HybridMessageMap(FrameConfig{}) {}

    HybridMessageMap::HybridMessageMap(const FrameConfig& config)
        : cfg_(config), is_vector_mode(true), map_storage(nullptr) {
        prime_storage();
    }

    void HybridMessageMap::prime_storage() {
        if (cfg_.initial_reserve > SMALL_CAPACITY) {
            // Skip vector mode entirely: add()/add_flat() never touch the
            // vector, convert_to_map() never runs, and the map is sized for
            // the real expected count instead of SMALL_CAPACITY — this is
            // what actually removes the rehash overhead on large frames.
            is_vector_mode = false;
            map_storage = std::make_unique<MapImpl>();
            map_storage->map.reserve(cfg_.initial_reserve);
        }
        else {
            is_vector_mode = true;
            vector_storage.reserve(SMALL_CAPACITY); // unchanged from today
        }
    }

    HybridMessageMap::~HybridMessageMap() noexcept {};

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

    void HybridMessageMap::add_flat(const FlatKey& flat_key, const ParameterValue& val) {
#ifndef NDEBUG
        if (find_flat(flat_key) != nullptr) {
            assert(false && "HybridMessageMap::add_flat() called with duplicate key — use set_flat() for upsert semantics");
        }
#endif
        if (is_vector_mode) {
            if (vector_storage.size() >= SMALL_CAPACITY) {
                convert_to_map();
            } else {
                // FlatKey is already composed — no split, no re-concatenation.
                vector_storage.emplace_back(ParameterKey{std::string(flat_key.view())}, val);
                return;
            }
        }
        map_storage->map.emplace(ParameterKey{std::string(flat_key.view())}, val);
    }

    void HybridMessageMap::add_flat(const FlatKey& flat_key, ParameterValue&& val) {
#ifndef NDEBUG
        if (find_flat(flat_key) != nullptr) {
            assert(false && "HybridMessageMap::add_flat() called with duplicate key — use set_flat() for upsert semantics");
        }
#endif
        if (is_vector_mode) {
            if (vector_storage.size() >= SMALL_CAPACITY) {
                convert_to_map();
            } else {
                vector_storage.emplace_back(ParameterKey{std::string(flat_key.view())}, std::move(val));
                return;
            }
        }
        map_storage->map.emplace(ParameterKey{std::string(flat_key.view())}, std::move(val));
    }

    void HybridMessageMap::set(std::string_view device, std::string_view param, const ParameterValue& val) {
        set_impl(device, param, val);
    }

    void HybridMessageMap::set(std::string_view device, std::string_view param, ParameterValue&& val) {
        set_impl(device, param, std::move(val));
    }

    void HybridMessageMap::set_flat(const FlatKey& flat_key, const ParameterValue& val) {
        const ParameterValue* existing = find_flat(flat_key);
        if (existing != nullptr) {
            *const_cast<ParameterValue*>(existing) = val;
            return;
        }
        add_flat(flat_key, val);
    }
    
    void HybridMessageMap::set_flat(const FlatKey& flat_key, ParameterValue&& val) {
        const ParameterValue* existing = find_flat(flat_key);
        if (existing != nullptr) {
            *const_cast<ParameterValue*>(existing) = std::move(val);
            return;
        }
        add_flat(flat_key, std::move(val));
    }

    bool HybridMessageMap::update(std::string_view device, std::string_view param, const ParameterValue& val) {
        return update_impl(device, param, val);
    }

    bool HybridMessageMap::update(std::string_view device, std::string_view param, ParameterValue&& val) {
        return update_impl(device, param, std::move(val));
    }

    bool HybridMessageMap::update_flat(const FlatKey& flat_key, const ParameterValue& val) {
        const ParameterValue* existing = find_flat(flat_key);
        if (existing != nullptr) {
            *const_cast<ParameterValue*>(existing) = val;
            return true;
        }
        return false;
    }
    
    bool HybridMessageMap::update_flat(const FlatKey& flat_key, ParameterValue&& val) {
        const ParameterValue* existing = find_flat(flat_key);
        if (existing != nullptr) {
            *const_cast<ParameterValue*>(existing) = std::move(val);
            return true;
        }
        return false;
    }
    
    const ParameterValue* HybridMessageMap::find(std::string_view device, std::string_view param) const noexcept {
        if (is_vector_mode) {
            // Searching the vector without creating any temporary strings.
            auto it = std::find_if(vector_storage.begin(), vector_storage.end(),
                                   [device, param](const auto& pair) {
                                       std::string_view l_view(pair.first.full_key);
                                       if (l_view.size() != device.size() + 1 + param.size()) return false;
                                       if (l_view.compare(0, device.size(), device) != 0) return false;
                                       if (l_view[device.size()] != '\x1F') return false; //0x1F — Unit Separator (US)
                                       return l_view.substr(device.size() + 1) == param;
                                   });
            return (it != vector_storage.end()) ? &(it->second) : nullptr;
        } else {
            // Guaranteed Zero-Allocation key failure on stack (due to SSO std::string)
            std::string stack_key;
            stack_key.reserve(device.size() + 1 + param.size());

            // Using "0x1F" - Unit Separator (US) - special hidden control character from ASCII table,
            // to protect from many dots in "device" or "parameter":
            // for example, engine.cylinder.1.temperature
            stack_key.append(device).append("\x1F").append(param);

            // Map transparently looks for string_view from SSO string, which is 100% alive during the entire lookup
            auto it = map_storage->map.find(std::string_view(stack_key));
            return (it != map_storage->map.end()) ? &(it->second) : nullptr;
        }
    }  

    const ParameterValue* HybridMessageMap::find_flat(const FlatKey& flat_key) const noexcept {
        std::string_view key_view = flat_key.view();
        if (is_vector_mode) {
            auto it = std::find_if(vector_storage.begin(), vector_storage.end(),
                                   [key_view](const auto& pair) { return pair.first.full_key == key_view; });
            return (it != vector_storage.end()) ? &(it->second) : nullptr;
        } else {
            auto it = map_storage->map.find(key_view);
            return (it != map_storage->map.end()) ? &(it->second) : nullptr;
        }
    }

    void HybridMessageMap::clear() noexcept {
        vector_storage.clear();
        map_storage.reset();
        is_vector_mode = true;
        try {
            prime_storage(); // re-apply the original hint, not just "forget" it
        }
        catch (const std::bad_alloc&) {
            // Preserve clear()'s noexcept contract even on OOM: fall back to
            // lazy vector mode instead of terminating a hot-path clear().
            is_vector_mode = true;
            map_storage.reset();
            vector_storage.reserve(SMALL_CAPACITY);
        }
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
        for (auto& pair : vector_storage) {
            map_storage->map.emplace(ParameterKey{ pair.first.full_key }, std::move(pair.second));
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
