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
        //tsl::robin_map<std::string, ParameterValue> map;
        tsl::robin_map<std::string, ParameterValue, StringViewHash, std::equal_to<>> map;
    };

    template<typename T>
    void HybridMessageMap::add_flat_impl(std::string_view flat_key, T&& val) {
#ifndef NDEBUG
        if (find_flat(flat_key) != nullptr) {
            assert(false && "HybridMessageMap::add() called with duplicate key — use set() for upsert semantics");
        }
#endif

        if (is_flat_map) {
            if (vector_storage.size() >= SMALL_CAPACITY) {
                convert_to_map();
                // Код іде далі вниз і додає елемент у мапу
            }
            else {
                // std::forward збереже тип: зробить move для rvalue і копію для lvalue!
                vector_storage.push_back({ FlatKey{ std::string(flat_key) }, std::forward<T>(val) });
                return;
            }
        }

        // Швидка передача в мапу
        map_storage->map.emplace(std::string(flat_key), std::forward<T>(val));
    }

    template<typename T>
    void HybridMessageMap::set_flat_impl(std::string_view flat_key, T&& val) {
        if (is_flat_map) {
            auto it = std::find_if(vector_storage.begin(), vector_storage.end(),
                [flat_key](const auto& pair) { return pair.first.full_key == flat_key; });

            if (it != vector_storage.end()) {
                it->second = std::forward<T>(val); // Upsert-оновлення у векторі
                return;
            }

            if (vector_storage.size() >= SMALL_CAPACITY) {
                convert_to_map();
            }
            else {
                vector_storage.push_back({ FlatKey{ std::string(flat_key) }, std::forward<T>(val) });
                return;
            }
        }

        // Вставка або заміна у мапі. insert_or_assign ідеально підходить для модифікації ключів
        map_storage->map.insert_or_assign(std::string(flat_key), std::forward<T>(val));
    }

    template<typename T>
    bool HybridMessageMap::update_flat_impl(std::string_view flat_key, T&& val) {
        if (is_flat_map) {
            auto it = std::find_if(vector_storage.begin(), vector_storage.end(),
                [flat_key](const auto& pair) { return pair.first.full_key == flat_key; });

            if (it == vector_storage.end()) return false;
            it->second = std::forward<T>(val);
            return true;
        }
        else {
            auto it = map_storage->map.find(flat_key);
            if (it == map_storage->map.end()) return false;

            // Щоб обійти константність ітератора і змусити мапу виконати легальну low-latency заміну:
            map_storage->map.insert_or_assign(it->first, std::forward<T>(val));
            return true;
        }
    }

    // Явна інстанціація шаблонів для двох типів, з якими реально викликаються ці шаблони
    template void HybridMessageMap::add_flat_impl<const ParameterValue&>(std::string_view, const ParameterValue&);
    template void HybridMessageMap::add_flat_impl<ParameterValue>(std::string_view, ParameterValue&&);
    template void HybridMessageMap::set_flat_impl<const ParameterValue&>(std::string_view, const ParameterValue&);
    template void HybridMessageMap::set_flat_impl<ParameterValue>(std::string_view, ParameterValue&&);
    template bool HybridMessageMap::update_flat_impl<const ParameterValue&>(std::string_view, const ParameterValue&);
    template bool HybridMessageMap::update_flat_impl<ParameterValue>(std::string_view, ParameterValue&&);


    HybridMessageMap::HybridMessageMap() : 
        is_flat_map(true), 
        map_storage(nullptr) {

        // Optimize the vector for the CPU L1 cache line 
        vector_storage.reserve(SMALL_CAPACITY);
    }

    HybridMessageMap::~HybridMessageMap() {}; 

    // Moving objects
    HybridMessageMap::HybridMessageMap(HybridMessageMap&& other) noexcept 
        : is_flat_map(other.is_flat_map),
          vector_storage(std::move(other.vector_storage)),
          map_storage(std::move(other.map_storage)) {}

    HybridMessageMap& HybridMessageMap::operator=(HybridMessageMap&& other) noexcept {
        if (this != &other) {
            is_flat_map = other.is_flat_map;
            vector_storage = std::move(other.vector_storage);
            map_storage = std::move(other.map_storage);
        }
        return *this;
    }

    // Zero-Allocation key collection via stack with Small String Optimization (SSO)
    void HybridMessageMap::add(std::string_view device, std::string_view param, const ParameterValue& val) {
        // Реалізація для lvalue (копіюємо тільки в момент вставки в контейнер)
        add_impl(device, param, val);
    }
        
    void HybridMessageMap::add(std::string_view device, std::string_view param, ParameterValue&& val) {
        // Реалізація для rvalue (максимальний Low-Latency шлях, 0 копіювань!)
        add_impl(device, param, std::move(val));
    }

    void HybridMessageMap::add_flat(std::string_view flat_key, const ParameterValue& val) {
        add_flat_impl(flat_key, val); // Передасться як const&
    }

    void HybridMessageMap::add_flat(std::string_view flat_key, ParameterValue&& val) {
        add_flat_impl(flat_key, std::move(val)); // Передасться як &&
    }

    void HybridMessageMap::set(std::string_view device, std::string_view param, const ParameterValue& val) {
        set_impl(device, param, val);
    }

    void HybridMessageMap::set(std::string_view device, std::string_view param, ParameterValue&& val) {
        set_impl(device, param, std::move(val));
    }

    void HybridMessageMap::set_flat(std::string_view flat_key, const ParameterValue& val) {
        set_flat_impl(flat_key, val);
    }
    
    void HybridMessageMap::set_flat(std::string_view flat_key, ParameterValue&& val) {
        set_flat_impl(flat_key, std::move(val));
    }

    bool HybridMessageMap::update(std::string_view device, std::string_view param, const ParameterValue& val) {
        return update_impl(device, param, val);
    }

    bool HybridMessageMap::update(std::string_view device, std::string_view param, ParameterValue&& val) {
        return update_impl(device, param, std::move(val));
    }

    bool HybridMessageMap::update_flat(std::string_view flat_key, const ParameterValue& val) {
        return update_flat_impl(flat_key, val);
    }

    bool HybridMessageMap::update_flat(std::string_view flat_key, ParameterValue&& val) {
        return update_flat_impl(flat_key, std::move(val));
    }
    
    const ParameterValue* HybridMessageMap::find(std::string_view device, std::string_view param) const noexcept {
        if (is_flat_map) {
            // Шукаємо у векторі без створення жодного тимчасового рядка.
            for (const auto& pair : vector_storage) {
                const std::string& key = pair.first.full_key;
                // Швидка перевірка: довжина та розділова крапка
                if (key.size() == device.size() + 1 + param.size() &&
                    key.compare(0, device.size(), device) == 0 &&
                    key[device.size()] == '.' &&
                    key.compare(device.size() + 1, param.size(), param) == 0) {
                        return &pair.second;
                }
            }
            return nullptr;
        } else {
            // If we are already in the map, we’ll need to build a temporary key.
            // But thanks to tsl::robin_map and Small String Optimization (SSO),
            // short device names won’t touch the heap.
            std::string flat_key;
            flat_key.append(device).append(".").append(param);
            return find_flat(flat_key);
        }
    }  

    const ParameterValue* HybridMessageMap::find_flat(std::string_view flat_key) const noexcept {
        if (is_flat_map) {
            auto it = std::find_if(vector_storage.begin(), vector_storage.end(),
                [flat_key](const auto& pair) { return pair.first.full_key == flat_key; });

            return (it != vector_storage.end()) ? &(it->second) : nullptr;
        } else {
            if (!map_storage) return nullptr;
            // tsl::robin_map supports heterogeneous lookup via std::string_view
            auto it = map_storage->map.find(flat_key);
            return (it != map_storage->map.end()) ? &(it->second) : nullptr;
        }
    }

    void HybridMessageMap::clear() noexcept {
        vector_storage.clear();
        if (map_storage) {
            map_storage->map.clear();
        }
        is_flat_map = true;
    }

    size_t HybridMessageMap::size() const noexcept {
        return is_flat_map ? vector_storage.size() : map_storage->map.size();
    }

    void HybridMessageMap::convert_to_map() {
        map_storage = std::make_unique<MapImpl>();
        
		// Allocate buckets for future size, avoiding rehashing
		map_storage->map.reserve(vector_storage.size() * 2);
		
		// Move data from vector to map without copying values
		for (auto& pair : vector_storage) {
            map_storage->map.emplace(std::move(pair.first.full_key), std::move(pair.second));
		}

        vector_storage.clear();
        is_flat_map = false;
    }

    void HybridMessageMap::iterate(ConstCallback callback, void* user_data) const {
        if (!callback) return;

        if (is_flat_map) {
            for (const auto& pair : vector_storage) {
                callback(pair.first.full_key, pair.second, user_data);
            }
        } else if (map_storage) {
            for (const auto& pair : map_storage->map) {
                callback(pair.first, pair.second, user_data);
            }
        }
    }

    // MessagePack parameter map serialization
    void HybridMessageMap::pack(void* packer_ptr) const {
        // Замість конкретного sbuffer використовуємо абстрактний шаблонний інтерфейс обгортки
        auto* pk = static_cast<msgpack::packer<VectorBuffer>*>(packer_ptr);

        pk->pack_map(size());

        if (is_flat_map) {
            for (const auto& pair : vector_storage) {
                pk->pack(pair.first.full_key);
                pair.second.pack(pk); 
            }
        } else if (map_storage) {
            for (const auto& pair : map_storage->map) {
                pk->pack(pair.first);
                pair.second.pack(pk);
            }
        }
    }

    // MessagePack parameter map deserialization
    void HybridMessageMap::unpack(const void* object_ptr) {
        clear();

        const auto& obj = *static_cast<const msgpack::object*>(object_ptr);
        if (obj.type != msgpack::type::MAP) return;

        size_t map_size = obj.via.map.size;
        
        // If there are many elements at once, migrate to the map immediately
        if (map_size > SMALL_CAPACITY) {
            map_storage = std::make_unique<MapImpl>();
            map_storage->map.reserve(map_size);
            is_flat_map = false;
        }

        const auto* ptr = obj.via.map.ptr;
        for (size_t i = 0; i < map_size; ++i) {
            std::string key = (ptr + i)->key.as<std::string>();
            
            ParameterValue val;
            val.unpack(&(ptr + i)->val);

            if (is_flat_map) {
                vector_storage.push_back({ FlatKey{ std::move(key) }, std::move(val) });
            } else {
                // map_storage->map[std::move(key)] = std::move(val);
                map_storage->map.emplace(std::move(key), std::move(val));
            }
        }
    }

    

    

    

} // namespace msgframe