#pragma once

#include <type_traits>
#include "Arduino.h"

template <typename T>
struct AsHex
{
  T value;
  inline AsHex(const T &value) : value{value} {}
};

template <typename T>
struct _IsHex : std::false_type {};

template <typename T>
struct _IsHex<AsHex<T>> : std::true_type {};

template <typename T>
using IsHex = _IsHex<std::remove_cv_t<std::remove_reference_t<T>>>;

template <typename T>
inline void _print(T && arg)
{
  if constexpr (IsHex<T>::value)
    Serial.print(arg.value, HEX);
  else
    Serial.print(arg);
}

template <typename ...Args>
inline void print(Args && ...arg)
{
  (_print(arg), ...);
}

template <typename ...Args>
inline void println(Args && ...arg)
{
  (_print(arg), ...);
  Serial.println();
}
