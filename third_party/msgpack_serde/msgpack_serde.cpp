#include "msgpack_serde.h"

#include <cstring>

namespace msgpack_serde
{
namespace
{
const int kMaxDepth = 64;

bool isNegativeFixInt(uint8_t tag)
{
    return tag >= 0xe0;
}

bool isFixStr(uint8_t tag)
{
    return (tag & 0xe0) == 0xa0;
}

bool isFixArray(uint8_t tag)
{
    return (tag & 0xf0) == 0x90;
}

bool isFixMap(uint8_t tag)
{
    return (tag & 0xf0) == 0x80;
}

double doubleFromBits(uint64_t bits)
{
    double value = 0.0;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

float floatFromBits(uint32_t bits)
{
    float value = 0.0f;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}
}

const std::vector<uint8_t>& Writer::bytes() const
{
    return bytes_;
}

void Writer::reserve(size_t size)
{
    bytes_.reserve(size);
}

void Writer::writeNil()
{
    writeU8(0xc0);
}

void Writer::writeBool(bool value)
{
    writeU8(value ? 0xc3 : 0xc2);
}

void Writer::writeInt(int64_t value)
{
    if (value >= 0)
    {
        writeUInt(static_cast<uint64_t>(value));
    }
    else if (value >= -32)
    {
        writeU8(static_cast<uint8_t>(value));
    }
    else if (value >= INT8_MIN)
    {
        writeU8(0xd0);
        writeU8(static_cast<uint8_t>(value));
    }
    else if (value >= INT16_MIN)
    {
        writeU8(0xd1);
        writeU16(static_cast<uint16_t>(value));
    }
    else if (value >= INT32_MIN)
    {
        writeU8(0xd2);
        writeU32(static_cast<uint32_t>(value));
    }
    else
    {
        writeU8(0xd3);
        writeU64(static_cast<uint64_t>(value));
    }
}

void Writer::writeUInt(uint64_t value)
{
    if (value <= 0x7f)
    {
        writeU8(static_cast<uint8_t>(value));
    }
    else if (value <= UINT8_MAX)
    {
        writeU8(0xcc);
        writeU8(static_cast<uint8_t>(value));
    }
    else if (value <= UINT16_MAX)
    {
        writeU8(0xcd);
        writeU16(static_cast<uint16_t>(value));
    }
    else if (value <= UINT32_MAX)
    {
        writeU8(0xce);
        writeU32(static_cast<uint32_t>(value));
    }
    else
    {
        writeU8(0xcf);
        writeU64(value);
    }
}

void Writer::writeDouble(double value)
{
    uint64_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    writeU8(0xcb);
    writeU64(bits);
}

void Writer::writeString(const std::string& value)
{
    const uint32_t size = static_cast<uint32_t>(value.size());
    if (size <= 31)
    {
        writeU8(static_cast<uint8_t>(0xa0 | size));
    }
    else if (size <= UINT8_MAX)
    {
        writeU8(0xd9);
        writeU8(static_cast<uint8_t>(size));
    }
    else if (size <= UINT16_MAX)
    {
        writeU8(0xda);
        writeU16(static_cast<uint16_t>(size));
    }
    else
    {
        writeU8(0xdb);
        writeU32(size);
    }

    appendBytes(value.data(), value.size());
}

void Writer::writeBinary(const uint8_t* data, size_t size)
{
    if (size <= UINT8_MAX)
    {
        writeU8(0xc4);
        writeU8(static_cast<uint8_t>(size));
    }
    else if (size <= UINT16_MAX)
    {
        writeU8(0xc5);
        writeU16(static_cast<uint16_t>(size));
    }
    else
    {
        writeU8(0xc6);
        writeU32(static_cast<uint32_t>(size));
    }

    appendBytes(data, size);
}

void Writer::writeExtension(int8_t type, const uint8_t* data, size_t size)
{
    switch (size)
    {
    case 1:
        writeU8(0xd4);
        break;
    case 2:
        writeU8(0xd5);
        break;
    case 4:
        writeU8(0xd6);
        break;
    case 8:
        writeU8(0xd7);
        break;
    case 16:
        writeU8(0xd8);
        break;
    default:
        if (size <= UINT8_MAX)
        {
            writeU8(0xc7);
            writeU8(static_cast<uint8_t>(size));
        }
        else if (size <= UINT16_MAX)
        {
            writeU8(0xc8);
            writeU16(static_cast<uint16_t>(size));
        }
        else
        {
            writeU8(0xc9);
            writeU32(static_cast<uint32_t>(size));
        }
        break;
    }

    writeU8(static_cast<uint8_t>(type));
    appendBytes(data, size);
}

void Writer::writeArrayHeader(uint32_t size)
{
    if (size <= 15)
    {
        writeU8(static_cast<uint8_t>(0x90 | size));
    }
    else if (size <= UINT16_MAX)
    {
        writeU8(0xdc);
        writeU16(static_cast<uint16_t>(size));
    }
    else
    {
        writeU8(0xdd);
        writeU32(size);
    }
}

void Writer::writeMapHeader(uint32_t size)
{
    if (size <= 15)
    {
        writeU8(static_cast<uint8_t>(0x80 | size));
    }
    else if (size <= UINT16_MAX)
    {
        writeU8(0xde);
        writeU16(static_cast<uint16_t>(size));
    }
    else
    {
        writeU8(0xdf);
        writeU32(size);
    }
}

void Writer::appendBytes(const void* data, size_t size)
{
    if (size == 0)
    {
        return;
    }

    const size_t offset = bytes_.size();
    bytes_.resize(offset + size);
    std::memcpy(&bytes_[offset], data, size);
}

void Writer::writeU8(uint8_t value)
{
    bytes_.push_back(value);
}

void Writer::writeU16(uint16_t value)
{
    const size_t offset = bytes_.size();
    bytes_.resize(offset + 2);
    uint8_t* out = &bytes_[offset];
    out[0] = static_cast<uint8_t>(value >> 8);
    out[1] = static_cast<uint8_t>(value);
}

void Writer::writeU32(uint32_t value)
{
    const size_t offset = bytes_.size();
    bytes_.resize(offset + 4);
    uint8_t* out = &bytes_[offset];
    out[0] = static_cast<uint8_t>(value >> 24);
    out[1] = static_cast<uint8_t>(value >> 16);
    out[2] = static_cast<uint8_t>(value >> 8);
    out[3] = static_cast<uint8_t>(value);
}

void Writer::writeU64(uint64_t value)
{
    const size_t offset = bytes_.size();
    bytes_.resize(offset + 8);
    uint8_t* out = &bytes_[offset];
    out[0] = static_cast<uint8_t>(value >> 56);
    out[1] = static_cast<uint8_t>(value >> 48);
    out[2] = static_cast<uint8_t>(value >> 40);
    out[3] = static_cast<uint8_t>(value >> 32);
    out[4] = static_cast<uint8_t>(value >> 24);
    out[5] = static_cast<uint8_t>(value >> 16);
    out[6] = static_cast<uint8_t>(value >> 8);
    out[7] = static_cast<uint8_t>(value);
}

Reader::Reader(const uint8_t* data, size_t size)
    : data_(data)
    , size_(size)
    , cursor_(data)
    , end_(size == 0 ? data : data + size)
{
}

bool Reader::read(Value& value, std::string& error)
{
    if (!readValue(value, error, 0))
    {
        return false;
    }

    if (cursor_ != end_)
    {
        error = "trailing bytes after MessagePack value";
        return false;
    }

    return true;
}

bool Reader::readValue(Value& value, std::string& error, int depth)
{
    if (depth > kMaxDepth)
    {
        error = "MessagePack nesting is too deep";
        return false;
    }

    uint8_t tag = 0;
    if (!readU8(tag))
    {
        error = "unexpected end of MessagePack input";
        return false;
    }

    if (tag <= 0x7f)
    {
        value.type = Value::Type::UInt;
        value.uintValue = tag;
        return true;
    }

    if (isNegativeFixInt(tag))
    {
        value.type = Value::Type::Int;
        value.intValue = static_cast<int8_t>(tag);
        return true;
    }

    if (isFixStr(tag))
    {
        const uint32_t length = tag & 0x1f;
        const uint8_t* data = nullptr;
        if (!readBytes(length, data))
        {
            error = "truncated MessagePack string";
            return false;
        }
        value.type = Value::Type::String;
        value.stringValue.assign(reinterpret_cast<const char*>(data), reinterpret_cast<const char*>(data + length));
        return true;
    }

    if (isFixArray(tag))
    {
        const uint32_t length = tag & 0x0f;
        value.type = Value::Type::Array;
        value.arrayValue.resize(length);
        for (uint32_t i = 0; i < length; ++i)
        {
            if (!readValue(value.arrayValue[i], error, depth + 1))
            {
                return false;
            }
        }
        return true;
    }

    if (isFixMap(tag))
    {
        const uint32_t length = tag & 0x0f;
        value.type = Value::Type::Map;
        value.mapValue.reserve(length);
        for (uint32_t i = 0; i < length; ++i)
        {
            Value key;
            Value entryValue;
            if (!readValue(key, error, depth + 1) || !readValue(entryValue, error, depth + 1))
            {
                return false;
            }
            value.mapValue.emplace_back(std::move(key), std::move(entryValue));
        }
        return true;
    }

    switch (tag)
    {
    case 0xc0:
        value.type = Value::Type::Nil;
        return true;
    case 0xc2:
    case 0xc3:
        value.type = Value::Type::Bool;
        value.boolValue = tag == 0xc3;
        return true;
    case 0xc7:
    case 0xc8:
    case 0xc9:
    {
        uint32_t length = 0;
        if (tag == 0xc7)
        {
            uint8_t small = 0;
            if (!readU8(small))
            {
                error = "truncated MessagePack extension length";
                return false;
            }
            length = small;
        }
        else if (tag == 0xc8)
        {
            uint16_t medium = 0;
            if (!readU16(medium))
            {
                error = "truncated MessagePack extension length";
                return false;
            }
            length = medium;
        }
        else if (!readU32(length))
        {
            error = "truncated MessagePack extension length";
            return false;
        }

        uint8_t type = 0;
        const uint8_t* data = nullptr;
        if (!readU8(type) || !readBytes(length, data))
        {
            error = "truncated MessagePack extension";
            return false;
        }
        value.type = Value::Type::Extension;
        value.extensionType = static_cast<int8_t>(type);
        value.extensionData.assign(data, data + length);
        return true;
    }
    case 0xc4:
    case 0xc5:
    case 0xc6:
    {
        uint32_t length = 0;
        if (tag == 0xc4)
        {
            uint8_t small = 0;
            if (!readU8(small))
            {
                error = "truncated MessagePack binary length";
                return false;
            }
            length = small;
        }
        else if (tag == 0xc5)
        {
            uint16_t medium = 0;
            if (!readU16(medium))
            {
                error = "truncated MessagePack binary length";
                return false;
            }
            length = medium;
        }
        else if (!readU32(length))
        {
            error = "truncated MessagePack binary length";
            return false;
        }
        const uint8_t* data = nullptr;
        if (!readBytes(length, data))
        {
            error = "truncated MessagePack binary";
            return false;
        }
        value.type = Value::Type::Binary;
        value.binaryValue.assign(data, data + length);
        return true;
    }
    case 0xca:
    {
        uint32_t bits = 0;
        if (!readU32(bits))
        {
            error = "truncated MessagePack float";
            return false;
        }
        value.type = Value::Type::Double;
        value.doubleValue = floatFromBits(bits);
        return true;
    }
    case 0xcb:
    {
        uint64_t bits = 0;
        if (!readU64(bits))
        {
            error = "truncated MessagePack double";
            return false;
        }
        value.type = Value::Type::Double;
        value.doubleValue = doubleFromBits(bits);
        return true;
    }
    case 0xcc:
    {
        uint8_t number = 0;
        if (!readU8(number))
        {
            error = "truncated MessagePack uint8";
            return false;
        }
        value.type = Value::Type::UInt;
        value.uintValue = number;
        return true;
    }
    case 0xcd:
    {
        uint16_t number = 0;
        if (!readU16(number))
        {
            error = "truncated MessagePack uint16";
            return false;
        }
        value.type = Value::Type::UInt;
        value.uintValue = number;
        return true;
    }
    case 0xce:
    {
        uint32_t number = 0;
        if (!readU32(number))
        {
            error = "truncated MessagePack uint32";
            return false;
        }
        value.type = Value::Type::UInt;
        value.uintValue = number;
        return true;
    }
    case 0xcf:
    {
        uint64_t number = 0;
        if (!readU64(number))
        {
            error = "truncated MessagePack uint64";
            return false;
        }
        value.type = Value::Type::UInt;
        value.uintValue = number;
        return true;
    }
    case 0xd0:
    {
        uint8_t number = 0;
        if (!readU8(number))
        {
            error = "truncated MessagePack int8";
            return false;
        }
        value.type = Value::Type::Int;
        value.intValue = static_cast<int8_t>(number);
        return true;
    }
    case 0xd1:
    {
        uint16_t number = 0;
        if (!readU16(number))
        {
            error = "truncated MessagePack int16";
            return false;
        }
        value.type = Value::Type::Int;
        value.intValue = static_cast<int16_t>(number);
        return true;
    }
    case 0xd2:
    {
        uint32_t number = 0;
        if (!readU32(number))
        {
            error = "truncated MessagePack int32";
            return false;
        }
        value.type = Value::Type::Int;
        value.intValue = static_cast<int32_t>(number);
        return true;
    }
    case 0xd3:
    {
        uint64_t number = 0;
        if (!readU64(number))
        {
            error = "truncated MessagePack int64";
            return false;
        }
        value.type = Value::Type::Int;
        value.intValue = static_cast<int64_t>(number);
        return true;
    }
    case 0xd4:
    case 0xd5:
    case 0xd6:
    case 0xd7:
    case 0xd8:
    {
        uint32_t length = 0;
        if (tag == 0xd4)
        {
            length = 1;
        }
        else if (tag == 0xd5)
        {
            length = 2;
        }
        else if (tag == 0xd6)
        {
            length = 4;
        }
        else if (tag == 0xd7)
        {
            length = 8;
        }
        else
        {
            length = 16;
        }

        uint8_t type = 0;
        const uint8_t* data = nullptr;
        if (!readU8(type) || !readBytes(length, data))
        {
            error = "truncated MessagePack fixed extension";
            return false;
        }
        value.type = Value::Type::Extension;
        value.extensionType = static_cast<int8_t>(type);
        value.extensionData.assign(data, data + length);
        return true;
    }
    case 0xd9:
    case 0xda:
    case 0xdb:
    {
        uint32_t length = 0;
        if (tag == 0xd9)
        {
            uint8_t small = 0;
            if (!readU8(small))
            {
                error = "truncated MessagePack string length";
                return false;
            }
            length = small;
        }
        else if (tag == 0xda)
        {
            uint16_t medium = 0;
            if (!readU16(medium))
            {
                error = "truncated MessagePack string length";
                return false;
            }
            length = medium;
        }
        else if (!readU32(length))
        {
            error = "truncated MessagePack string length";
            return false;
        }

        const uint8_t* data = nullptr;
        if (!readBytes(length, data))
        {
            error = "truncated MessagePack string";
            return false;
        }
        value.type = Value::Type::String;
        value.stringValue.assign(reinterpret_cast<const char*>(data), reinterpret_cast<const char*>(data + length));
        return true;
    }
    case 0xdc:
    case 0xdd:
    {
        uint32_t length = 0;
        if (tag == 0xdc)
        {
            uint16_t medium = 0;
            if (!readU16(medium))
            {
                error = "truncated MessagePack array length";
                return false;
            }
            length = medium;
        }
        else if (!readU32(length))
        {
            error = "truncated MessagePack array length";
            return false;
        }
        value.type = Value::Type::Array;
        value.arrayValue.resize(length);
        for (uint32_t i = 0; i < length; ++i)
        {
            if (!readValue(value.arrayValue[i], error, depth + 1))
            {
                return false;
            }
        }
        return true;
    }
    case 0xde:
    case 0xdf:
    {
        uint32_t length = 0;
        if (tag == 0xde)
        {
            uint16_t medium = 0;
            if (!readU16(medium))
            {
                error = "truncated MessagePack map length";
                return false;
            }
            length = medium;
        }
        else if (!readU32(length))
        {
            error = "truncated MessagePack map length";
            return false;
        }
        value.type = Value::Type::Map;
        value.mapValue.reserve(length);
        for (uint32_t i = 0; i < length; ++i)
        {
            Value key;
            Value entryValue;
            if (!readValue(key, error, depth + 1) || !readValue(entryValue, error, depth + 1))
            {
                return false;
            }
            value.mapValue.emplace_back(std::move(key), std::move(entryValue));
        }
        return true;
    }
    default:
        error = "unsupported MessagePack type";
        return false;
    }
}

bool Reader::readBytes(size_t size, const uint8_t*& data)
{
    if (size > static_cast<size_t>(end_ - cursor_))
    {
        return false;
    }
    data = cursor_;
    cursor_ += size;
    return true;
}

bool Reader::readU8(uint8_t& value)
{
    if (cursor_ == end_)
    {
        return false;
    }
    value = *cursor_;
    ++cursor_;
    return true;
}

bool Reader::readU16(uint16_t& value)
{
    if (static_cast<size_t>(end_ - cursor_) < 2)
    {
        return false;
    }
    value = static_cast<uint16_t>((cursor_[0] << 8) | cursor_[1]);
    cursor_ += 2;
    return true;
}

bool Reader::readU32(uint32_t& value)
{
    if (static_cast<size_t>(end_ - cursor_) < 4)
    {
        return false;
    }
    value = (static_cast<uint32_t>(cursor_[0]) << 24) |
        (static_cast<uint32_t>(cursor_[1]) << 16) |
        (static_cast<uint32_t>(cursor_[2]) << 8) |
        static_cast<uint32_t>(cursor_[3]);
    cursor_ += 4;
    return true;
}

bool Reader::readU64(uint64_t& value)
{
    if (static_cast<size_t>(end_ - cursor_) < 8)
    {
        return false;
    }
    value = (static_cast<uint64_t>(cursor_[0]) << 56) |
        (static_cast<uint64_t>(cursor_[1]) << 48) |
        (static_cast<uint64_t>(cursor_[2]) << 40) |
        (static_cast<uint64_t>(cursor_[3]) << 32) |
        (static_cast<uint64_t>(cursor_[4]) << 24) |
        (static_cast<uint64_t>(cursor_[5]) << 16) |
        (static_cast<uint64_t>(cursor_[6]) << 8) |
        static_cast<uint64_t>(cursor_[7]);
    cursor_ += 8;
    return true;
}
}
