#ifndef QUICKJS_MSVC_STDATOMIC_H
#define QUICKJS_MSVC_STDATOMIC_H

#if !defined(_MSC_VER)
#error "This compatibility header is only intended for MSVC builds without stdatomic.h."
#endif

#include <stdbool.h>
#include <stdint.h>

#define _Atomic

static __inline uint64_t qjs_atomic_load_impl(volatile void* obj, size_t size)
{
    switch (size)
    {
    case 1:
        return *(volatile uint8_t*)obj;
    case 2:
        return *(volatile uint16_t*)obj;
    case 4:
        return *(volatile uint32_t*)obj;
    default:
        return *(volatile uint64_t*)obj;
    }
}

static __inline void qjs_atomic_store_impl(volatile void* obj, uint64_t desired, size_t size)
{
    switch (size)
    {
    case 1:
        *(volatile uint8_t*)obj = (uint8_t)desired;
        break;
    case 2:
        *(volatile uint16_t*)obj = (uint16_t)desired;
        break;
    case 4:
        *(volatile uint32_t*)obj = (uint32_t)desired;
        break;
    default:
        *(volatile uint64_t*)obj = desired;
        break;
    }
}

static __inline uint64_t qjs_atomic_exchange_impl(volatile void* obj, uint64_t desired, size_t size)
{
    const uint64_t previous = qjs_atomic_load_impl(obj, size);
    qjs_atomic_store_impl(obj, desired, size);
    return previous;
}

static __inline uint64_t qjs_atomic_fetch_add_impl(volatile void* obj, uint64_t arg, size_t size)
{
    const uint64_t previous = qjs_atomic_load_impl(obj, size);
    qjs_atomic_store_impl(obj, previous + arg, size);
    return previous;
}

static __inline uint64_t qjs_atomic_fetch_sub_impl(volatile void* obj, uint64_t arg, size_t size)
{
    const uint64_t previous = qjs_atomic_load_impl(obj, size);
    qjs_atomic_store_impl(obj, previous - arg, size);
    return previous;
}

static __inline uint64_t qjs_atomic_fetch_and_impl(volatile void* obj, uint64_t arg, size_t size)
{
    const uint64_t previous = qjs_atomic_load_impl(obj, size);
    qjs_atomic_store_impl(obj, previous & arg, size);
    return previous;
}

static __inline uint64_t qjs_atomic_fetch_or_impl(volatile void* obj, uint64_t arg, size_t size)
{
    const uint64_t previous = qjs_atomic_load_impl(obj, size);
    qjs_atomic_store_impl(obj, previous | arg, size);
    return previous;
}

static __inline uint64_t qjs_atomic_fetch_xor_impl(volatile void* obj, uint64_t arg, size_t size)
{
    const uint64_t previous = qjs_atomic_load_impl(obj, size);
    qjs_atomic_store_impl(obj, previous ^ arg, size);
    return previous;
}

static __inline bool qjs_atomic_compare_exchange_strong_impl(
    volatile void* obj,
    void* expected,
    uint64_t desired,
    size_t size)
{
    const uint64_t current = qjs_atomic_load_impl(obj, size);
    const uint64_t expected_value = qjs_atomic_load_impl(expected, size);
    if (current == expected_value)
    {
        qjs_atomic_store_impl(obj, desired, size);
        return true;
    }

    qjs_atomic_store_impl(expected, current, size);
    return false;
}

#define atomic_load(obj) qjs_atomic_load_impl((volatile void*)(obj), sizeof(*(obj)))
#define atomic_store(obj, desired) qjs_atomic_store_impl((volatile void*)(obj), (uint64_t)(desired), sizeof(*(obj)))
#define atomic_exchange(obj, desired) qjs_atomic_exchange_impl((volatile void*)(obj), (uint64_t)(desired), sizeof(*(obj)))
#define atomic_fetch_add(obj, arg) qjs_atomic_fetch_add_impl((volatile void*)(obj), (uint64_t)(arg), sizeof(*(obj)))
#define atomic_fetch_sub(obj, arg) qjs_atomic_fetch_sub_impl((volatile void*)(obj), (uint64_t)(arg), sizeof(*(obj)))
#define atomic_fetch_and(obj, arg) qjs_atomic_fetch_and_impl((volatile void*)(obj), (uint64_t)(arg), sizeof(*(obj)))
#define atomic_fetch_or(obj, arg) qjs_atomic_fetch_or_impl((volatile void*)(obj), (uint64_t)(arg), sizeof(*(obj)))
#define atomic_fetch_xor(obj, arg) qjs_atomic_fetch_xor_impl((volatile void*)(obj), (uint64_t)(arg), sizeof(*(obj)))
#define atomic_compare_exchange_strong(obj, expected, desired) \
    qjs_atomic_compare_exchange_strong_impl((volatile void*)(obj), (void*)(expected), (uint64_t)(desired), sizeof(*(obj)))

#endif
