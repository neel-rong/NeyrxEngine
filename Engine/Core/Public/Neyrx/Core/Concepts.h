#pragma once
#include <type_traits>
#include <concepts>


namespace Concepts
{
	// Arithmetic Type
	template<typename T>
	concept ArithmeticType = std::is_arithmetic_v<T>;

	// Moveable Object Type
	template<typename T>
	concept MovableObjectType = std::is_object_v<T> && std::movable<T>
}