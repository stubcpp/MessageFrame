#pragma once
#include "messageframe/Structures.hpp"
#include "messageframe/Value.hpp"
#include <memory>
#include <vector>
#include <utility>
#include <string_view>
#include <algorithm>   
#include <cassert> 

// HYBRID CONTAINER (HYBRID MESSAGE MAP)

namespace msgframe {
    class HybridMessageMap {
    public:
        static constexpr size_t SMALL_CAPACITY = 128;

        HybridMessageMap();
        ~HybridMessageMap();

        // Moving is allowed, copying is prohibited
        HybridMessageMap(const HybridMessageMap&) = delete;
        HybridMessageMap& operator=(const HybridMessageMap&) = delete;
        HybridMessageMap(HybridMessageMap&&) noexcept;
        HybridMessageMap& operator=(HybridMessageMap&&) noexcept;

        // Adding parameters (Zero-Allocation)
        // CRITICAL. Повторний add() з тим самим ключем створить дублікат запису в vector - режимі (поведінка undefined для подальшого find — знайде перший) 
        // і призведе до некоректної поведінки. Використовуйте set() для upsert - семантики
        /**
        * @brief Додає новий параметр без перевірки на наявність дублікатів (Максимум швидкості).
        * @note Складність: O(1). У векторному режимі робить чистий push_back.
        * @warning Якщо ключ уже існує, у збірці Release утвориться дублікат (find() поверне перший).
        *          У збірці Debug спрацює assert. Використовуйте set() для логіки "вставити або оновити".
        * @param device Назва пристрою / домену (напр., "sensor_alpha")
        * @param param Назва метрики (напр., "voltage")
        * @param val Значення параметра
        */
        void add(std::string_view device, std::string_view param, const ParameterValue& val); // Для lvalue (звичайних змінних) — робимо const&, щоб не копіювати на вході
        void add(std::string_view device, std::string_view param, ParameterValue&& val);      // Для rvalue (тимчасових об'єктів / мувнутих змінних) — чистий move
        template<typename T>
        void add_impl(std::string_view device, std::string_view param, T&& val) {
            std::string flat_key;
            flat_key.reserve(device.size() + 1 + param.size());
            flat_key.append(device).append(".").append(param);

            // Передаємо як const&, add_flat сам вирішить, коли скопіювати
            add_flat(flat_key, std::forward<T>(val));
        }
		
		// Adding parameters if the key is already collected ("device.parameter")
        /**
        * @brief Додає новий параметр якщо ключ вже об'єданий ("device.parameter") без перевірки на наявність дублікатів (Максимум швидкості).
        * @note Складність: O(1). У векторному режимі робить чистий push_back.
        * @warning Якщо ключ уже існує, у збірці Release утвориться дублікат (find() поверне перший).
        *          У збірці Debug спрацює assert. Використовуйте set() для логіки "вставити або оновити".
        * @param flat_key Об'єднаний ключ (напр., "device.parameter")
        * @param val Значення параметра
        */
		void add_flat(std::string_view flat_key, const ParameterValue& val);
        void add_flat(std::string_view flat_key, ParameterValue&& val);
        template<typename T>
        void add_flat_impl(std::string_view flat_key, T&& val);

        // Upsert: оновлює значення, якщо ключ існує; інакше додає новий (повільніше за add() — лінійний пошук у vector-режимі)
        /**
        * @brief Семантика Upsert: Оновлює значення, якщо ключ уже існує, або додає новий.
        * @note Складність: у векторному режимі O(N) (лінійний пошук дубліката), у режимі мапи O(1).
        * @param device Назва пристрою / домену
        * @param param Назва метрики
        * @param val Нове значення для запису/оновлення
        */
        void set(std::string_view device, std::string_view param, const ParameterValue& val);
        void set(std::string_view device, std::string_view param, ParameterValue&& val);
        template<typename T>
        void set_impl(std::string_view device, std::string_view param, T&& val) {
            std::string flat_key;
            flat_key.reserve(device.size() + 1 + param.size());
            flat_key.append(device).append(".").append(param);
            set_flat(flat_key, std::forward<T>(val));
        }

        // Upsert: оновлює значення для об'єднаного ключа ("device.param"), якщо ключ існує; 
        // інакше додає новий (повільніше за add() — лінійний пошук у vector-режимі)
        /**
        * @brief Семантика Upsert: Оновлює значення, якщо ключ уже існує, або додає новий.
        * @note Складність: у векторному режимі O(N) (лінійний пошук дубліката), у режимі мапи O(1).
        * @param device Назва пристрою / домену
        * @param param Назва метрики
        * @param val Нове значення для запису/оновлення
        */
        void set_flat(std::string_view flat_key, const ParameterValue& val);
        void set_flat(std::string_view flat_key, ParameterValue&& val);
        template<typename T>
        void set_flat_impl(std::string_view flat_key, T&& val);

        // Strict update: оновлює лише існуючий ключ. Повертає false, якщо ключ не знайдено (значення не змінюється)
        /**
        * @brief Строге оновлення: Модифікує значення ТІЛЬКИ якщо ключ уже існує в контейнері.
        * @note Метод ніколи не збільшує розмір контейнера та не створює нових записів.
        * @param device Назва пристрою / домену
        * @param param Назва метрики
        * @param val Нове значення для існуючого параметра
        * @return true — значення успішно оновлено; false — такий ключ не знайдено (структура не змінилась).
        */
        bool update(std::string_view device, std::string_view param, const ParameterValue& val);
        bool update(std::string_view device, std::string_view param, ParameterValue&& val);
        template<typename T>
        bool update_impl(std::string_view device, std::string_view param, T&& val) {
            std::string flat_key;
            flat_key.reserve(device.size() + 1 + param.size());
            flat_key.append(device).append(".").append(param);
            return update_flat(flat_key, std::forward<T>(val));
        }

        // Strict update: оновлює лише існуючий ключ (об'єднаний ключ "device.param"). 
        // Повертає false, якщо ключ не знайдено (значення не змінюється).
        /**
        * @brief Строге оновлення: Модифікує значення ТІЛЬКИ якщо ключ уже існує в контейнері.
        * @note Метод ніколи не збільшує розмір контейнера та не створює нових записів.
        * @param device Назва пристрою / домену
        * @param param Назва метрики
        * @param val Нове значення для існуючого параметра
        * @return true — значення успішно оновлено; false — такий ключ не знайдено (структура не змінилась).
        */
        bool update_flat(std::string_view flat_key, const ParameterValue& val);
        bool update_flat(std::string_view flat_key, ParameterValue&& val);
        template<typename T>
        bool update_flat_impl(std::string_view flat_key, T&& val);
                		
		// Finding parameters without generating temporary std::string
		const ParameterValue* find(std::string_view device, std::string_view param) const noexcept;
		const ParameterValue* find_flat(std::string_view flat_key) const noexcept;

        void clear() noexcept;
        size_t size() const noexcept;

        // Quickly traverse all container elements
        using ConstCallback = void(*)(std::string_view flat_key, const ParameterValue& val, void* user_data);
        void iterate(ConstCallback callback, void* user_data) const;

        // Internal pImpl methods for MessagePack
        void pack(void* packer_ptr) const;
        void unpack(const void* object_ptr);

    private:
        void convert_to_map();

        bool is_flat_map{true};
        std::vector<std::pair<FlatKey, ParameterValue>> vector_storage; // CPU L1-cache line
		
		struct MapImpl;
        std::unique_ptr<MapImpl> map_storage; // Hidden tsl::robin_map
    };
}  // namespace msgframe