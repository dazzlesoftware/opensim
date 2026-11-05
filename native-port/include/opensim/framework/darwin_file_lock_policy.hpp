// Derived from dotnet/runtime v8.0.31 PAL; Copyright .NET Foundation and Contributors.
// MIT: native-port/third_party/dotnet_collation/LICENSE.
#pragma once
#include <algorithm>
#include <iterator>
#include <string_view>
namespace opensim::framework {
// .NET8.0.31 PAL MapFileSystemNameToEnum + SafeFileHandle.CanLockTheFile.
// Source-backed Darwin shared-write policy only; no macOS execution claim.
inline bool darwin_shared_write_can_lock(std::string_view name) {
    name = name.substr(0, name.find('\0'));
    constexpr std::string_view known[] = {
        "adfs",       "affs",     "afs",       "anoninode",  "apfs",      "aufs",        "autofs",
        "autofs4",    "befs",     "bdevfs",    "bfs",        "bpf_fs",    "binfmt_misc", "bootfs",
        "btrfs",      "ceph",     "cgroupfs",  "cgroup2fs",  "cifs",      "coda",        "coherent",
        "configfs",   "cpuset",   "cramfs",    "ctfs",       "debugfs",   "dev",         "devfs",
        "devpts",     "ecryptfs", "efs",       "exofs",      "ext",       "ext2_old",    "ext2",
        "ext3",       "ext4",     "f2fs",      "fat",        "fd",        "fhgfs",       "fuse",
        "fuseblk",    "fusectl",  "futexfs",   "gfsgfs2",    "gfs2",      "gpfs",        "hfs",
        "hfsplus",    "hpfs",     "hugetlbfs", "inodefs",    "inotifyfs", "isofs",       "jffs",
        "jffs2",      "jfs",      "kafs",      "lofs",       "logfs",     "lustre",      "minix_old",
        "minix",      "minix2",   "minix2v2",  "minix3",     "mntfs",     "mqueue",      "msdos",
        "nfs",        "nfsd",     "nilfs",     "novell",     "ntfs",      "objfs",       "ocfs2",
        "openprom",   "omfs",     "overlay",   "overlayfs",  "panfs",     "pipefs",      "proc",
        "pstorefs",   "qnx4",     "qnx6",      "ramfs",      "reiserfs",  "romfs",       "rootfs",
        "rpc_pipefs", "samba",    "sdcardfs",  "securityfs", "selinux",   "sffs",        "sharefs",
        "smb",        "smb2",     "sockfs",    "squashfs",   "sysfs",     "sysv2",       "sysv4",
        "tmpfs",      "tracefs",  "ubifs",     "udf",        "ufs",       "ufscigam",    "ufs2",
        "usbdevice",  "v9fs",     "vagrant",   "vboxfs",     "vmhgfs",    "vxfs",        "vzfs",
        "xenfs",      "xenix",    "xfs",       "xia",        "udev",      "zfs"};
    bool mapped = std::find(std::begin(known), std::end(known), name) != std::end(known);
    return mapped && name != "cifs" && name != "nfs" && name != "samba" && name != "smb" && name != "smb2";
}

}
