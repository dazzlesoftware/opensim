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
#include "opensim/framework/permissions.hpp"
#include <iomanip>
#include <locale>
#include <sstream>

namespace opensim::framework {
std::string PermissionsUtil::permissions_to_string(std::uint32_t perms) {
    std::string result;
    if (perms & bits(PermissionMask::Modify)) result += 'M';
    if (perms & bits(PermissionMask::Copy)) result += 'C';
    if (perms & bits(PermissionMask::Transfer)) result += 'T';
    if (perms & bits(PermissionMask::Export)) result += 'X';
    return result.empty() ? "." : result;
}
std::string PermissionsUtil::log_permissions(const std::string& name, const std::string& message,
    std::uint32_t base, std::uint32_t current, std::uint32_t next) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    const auto append = [&out](std::uint32_t value) {
        out << permissions_to_string(value) << " (" << std::uppercase << std::hex
            << std::setfill('0') << std::setw(4) << value << ')';
    };
    out << "Permissions of \"" << name << "\" at \"" << message << "\": Base ";
    append(base);
    out << ", Current "; append(current);
    out << ", NextOwner "; append(next);
    return out.str();
}
void PermissionsUtil::apply_folded_permissions(std::uint32_t source, std::uint32_t& target) noexcept {
    auto folded = source & bits(PermissionMask::FoldedMask);
    if (folded == 0 || folded == bits(PermissionMask::FoldedMask)) return;
    folded <<= bits(PermissionMask::FoldingShift);
    target &= folded | ~bits(PermissionMask::UnfoldedMask);
}
void PermissionsUtil::apply_no_mod_folded_permissions(std::uint32_t source, std::uint32_t& target) noexcept {
    auto folded = source & bits(PermissionMask::FoldedMask);
    if (folded == 0 || folded == bits(PermissionMask::FoldedMask)) return;
    folded <<= bits(PermissionMask::FoldingShift);
    target &= folded | ~bits(PermissionMask::UnfoldedMask) | bits(PermissionMask::Modify);
}
std::uint32_t PermissionsUtil::fix_and_fold_permissions(std::uint32_t perms) noexcept {
    if ((perms & (bits(PermissionMask::Copy) | bits(PermissionMask::Transfer))) == 0)
        perms |= bits(PermissionMask::Transfer);
    perms |= bits(PermissionMask::Move);
    perms &= ~bits(PermissionMask::FoldedMask);
    return perms | ((perms >> bits(PermissionMask::FoldingShift)) & bits(PermissionMask::FoldedMask));
}
}
