#pragma once
namespace opensim::framework {
// Native stack bounds with the .NET 8 reserve (128 KiB on 64-bit).
// Physical native and managed frame depths are not interchangeable.
bool sufficient_execution_stack() noexcept;
}
