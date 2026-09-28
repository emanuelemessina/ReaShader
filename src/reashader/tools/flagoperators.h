/******************************************************************************
 * Copyright (c) Emanuele Messina (https://github.com/emanuelemessina)
 * All rights reserved.
 *
 * This code is licensed under the MIT License.
 * See the LICENSE file (https://github.com/emanuelemessina/ReaShader/blob/main/LICENSE) for more information.
 *****************************************************************************/

#pragma once

#include <type_traits>

template <typename FlagType> constexpr FlagType operator|(FlagType lhs, FlagType rhs)
{
	using T = std::underlying_type_t<FlagType>;
	return static_cast<FlagType>(static_cast<T>(lhs) | static_cast<T>(rhs));
}

template <typename FlagType> constexpr FlagType operator&(FlagType lhs, FlagType rhs)
{
	using T = std::underlying_type_t<FlagType>;
	return static_cast<FlagType>(static_cast<T>(lhs) & static_cast<T>(rhs));
}

template <typename FlagType> constexpr FlagType operator^(FlagType lhs, FlagType rhs)
{
	using T = std::underlying_type_t<FlagType>;
	return static_cast<FlagType>(static_cast<T>(lhs) ^ static_cast<T>(rhs));
}

template <typename FlagType> constexpr FlagType operator~(FlagType value)
{
	using T = std::underlying_type_t<FlagType>;
	return static_cast<FlagType>(~static_cast<T>(value));
}
