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
// C++ adaptation of OpenSim/Framework/PermissionsUtil.cs and PermissionMask in Util.cs.
// Distributed under the BSD license in native-port/LICENSE.txt.
#pragma once
#include <cstdint>
#include <string>

namespace opensim::framework {
enum class PermissionMask : std::uint32_t {
    None = 0, FoldedTransfer = 1, FoldedModify = 1 << 1,
    FoldedCopy = 1 << 2, FoldedExport = 1 << 3,
    Slam = 1 << 4, // Legacy only: new code must not set this bit.
    FoldedMask = 0x0f, FoldingShift = 13,
    Transfer = 1 << 13, Modify = 1 << 14, Copy = 1 << 15,
    Export = 1 << 16, Move = 1 << 19, Damage = 1 << 20,
    All = 0x8e000, AllAndExport = 0x9e000,
    AllAndExportNoMod = 0x9a000, AllEffective = 0x9e000,
    UnfoldedMask = 0x1e000
};
constexpr std::uint32_t bits(PermissionMask mask) noexcept {
    return static_cast<std::uint32_t>(mask);
}
struct PermissionsUtil {
    static std::string permissions_to_string(std::uint32_t permissions);
    // Returns the existing log message so the host can route it to its logger.
    static std::string log_permissions(const std::string& name, const std::string& message,
        std::uint32_t base, std::uint32_t current, std::uint32_t next);
    static void apply_folded_permissions(std::uint32_t source, std::uint32_t& target) noexcept;
    static void apply_no_mod_folded_permissions(std::uint32_t source, std::uint32_t& target) noexcept;
    static std::uint32_t fix_and_fold_permissions(std::uint32_t permissions) noexcept;
};
}
