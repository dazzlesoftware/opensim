/*
Copyright (c) Contributors, http://opensimulator.org/
See CONTRIBUTORS.TXT for a full list of copyright holders.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of the OpenSimulator Project nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE DEVELOPERS ``AS IS'' AND ANY EXPRESS OR
IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE CONTRIBUTORS BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/
// Copyright (c) Contributors, http://opensimulator.org/
// C++ adaptation; BSD license: native-port/LICENSE.txt.
#include "opensim/framework/prime_number_helper.hpp"
#include <cmath>
#include <iterator>
#include <limits>

namespace opensim::framework {
namespace {
constexpr std::int32_t primes[] = {
    3,7,11,17,23,29,37,47,59,71,89,107,131,163,197,239,
    293,353,431,521,631,761,919,1103,1327,1597,1931,2333,
    2801,3371,4049,4861,5839,7013,8419,10103,12143,14591,
    17519,21023,25229,30293,36353,43627,52361,62851,75431,
    90523,108631,130363,156437,187751,225307,270371,324449,
    389357,467237,560689,672827,807403,968897,1162687,1395263,
    1674319,2009191,2411033,2893249,3471899,4166287,4999559,5999471,7199369
};
}
bool PrimeNumberHelper::is_prime(std::int32_t candidate) noexcept {
    if ((candidate & 1) == 0) return candidate == 2;
    // C# converts sqrt(negative) to a nonpositive integer on the reference runtime,
    // so no divisibility loop executes. Avoid undefined NaN-to-integer casts in C++.
    if (candidate < 0) return true;
    const auto upper = static_cast<std::int32_t>(std::sqrt(static_cast<double>(candidate)));
    for (std::int32_t i = 3; i < upper; i += 2)
        if (candidate % i == 0) return false;
    return true;
}
std::int32_t PrimeNumberHelper::get_prime(std::int32_t minimum) noexcept {
    if (minimum <= 2) return 2;
    if (primes[std::size(primes) - 1] < minimum) {
        for (auto i = minimum | 1; i < std::numeric_limits<std::int32_t>::max(); i += 2)
            if (is_prime(i)) return i;
        return -1;
    }
    for (auto i = static_cast<int>(std::size(primes)) - 2; i >= 0; --i) {
        if (minimum == primes[i]) return minimum;
        if (minimum > primes[i]) return primes[i + 1];
    }
    return 2;
}
}
