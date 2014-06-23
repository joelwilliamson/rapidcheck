#pragma once

#include <limits>
#include <type_traits>
#include <map>

#include "RandomEngine.h"

namespace rc {

template<typename T>
class Arbitrary : public gen::Generator<T>
{
public:
    static_assert(std::is_integral<T>::value,
                  "No specialization of Arbitrary for type");

    T operator()() const override
    {
        using namespace detail;

        size_t size = std::min(gen::currentSize(), gen::kReferenceSize);
        RandomEngine::Atom r;
        // TODO this switching shouldn't be done here. pickAtom?
        ImplicitParam<param::CurrentNode> currentNode;
        if (currentNode.hasBinding() && (*currentNode != nullptr)) {
            r = (*currentNode)->atom();
        } else {
            ImplicitParam<param::RandomEngine> randomEngine;
            r = randomEngine->nextAtom();
        }

        // We vary the size by using different number of bits. This way, we can be
        // that the max value can also be generated.
        int nBits = (size * std::numeric_limits<T>::digits) / gen::kReferenceSize;
        if (nBits == 0)
            return 0;
        constexpr RandomEngine::Atom randIntMax =
            std::numeric_limits<RandomEngine::Atom>::max();
        RandomEngine::Atom mask = ~((randIntMax - 1) << (nBits - 1));

        T x = static_cast<T>(r & mask);
        if (std::numeric_limits<T>::is_signed)
        {
            // Use the topmost bit as the signed bit. Even in the case of a signed
            // 64-bit integer, it won't be used since it actually IS the sign bit.
            constexpr int basicBits =
                std::numeric_limits<RandomEngine::Atom>::digits;
            x *= ((r >> (basicBits - 1)) == 0) ? 1 : -1;
        }

        return x;
    }

    shrink::IteratorUP<T> shrink(T value) const override
    {
        std::vector<T> constants;
        if (value < 0)
            constants.push_back(-value);

        return shrink::sequentially(
            shrink::constant(constants),
            shrink::towards(value, static_cast<T>(0)));
    }
};

// Base for float and double arbitrary instances
template<typename T>
class ArbitraryReal : public gen::Generator<T>
{
public:
    T operator()() const override
    {
        int64_t i = pick(gen::arbitrary<int64_t>());
        T x = static_cast<T>(i) / std::numeric_limits<int64_t>::max();
        return std::pow<T>(1.2, gen::currentSize()) * x;
    }

    shrink::IteratorUP<T> shrink(T value) const override
    {
        std::vector<T> constants;

        if (value < 0)
            constants.push_back(-value);

        T truncated = std::trunc(value);
        if (std::abs(truncated) < std::abs(value))
            constants.push_back(truncated);

        return shrink::constant(constants);
    }
};

template<>
class Arbitrary<float> : public ArbitraryReal<float> {};

template<>
class Arbitrary<double> : public ArbitraryReal<double> {};

template<>
class Arbitrary<bool> : public gen::Generator<bool>
{
public:
    bool operator()() const override
    {
        return (pick(gen::resize(gen::kReferenceSize,
                                 gen::arbitrary<uint8_t>())) & 0x1) == 0;
    }

    shrink::IteratorUP<bool> shrink(bool value)
    {
        if (value)
            return shrink::constant<bool>({false});
        else
            return shrink::nothing<bool>();
    }
};

template<typename ...Types>
class Arbitrary<std::tuple<Types...>>
    : public gen::TupleOf<Arbitrary<Types>...>
{
public:
    Arbitrary() : gen::TupleOf<Arbitrary<Types>...>(
            gen::arbitrary<Types>()...) {}
};

template<typename T1, typename T2>
class Arbitrary<std::pair<T1, T2>> : public gen::Generator<std::pair<T1, T2>>
{
public:
    std::pair<T1, T2> operator()() const override
    { return std::make_pair(pick<T1>(), pick<T2>()); }

    shrink::IteratorUP<std::pair<T1, T2>>
    shrink(std::pair<T1, T2> pair) const override
    {
        return shrink::sequentially(
            shrink::map(gen::arbitrary<T1>().shrink(pair.first),
                      [=](T1 x) { return std::make_pair(x, pair.second); }),
            shrink::map(gen::arbitrary<T2>().shrink(pair.second),
                      [=](T2 x) { return std::make_pair(pair.first, x); }));
    }
};

// Base template class for collection types
template<typename Coll, typename ValueType>
class ArbitraryCollection : public gen::Collection<Coll, Arbitrary<ValueType>>
{
public:
    ArbitraryCollection() : gen::Collection<Coll, Arbitrary<ValueType>>(
        gen::arbitrary<ValueType>()) {}
};

// std::vector
template<typename T, typename Alloc>
class Arbitrary<std::vector<T, Alloc>>
    : public ArbitraryCollection<std::vector<T, Alloc>, T> {};

// std::map
template<typename Key, typename T, typename Compare, typename Alloc>
class Arbitrary<std::map<Key, T, Compare, Alloc>>
    : public ArbitraryCollection<std::map<Key, T, Compare, Alloc>,
                                 std::pair<Key, T>> {};

// std::basic_string
template<typename T, typename Traits, typename Alloc>
class Arbitrary<std::basic_string<T, Traits, Alloc>>
    : public gen::Collection<std::basic_string<T, Traits, Alloc>, gen::Character<T>>
{
public:
    typedef std::basic_string<T, Traits, Alloc> StringType;

    Arbitrary() : gen::Collection<StringType, gen::Character<T>>(
        gen::character<T>()) {}
};

} // namespace rc