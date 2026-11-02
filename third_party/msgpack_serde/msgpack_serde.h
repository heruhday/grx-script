#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace msgpack_serde
{
struct Value
{
    enum class Type
    {
        Nil,
        Bool,
        Int,
        UInt,
        Double,
        String,
        Binary,
        Array,
        Map,
        Extension
    };

    Type type = Type::Nil;
    bool boolValue = false;
    int64_t intValue = 0;
    uint64_t uintValue = 0;
    double doubleValue = 0.0;
    std::string stringValue;
    std::vector<uint8_t> binaryValue;
    std::vector<Value> arrayValue;
    std::vector<std::pair<Value, Value>> mapValue;
    int8_t extensionType = 0;
    std::vector<uint8_t> extensionData;
};

class Writer
{
public:
    const std::vector<uint8_t>& bytes() const;
    void reserve(size_t size);

    void writeNil();
    void writeBool(bool value);
    void writeInt(int64_t value);
    void writeUInt(uint64_t value);
    void writeDouble(double value);
    void writeString(const std::string& value);
    void writeBinary(const uint8_t* data, size_t size);
    void writeExtension(int8_t type, const uint8_t* data, size_t size);
    void writeArrayHeader(uint32_t size);
    void writeMapHeader(uint32_t size);

private:
    void appendBytes(const void* data, size_t size);
    void writeU8(uint8_t value);
    void writeU16(uint16_t value);
    void writeU32(uint32_t value);
    void writeU64(uint64_t value);

    std::vector<uint8_t> bytes_;
};

class Reader
{
public:
    Reader(const uint8_t* data, size_t size);

    bool read(Value& value, std::string& error);

private:
    bool readValue(Value& value, std::string& error, int depth);
    bool readBytes(size_t size, const uint8_t*& data);
    bool readU8(uint8_t& value);
    bool readU16(uint16_t& value);
    bool readU32(uint32_t& value);
    bool readU64(uint64_t& value);

    const uint8_t* data_;
    size_t size_;
    const uint8_t* cursor_;
    const uint8_t* end_;
};
}
