#include "messageframe/Value.hpp"
#include <msgpack.hpp> 
#include <stdexcept>
#include <string>
#include <new>


namespace msgframe {

    class VectorBuffer {
    public:
        VectorBuffer(std::vector<uint8_t>& buffer) : m_buf(buffer) {}
        void write(const char* buf, size_t len) {
            size_t old_size = m_buf.size();
            m_buf.resize(old_size + len);
            std::memcpy(m_buf.data() + old_size, buf, len);
        }
    private:
        std::vector<uint8_t>& m_buf;
    };


    ParameterValue::ParameterValue() noexcept : type(Type::Unknown) {
        value.intValue = 0;
    }

    ParameterValue::ParameterValue(int64_t v) noexcept : type(Type::Int64) {
        value.intValue = v;
    }

    ParameterValue::ParameterValue(double v) noexcept : type(Type::Double) {
        value.doubleValue = v;
    }

    ParameterValue::ParameterValue(const std::string& v) : type(Type::String) {
        ::new (value.stringBuffer) std::string(v); 
    }

    ParameterValue::ParameterValue(std::string&& v) noexcept : type(Type::String) {
        ::new (value.stringBuffer) std::string(std::move(v));
    }

    ParameterValue::ParameterValue(const char* v) : type(Type::String) {
        ::new (value.stringBuffer) std::string(v);
    }

    ParameterValue::ParameterValue(bool v) noexcept : type(Type::Bool) {
        value.boolValue = v;
    }

    ParameterValue::~ParameterValue() {
        reset();
    }

    void ParameterValue::reset() noexcept {
        if (type == Type::String) {
            using string_type = std::string;
            as_string().~string_type();
        }
        type = Type::Unknown;
		value.intValue = 0;
    }

    // Copy Ctor
    ParameterValue::ParameterValue(const ParameterValue& other) : type(other.type) {
        if (type == Type::String) {
            ::new (value.stringBuffer) std::string(other.as_string());
        } else {
            value = other.value;
        }
    }

	// Move Ctor
    ParameterValue::ParameterValue(ParameterValue&& other) noexcept : type(other.type) {
        if (type == Type::String) {
            ::new (value.stringBuffer) std::string(std::move(other.as_string()));
        } else {
            value = other.value;
        }
        other.reset();
    }

    ParameterValue& ParameterValue::operator=(const ParameterValue& other) {
        if (this != &other) {
            reset();
            type = other.type;
            if (type == Type::String) {
                ::new (value.stringBuffer) std::string(other.as_string());
            } else {
                value = other.value;
            }
        }
        return *this;
    }

    ParameterValue& ParameterValue::operator=(ParameterValue&& other) noexcept {
        if (this != &other) {
            reset();
            type = other.type;
            if (type == Type::String) {
                ::new (value.stringBuffer) std::string(std::move(other.as_string()));
            } else {
                value = other.value;
            }
            other.reset();
        }
        return *this;
    }

    // Setters
    void ParameterValue::setValue(int64_t v) noexcept { reset(); type = Type::Int64; value.intValue = v; }
    void ParameterValue::setValue(double v) noexcept { reset(); type = Type::Double; value.doubleValue = v; }
    void ParameterValue::setValue(bool v) noexcept { reset(); type = Type::Bool; value.boolValue = v; }
    
    void ParameterValue::setValue(const std::string& v) {
        if (type == Type::String) {
            as_string() = v; // Перевикористовуємо купу (Heap reuse!)
        } else {
            reset();
            type = Type::String;
            ::new (value.stringBuffer) std::string(v);
        }
    }
	
    void ParameterValue::setValue(std::string&& v) noexcept {
        if (type == Type::String) {
            as_string() = std::move(v); // Перевикористовуємо купу через move
        } else {
            reset();
            type = Type::String;
            ::new (value.stringBuffer) std::string(std::move(v));
        }
    }

    // Getters
    std::optional<int64_t> ParameterValue::tryGetInt() const noexcept {
        return type == Type::Int64 ? std::make_optional(value.intValue) : std::nullopt;
    }
    std::optional<double> ParameterValue::tryGetDouble() const noexcept {
        return type == Type::Double ? std::make_optional(value.doubleValue) : std::nullopt;
    }
    std::optional<std::string> ParameterValue::tryGetString() const {
        return type == Type::String ? std::make_optional(as_string()) : std::nullopt;
    }
    std::optional<bool> ParameterValue::tryGetBool() const noexcept {
        return type == Type::Bool ? std::make_optional(value.boolValue) : std::nullopt;
    }

    std::string ParameterValue::toString() const {
        switch (type) {
            case Type::Int64:  return std::to_string(value.intValue);
            case Type::Double: return std::to_string(value.doubleValue);
            case Type::String: return as_string();
            case Type::Bool:   return value.boolValue ? "true" : "false";
            default:           return "Unknown";
        }
    }

    void ParameterValue::pack(void* packer_ptr) const {
        if (!packer_ptr) return;

        auto* pk = static_cast<msgpack::packer<VectorBuffer>*>(packer_ptr);

        pk->pack_array(2);
        pk->pack(static_cast<uint8_t>(type));

        switch (type) {
            case Type::Int64:  pk->pack(value.intValue); break;
            case Type::Double: pk->pack(value.doubleValue); break;
            case Type::Bool:   pk->pack(value.boolValue); break;
            case Type::String: pk->pack(as_string()); break;
            default:           pk->pack_nil(); break;
        }
    }

    void ParameterValue::unpack(const void* object_ptr) {
        if (!object_ptr) return;

        reset();

        const auto& obj = *static_cast<const msgpack::object*>(object_ptr);

        if (obj.type != msgpack::type::ARRAY || obj.via.array.size < 2) {
            type = Type::Unknown;
            return;
        }

        // 1. Deserialising a data type
        uint8_t raw_type = obj.via.array.ptr[0].as<uint8_t>();
        type = static_cast<Type>(raw_type);

        // 2. Deserialising a value
        const auto& val_obj = obj.via.array.ptr[1];

        switch (type) {
            case Type::Int64:
                value.intValue = val_obj.as<int64_t>();
                break;
            case Type::Double:
                value.doubleValue = val_obj.as<double>();
                break;
            case Type::Bool:
                value.boolValue = val_obj.as<bool>();
                break;
            case Type::String:
                ::new (&value.stringBuffer) std::string(val_obj.as<std::string>());
                break;
            default:
                type = Type::Unknown;
                value.intValue = 0;
                break;
        }
    }
}
