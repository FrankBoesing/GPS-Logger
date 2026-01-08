#ifndef SORTEDSTATICARRAY_H
#define SORTEDSTATICARRAY_H

#include <iostream>
#include <array>
#include <algorithm>
#include <concepts>
#include <iterator>
#include <ranges>

template <typename T>
concept Sortable = std::totally_ordered<T>;

template <Sortable T, std::size_t Capacity>
class SortedStaticArray
{
private:
	std::array<T, Capacity> data{};
	std::size_t current_size = 0;

public:
	constexpr SortedStaticArray() = default;
	constexpr void clear() { current_size = 0; }
	constexpr T *find(const T &target)
	{
		auto it = std::lower_bound(data.begin(), data.begin() + current_size, target);
		if (it != data.begin() + current_size && *it == target)
			return &(*it);
		return nullptr;
	}

	constexpr void insert(const T &value)
	{
		if (current_size == Capacity) // voll → evtl. ältestes Element verwerfen
		{
			if (!(value > data[0]))
				return;
			std::move(data.begin() + 1,
					  data.begin() + current_size,
					  data.begin());
			current_size--;
		}

		auto it = std::lower_bound(data.begin(), data.begin() + current_size, value);

		if (it == data.begin() + current_size)
			data[current_size] = value; // Einfügen am Ende → kein Verschieben nötig
		else
		{ // Platz schaffen manuell (von hinten nach vorne schieben)
			for (std::size_t i = current_size; i > (std::size_t)(it - data.begin()); --i)
			{
				data[i] = std::move(data[i - 1]);
			}
			*it = value;
		}

		current_size++;
	}

	constexpr bool remove(const T &value)
	{
		auto *it = find(value);
		if (!it)
			return false;
		std::move(it + 1, data.begin() + current_size, it);
		current_size--;
		return true;
	}
	constexpr auto size() const { return current_size; }	
	constexpr auto begin() const { return data.begin(); }
	constexpr auto end() const { return data.begin() + current_size; }
	constexpr auto descending() const { return std::ranges::subrange(begin(), end()) | std::views::reverse; }
};

#endif
