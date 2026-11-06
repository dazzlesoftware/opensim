using System;
using System.IO;
using System.Text;
static class Program
{
    static string Code(Exception e)
    {
        uint value = unchecked((uint)e.HResult);
        return (e is IOException || e is UnauthorizedAccessException) && (value & 0xffff0000u) == 0x80070000u
                   ? (value & 0xffffu).ToString("x8")
                   : "-";
    }
    public static void Main(string[] args)
    {
        Console.OutputEncoding = new UTF8Encoding(false);
        if (args.Length > 0 && args[0] == "--unix-probe")
        {
            UnixProbe.MainLoop();
            return;
        }
        bool metadata = args.Length > 0 && args[0] == "--metadata-probe";
        if (args.Length > 0 && !metadata)
        {
            Console.WriteLine(typeof(Path).Assembly.Location);
            Console.WriteLine(Environment.Version);
            return;
        }
        string line;
        while ((line = Console.ReadLine()) != null)
        {
            try
            {
                string path = line == "-"   ? null
                              : line == "." ? ""
                                            : Encoding.UTF8.GetString(Convert.FromHexString(line));
                Console.WriteLine(
                    "OK:" +
                    Convert.ToHexString(Encoding.UTF8.GetBytes(Path.GetFullPath(path))).ToLowerInvariant());
            }
            catch (Exception e)
            {
                Console.WriteLine(
                    "EX:" + e.GetType().Name + ":" + (e is ArgumentException a ? a.ParamName ?? "" : "") +
                    ":" + Convert.ToHexString(Encoding.UTF8.GetBytes(e.Message)).ToLowerInvariant() +
                    (metadata ? ":" + unchecked((uint)e.HResult).ToString("x8") + ":" + Code(e) : ""));
            }
        }
    }
}

// BEGIN PINNED UNIX ORACLE
/*
The MIT License (MIT)

Copyright (c) .NET Foundation and Contributors

All rights reserved.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

*/
// Source: dotnet/runtime v8.0.31. Methods below retained unchanged.
// Path.Unix.cs 3b787b28d22f8fb453a1f740c3d6c402b4877ad64d4e670982c2623c0532fe65 https://raw.githubusercontent.com/dotnet/runtime/v8.0.31/src/libraries/System.Private.CoreLib/src/System/IO/Path.Unix.cs
// PathInternal.cs dd64059a38596cc7e88306141fc118ffa238b7eb0cc32f9391358fe8e7f4f9e0 https://raw.githubusercontent.com/dotnet/runtime/v8.0.31/src/libraries/Common/src/System/IO/PathInternal.cs
// PathInternal.Unix.cs eac854d73e9c370ec27d6a64b3041f36685665ab8b2b45e82810e44a45016930 https://raw.githubusercontent.com/dotnet/runtime/v8.0.31/src/libraries/Common/src/System/IO/PathInternal.Unix.cs
// Interop.IOErrors.cs 467105d02725baedea5556e85aa740d0e02ce64d8a5d49955a9233cde9ec9e3d https://raw.githubusercontent.com/dotnet/runtime/v8.0.31/src/libraries/Common/src/Interop/Unix/Interop.IOErrors.cs
// Strings.resx 489453bf0b17d398453b29c98f41570fff303bd549e327d5f0b47da95c9c204a https://raw.githubusercontent.com/dotnet/runtime/v8.0.31/src/libraries/System.Private.CoreLib/src/Resources/Strings.resx

namespace SourceUnix
{
    using System;
    using System.IO;
    using System.Diagnostics;
    using System.Diagnostics.CodeAnalysis;
    using System.Runtime.InteropServices;
    using System.Text;
    ref struct ValueStringBuilder
    {
        StringBuilder value;
        public ValueStringBuilder(Span<char> initial)
        {
            value = new StringBuilder();
        }
        public int Length
        {
            get => value.Length;
            set => this.value.Length = value;
        }
        public char this[int index] => value[index];
        public void Append(char ch) => value.Append(ch);
        public void Append(ReadOnlySpan<char> chars) => value.Append(chars);
        public override string ToString() => value.ToString();
        public void Dispose()
        {
        }
    }
    static class SR
    {
        internal const string Argument_NullCharInPath = "Null character in path.";
        public static string Format(string resource, params object[] args) =>
            string.Format(System.Globalization.CultureInfo.InvariantCulture, resource, args);
        internal const string ArgumentOutOfRange_FileLengthTooBig =
            "Specified file length was too large for the file system.";
        internal const string IO_FileExists_Name = "The file '{0}' already exists.";
        internal const string IO_FileNotFound = "Unable to find the specified file.";
        internal const string IO_FileNotFound_FileName = "Could not find file '{0}'.";
        internal const string IO_PathNotFound_NoPathName = "Could not find a part of the path.";
        internal const string IO_PathNotFound_Path = "Could not find a part of the path '{0}'.";
        internal const string IO_PathTooLong =
            "The specified file name or path is too long, or a component of the specified path is too long.";
        internal const string IO_PathTooLong_Path =
            "The path '{0}' is too long, or a component of the specified path is too long.";
        internal const string IO_SharingViolation_File =
            "The process cannot access the file '{0}' because it is being used by another process.";
        internal const string IO_SharingViolation_NoFileName =
            "The process cannot access the file because it is being used by another process.";
        internal const string UnauthorizedAccess_IODenied_NoPathName = "Access to the path is denied.";
        internal const string UnauthorizedAccess_IODenied_Path = "Access to the path '{0}' is denied.";
    }
    static class PathInternal
    {
        internal const char DirectorySeparatorChar = '/';
        internal const char AltDirectorySeparatorChar = '/';
        internal const string DirectorySeparatorCharAsString = "/";
        internal static int GetRootLength(ReadOnlySpan<char> path)
        {
            return path.Length > 0 && IsDirectorySeparator(path[0]) ? 1 : 0;
        }
        internal static bool IsDirectorySeparator(char c)
        {
            // The alternate directory separator char is the same as the directory separator,
            // so we only need to check one.
            Debug.Assert(DirectorySeparatorChar == AltDirectorySeparatorChar);
            return c == DirectorySeparatorChar;
        }
        internal static string RemoveRelativeSegments(string path, int rootLength)
        {
            var sb = new ValueStringBuilder(stackalloc char[260 /* PathInternal.MaxShortPath */]);

            if (RemoveRelativeSegments(path.AsSpan(), rootLength, ref sb))
            {
                path = sb.ToString();
            }

            sb.Dispose();
            return path;
        }
        internal static bool RemoveRelativeSegments(ReadOnlySpan<char> path, int rootLength,
                                                    ref ValueStringBuilder sb)
        {
            Debug.Assert(rootLength > 0);
            bool flippedSeparator = false;

            int skip = rootLength;
            // We treat "\.." , "\." and "\\" as a relative segment. We want to collapse the first separator past the root presuming
            // the root actually ends in a separator. Otherwise the first segment for RemoveRelativeSegments
            // in cases like "\\?\C:\.\" and "\\?\C:\..\", the first segment after the root will be ".\" and "..\" which is not considered as a relative segment and hence not be removed.
            if (IsDirectorySeparator(path[skip - 1]))
                skip--;

            // Remove "//", "/./", and "/../" from the path by copying each character to the output,
            // except the ones we're removing, such that the builder contains the normalized path
            // at the end.
            if (skip > 0)
            {
                sb.Append(path.Slice(0, skip));
            }

            for (int i = skip; i < path.Length; i++)
            {
                char c = path[i];

                if (IsDirectorySeparator(c) && i + 1 < path.Length)
                {
                    // Skip this character if it's a directory separator and if the next character is, too,
                    // e.g. "parent//child" => "parent/child"
                    if (IsDirectorySeparator(path[i + 1]))
                    {
                        continue;
                    }

                    // Skip this character and the next if it's referring to the current directory,
                    // e.g. "parent/./child" => "parent/child"
                    if ((i + 2 == path.Length || IsDirectorySeparator(path[i + 2])) && path[i + 1] == '.')
                    {
                        i++;
                        continue;
                    }

                    // Skip this character and the next two if it's referring to the parent directory,
                    // e.g. "parent/child/../grandchild" => "parent/grandchild"
                    if (i + 2 < path.Length && (i + 3 == path.Length || IsDirectorySeparator(path[i + 3])) &&
                        path[i + 1] == '.' && path[i + 2] == '.')
                    {
                        // Unwind back to the last slash (and if there isn't one, clear out everything).
                        int s;
                        for (s = sb.Length - 1; s >= skip; s--)
                        {
                            if (IsDirectorySeparator(sb[s]))
                            {
                                sb.Length =
                                    (i + 3 >= path.Length && s == skip)
                                        ? s + 1
                                        : s; // to avoid removing the complete "\tmp\" segment in cases like \\?\C:\tmp\..\, C:\tmp\..
                                break;
                            }
                        }
                        if (s < skip)
                        {
                            sb.Length = skip;
                        }

                        i += 2;
                        continue;
                    }
                }

                // Normalize the directory separator if needed
                if (c != DirectorySeparatorChar && c == AltDirectorySeparatorChar)
                {
                    c = DirectorySeparatorChar;
                    flippedSeparator = true;
                }

                sb.Append(c);
            }

            // If we haven't changed the source path, return the original
            if (!flippedSeparator && sb.Length == path.Length)
            {
                return false;
            }

            // We may have eaten the trailing separator from the root when we started and not replaced it
            if (skip != rootLength && sb.Length < rootLength)
            {
                sb.Append(path[rootLength - 1]);
            }

            return true;
        }
    }
    static class Path
    {
        // Helper branches with a path argument are not used by GetCwd's null-path errno mapping.
        public static string GetDirectoryName(string path) => System.IO.Path.GetDirectoryName(path);
        public static string TrimEndingDirectorySeparator(string path) =>
            System.IO.Path.TrimEndingDirectorySeparator(path);
        public static string Combine(string first, string second) => first.Length == 0    ? second
                                                                     : second.Length == 0 ? first
                                                                     : first.EndsWith('/')
                                                                         ? first + second
                                                                         : first + "/" + second;
        public static string GetFullPath(string path)
        {
            ArgumentException.ThrowIfNullOrEmpty(path);

            if (path.Contains('\0'))
                throw new ArgumentException(SR.Argument_NullCharInPath, nameof(path));

            return GetFullPathInternal(path);
        }
        private static string GetFullPathInternal(string path)
        {
            Debug.Assert(!string.IsNullOrEmpty(path));
            Debug.Assert(!path.Contains('\0'));

            // Expand with current directory if necessary
            if (!IsPathRooted(path))
            {
                path = Combine(Interop.Sys.GetCwd(), path);
            }

            // We would ideally use realpath to do this, but it resolves symlinks and requires that the file actually exist.
            string collapsedString =
                PathInternal.RemoveRelativeSegments(path, PathInternal.GetRootLength(path));

            Debug.Assert(
                collapsedString.Length < path.Length || collapsedString.ToString() == path,
                "Either we've removed characters, or the string should be unmodified from the input path.");

            string result =
                collapsedString.Length == 0 ? PathInternal.DirectorySeparatorCharAsString : collapsedString;

            return result;
        }
        public static bool IsPathRooted([NotNullWhen(true)] string? path)
        {
            if (path == null)
                return false;

            return IsPathRooted(path.AsSpan());
        }
        public static bool IsPathRooted(ReadOnlySpan<char> path)
        {
            return path.Length > 0 && path[0] == PathInternal.DirectorySeparatorChar;
        }
    }
    static class Interop
    {
        internal enum Error
        {
            SUCCESS = 0,
            ENOENT = 1,
            ENOTDIR = 2,
            EACCES = 3,
            EBADF = 4,
            EPERM = 5,
            ENAMETOOLONG = 6,
            EWOULDBLOCK = 7,
            ECANCELED = 8,
            EFBIG = 9,
            EEXIST = 10,
            Other = 11
        }
        internal struct ErrorInfo
        {
            public Error Error;
            public int RawErrno;
            public string Message;
            public string GetErrorMessage() => Message;
        }
        internal static class Sys
        {
            internal static unsafe string GetCwd()
            {
                UnixProbe.Calls++;
                if (UnixProbe.Fault)
                    throw new ProviderFault();
                if (UnixProbe.Error != 0)
                    throw GetExceptionForIoErrno(new ErrorInfo { Error = (Error)UnixProbe.Error,
                                                                 RawErrno = UnixProbe.Error,
                                                                 Message = "controlled system error" });
                byte[] value = new byte[UnixProbe.Cwd.Length + 1];
                UnixProbe.Cwd.CopyTo(value, 0);
                fixed(byte *ptr = value) return Marshal.PtrToStringUTF8((IntPtr)ptr);
            }
        }
        internal static Exception GetExceptionForIoErrno(ErrorInfo errorInfo, string? path = null,
                                                         bool isDirError = false)
        {
            // Translate the errno into a known set of exception types.  For cases where multiple errnos map
            // to the same exception type, include an inner exception with the details.
            switch (errorInfo.Error)
            {
            case Error.ENOENT:
                // For Windows compatibility, throw DirectoryNotFoundException instead of FileNotFoundException
                // when the parent folder does not exist.
                if (!isDirError && (path is null || ParentDirectoryExists(path)))
                {
                    return !string.IsNullOrEmpty(path)
                               ? new FileNotFoundException(SR.Format(SR.IO_FileNotFound_FileName, path), path)
                               : new FileNotFoundException(SR.IO_FileNotFound);
                }
                goto case Error.ENOTDIR;

            case Error.ENOTDIR:
                return !string.IsNullOrEmpty(path)
                           ? new DirectoryNotFoundException(SR.Format(SR.IO_PathNotFound_Path, path))
                           : new DirectoryNotFoundException(SR.IO_PathNotFound_NoPathName);

            case Error.EACCES:
            case Error.EBADF:
            case Error.EPERM:
                Exception inner = GetIOException(errorInfo);
                return !string.IsNullOrEmpty(path)
                           ? new UnauthorizedAccessException(
                                 SR.Format(SR.UnauthorizedAccess_IODenied_Path, path), inner)
                           : new UnauthorizedAccessException(SR.UnauthorizedAccess_IODenied_NoPathName,
                                                             inner);

            case Error.ENAMETOOLONG:
                return !string.IsNullOrEmpty(path)
                           ? new PathTooLongException(SR.Format(SR.IO_PathTooLong_Path, path))
                           : new PathTooLongException(SR.IO_PathTooLong);

            case Error.EWOULDBLOCK:
                return !string.IsNullOrEmpty(path)
                           ? new IOException(SR.Format(SR.IO_SharingViolation_File, path), errorInfo.RawErrno)
                           : new IOException(SR.IO_SharingViolation_NoFileName, errorInfo.RawErrno);

            case Error.ECANCELED:
                return new OperationCanceledException();

            case Error.EFBIG:
                return new ArgumentOutOfRangeException("value", SR.ArgumentOutOfRange_FileLengthTooBig);

            case Error.EEXIST:
                if (!string.IsNullOrEmpty(path))
                {
                    return new IOException(SR.Format(SR.IO_FileExists_Name, path), errorInfo.RawErrno);
                }
                goto default;

            default:
                return GetIOException(errorInfo, path);

                static bool ParentDirectoryExists(string fullPath)
                {
                    string? parentPath = Path.GetDirectoryName(Path.TrimEndingDirectorySeparator(fullPath));

                    return Directory.Exists(parentPath);
                }
            }
        }
        internal static Exception GetIOException(Interop.ErrorInfo errorInfo, string? path = null)
        {
            string msg = errorInfo.GetErrorMessage();
            return new IOException(string.IsNullOrEmpty(path) ? msg : $"{msg} : '{path}'",
                                   errorInfo.RawErrno);
        }
    }
}
class ProviderFault : Exception
{
    public ProviderFault() : base("cwd provider fault")
    {
    }
}
static class UnixProbe
{
    public static int Calls, Error;
    public static bool Fault;
    public static byte[] Cwd;
    static byte[] Bytes(string text) => text == "." || text == "-" ? Array.Empty<byte>()
                                                                   : Convert.FromHexString(text);
    static string Hex(string value) => Convert.ToHexString(Encoding.UTF8.GetBytes(value)).ToLowerInvariant();
    public static string Run(string line)
    {
        var p = line.Split(' ');
        Calls = 0;
        Error = int.Parse(p[2]);
        Fault = p[3] == "1";
        Cwd = Bytes(p[1]);
        try
        {
            string path = p[0] == "-" ? null : Encoding.UTF8.GetString(Bytes(p[0]));
            return "OK:" + Hex(SourceUnix.Path.GetFullPath(path)) + "|" + Calls;
        }
        catch (Exception e)
        {
            return "EX:" + e.GetType().Name + ":" + (e is ArgumentException a ? a.ParamName ?? "" : "") +
                   ":" + Hex(e.Message) + "|" + Calls;
        }
    }
    public static void MainLoop()
    {
        string line;
        while ((line = Console.ReadLine()) != null)
            Console.WriteLine(Run(line));
    }
}
