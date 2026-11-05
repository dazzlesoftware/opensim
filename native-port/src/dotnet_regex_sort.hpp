// .NET8 ArraySortHelper introsort; MIT runtime license.
#pragma once
#include <algorithm>
#include <bit>
#include <span>
#include <vector>
namespace opensim::framework::regex_detail {
template <class T, class Compare> void intro_sort(std::span<T> values, int depth, Compare compare) {
    auto swap_greater = [&](int a, int b) {
        if (compare(values[a], values[b]) > 0)
            std::swap(values[a], values[b]);
    };
    int count = static_cast<int>(values.size());
    while (count > 1) {
        if (count <= 16) {
            if (count == 2)
                swap_greater(0, 1);
            else if (count == 3) {
                swap_greater(0, 1);
                swap_greater(0, 2);
                swap_greater(1, 2);
            } else
                for (int i = 0; i < count - 1; ++i) {
                    auto value = values[i + 1];
                    int j = i;
                    while (j >= 0 && compare(value, values[j]) < 0) {
                        values[j + 1] = values[j];
                        --j;
                    }
                    values[j + 1] = value;
                }
            return;
        }
        if (depth == 0) {
            auto down = [&](int index, int n) {
                auto value = values[index - 1];
                while (index <= n / 2) {
                    int child = index * 2;
                    if (child < n && compare(values[child - 1], values[child]) < 0)
                        ++child;
                    if (compare(value, values[child - 1]) >= 0)
                        break;
                    values[index - 1] = values[child - 1];
                    index = child;
                }
                values[index - 1] = value;
            };
            for (int i = count / 2; i >= 1; --i)
                down(i, count);
            for (int i = count; i > 1; --i) {
                std::swap(values[0], values[i - 1]);
                down(1, i - 1);
            }
            return;
        }
        --depth;
        int high = count - 1, middle = high / 2;
        swap_greater(0, middle);
        swap_greater(0, high);
        swap_greater(middle, high);
        auto pivot = values[middle];
        std::swap(values[middle], values[high - 1]);
        int left = 0, right = high - 1;
        while (left < right) {
            while (compare(values[++left], pivot) < 0) {
            }
            while (compare(pivot, values[--right]) < 0) {
            }
            if (left >= right)
                break;
            std::swap(values[left], values[right]);
        }
        if (left != high - 1)
            std::swap(values[left], values[high - 1]);
        intro_sort(values.subspan(left + 1, count - left - 1), depth, compare);
        count = left;
    }
}
template <class T, class Compare> void sort(std::vector<T>& values, Compare compare) {
    if (values.size() > 1)
        intro_sort(std::span<T>(values), 2 * std::bit_width(static_cast<unsigned>(values.size())), compare);
}
}
