// .NET8 RegexWriter and interpreter opcodes; MIT runtime license.
#pragma once
#include "opensim/framework/dotnet_regex_parser.hpp"
namespace opensim::framework {
enum class DotnetRegexOpcode:std::int32_t {
 Onerep=0,Notonerep=1,Setrep=2,Oneloop=3,Notoneloop=4,Setloop=5,Onelazy=6,Notonelazy=7,Setlazy=8,One=9,Notone=10,Set=11,Multi=12,Backreference=13,Bol=14,Eol=15,Boundary=16,NonBoundary=17,Beginning=18,Start=19,EndZ=20,End=21,Nothing=22,Lazybranch=23,Branchmark=24,Lazybranchmark=25,Nullcount=26,Setcount=27,Branchcount=28,Lazybranchcount=29,Nullmark=30,Setmark=31,Capturemark=32,Getmark=33,Setjump=34,Backjump=35,Forejump=36,TestBackreference=37,Goto=38,Stop=40,ECMABoundary=41,NonECMABoundary=42,Oneloopatomic=43,Notoneloopatomic=44,Setloopatomic=45,UpdateBumpalong=46,OperatorMask=63,RightToLeft=64,Backtracking=128,BacktrackingSecond=256,CaseInsensitive=512
};
bool dotnet_regex_opcode_backtracks(std::int32_t);
struct DotnetRegexBytecode {
 std::int32_t options=0,track_count=0;
 std::vector<std::int32_t> codes;
 std::vector<DotnetRegexNodeText> strings;
 // Same initial null per-string ASCII cache slots as RegexInterpreterCode.
 std::unique_ptr<DotnetRegexAsciiCacheSlot[]> strings_ascii_lookup;
};
// Emits execution data. Starting-position optimization and the interpreter are
// separate consumers still under port; this does not execute a partial engine.
DotnetRegexBytecode dotnet_regex_write(const DotnetRegexParsedPattern&);
}
