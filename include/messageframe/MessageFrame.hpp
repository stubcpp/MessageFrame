// MESSAGE FRAME MAIN CLASS
#pragma once
#include "messageframe/Structures.hpp"
#include "messageframe/Header.hpp"   
#include "messageframe/Value.hpp"  
#include "messageframe/HybridMessageMap.hpp"
#include <vector>
#include <string>
#include <string_view>
#include <memory>
#include <utility>


namespace msgframe {
	
class MessageFrame {
    public:
        MessageFrame() = default;

        // Ctor for fast initialization
        template <typename IdT, typename TypeT>
        MessageFrame(IdT msg_id, TypeT msg_type, uint32_t src_id, uint32_t tgt_id,
            uint64_t msg_cnt = 0, uint16_t proto_version = 1, uint16_t msg_flags = 0) noexcept
            : m_header(msg_id, msg_type, src_id, tgt_id, msg_cnt, proto_version, msg_flags) {
        }

        // Direct access to the header
        MessageHeader& header() noexcept { return m_header; }
        const MessageHeader& header() const noexcept { return m_header; }

        // ----------------------------------------------------------------
        // Proxy methods for HybridMessageMap (Redirect to parameters)
        //
        // Кожен метод дублюється в lvalue (const ParameterValue&) і rvalue
        // (ParameterValue&&) версіях — так само, як у HybridMessageMap.
        // Це навмисно: один by-value overload виглядав би простіше, але
        // примусив би компілятор робити КОПІЮ ParameterValue на вході для
        // lvalue-аргументів (param передається by value -> завжди
        // copy/move-конструюється), що ламає Zero-Allocation гарантію
        // бібліотеки саме на рівні публічного фасаду MessageFrame.
        // ----------------------------------------------------------------

        // CRITICAL. Повторний add() з тим самим ключем створить дублікат запису в vector-режимі
        // (поведінка undefined для подальшого find — знайде перший) і призведе до некоректної
        // поведінки. Використовуйте set() для upsert-семантики.
        /**
        * @brief Додає новий параметр без перевірки на наявність дублікатів (Максимум швидкості).
        * @note Складність: O(1) у векторному режимі (proxy до HybridMessageMap::add).
        * @warning Якщо ключ уже існує, у збірці Release утвориться дублікат (find() поверне перший).
        *          У збірці Debug спрацює assert. Використовуйте set() для логіки "вставити або оновити".
        * @param device Назва пристрою / домену (напр., "sensor_alpha")
        * @param param Назва метрики (напр., "voltage")
        * @param val Значення параметра
        */
        void add(std::string_view device, std::string_view param, const ParameterValue& val) {
            parameters.add(device, param, val);
        }
        void add(std::string_view device, std::string_view param, ParameterValue&& val) {
            parameters.add(device, param, std::move(val));
        }

        /**
        * @brief Додає новий параметр якщо ключ вже об'єднаний ("device.parameter") без перевірки
        *        на наявність дублікатів (Максимум швидкості).
        * @note Складність: O(1) у векторному режимі (proxy до HybridMessageMap::add_flat).
        * @warning Якщо ключ уже існує, у збірці Release утвориться дублікат (find_flat() поверне перший).
        *          У збірці Debug спрацює assert. Використовуйте set_flat() для логіки "вставити або оновити".
        * @param flat_key Об'єднаний ключ (напр., "device.parameter")
        * @param val Значення параметра
        */
        void add_flat(std::string_view flat_key, const ParameterValue& val) {
            parameters.add_flat(flat_key, val);
        }
        void add_flat(std::string_view flat_key, ParameterValue&& val) {
            parameters.add_flat(flat_key, std::move(val));
        }

        /**
        * @brief Семантика Upsert: Оновлює значення, якщо ключ уже існує, або додає новий.
        * @note Складність: у векторному режимі O(N) (лінійний пошук дубліката), у режимі мапи O(1)
        *       (proxy до HybridMessageMap::set).
        * @param device Назва пристрою / домену
        * @param param Назва метрики
        * @param val Нове значення для запису/оновлення
        */
        void set(std::string_view device, std::string_view param, const ParameterValue& val) {
            parameters.set(device, param, val);
        }
        void set(std::string_view device, std::string_view param, ParameterValue&& val) {
            parameters.set(device, param, std::move(val));
        }

        /**
        * @brief Семантика Upsert: Оновлює значення, якщо ключ уже існує, або додає новий
        *        (об'єднаний ключ "device.param").
        * @note Складність: у векторному режимі O(N) (лінійний пошук дубліката), у режимі мапи O(1)
        *       (proxy до HybridMessageMap::set_flat).
        * @param flat_key Об'єднаний ключ (напр., "device.parameter")
        * @param val Нове значення для запису/оновлення
        */
        void set_flat(std::string_view flat_key, const ParameterValue& val) {
            parameters.set_flat(flat_key, val);
        }
        void set_flat(std::string_view flat_key, ParameterValue&& val) {
            parameters.set_flat(flat_key, std::move(val));
        }

        /**
        * @brief Строге оновлення: Модифікує значення ТІЛЬКИ якщо ключ уже існує в контейнері.
        * @note Метод ніколи не збільшує кількість параметрів та не створює нових записів
        *       (proxy до HybridMessageMap::update).
        * @param device Назва пристрою / домену
        * @param param Назва метрики
        * @param val Нове значення для існуючого параметра
        * @return true — значення успішно оновлено; false — такий ключ не знайдено (структура не змінилась).
        */
        bool update(std::string_view device, std::string_view param, const ParameterValue& val) {
            return parameters.update(device, param, val);
        }
        bool update(std::string_view device, std::string_view param, ParameterValue&& val) {
            return parameters.update(device, param, std::move(val));
        }

        /**
        * @brief Строге оновлення: Модифікує значення ТІЛЬКИ якщо ключ уже існує в контейнері
        *        (об'єднаний ключ "device.param").
        * @note Метод ніколи не збільшує кількість параметрів та не створює нових записів
        *       (proxy до HybridMessageMap::update_flat).
        * @param flat_key Об'єднаний ключ (напр., "device.parameter")
        * @param val Нове значення для існуючого параметра
        * @return true — значення успішно оновлено; false — такий ключ не знайдено (структура не змінилась).
        */
        bool update_flat(std::string_view flat_key, const ParameterValue& val) {
            return parameters.update_flat(flat_key, val);
        }
        bool update_flat(std::string_view flat_key, ParameterValue&& val) {
            return parameters.update_flat(flat_key, std::move(val));
        }

        /**
        * @brief Пошук параметра без створення тимчасового std::string (Zero-Allocation find).
        * @note Складність: O(N) у векторному режимі (лінійне порівняння), O(1) у режимі мапи.
        * @param device Назва пристрою / домену
        * @param param Назва метрики
        * @return Вказівник на знайдене значення, або nullptr якщо ключ не знайдено.
        */
        const ParameterValue* find(std::string_view device, std::string_view param) const noexcept {
            return parameters.find(device, param);
        }

        /**
        * @brief Пошук параметра за об'єднаним ключем ("device.param") без створення тимчасового
        *        std::string (Zero-Allocation find).
        * @note Складність: O(N) у векторному режимі (лінійне порівняння), O(1) у режимі мапи.
        * @param flat_key Об'єднаний ключ (напр., "device.parameter")
        * @return Вказівник на знайдене значення, або nullptr якщо ключ не знайдено.
        */
        const ParameterValue* find_flat(std::string_view flat_key) const noexcept {
            return parameters.find_flat(flat_key);
        }

        // Working with large binary attachments
        void add_attachment(std::string name, std::vector<uint8_t> data);
        const std::vector<Attachment>& get_attachments() const noexcept { return attachments; }

        // Повертає поточну кількість параметрів у повідомленні
        size_t parameters_size() const noexcept { return parameters.size(); }

        // Доступ до ітератора для обходу
        void iterate_parameters(HybridMessageMap::ConstCallback callback, void* user_data) const {
            parameters.iterate(callback, user_data);
        }


        void clear() noexcept;

        // Network serialization
        void serialize(std::vector<uint8_t>& output_buffer) const;
        bool deserialize(const uint8_t* data, size_t size);

    private:
        MessageHeader m_header; 
        HybridMessageMap parameters;
        std::vector<Attachment> attachments;
    };

} // namespace msgframe