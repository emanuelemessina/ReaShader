#pragma once

#include "tools/fwd_decl.h"

#include <vulkan/vulkan.h>

#include <vector>

#include <algorithm>
#include <any>
#include <array>
#include <assert.h>
#include <concepts>
#include <cstdlib>
#include <deque>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <numeric>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <unordered_map>

#include "vk_mem_alloc.h"

#define GLM_FORCE_DEFAULT_ALIGNED_GENTYPES
#include "glm/glm.hpp"
#include "glm/gtx/transform.hpp"
#include "glm/vec2.hpp"

#define GLM_ENABLE_EXPERIMENTAL
#include "glm/gtx/hash.hpp"

#define VK_CHECK_RESULT(f)                                                                                             \
	{                                                                                                                  \
		VkResult res = (f);                                                                                            \
		if (res != VK_SUCCESS)                                                                                         \
		{                                                                                                              \
			std::string msg = "Fatal : \"";                                                                            \
			msg += res;                                                                                                \
			msg += "\" in ";                                                                                           \
			msg += __FILE__;                                                                                           \
			msg += " at line ";                                                                                        \
			msg += std::to_string(__LINE__);                                                                           \
			throw std::runtime_error(msg);                                                                             \
		}                                                                                                              \
	}

namespace vkt
{

#ifdef NDEBUG
	const bool enableVkValidationLayers = false;
#else
	const bool enableVkValidationLayers = true;
#endif

	// VALIDATION LAYERS

	const std::vector<const char*> vkValidationLayers = { "VK_LAYER_KHRONOS_validation" };

	struct deletion_queue
	{
		std::deque<std::function<void()>> deletors;

		void push_function(std::function<void()>&& function)
		{
			deletors.push_back(function);
		}

		void flush()
		{
			// reverse iterate the deletion queue to execute all the functions
			for (auto it = deletors.rbegin(); it != deletors.rend(); it++)
			{
				(*it)(); // call the function
			}

			deletors.clear();
		}
	};

	VkInstance createVkInstance(deletion_queue& deletionQueue, const char* applicationName, const char* engineName);

	namespace io
	{

		std::vector<char> readFile(const std::string& filename);

	}

	namespace vectors
	{

		template <typename K>
		/**
		Checks wheters the type is hashable
		*/
		concept Key = requires(K key) {
						  {
							  std::hash<K>(key)
							  } -> std::same_as<size_t>;
					  };

		template <typename Key, typename T> struct searchable_map
		{

			using map = std::unordered_map<Key, T>;

			void add(Key key, T obj)
			{
				objs[key] = obj;
			}
			T* get(Key key)
			{
				auto it = objs.find(key);
				if (it == objs.end())
				{
					return nullptr;
				}
				else
				{
					return (T*)&(it->second);
				}
			}
			/**
			Returns the internal unordered map.
			*/
			map get()
			{
				return objs;
			}

		  private:
			map objs;
		};

		template <typename S, typename T>
		/**
		Returns a vector containing a reference wrapper to member, specified at offset, for each struct.
		*/
		inline std::vector<T> extract_member_copy_vector(const std::vector<std::reference_wrapper<S>>& structs,
														 size_t offset)
		{
			std::vector<T> ms(structs.size());
			for (size_t i = 0; i < structs.size(); i++)
			{
				S iRef = structs[i].get();
				ms[i] = *(T*)(reinterpret_cast<uintptr_t>(&iRef) + offset);
			}
			return ms;
		}
		template <typename S, typename T>
		/**
		Returns a vector containing the member (copied), specified at offset, for each struct.
		*/
		inline std::vector<std::reference_wrapper<T>> extract_member_ref_vector(const std::vector<S>& structs,
																				size_t offset)
		{
			std::vector<std::reference_wrapper<T>> ms(structs.size());
			for (int i = 0; i < structs.size(); i++)
			{
				S iRef = structs[i];
				ms[i] = *(T*)((int)&iRef + offset);
			}
			return ms;
		}

		template <typename S, typename T>
		/**
		Returns a vector containing a pointer to member, specified at offset, for each struct.
		*/
		inline std::vector<T*> extract_member_ptr_vector(const std::vector<S>& structs, size_t offset)
		{
			std::vector<T*> ms(structs.size());
			for (int i = 0; i < structs.size(); i++)
			{
				S iRef = structs[i];
				ms[i] = (T*)((int)&iRef + offset);
			}
			return ms;
		}

		template <typename T, typename Lambda>
		/**
		Returns reference to first item found satisfying the predicate condition.
		*/
		inline T& findRef(std::vector<T>& vector, Lambda predicate)
		{
			auto pos = std::find_if(vector.cbegin(), vector.cend(), predicate);
			return vector[std::distance(vector.cbegin(), pos)];
		}
		template <typename T>
		/**
		Returns index of item in vector
		*/
		inline int findPos(std::vector<T>& vector, T item)
		{
			auto pos = std::find(vector.cbegin(), vector.cend(), item);
			return std::distance(vector.cbegin(), pos);
		}

	} // namespace vectors

	template <typename B>
	/**
	 * Remember to disable copy constructor!!
	 * @param B is the type to be built
	 */
	class IBuilder
	{
	  public:
		/**
		 * The build function returns the type to be built.
		 */
		virtual B build() = 0;
	};

	template <typename VkType>
	/**
	 * Interface for a class wrapping a VkType in a high level abstraction class
	 */
	class IVkWrapper
	{
	  public:
		/**
		 * Returns the wrapped low level VkType
		 */
		virtual VkType vk() = 0;
	};
} // namespace vkt

/* MACROS */

#define arr_length(arr) (*(&arr + 1) - arr)

#define vkt_size2count(size) static_cast<uint32_t>(size)
