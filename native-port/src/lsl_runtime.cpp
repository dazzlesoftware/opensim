// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/script/lsl_runtime.hpp"
#include "opensim/framework/avatar_visual_parameter.hpp"
#include "opensim/framework/particle_system.hpp"
#include "opensim/framework/invariant_number_parse.hpp"
#include "opensim/framework/sha256.hpp"
#include "opensim/scene/script_control.hpp"
#include "opensim/framework/text.hpp"
#include "opensim/framework/identity.hpp"
#include "opensim/framework/sha1.hpp"
#include "opensim/framework/dotnet_regex_interpreter.hpp"
#include "opensim/framework/culture_sort.hpp"
#include "opensim/framework/execution_stack.hpp"
#include "opensim/network/osd_value.hpp"
#include "opensim/network/osd_json.hpp"
#include "opensim/framework/culture_prefix.hpp"
#include "opensim/framework/config_uri_escape.hpp"
#include "opensim/framework/config_native_uri_object.hpp"
#include "opensim/framework/config_uri_unescape.hpp"
#include "opensim/framework/primitive_base_shape.hpp"
#include "opensim/framework/sdk_uuid.hpp"
#include "opensim/framework/sdk_quaternion.hpp"
#include <bit>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <deque>
#include <limits>
#include <map>
#include <random>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <initializer_list>
#include <ranges>
namespace opensim::script {
namespace {
enum class Kind { End, Name, Integer, Float, String, Symbol };
enum class Type { Integer, Float, String, Key, Vector, Rotation, List };
constexpr std::pair<std::string_view, unsigned> event_mask_bits[] = {
    {"state_entry", 30},
    {"state_exit", 1},
    {"touch_start", 21},
    {"touch", 3},
    {"touch_end", 29},
    {"dataserver", 8},
    {"collision", 4},
    {"collision_end", 5},
    {"collision_start", 6},
    {"land_collision", 11},
    {"land_collision_end", 12},
    {"land_collision_start", 13},
    {"control", 7},
    {"listen", 15},
    {"email", 9},
    {"http_response", 10},
    {"remote_data", 23},
    {"attach", 0},
    {"http_request", 38},
    {"changed", 33},
    {"link_message", 34},
    {"linkset_data", 41},
    {"on_rez", 36},
    {"object_rez", 22},
    {"run_time_permissions", 28},
    {"timer", 2},
    {"no_sensor", 35},
    {"sensor", 37},
    {"at_target", 14},
    {"not_at_target", 20},
    {"at_rot_target", 24},
    {"not_at_rot_target", 19},
};
bool is_land_collision_event(std::string_view event) {
    return event == "land_collision_start" || event == "land_collision" || event == "land_collision_end";
}
bool numeric(Type type) {
    return type == Type::Integer || type == Type::Float;
}
struct Token {
    Kind kind;
    std::string text;
    std::int32_t integer = 0;
    std::size_t line = 1, column = 1;
    double real = 0;
};
struct CompileFailure {
    LslDiagnostic diagnostic;
};
[[noreturn]] void fail(const Token& t, std::string message) {
    throw CompileFailure{{t.line, t.column, std::move(message)}};
}
std::size_t utf16_length(std::string_view text) {
    std::size_t n = 0;
    for (unsigned char c : text)
        if ((c & 0xc0) != 0x80)
            n += c >= 0xf0 ? 2 : 1;
    return n;
}
class Lexer {
    std::string_view source;
    std::size_t pos = 0, line = 1, column = 1;
    char take() {
        char c = source[pos++];
        if (c == '\n') {
            line++;
            column = 1;
        } else
            column++;
        return c;
    }

  public:
    explicit Lexer(std::string_view s) : source(s) {
    }
    Token next() {
        for (;;) {
            while (pos < source.size() && static_cast<unsigned char>(source[pos]) <= 32)
                take();
            if (source.substr(pos, 2) == "//") {
                while (pos < source.size() && take() != '\n') {
                }
                continue;
            }
            if (source.substr(pos, 2) == "/*") {
                take();
                take();
                while (pos < source.size() && source.substr(pos, 2) != "*/")
                    take();
                if (pos < source.size()) {
                    take();
                    take();
                }
                continue;
            }
            break;
        }
        Token t{Kind::End, "", 0, line, column};
        if (pos == source.size())
            return t;
        char c = take();
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_') {
            t.kind = Kind::Name;
            t.text += c;
            while (pos < source.size()) {
                c = source[pos];
                if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'))
                    break;
                t.text += take();
            }
            if (t.text.size() > 255)
                fail(t, "name too long, max 255");
            return t;
        }
        if ((c >= '0' && c <= '9') || c == '.') {
            const auto* first = source.data() + pos - 1;
            const auto parsed = std::from_chars(first, source.data() + source.size(), t.real);
            if (std::string_view(first, parsed.ptr - first).find_first_of(".eE") != std::string_view::npos) {
                if (parsed.ec != std::errc{})
                    fail(t, "floating-point literal out of range");
                while (source.data() + pos < parsed.ptr)
                    take();
                t.kind = Kind::Float;
                return t;
            }
        }
        if (c >= '0' && c <= '9') {
            t.kind = Kind::Integer;
            std::uint32_t value = c - '0', base = 10;
            if (c == '0' && pos < source.size() && (source[pos] == 'x' || source[pos] == 'X')) {
                take();
                base = 16;
                value = 0;
            }
            while (pos < source.size()) {
                unsigned char d = source[pos];
                if ((d == 'x' || d == 'X') && value == 0 && base == 10) {
                    take();
                    base = 16;
                    continue;
                }
                unsigned n = d >= '0' && d <= '9'   ? d - '0'
                             : d >= 'a' && d <= 'f' ? d - 'a' + 10
                             : d >= 'A' && d <= 'F' ? d - 'A' + 10
                                                    : 99;
                if (n >= base)
                    break;
                take();
                auto v = std::uint64_t(value) * base + n;
                if (v > 0xffffffffull)
                    fail(t, "integer overflow");
                value = static_cast<std::uint32_t>(v);
            }
            t.integer = std::bit_cast<std::int32_t>(value);
            return t;
        }
        if (c == '"') {
            t.kind = Kind::String;
            auto begin = pos - 1;
            bool escaped = false, closed = false;
            while (pos < source.size()) {
                c = take();
                if (c == '\\' && !escaped) {
                    escaped = true;
                    continue;
                }
                if (c == '"' && !escaped) {
                    closed = true;
                    break;
                }
                if (escaped && c == 'n')
                    t.text += '\n';
                else if (escaped && c == 't')
                    t.text += "    ";
                else
                    t.text += c;
                escaped = false;
            }
            if (!closed)
                fail(t, "unterminated string");
            if (utf16_length(source.substr(begin, pos - begin - 1)) > 4096)
                fail(t, "string too long, max 4096");
            return t;
        }
        if ((c == '&' || c == '|') && pos < source.size() && source[pos] == c) {
            t.kind = Kind::Symbol;
            t.text += c;
            t.text += take();
            if (pos < source.size() && source[pos] == c)
                t.text += take();
            return t;
        }
        if (std::string_view("{}[](),;+-<>*/%=!&|^~@.").find(c) != std::string_view::npos) {
            t.kind = Kind::Symbol;
            t.text += c;
            if (std::string_view("<>+-").find(c) != std::string_view::npos && pos < source.size() &&
                source[pos] == c)
                t.text += take();
            const bool shift = t.text == "<<" || t.text == ">>";
            if ((shift || (t.text.size() == 1 &&
                           std::string_view("<>=!+-*/%&|^").find(c) != std::string_view::npos)) &&
                pos < source.size() && source[pos] == '=')
                t.text += take();
            return t;
        }
        fail(t, "unsupported token in native LSL subset");
    }
};
struct Expression {
    enum class Op {
        Literal,
        ListLiteral,
        ListLength,
        ListStatistics,
        ListSort,
        ListRandomize,
        ListSlice,
        ListStrided,
        ListInsert,
        ListReplace,
        ListDelete,
        ListString,
        ListKey,
        ListInteger,
        ListFloat,
        ListVector,
        ListRotation,
        ListEntryType,
        CsvToList,
        ParseString,
        ParseStringKeepNulls,
        ListJoin,
        ListToJson,
        JsonToList,
        JsonLookup,
        JsonSet,
        ListFind,
        ListFindStrided,
        VectorLiteral,
        Component,
        ColorConversion,
        VectorMagnitude,
        VectorNormalize,
        VectorDistance,
        AxisAngleToRotation,
        RotationToAxis,
        RotationToAngle,
        RotationAngleBetween,
        RotationBasis,
        RotationBetweenVectors,
        EulerToRotation,
        RotationToEuler,
        AxesToRotation,
        Floor,
        Ceil,
        Round,
        Abs,
        Fabs,
        Sqrt,
        Pow,
        SetMemoryLimit,
        ManageEstateAccess,
        GetUsedMemory,
        GetFreeMemory,
        RequestUrl,
        RequestSecureUrl,
        ReleaseUrl,
        FreeUrls,
        SimulatorHostname,
        RegionSize,
        ScriptEngineName,
        GridName,
        GridNick,
        GridInfo,
        EnvironmentValue,
        HttpHeader,
        HttpResponse,
        HttpRequest,
        SendRemoteData,
        HttpContentType,
        ModPow,
        Sin,
        Cos,
        Tan,
        Asin,
        Acos,
        Atan2,
        Log,
        Log10,
        Frand,
        Local,
        ObjectPosition,
        ObjectRootPosition,
        ObjectLocalPosition,
        GeometricCenter,
        CenterOfMass,
        ObjectLocalRotation,
        ObjectRootRotation,
        ObjectWorldRotation,
        ObjectVelocity,
        ObjectOmega,
        ObjectAcceleration,
        ObjectScale,
        GroupMinScaleFactor,
        GroupMaxScaleFactor,
        ScaleGroup,
        ObjectName,
        TextureProperty,
        SetTextureProperty,
        SetLinkedTextureProperty,
        SetTextureAnimation,
        SetParticleSystem,
        SendLinkMessage,
        PhysicsMaterial,
        SetPhysicsMaterial,
        SetBuoyancy,
        SetForce,
        GetForce,
        SetTorque,
        GetTorque,
        SetForceAndTorque,
        SetVelocity,
        SetAngularVelocity,
        ApplyImpulse,
        PushObject,
        ApplyRotationalImpulse,
        SetHover,
        GroundRepel,
        MoveToTarget,
        LookAt,
        ObjectDescription,
        StartString,
        StartParameter,
        ScriptPermissions,
        ScriptPermissionsKey,
        CameraPosition,
        WorldPositionToHud,
        CameraRotation,
        CameraFov,
        CameraAspect,
        AnimationState,
        AnimationList,
        SetRegionPosition,
        ObjectAnimationNames,
        AnimationOverride,
        LinksetDataRead,
        LinksetDataWrite,
        LinksetDataCount,
        LinksetDataAvailable,
        LinksetDataList,
        LinksetDataFind,
        LinksetDataCountFound,
        LinksetDataDeleteFound,
        InventoryType,
        ScriptRunning,
        InventoryDescription,
        InventoryCreator,
        InventoryAcquireTime,
        InventoryKey,
        InventoryPermission,
        ObjectPermission,
        ObjectLinkKey,
        SitTargetAvatar,
        SitFlags,
        SitActiveRange,
        StandTarget,
        InventoryNumber,
        InventoryName,
        ScriptName,
        RegionName,
        RegionAgentCount,
        RegionFlags,
        DerezObject,
        ParcelFlags,
        LandOwnerAt,
        OverMyLand,
        ScriptDanger,
        ParcelDetails,
        ParcelMaxPrims,
        ParcelPrimCount,
        ParcelPrimOwners,
        ParcelMusicUrl,
        ParcelMediaQuery,
        AgentList,
        RegionAgentNames,
        InertiaData,
        AgentIpAddress,
        DrawingCommand,
        ApparentSunHour,
        ApparentTime,
        ApparentTimeString,
        SunParameter,
        EnvironmentQuery,
        ReplaceRegionEnvironment,
        ReplaceParcelEnvironment,
        ReplaceAgentEnvironment,
        RegionCorner,
        RegionTimeDilation,
        RegionFPS,
        SimulatorStats,
        ObjectKey,
        CreatorKey,
        GenerateKey,
        OwnerKey,
        LookupOwnerKey,
        SameGroup,
        IsFriend,
        AgentSize,
        AgentInfo,
        AgentHealth,
        AgentHealRate,
        BoundingBox,
        VisualParams,
        KeyToName,
        NameToKey,
        Username,
        DisplayName,
        RequestUsername,
        RequestDisplayName,
        RequestUserKey,
        NotecardLineSync,
        NotecardLine,
        NotecardLineCount,
        RequestInventoryData,
        RequestAgentData,
        RequestSimulatorData,
        AgentLanguage,
        AttachmentPoint,
        ObjectStatus,
        AttachedList,
        LinkNumber,
        LinkKey,
        LinkName,
        PrimMedia,
        SetPrimMedia,
        ClearPrimMedia,
        LinkSides,
        LinkHasRenderMaterial,
        RenderMaterial,
        PrimCount,
        ObjectPrimCount,
        ObjectMass,
        ScriptMass,
        ScriptMassMks,
        DetectedKey,
        DetectedName,
        DetectedOwner,
        DetectedType,
        DetectedPosition,
        DetectedVelocity,
        DetectedGrab,
        DetectedRotation,
        DetectedGroup,
        DetectedLinkNumber,
        DetectedTouchFace,
        DetectedTouchPos,
        DetectedTouchNormal,
        DetectedTouchBinormal,
        DetectedTouchST,
        DetectedTouchUV,
        Assign,
        Update,
        Negate,
        Not,
        BitNot,
        BitAnd,
        BitOr,
        BitXor,
        ShiftLeft,
        ShiftRight,
        And,
        Or,
        ShortAnd,
        ShortOr,
        Add,
        Subtract,
        Multiply,
        Divide,
        Remainder,
        Equal,
        NotEqual,
        Less,
        LessEqual,
        Greater,
        GreaterEqual,
        StringCast,
        VectorCast,
        RotationCast,
        EncodeBase64,
        DecodeBase64,
        XorBase64,
        EncodeInteger64,
        DecodeInteger64,
        Md5,
        Sha1,
        Sha256,
        ComputeHash,
        Hmac,
        EscapeUrl,
        UnescapeUrl,
        Upper,
        Lower,
        Char,
        Ord,
        Hash,
        StringTrim,
        GetSubstring,
        DeleteSubstring,
        InsertString,
        ReplaceSubstring,
        RegexIsMatch,
        RegexMatchString,
        RegexReplaceString,
        SubstringIndex,
        StringLength,
        Water,
        Ground,
        GroundNormal,
        GroundSlope,
        EdgeOfWorld,
        ModifyLand,
        RegisterTarget,
        RemoveTarget,
        GroundContour,
        Cloud,
        Wind,
        UnixTime,
        Date,
        Timestamp,
        GmtClock,
        Wallclock,
        TimeOfDay,
        Elapsed,
        ElapsedReset,
        LegacyCommand,
        Listen,
        ListenControl,
        ListenRemove,
        RequestEmail,
        SetRemoteScriptPin,
        AttachToAvatar,
        Sensor,
        CreateLink,
        BreakLink,
        DetachFromAvatar,
        RemoteLoadScript,
        SendEmail,
        RegionSayTo
    };
    Op op = Op::Literal;
    unsigned height = 1;
    std::size_t slot = 0;
    bool global = false;
    int delta = 0;
    bool postfix = false;
    Type type = Type::Integer;
    LslValue value = std::int32_t(0);
    std::unique_ptr<Expression> left, right, z, w;
    std::vector<std::unique_ptr<Expression>> elements;
};
struct LegacyCommand {
    std::string_view name;
    std::string_view replacement;
    std::initializer_list<Type> parameters;
    int script_delay = 0;
    bool report_deprecation = true;
};
const LegacyCommand legacy_commands[] = {
    {"llSound", "llPlaySound", {Type::String, Type::Float, Type::Integer, Type::Integer}},
    {"llSoundPreload", "llPreloadSound", {Type::String}},
    {"llMakeExplosion", "llParticleSystem",
     {Type::Integer, Type::Float, Type::Float, Type::Float, Type::Float, Type::String, Type::Vector}, 100},
    {"llMakeFountain", "llParticleSystem",
     {Type::Integer, Type::Float, Type::Float, Type::Float, Type::Float, Type::Integer, Type::String,
      Type::Vector, Type::Float}, 100},
    {"llMakeSmoke", "llParticleSystem",
     {Type::Integer, Type::Float, Type::Float, Type::Float, Type::Float, Type::String, Type::Vector}, 100},
    {"llMakeFire", "llParticleSystem",
     {Type::Integer, Type::Float, Type::Float, Type::Float, Type::Float, Type::String, Type::Vector}, 100},
    {"llTakeCamera", "llSetCameraParams", {Type::String}},
    {"llReleaseCamera", "llClearCameraParams", {Type::String}},
    {"llRemoteLoadScript", "llRemoteLoadScriptPin", {Type::String, Type::String, Type::Integer, Type::Integer}, 3000},
    {"llRemoteDataSetRegion", "llOpenRemoteDataChannel", {}},
    {"llRemoteDataReply", "", {Type::String, Type::String, Type::String, Type::Integer}, 3000, false},
    {"llSetPrimURL", "llSetPrimMediaParams", {Type::String}, 2000},
    {"llRefreshPrimURL", "", {}, 20000},
    {"llCollisionSprite", "", {Type::String}, 0, false},
    {"llPointAt", "", {Type::Vector}, 0, false},
    {"llStopPointAt", "", {}, 0, false},
};
enum class DrawingFormat { Arguments, LineFrom, Polygon, Color, DeprecatedColor };
struct DrawingCommand {
    std::string_view function;
    std::string_view prefix;
    std::initializer_list<Type> parameters;
    std::string_view suffix = "; ";
    DrawingFormat format = DrawingFormat::Arguments;
};
const DrawingCommand drawing_commands[] = {
    {"osDrawResetTransform", "ResetTransf", {}, ";"},
    {"osDrawRotationTransform", "RotTransf ", {Type::Float}, ";"},
    {"osDrawScaleTransform", "ScaleTransf ", {Type::Float, Type::Float}, ";"},
    {"osDrawTranslationTransform", "TransTransf ", {Type::Float, Type::Float}, ";"},
    {"osMovePen", "MoveTo ", {Type::Integer, Type::Integer}, ";"},
    {"osDrawLine", "LineTo ", {Type::Integer, Type::Integer}},
    {"osDrawLine", "MoveTo ", {Type::Integer, Type::Integer, Type::Integer, Type::Integer},
     "; ", DrawingFormat::LineFrom},
    {"osDrawText", "Text ", {Type::String}},
    {"osDrawEllipse", "Ellipse ", {Type::Integer, Type::Integer}},
    {"osDrawFilledEllipse", "FillEllipse ", {Type::Integer, Type::Integer}},
    {"osDrawRectangle", "Rectangle ", {Type::Integer, Type::Integer}},
    {"osDrawFilledRectangle", "FillRectangle ", {Type::Integer, Type::Integer}},
    {"osDrawPolygon", "Polygon ", {Type::List, Type::List}, "; ", DrawingFormat::Polygon},
    {"osDrawFilledPolygon", "FillPolygon ", {Type::List, Type::List}, "; ", DrawingFormat::Polygon},
    {"osSetFontSize", "FontSize ", {Type::Integer}},
    {"osSetFontName", "FontName ", {Type::String}},
    {"osSetPenSize", "PenSize ", {Type::Integer}},
    {"osSetPenColor", "PenColor ", {Type::String}},
    {"osSetPenColor", "PenColor ", {Type::Vector}, "; ", DrawingFormat::Color},
    {"osSetPenColor", "PenColor ", {Type::Vector, Type::Float}, "; ", DrawingFormat::Color},
    {"osSetPenColour", "PenColour ", {Type::String}, "; ", DrawingFormat::DeprecatedColor},
    {"osSetPenCap", "PenCap ", {Type::String, Type::String}},
    {"osDrawImage", "Image ", {Type::Integer, Type::Integer, Type::String}},
};
bool is_drawing_function(std::string_view name) {
    return name.starts_with("os") && std::ranges::any_of(drawing_commands, [name](const auto& command) {
        return command.function == name;
    });
}
bool is_environment_query(std::string_view name) {
    return name == "osGetCurrentSunHour" || name == "osGetApparentTime" ||
           name == "osGetApparentRegionTime" || name == "osGetApparentTimeString" ||
           name == "osGetApparentRegionTimeString" || name == "osGetSunParam" || name == "osSunGetParam";
}
enum class EnvironmentField { DayLength, DayOffset, SunDirection, MoonDirection, SunRotation, MoonRotation };
struct LslEnvironmentQuery {
    std::string_view name;
    EnvironmentField field;
    Type type;
    bool region;
};
constexpr LslEnvironmentQuery lsl_environment_queries[] = {
    {"llGetDayLength", EnvironmentField::DayLength, Type::Integer, false},
    {"llGetRegionDayLength", EnvironmentField::DayLength, Type::Integer, true},
    {"llGetDayOffset", EnvironmentField::DayOffset, Type::Integer, false},
    {"llGetRegionDayOffset", EnvironmentField::DayOffset, Type::Integer, true},
    {"llGetSunDirection", EnvironmentField::SunDirection, Type::Vector, false},
    {"llGetRegionSunDirection", EnvironmentField::SunDirection, Type::Vector, true},
    {"llGetMoonDirection", EnvironmentField::MoonDirection, Type::Vector, false},
    {"llGetRegionMoonDirection", EnvironmentField::MoonDirection, Type::Vector, true},
    {"llGetSunRotation", EnvironmentField::SunRotation, Type::Rotation, false},
    {"llGetRegionSunRotation", EnvironmentField::SunRotation, Type::Rotation, true},
    {"llGetMoonRotation", EnvironmentField::MoonRotation, Type::Rotation, false},
    {"llGetRegionMoonRotation", EnvironmentField::MoonRotation, Type::Rotation, true},
};
bool writable(const Expression& e) {
    return e.op == Expression::Op::Local ||
           (e.op == Expression::Op::Component && e.left->op == Expression::Op::Local);
}
std::unique_ptr<Expression> copy_target(const Expression& e) {
    auto copy = std::make_unique<Expression>();
    copy->op = e.op;
    copy->type = e.type;
    copy->height = e.height;
    copy->slot = e.slot;
    copy->global = e.global;
    if (e.left)
        copy->left = copy_target(*e.left);
    return copy;
}
std::optional<Type> spatial_binary_type(Expression::Op op, Type left, Type right) {
    using Op = Expression::Op;
    if (left == Type::Vector && right == Type::Rotation && (op == Op::Multiply || op == Op::Divide))
        return Type::Vector;
    if (left == Type::Rotation && right == Type::Rotation) {
        if (op == Op::Equal || op == Op::NotEqual)
            return Type::Integer;
        if (op == Op::Add || op == Op::Subtract || op == Op::Multiply || op == Op::Divide)
            return Type::Rotation;
    }
    if (left == Type::Vector && right == Type::Vector) {
        if (op == Op::Add || op == Op::Subtract || op == Op::Remainder)
            return Type::Vector;
        if (op == Op::Multiply)
            return Type::Float;
        if (op == Op::Equal || op == Op::NotEqual)
            return Type::Integer;
    } else if ((left == Type::Vector && numeric(right)) || (right == Type::Vector && numeric(left))) {
        if (op == Op::Multiply || (op == Op::Divide && left == Type::Vector))
            return Type::Vector;
    }
    return {};
}
LslValue default_value(Type type) {
    switch (type) {
    case Type::Integer:
        return std::int32_t{0};
    case Type::Float:
        return 0.;
    case Type::Rotation:
        return LslRotation{};
    case Type::Vector:
        return framework::Vector3d{};
    case Type::List:
        return std::make_shared<const LslList>();
    default:
        return std::string{};
    }
}
struct Instruction {
    bool open_remote_data_channel = false;
    std::unique_ptr<Expression> close_remote_data_channel;
    bool owner = false;
    LslChatType chat_type = LslChatType::Say;
    bool sound = false;
    bool play_sound = false;
    bool trigger_sound = false;
    bool collision_sound = false;
    bool ossl_collision_sound = false;
    std::uint8_t sound_sync_flags = 0;
    bool set_metadata = false;
    bool description = false;
    bool discard = false;
    bool global = false;
    bool reset_time = false;
    bool reset_linkset_data = false;
    bool start_animation = false;
    std::unique_ptr<Expression> object_position;
    std::unique_ptr<Expression> object_rotation;
    std::unique_ptr<Expression> inventory_remove_name;
    std::array<std::unique_ptr<Expression>, 3> inventory_permission_args;
    bool die = false;
    std::array<std::unique_ptr<Expression>, 3> map_arguments;
    bool map_beacon = false;
    std::array<std::unique_ptr<Expression>, 3> load_url_arguments;
    std::array<std::unique_ptr<Expression>, 2> instant_message_arguments;
    bool give_inventory = false;
    std::array<std::unique_ptr<Expression>, 3> give_inventory_list_arguments;
    std::array<std::unique_ptr<Expression>, 4> dialog_arguments;
    std::array<std::unique_ptr<Expression>, 2> object_permission_args;
    bool local_object_rotation = false;
    std::array<std::unique_ptr<Expression>, 5> rez_arguments;
    bool rez_at_root = false;
    std::unique_ptr<Expression> status_mask, status_value;
    std::unique_ptr<Expression> volume_detect;
    std::unique_ptr<Expression> damage;
    std::unique_ptr<Expression> render_material_name, render_material_face;
    std::unique_ptr<Expression> relocate_agent;
    std::array<std::unique_ptr<Expression>, 5> local_teleport_arguments;
    std::vector<std::unique_ptr<Expression>> teleport_arguments;
    bool teleport_owner = false;
    bool teleport_viewer = false;
    bool eject_from_land = false;
    std::unique_ptr<Expression> heal_agent, heal_rate;
    bool volume_detect_part = false;
    enum class InertiaShape { Clear, Principal, Box, Sphere, Cylinder };
    std::optional<InertiaShape> inertia_shape;
    std::vector<std::unique_ptr<Expression>> inertia_arguments;
    bool object_animation = false;
    std::unique_ptr<Expression> animation_avatar;
    bool reset_script = false;
    std::unique_ptr<Expression> unsit_avatar;
    std::unique_ptr<Expression> force_sit_avatar;
    std::unique_ptr<Expression> force_sit_target;
    std::unique_ptr<Expression> region_water_height;
    std::unique_ptr<Expression> parcel_music_url;
    std::unique_ptr<Expression> parcel_media_commands;
    bool estate_sun = false;
    std::unique_ptr<Expression> sun_use_estate, sun_fixed, sun_hour;
    std::unique_ptr<Expression> sit_target_link, sit_target_position, sit_target_rotation;
    std::unique_ptr<Expression> sit_range_link, sit_range_value;
    std::unique_ptr<Expression> stand_target_link, stand_target_value;
    std::unique_ptr<Expression> sit_flags_link, sit_flags_value;
    std::unique_ptr<Expression> seat_camera_link, seat_camera_eye, seat_camera_at, seat_camera_mouselook;
    std::unique_ptr<Expression> script_control_name, script_control_run;
    std::array<std::unique_ptr<Expression>, 3> taken_controls;
    bool release_controls = false;
    bool return_event = false;
    bool jump_when_true = false;
    std::string target_state;
    std::unique_ptr<Expression> channel, text, condition, permission_agent, permission_mask, animation_name,
        override_state, override_animation, sleep, timer, event_delay, scale, click_action, pass_touches, pass_collisions,
        allow_inventory_drop, text_color,
        text_alpha, sound_gain, adjust_sound_gain, sound_radius, preload_sound, sound_queueing, sound_link,
        sound_mode, sound_min,
        sound_max, pay_price, pay_buttons;
    struct CollisionFilterArguments {
        std::unique_ptr<Expression> name;
        std::unique_ptr<Expression> key;
        std::unique_ptr<Expression> accept;
    };
    std::optional<CollisionFilterArguments> collision_filter;
    std::optional<bool> sit_label;
    std::optional<LslParcelListAction> parcel_list_action;
    bool parcel_ban_list = false;
    std::unique_ptr<Expression> parcel_agent, parcel_hours;
    std::optional<std::size_t> jump, store;
    Type store_type = Type::Integer;
    std::array<std::unique_ptr<Expression>, 3> omega;
};
struct Handler {
    std::string parameter;
    std::size_t parameter_count = 0;
    std::vector<Type> local_types;
    std::vector<Instruction> instructions;
};
std::int32_t wrap(std::uint32_t n) {
    return std::bit_cast<std::int32_t>(n);
}
std::string utc_text(std::int64_t ticks, bool timestamp) {
    using namespace std::chrono;
    using Ticks = duration<std::int64_t, std::ratio<1, 10000000>>;
    const sys_time<Ticks> instant{Ticks{ticks - 621355968000000000LL}};
    const auto day = floor<days>(instant);
    const year_month_day date{day};
    const hh_mm_ss time{instant - day};
    char result[40];
    std::snprintf(result, sizeof result, "%04d-%02u-%02u", int(date.year()), unsigned(date.month()),
                  unsigned(date.day()));
    if (timestamp) {
        std::snprintf(result + 10, sizeof result - 10, "T%02d:%02d:%02d.%07dZ", int(time.hours().count()),
                      int(time.minutes().count()), int(time.seconds().count()),
                      int(time.subseconds().count()));
    }
    return result;
}
double seconds_of_day(std::int64_t ticks, bool local) {
    using namespace std::chrono;
    using Ticks = duration<std::int64_t, std::ratio<1, 10000000>>;
    const sys_time<Ticks> instant{Ticks{ticks - 621355968000000000LL}};
    if (!local)
        return duration<double>(instant - floor<days>(instant)).count();

    const auto whole = floor<seconds>(instant);
    const auto epoch = static_cast<std::time_t>(whole.time_since_epoch().count());
    std::tm time{};
#ifdef _WIN32
    if (localtime_s(&time, &epoch) != 0)
        throw std::runtime_error("LSL local time is out of range");
#else
    if (!localtime_r(&epoch, &time))
        throw std::runtime_error("LSL local time is out of range");
#endif
    return time.tm_hour * 3600 + time.tm_min * 60 + time.tm_sec + duration<double>(instant - whole).count();
}
std::int64_t ticks();
std::string sha512_text(std::string_view text, bool sha384 = false) {
    constexpr std::array<std::uint64_t, 80> constants{
        0x428a2f98d728ae22ull, 0x7137449123ef65cdull, 0xb5c0fbcfec4d3b2full, 0xe9b5dba58189dbbcull,
        0x3956c25bf348b538ull, 0x59f111f1b605d019ull, 0x923f82a4af194f9bull, 0xab1c5ed5da6d8118ull,
        0xd807aa98a3030242ull, 0x12835b0145706fbeull, 0x243185be4ee4b28cull, 0x550c7dc3d5ffb4e2ull,
        0x72be5d74f27b896full, 0x80deb1fe3b1696b1ull, 0x9bdc06a725c71235ull, 0xc19bf174cf692694ull,
        0xe49b69c19ef14ad2ull, 0xefbe4786384f25e3ull, 0x0fc19dc68b8cd5b5ull, 0x240ca1cc77ac9c65ull,
        0x2de92c6f592b0275ull, 0x4a7484aa6ea6e483ull, 0x5cb0a9dcbd41fbd4ull, 0x76f988da831153b5ull,
        0x983e5152ee66dfabull, 0xa831c66d2db43210ull, 0xb00327c898fb213full, 0xbf597fc7beef0ee4ull,
        0xc6e00bf33da88fc2ull, 0xd5a79147930aa725ull, 0x06ca6351e003826full, 0x142929670a0e6e70ull,
        0x27b70a8546d22ffcull, 0x2e1b21385c26c926ull, 0x4d2c6dfc5ac42aedull, 0x53380d139d95b3dfull,
        0x650a73548baf63deull, 0x766a0abb3c77b2a8ull, 0x81c2c92e47edaee6ull, 0x92722c851482353bull,
        0xa2bfe8a14cf10364ull, 0xa81a664bbc423001ull, 0xc24b8b70d0f89791ull, 0xc76c51a30654be30ull,
        0xd192e819d6ef5218ull, 0xd69906245565a910ull, 0xf40e35855771202aull, 0x106aa07032bbd1b8ull,
        0x19a4c116b8d2d0c8ull, 0x1e376c085141ab53ull, 0x2748774cdf8eeb99ull, 0x34b0bcb5e19b48a8ull,
        0x391c0cb3c5c95a63ull, 0x4ed8aa4ae3418acbull, 0x5b9cca4f7763e373ull, 0x682e6ff3d6b2b8a3ull,
        0x748f82ee5defb2fcull, 0x78a5636f43172f60ull, 0x84c87814a1f0ab72ull, 0x8cc702081a6439ecull,
        0x90befffa23631e28ull, 0xa4506cebde82bde9ull, 0xbef9a3f7b2c67915ull, 0xc67178f2e372532bull,
        0xca273eceea26619cull, 0xd186b8c721c0c207ull, 0xeada7dd6cde0eb1eull, 0xf57d4f7fee6ed178ull,
        0x06f067aa72176fbaull, 0x0a637dc5a2c898a6ull, 0x113f9804bef90daeull, 0x1b710b35131c471bull,
        0x28db77f523047d84ull, 0x32caab7b40c72493ull, 0x3c9ebe0a15c9bebcull, 0x431d67c49c100d4cull,
        0x4cc5d4becb3e42b6ull, 0x597f299cfc657e2aull, 0x5fcb6fab3ad6faecull, 0x6c44198c4a475817ull};
    std::array<std::uint64_t, 8> state{0x6a09e667f3bcc908ull, 0xbb67ae8584caa73bull, 0x3c6ef372fe94f82bull,
                                       0xa54ff53a5f1d36f1ull, 0x510e527fade682d1ull, 0x9b05688c2b3e6c1full,
                                       0x1f83d9abfb41bd6bull, 0x5be0cd19137e2179ull};
    if (sha384)
        state = {0xcbbb9d5dc1059ed8ull, 0x629a292a367cd507ull, 0x9159015a3070dd17ull, 0x152fecd8f70e5939ull,
                 0x67332667ffc00b31ull, 0x8eb44a8768581511ull, 0xdb0c2e0d64f98fa7ull, 0x47b5481dbefa4fa4ull};
    const auto compress = [&](std::string_view block) {
        std::array<std::uint64_t, 80> words{};
        for (std::size_t i = 0; i < 16; ++i)
            for (std::size_t byte = 0; byte < 8; ++byte)
                words[i] = (words[i] << 8) | static_cast<unsigned char>(block[8 * i + byte]);
        for (std::size_t i = 16; i < words.size(); ++i) {
            const auto x = words[i - 15], y = words[i - 2];
            const auto low = std::rotr(x, 1) ^ std::rotr(x, 8) ^ (x >> 7);
            const auto high = std::rotr(y, 19) ^ std::rotr(y, 61) ^ (y >> 6);
            words[i] = words[i - 16] + low + words[i - 7] + high;
        }
        auto [a, b, c, d, e, f, g, h] = state;
        for (std::size_t i = 0; i < words.size(); ++i) {
            const auto high = std::rotr(e, 14) ^ std::rotr(e, 18) ^ std::rotr(e, 41);
            const auto choice = (e & f) ^ (~e & g);
            const auto t1 = h + high + choice + constants[i] + words[i];
            const auto low = std::rotr(a, 28) ^ std::rotr(a, 34) ^ std::rotr(a, 39);
            const auto majority = (a & b) ^ (a & c) ^ (b & c);
            h = g;
            g = f;
            f = e;
            e = d + t1;
            d = c;
            c = b;
            b = a;
            a = t1 + low + majority;
        }
        const std::array working{a, b, c, d, e, f, g, h};
        for (std::size_t i = 0; i < state.size(); ++i)
            state[i] += working[i];
    };
    const auto bits = static_cast<std::uint64_t>(text.size()) * 8;
    while (text.size() >= 128) {
        compress(text.substr(0, 128));
        text.remove_prefix(128);
    }
    std::array<char, 256> tail{};
    std::copy(text.begin(), text.end(), tail.begin());
    tail[text.size()] = static_cast<char>(0x80);
    const std::size_t size = text.size() < 112 ? 128 : 256;
    for (std::size_t i = 0; i < 8; ++i)
        tail[size - 1 - i] = static_cast<char>((bits >> (8 * i)) & 0xff);
    for (std::size_t i = 0; i < size; i += 128)
        compress(std::string_view(tail.data() + i, 128));
    constexpr std::string_view digits = "0123456789abcdef";
    std::string result;
    result.reserve(128);
    for (auto word : state)
        for (int shift = 60; shift >= 0; shift -= 4)
            result += digits[(word >> shift) & 15];
    if (sha384)
        result.resize(96);
    return result;
}
framework::DotnetRegexNodeText regex_text(std::string_view text) {
    return std::make_shared<const std::u16string>(framework::utf16_replacing(text));
}

std::optional<std::int32_t> regex_start(std::size_t length, std::int32_t start) {
    if (length > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max()))
        return std::nullopt;
    const auto size = static_cast<std::int32_t>(length);
    if (start < 0)
        start += size;
    if (start < 0 || start >= size)
        return std::nullopt;
    return start;
}

std::string replace_regex(const framework::DotnetRegexNodeText& input, std::string_view pattern,
                          std::string_view replacement, std::int32_t count, std::int32_t position,
                          std::int32_t options) {
    using namespace framework;
    const auto expression = regex_text(pattern);
    const auto length = static_cast<std::int32_t>(input->size());
    const bool reverse = (options & 64) != 0;
    const auto culture = []() { return std::u16string{}; };
    const auto stack = []() { return sufficient_execution_stack(); };
    auto parsed = dotnet_regex_parse(expression, options, culture, stack);
    const auto rules = dotnet_regex_parse_replacement(
        regex_text(replacement), options, parsed.capture_number_sparse_mapping, parsed.capture_count,
        parsed.capture_name_to_number, culture, stack);
    DotnetRegexInterpreter regex(std::move(parsed), expression, stack);
    struct Edit {
        std::int32_t index, length;
        std::u16string text;
    };
    std::vector<Edit> edits;
    int previous = -1;
    for (std::int64_t replaced = 0; count == -1 || replaced < count; ++replaced) {
        const auto match = regex.scan(input, 0, length, position, previous,
                                      DotnetRegexRunnerMode::FullMatchRequired, 100000);
        if (!match.success)
            break;
        Edit edit{match.index, match.length, {}};
        for (const auto rule : rules.rules) {
            if (rule >= 0) {
                edit.text += *rules.strings.at(rule);
                continue;
            }
            const auto slot = -5 - rule;
            if (slot == -1)
                edit.text.append(*input, 0, match.index);
            else if (slot == -2)
                edit.text.append(*input, match.index + match.length);
            else if (slot == -4)
                edit.text += *input;
            else if (!match.groups.empty()) {
                const auto group = slot == -3 ? match.groups.size() - 1 : static_cast<std::size_t>(slot);
                if (group < match.groups.size() && match.groups[group].success)
                    edit.text.append(*input, match.groups[group].index, match.groups[group].length);
            }
        }
        edits.push_back(std::move(edit));
        position = match.text_position;
        previous = match.length;
    }
    if (reverse)
        std::reverse(edits.begin(), edits.end());
    std::u16string result;
    std::size_t cursor = 0;
    for (const auto& edit : edits) {
        result.append(*input, cursor, edit.index - cursor);
        result += edit.text;
        cursor = static_cast<std::size_t>(edit.index) + edit.length;
    }
    result.append(*input, cursor);
    return utf8(result);
}

std::string replace_substring(const std::string& source, const std::string& pattern,
                              const std::string& replacement, std::int32_t count) {
    if (source.empty() || pattern.empty() || count == std::numeric_limits<std::int32_t>::min())
        return source;
    try {
        const auto input = regex_text(source);
        if (input->size() > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max()))
            return source;
        const bool reverse = count < 0;
        const auto position = reverse ? static_cast<std::int32_t>(input->size()) : 0;
        return replace_regex(input, pattern, replacement, count == 0 ? -1 : std::abs(count),
                             position, 512 | (reverse ? 64 : 0));
    } catch (const std::exception&) {
        return source;
    }
}

bool truth(const LslValue& value, Type type) {
    if (type == Type::Key) {
        framework::Uuid id;
        return framework::try_parse_sdk_uuid(std::get<std::string>(value), id) && !id.is_zero();
    }
    return std::visit(
        [](const auto& item) {
            if constexpr (std::is_same_v<std::decay_t<decltype(item)>, std::string>)
                return !item.empty();
            else if constexpr (std::is_same_v<std::decay_t<decltype(item)>, framework::Vector3d>)
                return item.x != 0 || item.y != 0 || item.z != 0;
            else if constexpr (std::is_same_v<std::decay_t<decltype(item)>, std::shared_ptr<const LslList>>)
                return !item->items.empty();
            else if constexpr (std::is_same_v<std::decay_t<decltype(item)>, LslRotation>)
                return item != LslRotation{};
            else
                return item != 0;
        },
        value);
}
void append_list_range(LslList& result, const LslList& list, std::int64_t first, std::int64_t last) {
    first = std::max<std::int64_t>(0, first);
    last = std::min(static_cast<std::int64_t>(list.items.size()), last);
    if (first < last)
        result.items.insert(result.items.end(), list.items.begin() + first, list.items.begin() + last);
}
void append_list_slice(LslList& result, const LslList& list, std::int64_t start, std::int64_t end) {
    const auto size = static_cast<std::int64_t>(list.items.size());
    if (start < 0)
        start += size;
    if (end < 0)
        end += size;
    if (start <= end)
        append_list_range(result, list, start, end + 1);
    else if (start < 0)
        append_list_range(result, list, 0, size);
    else {
        append_list_range(result, list, 0, end + 1);
        append_list_range(result, list, start, size);
    }
}
std::int32_t list_integer(const LslValue& item) {
    if (const auto* value = std::get_if<std::int32_t>(&item))
        return *value;
    if (const auto* value = std::get_if<double>(&item)) {
        const auto truncated = std::trunc(*value);
        if (!std::isfinite(truncated) || truncated < -2147483648. || truncated >= 2147483648.)
            return std::numeric_limits<std::int32_t>::min();
        return static_cast<std::int32_t>(truncated);
    }
    const auto* text = std::get_if<std::string>(&item);
    if (!text)
        return 0;
    std::string_view digits = *text;
    const auto start = digits.find_first_not_of(' ');
    if (start == digits.npos)
        return 0;
    digits.remove_prefix(start);
    const bool hex = digits.starts_with("0x") || digits.starts_with("0X");
    const bool negative = digits.starts_with("-");
    if (hex)
        digits.remove_prefix(2);
    else if (negative || digits.starts_with("+"))
        digits.remove_prefix(1);
    std::uint32_t number = 0;
    const auto parsed = std::from_chars(digits.data(), digits.data() + digits.size(), number, hex ? 16 : 10);
    if (parsed.ec == std::errc::result_out_of_range || (!hex && number > 2147483647u))
        return -1;
    if (parsed.ec != std::errc{})
        return 0;
    if (hex)
        return wrap(number);
    return negative ? -static_cast<std::int32_t>(number) : static_cast<std::int32_t>(number);
}
double list_float(const LslValue& item) {
    if (const auto* value = std::get_if<double>(&item))
        return *value;
    if (const auto* value = std::get_if<std::int32_t>(&item))
        return *value;
    const auto* text = std::get_if<std::string>(&item);
    if (!text)
        return 0.;
    framework::Uuid key;
    if (framework::try_parse_sdk_uuid(*text, key))
        return list_integer(item);
    const auto units = framework::utf16_replacing(*text);
    std::size_t index = 0;
    while (index < units.size() && framework::unicode_whitespace(units[index]))
        ++index;
    const auto start = index;
    if (index < units.size() && units[index] == u'-')
        ++index;
    if (index < units.size() && units[index] == u'+')
        ++index;
    const auto digit = [](char16_t c) { return c >= u'0' && c <= u'9'; };
    const auto integer_start = index;
    while (index < units.size() && (digit(units[index]) || units[index] == u','))
        ++index;
    if (index == integer_start)
        return 0.;
    if (index < units.size() && units[index] == u'.') {
        ++index;
        while (index < units.size() && digit(units[index]))
            ++index;
    }
    double value = 0.;
    return network::try_parse_invariant_real(
               framework::utf8(std::u16string_view(units).substr(start, index - start)), value)
               ? value
               : 0.;
}
framework::Vector3d list_vector(const LslValue& item) {
    if (const auto* vector = std::get_if<framework::Vector3d>(&item))
        return *vector;
    const auto* text = std::get_if<std::string>(&item);
    if (!text || text->size() < 5)
        return {};
    std::string_view input = *text;
    const auto opening = input.find_first_of("<,");
    if (opening == input.npos)
        return {};
    if (input[opening] == '<')
        input.remove_prefix(opening + 1);
    framework::Vector3d result;
    for (auto* component : {&result.x, &result.y}) {
        const auto comma = input.find(',');
        if (comma == input.npos || !network::try_parse_invariant_real(input.substr(0, comma), *component))
            return {};
        input.remove_prefix(comma + 1);
    }
    if (!network::try_parse_invariant_real(input.substr(0, input.find('>')), result.z))
        return {};
    return result;
}
LslRotation list_rotation(const LslValue& item) {
    if (const auto* rotation = std::get_if<LslRotation>(&item))
        return *rotation;
    const auto* text = std::get_if<std::string>(&item);
    if (!text || framework::utf16(*text).size() < 7)
        return {};
    std::string_view input = *text;
    const auto opening = input.find_first_of("<,");
    if (opening == input.npos)
        return {};
    if (input[opening] == '<')
        input.remove_prefix(opening + 1);
    LslRotation result;
    for (auto* component : {&result.x, &result.y, &result.z}) {
        const auto comma = input.find(',');
        if (comma == input.npos || !network::try_parse_invariant_real(input.substr(0, comma), *component))
            return {};
        input.remove_prefix(comma + 1);
    }
    if (!network::try_parse_invariant_real(input.substr(0, input.find('>')), result.w))
        return {};
    return result;
}
std::optional<LslAvatarName> parse_avatar_name(std::string_view text) {
    const auto name = framework::utf16_replacing(text);
    const auto first_start = name.find_first_not_of(u' ');
    if (first_start == std::u16string::npos)
        return {};
    const auto first_end = name.find_first_of(u" .@", first_start);
    if (first_end == std::u16string::npos || first_end == first_start || name[first_end] == u'@')
        return {};
    const auto first = name.substr(first_start, first_end - first_start);
    auto last_start = name.find_first_not_of(u' ', first_end + 1);
    if (last_start == std::u16string::npos)
        return {};

    const bool first_separator_is_dot = name[first_end] == u'.';
    for (auto index = last_start; index < name.size(); ++index) {
        if (name[index] == u'.') {
            if (first_separator_is_dot || index + 1 == name.size())
                return {};
            last_start = index + 1;
        } else if (name[index] == u'@') {
            const auto server_start = name.find_first_not_of(u' ', index + 1);
            if (server_start == std::u16string::npos || name.size() - server_start < 3)
                return {};
            auto last_end = index;
            while (last_end > last_start && name[last_end - 1] == u' ')
                --last_end;
            if (last_end == last_start)
                return {};
            const auto last = name.substr(last_start, last_end - last_start);
            auto server_end = name.size();
            while (server_end > server_start && framework::unicode_whitespace(name[server_end - 1]))
                --server_end;
            return LslAvatarName{framework::utf8(first), framework::utf8(last),
                                 framework::utf8(name.substr(server_start, server_end - server_start))};
        }
    }
    auto last_end = name.size();
    while (last_end > last_start && framework::unicode_whitespace(name[last_end - 1]))
        --last_end;
    if (last_end == last_start)
        return {};
    return LslAvatarName{framework::utf8(first),
                         framework::utf8(name.substr(last_start, last_end - last_start)), {}};
}

std::string username_from_name(std::string_view text) {
    auto name = framework::utf16_replacing(text);
    const auto first_end = std::find_if(name.begin(), name.end(), framework::unicode_whitespace);
    if (first_end != name.end()) {
        const auto second_end = std::find_if(first_end + 1, name.end(), framework::unicode_whitespace);
        if (std::u16string(first_end + 1, second_end) == u"Resident")
            name.erase(first_end, name.end());
        else
            std::replace(name.begin(), name.end(), u' ', u'.');
    }
    return framework::utf8(framework::invariant_lower_utf16(name));
}

std::string value_text(const LslValue& v) {
    if (auto i = std::get_if<std::int32_t>(&v))
        return std::to_string(*i);
    if (auto real = std::get_if<double>(&v)) {
        char buffer[384];
        const auto result = std::to_chars(buffer, buffer + sizeof buffer, *real, std::chars_format::fixed, 6);
        return std::string(buffer, result.ptr);
    }
    if (const auto* vector = std::get_if<framework::Vector3d>(&v)) {
        std::string result = "<";
        for (double component : {vector->x, vector->y, vector->z}) {
            if (result.size() > 1)
                result += ", ";
            char buffer[384];
            const auto converted =
                std::to_chars(buffer, buffer + sizeof buffer, component, std::chars_format::fixed, 6);
            result.append(buffer, converted.ptr);
        }
        return result + ">";
    }
    if (const auto* rotation = std::get_if<LslRotation>(&v))
        return "<" + value_text(rotation->x) + ", " + value_text(rotation->y) + ", " +
               value_text(rotation->z) + ", " + value_text(rotation->w) + ">";
    return std::get<std::string>(v);
}
std::int32_t media_rule_integer(const LslValue& value) {
    if (std::holds_alternative<std::int32_t>(value) || std::holds_alternative<double>(value))
        return list_integer(value);
    if (const auto* text = std::get_if<std::string>(&value)) {
        constexpr auto whitespace = " \t\r\n\v\f";
        const auto begin = text->find_first_not_of(whitespace);
        if (begin != std::string::npos) {
            const auto end = text->find_last_not_of(whitespace) + 1;
            const auto* first = text->data() + begin;
            const auto* last = text->data() + end;
            if (*first == '+' && ++first != last && *first == '-')
                throw std::runtime_error("Expected an integer media rule");
            std::int32_t code;
            const auto parsed = std::from_chars(first, last, code);
            if (parsed.ec == std::errc{} && parsed.ptr == last)
                return code;
        }
    }
    throw std::runtime_error("Expected an integer media rule");
}
bool apply_media_rules(framework::ShapeMediaEntry& media, const LslList& rules) {
    for (std::size_t i = 1; i < rules.items.size(); i += 2) {
        const auto& value = rules.items[i];
        switch (media_rule_integer(rules.items[i - 1])) {
        case 0:
            media.enable_alternative_image = media_rule_integer(value) != 0;
            break;
        case 1:
            media.controls = media_rule_integer(value) != 0;
            break;
        case 2:
            media.current_url = framework::utf16_replacing(value_text(value));
            break;
        case 3:
            media.home_url = framework::utf16_replacing(value_text(value));
            break;
        case 4:
            media.auto_loop = media_rule_integer(value) != 0;
            break;
        case 5:
            media.auto_play = media_rule_integer(value) != 0;
            break;
        case 6:
            media.auto_scale = media_rule_integer(value) != 0;
            break;
        case 7:
            media.auto_zoom = media_rule_integer(value) != 0;
            break;
        case 8:
            media.interact_on_first_click = media_rule_integer(value) != 0;
            break;
        case 9:
            media.width = media_rule_integer(value);
            break;
        case 10:
            media.height = media_rule_integer(value);
            break;
        case 11:
            media.enable_whitelist = media_rule_integer(value) != 0;
            break;
        case 12: {
            const auto text = value_text(value);
            std::vector<std::optional<std::u16string>> urls;
            for (const auto item : std::string_view(text) | std::views::split(','))
                urls.emplace_back(framework::utf16_replacing(framework::trim_utf8(
                    std::string_view(item.begin(), item.end()))));
            if (urls.empty())
                urls.emplace_back(u"");
            media.whitelist = std::make_shared<framework::ShapeArray<std::optional<std::u16string>>>(std::move(urls));
            break;
        }
        case 13:
            media.interact_permissions = static_cast<std::uint8_t>(media_rule_integer(value));
            break;
        case 14:
            media.control_permissions = static_cast<std::uint8_t>(media_rule_integer(value));
            break;
        default:
            return false;
        }
    }
    return true;
}
std::string drawing_number(double value) {
    if (std::isnan(value))
        return "NaN";
    if (std::isinf(value))
        return std::signbit(value) ? "-Infinity" : "Infinity";

    // .NET's custom numeric format rounds the 15 significant decimal digits
    // of a double, then rounds again at the sixth fractional digit.
    char buffer[32];
    const auto encoded = std::to_chars(buffer, buffer + sizeof buffer, std::abs(value),
                                       std::chars_format::scientific, 14);
    const std::string scientific(buffer, encoded.ptr);
    const auto exponent_marker = scientific.find('e');
    auto digits = scientific.substr(0, exponent_marker);
    std::erase(digits, '.');
    const int keep = std::stoi(scientific.substr(exponent_marker + 1)) + 7;
    const auto retained = static_cast<std::size_t>(std::max(keep, 0));
    const bool round_up = keep >= 0 && retained < digits.size() && digits[retained] >= '5';
    digits.resize(retained, '0');
    if (round_up) {
        auto digit = digits.rbegin();
        while (digit != digits.rend() && *digit == '9') {
            *digit = '0';
            ++digit;
        }
        if (digit == digits.rend())
            digits.insert(0, 1, '1');
        else
            ++*digit;
    }
    if (digits.size() < 7)
        digits.insert(0, 7 - digits.size(), '0');
    digits.insert(digits.size() - 6, 1, '.');
    if (std::signbit(value))
        digits.insert(0, 1, '-');
    return digits;
}
std::string drawing_coordinate(const LslValue& value) {
    if (const auto* real = std::get_if<double>(&value))
        return drawing_number(*real);
    if (const auto* vector = std::get_if<framework::Vector3d>(&value))
        return "<" + drawing_number(vector->x) + ", " + drawing_number(vector->y) + ", " +
               drawing_number(vector->z) + ">";
    if (const auto* rotation = std::get_if<LslRotation>(&value))
        return "<" + drawing_number(rotation->x) + ", " + drawing_number(rotation->y) + ", " +
               drawing_number(rotation->z) + ", " + drawing_number(rotation->w) + ">";
    return value_text(value);
}
std::string json_marker(char16_t marker) {
    return framework::utf8(std::u16string_view(&marker, 1));
}
std::string quote_lsl_json(std::string_view input) {
    std::string result(1, '"');
    constexpr std::string_view hex = "0123456789ABCDEF";
    for (unsigned char ch : input) {
        switch (ch) {
        case '"':
        case '\\':
        case '/':
            result += '\\';
            result += static_cast<char>(ch);
            break;
        case '\b':
            result += "\\b";
            break;
        case '\t':
            result += "\\t";
            break;
        case '\n':
            result += "\\n";
            break;
        case '\f':
            result += "\\f";
            break;
        case '\r':
            result += "\\r";
            break;
        default:
            if (ch < 32) {
                result += "\\u00";
                result += hex[ch >> 4];
                result += hex[ch & 15];
            } else
                result += static_cast<char>(ch);
        }
    }
    return result + '"';
}
std::string lsl_json_item(const LslValue& item) {
    if (const auto* real = std::get_if<double>(&item)) {
        if (std::isinf(*real))
            return "\"Inf\"";
        if (std::isnan(*real))
            return "\"NaN\"";
        char buffer[64];
        const auto result = std::to_chars(buffer, buffer + sizeof buffer, *real);
        return {buffer, result.ptr};
    }
    if (std::holds_alternative<std::int32_t>(item))
        return value_text(item);
    if (const auto* text = std::get_if<std::string>(&item)) {
        auto units = framework::utf16(*text);
        std::u16string_view trimmed = units;
        while (!trimmed.empty() && framework::unicode_whitespace(trimmed.front()))
            trimmed.remove_prefix(1);
        while (!trimmed.empty() && framework::unicode_whitespace(trimmed.back()))
            trimmed.remove_suffix(1);
        const auto value = framework::utf8(trimmed);
        if (!value.empty() && (value.front() == '{' || value.front() == '['))
            return value;
        if (value == "true" || value == json_marker(0xFDD6))
            return "true";
        if (value == "false" || value == json_marker(0xFDD7))
            return "false";
        if (value == "null" || value == json_marker(0xFDD5))
            return "null";
        return quote_lsl_json(value);
    }
    if (std::holds_alternative<std::shared_ptr<const LslList>>(item))
        return {};
    return quote_lsl_json(value_text(item));
}
std::string compute_hash(std::string_view source, std::string_view algorithm) {
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(source.data());
    if (algorithm == "md5")
        return framework::md5(std::string(source));
    if (algorithm == "sha1")
        return framework::sha1_hex({bytes, source.size()});
    if (algorithm == "md5_sha1")
        return framework::md5(std::string(source)) + framework::sha1_hex({bytes, source.size()});
    if (algorithm == "sha224" || algorithm == "sha256")
        return framework::sha256_hex(source, algorithm == "sha224");
    if (algorithm == "sha384" || algorithm == "sha512")
        return sha512_text(source, algorithm == "sha384");
    return {};
}
std::string digest_bytes(std::string_view hex) {
    std::string result;
    const auto digit = [](char c) { return c <= '9' ? c - '0' : c - 'a' + 10; };
    for (std::size_t i = 0; i < hex.size(); i += 2)
        result += static_cast<char>(digit(hex[i]) * 16 + digit(hex[i + 1]));
    return result;
}
template <class Value> decltype(auto) component_at(Value& value, std::size_t slot) {
    if (auto* rotation = std::get_if<LslRotation>(&value)) {
        switch (slot) {
        case 0:
            return (rotation->x);
        case 1:
            return (rotation->y);
        case 2:
            return (rotation->z);
        default:
            return (rotation->w);
        }
    }
    auto& vector = std::get<framework::Vector3d>(value);
    switch (slot) {
    case 0:
        return (vector.x);
    case 1:
        return (vector.y);
    default:
        return (vector.z);
    }
}
struct ScriptTermination {
    LslRunStatus status;
};
class OsslPermissionError final : public std::runtime_error {
public:
    explicit OsslPermissionError(const std::string& reason) : std::runtime_error("OSSL Permission Error: " + reason) {}
};
class OsslRuntimeError final : public std::runtime_error {
public:
    explicit OsslRuntimeError(const std::string& reason) : std::runtime_error("OSSL Runtime Error: " + reason) {}
};

framework::Vector3d rotate_vector(framework::Vector3d vector, LslRotation rotation) {
    const auto [x, y, z, w] = rotation;
    const auto rx = w * vector.x + y * vector.z - z * vector.y;
    const auto ry = w * vector.y + z * vector.x - x * vector.z;
    const auto rz = w * vector.z + x * vector.y - y * vector.x;
    return {vector.x + 2.0 * (rz * y - ry * z),
            vector.y + 2.0 * (rx * z - rz * x),
            vector.z + 2.0 * (ry * x - rx * y)};
}
LslRotation axes_to_rotation(framework::Vector3d forward, framework::Vector3d left, framework::Vector3d up) {
    const auto trace = forward.x + left.y + up.z + 1.0;
    if (trace >= 1.0) {
        const auto scale = 0.5 / std::sqrt(trace);
        return LslRotation{(left.z - up.y) * scale, (up.x - forward.z) * scale,
                           (forward.y - left.x) * scale, 0.25 / scale};
    }
    const auto largest = left.y > up.z ? left.y : up.z;
    if (largest < forward.x) {
        const auto root = std::sqrt(forward.x - (left.y + up.z) + 1.0);
        const auto scale = 0.5 / root;
        return LslRotation{root * 0.5, (forward.y + left.x) * scale, (up.x + forward.z) * scale,
                           (left.z - up.y) * scale};
    }
    if (largest == left.y) {
        const auto root = std::sqrt(left.y - (up.z + forward.x) + 1.0);
        const auto scale = 0.5 / root;
        return LslRotation{(forward.y + left.x) * scale, root * 0.5, (left.z + up.y) * scale,
                           (up.x - forward.z) * scale};
    }
    const auto root = std::sqrt(up.z - (forward.x + left.y) + 1.0);
    const auto scale = 0.5 / root;
    return LslRotation{(up.x + forward.z) * scale, (left.z + up.y) * scale, root * 0.5,
                       (forward.y - left.x) * scale};
}
struct ParticleRule {
    std::string_view name;
    std::string_view expected_type;
};
constexpr ParticleRule particle_rules[] = {
    {"PSYS_PART_FLAGS", "integer"},
    {"PSYS_PART_START_COLOR", "vector"},
    {"PSYS_PART_START_ALPHA", "float"},
    {"PSYS_PART_END_COLOR", "vector"},
    {"PSYS_PART_END_ALPHA", "float"},
    {"PSYS_PART_START_SCALE", "vector"},
    {"PSYS_PART_END_SCALE", "vector"},
    {"PSYS_PART_MAX_AGE", "float"},
    {"PSYS_SRC_ACCEL", "vector"},
    {"PSYS_SRC_PATTERN", "integer"},
    {"PSYS_SRC_INNERANGLE", "float"},
    {"PSYS_SRC_OUTERANGLE", "float"},
    {"PSYS_SRC_TEXTURE", "string or key"},
    {"PSYS_SRC_BURST_RATE", "float"},
    {},
    {"PSYS_SRC_BURST_PART_COUNT", "integer"},
    {"PSYS_SRC_BURST_RADIUS", "float"},
    {"PSYS_SRC_BURST_SPEED_MIN", "float"},
    {"PSYS_SRC_BURST_SPEED_MAX", "float"},
    {"PSYS_SRC_MAX_AGE", "float"},
    {"PSYS_SRC_TARGET_KEY", {}},
    {"PSYS_SRC_OMEGA", "vector"},
    {"PSYS_SRC_ANGLE_BEGIN", "float"},
    {"PSYS_SRC_ANGLE_END", "float"},
    {"PSYS_PART_BLEND_FUNC_SOURCE", "integer"},
    {"PSYS_PART_BLEND_FUNC_DEST", "integer"},
    {"PSYS_PART_START_GLOW", "float"},
    {"PSYS_PART_END_GLOW", "float"},
};

struct Evaluator {
    std::vector<LslValue>& locals;
    std::vector<LslValue>& globals;
    LslValue& variable(const Expression& e) const {
        return (e.global ? globals : locals).at(e.slot);
    }
    const LslEvent* event;
    const LslContext& context;
    const LslHost& host;
    LslRotation object_rotation(LslRotationSpace space) const {
        if (host.object_rotation) {
            if (const auto rotation = host.object_rotation(context.object, space))
                return *rotation;
        }
        if (space == LslRotationSpace::Local)
            return context.local_rotation;
        if (space == LslRotationSpace::Root)
            return context.root_rotation.value_or(context.local_rotation);
        if (context.world_rotation)
            return *context.world_rotation;
        const auto root = context.root_rotation.value_or(context.local_rotation);
        if (!context.root_position)
            return root;
        const auto& local = context.local_rotation;
        return {root.w * local.x + root.x * local.w + root.y * local.z - root.z * local.y,
                root.w * local.y + root.y * local.w + root.z * local.x - root.x * local.z,
                root.w * local.z + root.z * local.w + root.x * local.y - root.y * local.x,
                root.w * local.w - root.x * local.x - root.y * local.y - root.z * local.z};
    }
    double& writable_component(const Expression& target) const {
        return component_at(variable(*target.left), target.slot);
    }
    std::int64_t& timer_start;
    std::function<void(std::int32_t)> delay;
    std::function<std::int32_t()> heap_bytes_used;
    std::function<void(const LslValue&, const LslValue&, Type, bool)> check_heap_store;
    void check_ossl_permission(std::string_view function) const {
        const auto reason = host.ossl_permission_error
            ? host.ossl_permission_error(context.object, context.item, function)
            : std::string(function) + " permission denied";
        if (!reason.empty())
            throw OsslPermissionError(reason);
    }
    void check_safe_ossl_permission() const {
        if (!host.ossl_enabled)
            throw OsslRuntimeError("permission denied. All unsafe OSSL funtions disabled");
    }
    void script_error(std::string message, std::int32_t sleep_ms = 1000,
                      LslChatType listener_type = LslChatType::Debug) const {
        LslChat chat{LslChatType::Debug, std::numeric_limits<std::int32_t>::max(),
                     framework::truncate_utf16(message, 1023), context};
        if (host.chat)
            host.chat(chat);
        if (host.listener) {
            chat.type = listener_type;
            host.listener(chat);
        }
        delay(sleep_ms);
    }
    void set_particle_rules(framework::Uuid part, const LslList& rules, std::string_view function,
                            bool timed) const {
        if (!host.set_particle_system)
            return;
        if (rules.items.empty()) {
            host.set_particle_system(part, {}, {});
            return;
        }
        framework::ParticleSystem particles;
        const auto integer = [](const LslValue& value) -> std::int32_t {
            if (const auto* number = std::get_if<std::int32_t>(&value))
                return *number;
            if (const auto* number = std::get_if<double>(&value)) {
                const auto truncated = std::trunc(*number);
                return truncated >= INT32_MIN && truncated <= INT32_MAX
                    ? static_cast<std::int32_t>(truncated) : INT32_MIN;
            }
            return framework::invariant_convert_to_int32(framework::utf16(std::get<std::string>(value)));
        };
        std::size_t index = 0;
        const ParticleRule* current = nullptr;
        try {
            for (; index < rules.items.size(); index += 2) {
                current = nullptr;
                const auto rule = integer(rules.items[index]);
                if (rule < 0 || static_cast<std::size_t>(rule) >= std::size(particle_rules) ||
                    particle_rules[rule].name.empty())
                    continue;
                current = &particle_rules[rule];
                if (index + 1 == rules.items.size())
                    throw std::out_of_range("Index was outside the bounds of the array.");
                const auto& value = rules.items[index + 1];
                const auto real = [&] {
                    if (const auto* text = std::get_if<std::string>(&value))
                        return framework::invariant_convert_to_single(framework::utf16(*text));
                    if (const auto* number = std::get_if<std::int32_t>(&value))
                        return static_cast<float>(*number);
                    return static_cast<float>(std::get<double>(value));
                };
                const auto vector = [&] {
                    const auto input = std::get<framework::Vector3d>(value);
                    return framework::Vector3{static_cast<float>(input.x), static_cast<float>(input.y),
                                              static_cast<float>(input.z)};
                };
                switch (rule) {
                case 0:
                    particles.particle_flags = static_cast<std::uint32_t>(integer(value));
                    break;
                case 1:
                case 3: {
                    const auto rgb = vector();
                    auto& color = rule == 1 ? particles.start_color : particles.end_color;
                    color[0] = rgb.x;
                    color[1] = rgb.y;
                    color[2] = rgb.z;
                    break;
                }
                case 2:
                    particles.start_color[3] = real();
                    break;
                case 4:
                    particles.end_color[3] = real();
                    break;
                case 5:
                case 6: {
                    const auto size = vector();
                    auto& scale = rule == 5 ? particles.start_scale : particles.end_scale;
                    scale = {std::min(size.x, 7.96f), std::min(size.y, 7.96f)};
                    break;
                }
                case 7:
                    particles.particle_age = real();
                    break;
                case 8:
                    particles.acceleration = vector();
                    break;
                case 9:
                    particles.pattern = static_cast<std::uint8_t>(integer(value));
                    break;
                case 10:
                case 22:
                    particles.inner_angle = real();
                    particles.source_flags = (particles.source_flags & ~2u) | (rule == 22 ? 2u : 0u);
                    break;
                case 11:
                case 23:
                    particles.outer_angle = real();
                    particles.source_flags = (particles.source_flags & ~2u) | (rule == 23 ? 2u : 0u);
                    break;
                case 12: {
                    const auto& texture = std::get<std::string>(value);
                    if (!framework::try_parse_sdk_uuid(texture, particles.texture))
                        particles.texture = host.particle_inventory_asset
                            ? host.particle_inventory_asset(context.object, texture) : framework::Uuid{};
                    break;
                }
                case 13:
                    particles.burst_rate = real();
                    break;
                case 15:
                    particles.burst_count = static_cast<std::uint8_t>(integer(value));
                    break;
                case 16:
                    particles.burst_radius = real();
                    break;
                case 17:
                    particles.speed_min = real();
                    break;
                case 18:
                    particles.speed_max = real();
                    break;
                case 19:
                    particles.source_age = real();
                    break;
                case 20:
                    if (!framework::try_parse_sdk_uuid(value_text(value), particles.target))
                        particles.target = part;
                    break;
                case 21:
                    particles.angular_velocity = vector();
                    break;
                case 24:
                    particles.blend_source = static_cast<std::uint8_t>(integer(value));
                    break;
                case 25:
                    particles.blend_destination = static_cast<std::uint8_t>(integer(value));
                    break;
                case 26:
                    particles.start_glow = real();
                    break;
                case 27:
                    particles.end_glow = real();
                    break;
                }
            }
        } catch (const std::bad_variant_access&) {
            auto message = std::string(function) + ": ";
            if (current) {
                message += "Error running rule ";
                message += current->name;
                message += ": arg #" + std::to_string(index + 1) + " - parameter 1 must be ";
                message += current->expected_type;
            } else {
                message += "Error running particle system params index #" + std::to_string(index) +
                           ": particle system parameter type must be integer";
            }
            script_error(std::move(message), 1000, LslChatType::Shout);
            return;
        }
        host.set_particle_system(part, particles.bytes(),
                                 timed ? std::optional<float>(particles.source_age) : std::nullopt);
    }
    std::int64_t timer_now() const {
        return host.monotonic_ticks ? host.monotonic_ticks() : ticks();
    }
    std::string evaluate_drawing_command(const Expression& e) const {
        std::array<LslValue, 5> arguments;
        for (std::size_t i = e.elements.size(); i > 0; --i)
            arguments[i - 1] = evaluate(*e.elements[i - 1]);
        check_safe_ossl_permission();

        const auto& command = drawing_commands[e.slot];
        auto result = std::get<std::string>(std::move(arguments[0]));
        result += command.prefix;
        if (command.format == DrawingFormat::Polygon) {
            using List = std::shared_ptr<const LslList>;
            const auto& x = std::get<List>(arguments[1])->items;
            const auto& y = std::get<List>(arguments[2])->items;
            if (x.size() < 3 || x.size() != y.size())
                return {};
            for (std::size_t i = 0; i < x.size(); ++i) {
                if (i != 0)
                    result += ',';
                result += drawing_coordinate(x[i]) + ',' + drawing_coordinate(y[i]);
            }
        } else if (command.format == DrawingFormat::Color) {
            const auto& color = std::get<framework::Vector3d>(arguments[1]);
            const auto append_hex = [&](double component) {
                constexpr std::string_view hex = "0123456789abcdef";
                const auto value = static_cast<float>(component);
                unsigned byte = 0;
                if (value >= 1.f)
                    byte = 255;
                else if (value > 0.f)
                    byte = static_cast<unsigned>(value * 255.f);
                result += hex[byte >> 4];
                result += hex[byte & 15];
            };
            if (e.elements.size() == 2)
                result += "FF";
            else
                append_hex(list_float(arguments[2]));
            for (double component : {color.x, color.y, color.z})
                append_hex(component);
        } else {
            if (command.format == DrawingFormat::DeprecatedColor)
                script_error("Use of function osSetPenColour is deprecated. Use osSetPenColor instead.");
            for (std::size_t i = 1; i < e.elements.size(); ++i) {
                if (command.format == DrawingFormat::LineFrom && i == 3)
                    result += "; LineTo ";
                else if (i > 1)
                    result += ',';
                const auto type = command.parameters.begin()[i - 1];
                result += type == Type::Float ? drawing_number(list_float(arguments[i])) : value_text(arguments[i]);
            }
        }
        result += command.suffix;
        return result;
    }
    framework::Vector3 current_world_position() const {
        if (host.object_position)
            if (const auto position = host.object_position(context.object, false, false))
                return *position;
        return context.position;
    }

    LslValue evaluate(const Expression& e) const {

        switch (e.op) {
        case Expression::Op::Literal:
            return e.value;
        case Expression::Op::ListLiteral: {
            auto list = std::make_shared<LslList>();
            list->items.resize(e.elements.size());
            for (std::size_t i = e.elements.size(); i > 0; --i)
                list->items[i - 1] = evaluate(*e.elements[i - 1]);
            return std::shared_ptr<const LslList>(std::move(list));
        }
        case Expression::Op::ListInsert: {
            std::int64_t index = std::get<std::int32_t>(evaluate(*e.z));
            const auto source = std::get<std::shared_ptr<const LslList>>(evaluate(*e.right));
            const auto destination = std::get<std::shared_ptr<const LslList>>(evaluate(*e.left));
            const auto size = static_cast<std::int64_t>(destination->items.size());
            if (index < 0)
                index += size;
            index = std::clamp(index, std::int64_t{0}, size);
            auto result = std::make_shared<LslList>();
            append_list_range(*result, *destination, 0, index);
            append_list_range(*result, *source, 0, static_cast<std::int64_t>(source->items.size()));
            append_list_range(*result, *destination, index, size);
            return std::shared_ptr<const LslList>(std::move(result));
        }
        case Expression::Op::ListReplace: {
            std::int64_t end = std::get<std::int32_t>(evaluate(*e.w));
            std::int64_t start = std::get<std::int32_t>(evaluate(*e.z));
            const auto source = std::get<std::shared_ptr<const LslList>>(evaluate(*e.right));
            const auto destination = std::get<std::shared_ptr<const LslList>>(evaluate(*e.left));
            const auto size = static_cast<std::int64_t>(destination->items.size());
            if (start < 0)
                start += size;
            if (end < 0)
                end += size;
            auto result = std::make_shared<LslList>();
            if (start > end)
                append_list_slice(*result, *destination, end + 1, start - 1);
            else if (start > 0)
                append_list_range(*result, *destination, 0, start);
            if (start >= 0 || start > end)
                append_list_range(*result, *source, 0, static_cast<std::int64_t>(source->items.size()));
            if (start <= end && end + 1 < size)
                append_list_slice(*result, *destination, end + 1, -1);
            return std::shared_ptr<const LslList>(std::move(result));
        }
        case Expression::Op::JsonSet: {
            using Value = network::Value;
            using Kind = Value::Kind;
            const auto replacement = std::get<std::string>(evaluate(*e.z));
            const auto path = std::get<std::shared_ptr<const LslList>>(evaluate(*e.right));
            const auto input = std::get<std::string>(evaluate(*e.left));
            const auto invalid = json_marker(0xFDD0);
            const bool erase = replacement == json_marker(0xFDD8);
            Value item(replacement);
            if (replacement == "null" || replacement == json_marker(0xFDD5))
                item = Value{};
            else if (replacement == "true" || replacement == json_marker(0xFDD6))
                item = Value(true);
            else if (replacement == "false" || replacement == json_marker(0xFDD7))
                item = Value(false);
            else {
                double parsed;
                if (network::try_parse_invariant_real(replacement, parsed)) {
                    const float number = static_cast<float>(parsed);
                    if (number >= -2147483648.0 && number < 2147483648.0 && std::trunc(number) == number &&
                        replacement.find('.') == std::string::npos)
                        item = Value(static_cast<std::int32_t>(number));
                    else {
                        item.kind = Kind::Real;
                        item.real =
                            static_cast<float>(std::nearbyint(static_cast<double>(number) * 1e6) / 1e6);
                    }
                }
            }
            try {
                Value document;
                if (input.empty())
                    document.kind = Kind::Array;
                else
                    document = network::parse_osd_json(input);
                const bool implicit = path->items.empty();
                const LslValue zero = std::int32_t{0};
                auto* node = &document;
                for (std::size_t i = 0; i < (implicit ? 1 : path->items.size()); ++i) {
                    const auto& part = implicit ? zero : path->items[i];
                    const auto* index = std::get_if<std::int32_t>(&part);
                    const auto* key = std::get_if<std::string>(&part);
                    const bool last = implicit || i + 1 == path->items.size();
                    if (!index && !key)
                        return invalid;
                    if (node->kind == Kind::Array && key && *key != "-1") {
                        if (node->array.size() >= 2) {
                            if (erase)
                                return invalid;
                            break;
                        }
                        *node = Value::object({});
                    } else if (node->kind == Kind::Map && index) {
                        if (*index != 0) {
                            if (erase)
                                return invalid;
                            break;
                        }
                        *node = Value{};
                        node->kind = Kind::Array;
                    } else if (node->kind != Kind::Array && node->kind != Kind::Map) {
                        if (node->kind == Kind::Undefined && i != 0)
                            return invalid;
                        *node = Value{};
                        node->kind = index || *key == "-1" ? Kind::Array : Kind::Map;
                    }
                    if (node->kind == Kind::Array) {
                        const auto size = node->array.size();
                        if (index && (*index < 0 || static_cast<std::size_t>(*index) > size))
                            return invalid;
                        const auto position = index ? static_cast<std::size_t>(*index) : size;
                        if (erase && last) {
                            if (position == size)
                                return invalid;
                            node->array.erase(node->array.begin() + position);
                            break;
                        }
                        if (position == size) {
                            if (erase)
                                return invalid;
                            node->array.emplace_back();
                            if (!last) {
                                const auto* next = std::get_if<std::string>(&path->items[i + 1]);
                                node->array.back().kind = next && *next != "-1" ? Kind::Map : Kind::Array;
                            }
                        }
                        node = &node->array[position];
                    } else {
                        if (erase && last) {
                            if (!node->map.erase(*key))
                                return invalid;
                            std::erase(node->map_order, *key);
                            break;
                        }
                        if (!node->contains(*key)) {
                            if (erase)
                                return invalid;
                            Value child;
                            if (!last) {
                                const auto* next = std::get_if<std::string>(&path->items[i + 1]);
                                child.kind = next && *next != "-1" ? Kind::Map : Kind::Array;
                            }
                            node->set(*key, std::move(child));
                        }
                        node = &node->map.at(*key);
                    }
                    if (last)
                        *node = std::move(item);
                }
                const auto output = network::osd_json(document, true);
                return implicit ? output.substr(1, output.size() - 2) : output;
            } catch (const std::exception&) {
                return invalid;
            }
        }
        case Expression::Op::JsonLookup: {
            const auto path = std::get<std::shared_ptr<const LslList>>(evaluate(*e.right));
            const auto input = std::get<std::string>(evaluate(*e.left));
            const auto invalid = json_marker(0xFDD0);
            if (input.empty())
                return invalid;
            using Kind = network::Value::Kind;
            const bool container = input.front() == '[' || input.front() == '{';
            if (!container && !path->items.empty())
                return invalid;
            network::Value document;
            try {
                document = network::parse_osd_json(container ? input : "[" + input + "]");
            } catch (const std::exception&) {
                return invalid;
            }
            if (!container && (document.kind != Kind::Array || document.array.empty()))
                return invalid;
            const auto* value = container ? &document : &document.array.front();
            for (const auto& part : path->items) {
                if (value->kind == Kind::Array) {
                    const auto* index = std::get_if<std::int32_t>(&part);
                    if (!index || *index < 0 || static_cast<std::size_t>(*index) >= value->array.size())
                        return invalid;
                    value = &value->array[*index];
                } else if (value->kind == Kind::Map) {
                    const auto* key = std::get_if<std::string>(&part);
                    if (!key || !value->contains(*key))
                        return invalid;
                    value = &value->get(*key);
                } else
                    return invalid;
            }
            const bool type = e.slot != 0;
            switch (value->kind) {
            case Kind::Array:
                return type ? json_marker(0xFDD2) : network::osd_json(*value, true);
            case Kind::Map:
                return type ? json_marker(0xFDD1) : network::osd_json(*value, true);
            case Kind::Boolean:
                return json_marker(value->boolean ? 0xFDD6 : 0xFDD7);
            case Kind::NullReference:
            case Kind::Undefined:
                return json_marker(0xFDD5);
            case Kind::Integer:
                return type ? json_marker(0xFDD3) : std::to_string(value->integer);
            case Kind::Long:
                return type ? json_marker(0xFDD3) : std::to_string(value->long_value);
            case Kind::Real: {
                if (type)
                    return json_marker(0xFDD3);
                if (!std::isfinite(value->real))
                    return network::osd_string(*value);
                auto result = value_text(value->real);
                while (result.back() == '0' && result[result.size() - 2] != '.')
                    result.pop_back();
                return result;
            }
            case Kind::String: {
                const auto text = network::osd_string(*value);
                if (!type || text == json_marker(0xFDD5) || text == json_marker(0xFDD6) ||
                    text == json_marker(0xFDD7))
                    return text;
                return json_marker(0xFDD4);
            }
            default:
                return invalid;
            }
        }
        case Expression::Op::JsonToList: {
            const auto input = std::get<std::string>(evaluate(*e.left));
            auto result = std::make_shared<LslList>();
            if (input.empty())
                return std::shared_ptr<const LslList>(std::move(result));
            using Kind = network::Value::Kind;
            network::Value document;
            try {
                document = input.front() == '[' || input.front() == '{' ? network::parse_osd_json(input)
                                                                        : network::Value(input);
            } catch (const std::exception&) {
                document = network::Value(input);
            }
            const auto append = [&](const network::Value& value) {
                switch (value.kind) {
                case Kind::Integer:
                    result->items.emplace_back(value.integer);
                    break;
                case Kind::Real:
                    result->items.emplace_back(value.real);
                    break;
                case Kind::String:
                    result->items.emplace_back(network::osd_string(value));
                    break;
                case Kind::Boolean:
                    result->items.emplace_back(json_marker(value.boolean ? 0xFDD6 : 0xFDD7));
                    break;
                case Kind::NullReference:
                case Kind::Undefined:
                    result->items.emplace_back(json_marker(0xFDD5));
                    break;
                case Kind::Array:
                case Kind::Map:
                    result->items.emplace_back(network::osd_json(value, true));
                    break;
                default:
                    throw std::invalid_argument("unsupported LSL JSON value");
                }
            };
            try {
                if (document.kind == Kind::Array) {
                    for (const auto& value : document.array)
                        append(value);
                } else if (document.kind == Kind::Map) {
                    for (const auto& key : document.ordered_keys()) {
                        result->items.emplace_back(key);
                        append(document.get(key));
                    }
                } else
                    append(document);
            } catch (const std::exception&) {
                result->items = {json_marker(0xFDD0)};
            }
            return std::shared_ptr<const LslList>(std::move(result));
        }
        case Expression::Op::ListToJson: {
            const auto list = std::get<std::shared_ptr<const LslList>>(evaluate(*e.right));
            const auto type = std::get<std::string>(evaluate(*e.left));
            const bool object = type == json_marker(0xFDD1);
            if (!object && type != json_marker(0xFDD2))
                return json_marker(0xFDD0);
            if (object && list->items.size() % 2 != 0)
                return json_marker(0xFDD0);
            std::string result(1, object ? '{' : '[');
            for (std::size_t i = 0; i < list->items.size(); ++i) {
                if (i != 0)
                    result += ',';
                if (object) {
                    const auto* key = std::get_if<std::string>(&list->items[i++]);
                    if (!key)
                        return json_marker(0xFDD0);
                    result += quote_lsl_json(*key);
                    result += ':';
                }
                const auto value = lsl_json_item(list->items[i]);
                if (value.empty())
                    return json_marker(0xFDD0);
                result += value;
            }
            return result + (object ? '}' : ']');
        }
        case Expression::Op::ListRandomize: {
            const auto stride =
                static_cast<std::size_t>(std::max(1, std::get<std::int32_t>(evaluate(*e.right))));
            const auto list = std::get<std::shared_ptr<const LslList>>(evaluate(*e.left));
            const auto size = list->items.size();
            if (size <= stride || size % stride != 0)
                return list;
            auto result = std::make_shared<LslList>(*list);
            auto& items = result->items;
            thread_local std::mt19937_64 engine{std::random_device{}()};
            for (auto chunk = size / stride - 1; chunk > 0; --chunk) {
                const auto other = std::uniform_int_distribution<std::size_t>(0, chunk)(engine);
                if (other != chunk)
                    std::swap_ranges(items.begin() + chunk * stride, items.begin() + (chunk + 1) * stride,
                                     items.begin() + other * stride);
            }
            return std::shared_ptr<const LslList>(std::move(result));
        }
        case Expression::Op::ListSort: {
            const bool ascending = std::get<std::int32_t>(evaluate(*e.z)) == 1;
            std::int64_t offset = e.w ? std::get<std::int32_t>(evaluate(*e.w)) : 0;
            const auto stride = std::max<std::int64_t>(1, std::get<std::int32_t>(evaluate(*e.right)));
            const auto list = std::get<std::shared_ptr<const LslList>>(evaluate(*e.left));
            if (offset < 0)
                offset += stride;
            if (offset < 0 || offset >= stride)
                return std::make_shared<const LslList>();
            const auto size = static_cast<std::int64_t>(list->items.size());
            if (size <= stride || size % stride != 0)
                return list;
            auto result = std::make_shared<LslList>(*list);
            auto& items = result->items;
            const auto less = [](const LslValue& a, const LslValue& b) {
                if (const auto* integer = std::get_if<std::int32_t>(&a))
                    return *integer < std::get<std::int32_t>(b);
                if (const auto* real = std::get_if<double>(&a))
                    return *real < std::get<double>(b);
                if (const auto* string = std::get_if<std::string>(&a))
                    return framework::utf16(*string) < framework::utf16(std::get<std::string>(b));
                if (const auto* vector = std::get_if<framework::Vector3d>(&a)) {
                    const auto squared = [](const auto& v) { return v.x * v.x + v.y * v.y + v.z * v.z; };
                    return squared(*vector) < squared(std::get<framework::Vector3d>(b));
                }
                return false;
            };
            for (std::int64_t first = 0; first < size - stride; first += stride) {
                for (auto next = first + stride; next < size; next += stride) {
                    const auto& a = items[first + offset];
                    const auto& b = items[next + offset];
                    if (a.index() == b.index() && (ascending ? less(b, a) : less(a, b)))
                        std::swap_ranges(items.begin() + first, items.begin() + first + stride,
                                         items.begin() + next);
                }
            }
            return std::shared_ptr<const LslList>(std::move(result));
        }
        case Expression::Op::ListStatistics: {
            const auto list = std::get<std::shared_ptr<const LslList>>(evaluate(*e.right));
            const auto operation = std::get<std::int32_t>(evaluate(*e.left));
            std::vector<double> values;
            double minimum = std::numeric_limits<double>::infinity(), maximum = -minimum;
            double sum = 0, squares = 0, product = 1, reciprocals = 0;
            for (const auto& item : list->items) {
                if (std::holds_alternative<std::shared_ptr<const LslList>>(item))
                    continue;
                double number;
                if (!network::try_parse_invariant_real(value_text(item), number))
                    continue;
                values.push_back(number);
                minimum = std::min(minimum, number);
                maximum = std::max(maximum, number);
                sum += number;
                squares += number * number;
                product *= number;
                reciprocals += 1.0 / number;
            }
            const auto count = static_cast<double>(values.size());
            const auto mean = sum / count;
            switch (operation) {
            case 0:
                return maximum / minimum;
            case 1:
                return minimum;
            case 2:
                return maximum;
            case 3:
                return mean;
            case 4: {
                if (values.empty())
                    throw std::runtime_error("median requires numeric list entries");
                std::sort(values.begin(), values.end(),
                          [](double a, double b) { return std::isnan(a) ? !std::isnan(b) : a < b; });
                const auto middle = values.size() / 2;
                return values.size() % 2 ? values[middle] : (values[middle - 1] + values[middle]) / 2.0;
            }
            case 5:
                return std::sqrt((squares - count * std::pow(mean, 2)) / (count - 1.0));
            case 6:
                return sum;
            case 7:
                return squares;
            case 8:
                return count;
            case 9:
                return std::exp(std::log(product) / count);
            case 100:
                return count / reciprocals;
            default:
                return 0.0;
            }
        }
        case Expression::Op::ListStrided: {
            std::int64_t offset =
                e.elements.empty() ? 0 : std::get<std::int32_t>(evaluate(*e.elements.front()));
            const auto stride = std::max<std::int64_t>(1, std::get<std::int32_t>(evaluate(*e.w)));
            std::int64_t end = std::get<std::int32_t>(evaluate(*e.z));
            std::int64_t start = std::get<std::int32_t>(evaluate(*e.right));
            const auto list = std::get<std::shared_ptr<const LslList>>(evaluate(*e.left));
            const auto size = static_cast<std::int64_t>(list->items.size());
            if (start < 0)
                start = std::max<std::int64_t>(0, start + size);
            if (end < 0)
                end = std::max<std::int64_t>(0, end + size);
            if (start > end) {
                start = 0;
                end = size - 1;
            } else
                end = std::min(end, size - 1);
            if (offset < 0)
                offset += stride;
            if (offset < 0 || offset >= stride)
                return std::make_shared<const LslList>();
            start = ((start + stride - 1) / stride) * stride + offset;
            auto result = std::make_shared<LslList>();
            for (auto index = start; index <= end; index += stride)
                result->items.push_back(list->items[index]);
            return std::shared_ptr<const LslList>(std::move(result));
        }
        case Expression::Op::ListSlice:
        case Expression::Op::ListDelete: {
            std::int64_t end = std::get<std::int32_t>(evaluate(*e.z));
            std::int64_t start = std::get<std::int32_t>(evaluate(*e.right));
            const auto list = std::get<std::shared_ptr<const LslList>>(evaluate(*e.left));
            const auto size = static_cast<std::int64_t>(list->items.size());
            auto result = std::make_shared<LslList>();
            if (e.op == Expression::Op::ListDelete) {
                start = std::max<std::int64_t>(0, start < 0 ? start + size : start);
                end = std::max<std::int64_t>(0, end < 0 ? end + size : end);
                if (start > end)
                    append_list_range(*result, *list, end + 1, start);
                else {
                    append_list_range(*result, *list, 0, start);
                    append_list_range(*result, *list, end + 1, size);
                }
            } else
                append_list_slice(*result, *list, start, end);
            return std::shared_ptr<const LslList>(std::move(result));
        }
        case Expression::Op::ListFind:
        case Expression::Op::ListFindStrided: {
            const bool strided = e.op == Expression::Op::ListFindStrided;
            const auto stride =
                strided ? std::max(1, std::get<std::int32_t>(evaluate(*e.elements.front()))) : 1;
            std::int64_t end = strided ? std::get<std::int32_t>(evaluate(*e.w)) : 0;
            std::int64_t instance = e.z ? std::get<std::int32_t>(evaluate(*e.z)) : 0;
            const auto pattern = std::get<std::shared_ptr<const LslList>>(evaluate(*e.right));
            const auto source = std::get<std::shared_ptr<const LslList>>(evaluate(*e.left));
            const auto size = static_cast<std::int64_t>(source->items.size());
            if (size == 0)
                return std::int32_t(!strided && e.z && pattern->items.empty() ? 0 : -1);
            if (pattern->items.empty()) {
                if (strided)
                    return std::int32_t{0};
                if (instance < 0)
                    instance += size;
                return std::int32_t(instance >= 0 && instance < size ? instance : -1);
            }
            const auto last = size - static_cast<std::int64_t>(pattern->items.size());
            if (last < 0)
                return std::int32_t{-1};
            const auto equal = [](const LslValue& a, const LslValue& b) {
                return !std::holds_alternative<std::shared_ptr<const LslList>>(a) && a == b;
            };
            if (strided) {
                auto start = instance;
                if (start < 0)
                    start += size;
                if (start < 0 || start >= size)
                    return std::int32_t{-1};
                if (end < 0)
                    end += size - static_cast<std::int64_t>(pattern->items.size()) + 1;
                else if (end >= size)
                    end = last;
                for (auto index = start; index <= end; index += stride) {
                    std::size_t matched = 0;
                    while (matched < pattern->items.size() &&
                           equal(pattern->items[matched], source->items.at(index + matched)))
                        ++matched;
                    if (matched == pattern->items.size())
                        return static_cast<std::int32_t>(index);
                }
                return std::int32_t{-1};
            }
            const bool reverse = instance < 0;
            if (reverse)
                instance = -instance - 1;
            for (auto index = reverse ? last : 0; index >= 0 && index <= last; index += reverse ? -1 : 1) {
                if (std::equal(pattern->items.begin(), pattern->items.end(), source->items.begin() + index,
                               equal) &&
                    instance-- == 0)
                    return static_cast<std::int32_t>(index);
            }
            return std::int32_t{-1};
        }
        case Expression::Op::CsvToList: {
            const auto input = framework::utf16_replacing(std::get<std::string>(evaluate(*e.left)));
            auto list = std::make_shared<LslList>();
            const auto append = [&](std::u16string_view field) {
                while (!field.empty() && framework::unicode_whitespace(field.front()))
                    field.remove_prefix(1);
                while (!field.empty() && framework::unicode_whitespace(field.back()))
                    field.remove_suffix(1);
                list->items.emplace_back(framework::utf8(field));
            };
            std::size_t depth = 0, start = 0;
            for (std::size_t i = 0; i < input.size(); ++i) {
                if (input[i] == u'<')
                    ++depth;
                else if (input[i] == u'>' && depth)
                    --depth;
                else if (input[i] == u',' && !depth) {
                    append(std::u16string_view(input).substr(start, i - start));
                    start = i + 1;
                }
            }
            append(std::u16string_view(input).substr(start));
            return std::shared_ptr<const LslList>(std::move(list));
        }
        case Expression::Op::ParseString:
        case Expression::Op::ParseStringKeepNulls: {
            const auto input = framework::utf16_replacing(std::get<std::string>(evaluate(*e.left)));
            const auto separators = std::get<std::shared_ptr<const LslList>>(evaluate(*e.right));
            const auto spacers = std::get<std::shared_ptr<const LslList>>(evaluate(*e.z));
            auto result = std::make_shared<LslList>();
            if (input.empty())
                return std::shared_ptr<const LslList>(std::move(result));

            struct Delimiter {
                std::u16string text;
                std::size_t next_match;
                bool retain;
            };
            std::vector<Delimiter> delimiters;
            const auto collect = [&](const LslList& list, bool retain) {
                for (const auto& item : list.items) {
                    auto text = framework::utf16_replacing(value_text(item));
                    if (!text.empty()) {
                        const auto match = input.find(text);
                        if (match != std::u16string::npos)
                            delimiters.push_back({std::move(text), match, retain});
                    }
                }
            };
            collect(*separators, false);
            collect(*spacers, true);

            const bool keep_nulls = e.op == Expression::Op::ParseStringKeepNulls;
            const std::u16string_view source(input);
            std::size_t start = 0;
            while (true) {
                const Delimiter* first = nullptr;
                auto end = source.size();
                for (auto& delimiter : delimiters) {
                    if (delimiter.next_match < start)
                        delimiter.next_match = source.find(delimiter.text, start);
                    // Equal positions preserve list order, with separators ahead of spacers.
                    if (delimiter.next_match < end) {
                        end = delimiter.next_match;
                        first = &delimiter;
                        if (end == start)
                            break;
                    }
                }
                if (keep_nulls || end > start)
                    result->items.emplace_back(framework::utf8(source.substr(start, end - start)));
                if (!first)
                    break;
                if (first->retain)
                    result->items.emplace_back(framework::utf8(first->text));
                start = end + first->text.size();
            }
            return std::shared_ptr<const LslList>(std::move(result));
        }
        case Expression::Op::ListJoin: {
            const auto separator = e.right ? std::get<std::string>(evaluate(*e.right)) : std::string(", ");
            const auto list = std::get<std::shared_ptr<const LslList>>(evaluate(*e.left));
            std::string result;
            for (std::size_t i = 0; i < list->items.size(); ++i) {
                if (i != 0)
                    result += separator;
                result += value_text(list->items[i]);
            }
            return result;
        }
        case Expression::Op::ListString:
        case Expression::Op::ListKey:
        case Expression::Op::ListInteger:
        case Expression::Op::ListFloat:
        case Expression::Op::ListVector:
        case Expression::Op::ListRotation:
        case Expression::Op::ListEntryType: {
            std::int64_t index = std::get<std::int32_t>(evaluate(*e.right));
            const auto list = std::get<std::shared_ptr<const LslList>>(evaluate(*e.left));
            const auto size = static_cast<std::int64_t>(list->items.size());
            if (index < 0)
                index += size;
            const bool entry_type = e.op == Expression::Op::ListEntryType;
            if (index < 0 || index >= size) {
                if (e.op == Expression::Op::ListFloat)
                    return 0.;
                if (e.op == Expression::Op::ListRotation)
                    return LslRotation{};
                if (e.op == Expression::Op::ListVector)
                    return framework::Vector3d{};
                if (entry_type || e.op == Expression::Op::ListInteger)
                    return std::int32_t{0};
                return std::string{};
            }
            const auto& item = list->items[index];
            if (e.op == Expression::Op::ListInteger)
                return list_integer(item);
            if (e.op == Expression::Op::ListFloat)
                return list_float(item);
            if (e.op == Expression::Op::ListVector)
                return list_vector(item);
            if (e.op == Expression::Op::ListRotation)
                return list_rotation(item);
            if (entry_type) {
                if (std::holds_alternative<std::int32_t>(item))
                    return std::int32_t{1};
                if (std::holds_alternative<double>(item))
                    return std::int32_t{2};
                if (const auto* text = std::get_if<std::string>(&item)) {
                    framework::Uuid key;
                    return std::int32_t(framework::try_parse_sdk_uuid(*text, key) ? 4 : 3);
                }
                if (std::holds_alternative<LslRotation>(item))
                    return std::int32_t{6};
                if (std::holds_alternative<framework::Vector3d>(item))
                    return std::int32_t{5};
                return std::int32_t{7};
            }
            if (e.op == Expression::Op::ListKey && !std::holds_alternative<std::string>(item))
                return std::string{};
            return value_text(item);
        }
        case Expression::Op::ListLength:
            return static_cast<std::int32_t>(
                std::get<std::shared_ptr<const LslList>>(evaluate(*e.left))->items.size());
        case Expression::Op::VectorLiteral: {
            const auto component = [&](const Expression& operand) {
                const auto value = evaluate(operand);
                if (const auto* real = std::get_if<double>(&value))
                    return *real;
                return static_cast<double>(std::get<std::int32_t>(value));
            };
            const auto w = e.w ? component(*e.w) : 1.;
            const auto z = component(*e.z);
            const auto y = component(*e.right);
            const auto x = component(*e.left);
            if (e.w)
                return LslRotation{x, y, z, w};
            return framework::Vector3d{x, y, z};
        }
        case Expression::Op::Component: {
            const auto value = evaluate(*e.left);
            return component_at(value, e.slot);
        }
        case Expression::Op::AxisAngleToRotation: {
            const auto angle = evaluate(*e.right);
            const auto half =
                (std::holds_alternative<double>(angle) ? std::get<double>(angle)
                                                       : static_cast<double>(std::get<std::int32_t>(angle))) *
                0.5;
            auto axis = std::get<framework::Vector3d>(evaluate(*e.left));
            const auto squared = axis.x * axis.x + axis.y * axis.y + axis.z * axis.z;
            if (squared > std::numeric_limits<float>::denorm_min()) {
                const auto scale = 1.0 / std::sqrt(squared);
                axis = {axis.x * scale, axis.y * scale, axis.z * scale};
            } else
                axis = {};
            const auto sine = std::sin(half);
            return LslRotation{axis.x * sine, axis.y * sine, axis.z * sine, std::cos(half)};
        }
        case Expression::Op::AxesToRotation: {
            const auto up = std::get<framework::Vector3d>(evaluate(*e.z));
            const auto left = std::get<framework::Vector3d>(evaluate(*e.right));
            const auto forward = std::get<framework::Vector3d>(evaluate(*e.left));
            return axes_to_rotation(forward, left, up);
        }
        case Expression::Op::EulerToRotation: {
            const auto [x, y, z] = std::get<framework::Vector3d>(evaluate(*e.left));
            const auto sx = std::sin(x * 0.5), cx = std::cos(x * 0.5);
            const auto sy = std::sin(y * 0.5), cy = std::cos(y * 0.5);
            const auto sz = std::sin(z * 0.5), cz = std::cos(z * 0.5);
            return LslRotation{sx * cy * cz + cx * sy * sz, cx * sy * cz - sx * cy * sz,
                               sx * sy * cz + cx * cy * sz, cx * cy * cz - sx * sy * sz};
        }
        case Expression::Op::RotationToEuler: {
            const auto [x, y, z, w] = std::get<LslRotation>(evaluate(*e.left));
            const auto xx = x * x, yy = y * y, zz = z * z, ww = w * w;
            const auto norm = xx + zz + yy + ww;
            const auto test = x * z + y * w;
            const auto threshold = 0.49999 * norm;
            constexpr double half_pi = 1.57079632679489661923;
            if (test > threshold)
                return framework::Vector3d{0, half_pi, 2 * std::atan2(x, w)};
            if (test < -threshold)
                return framework::Vector3d{0, -half_pi, -2 * std::atan2(x, w)};
            return framework::Vector3d{std::atan2(2 * x * w - 2 * z * y, -xx + zz - yy + ww),
                                       std::asin(2 * test / norm),
                                       std::atan2(2 * z * w - 2 * x * y, xx - zz - yy + ww)};
        }
        case Expression::Op::RotationBetweenVectors: {
            auto b = std::get<framework::Vector3d>(evaluate(*e.right));
            auto a = std::get<framework::Vector3d>(evaluate(*e.left));
            const auto dot = [](const auto& lhs, const auto& rhs) {
                return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z;
            };
            const auto normalize = [&](const framework::Vector3d& v) {
                const auto squared = dot(v, v);
                if (!(squared > std::numeric_limits<float>::denorm_min()))
                    return framework::Vector3d{};
                const auto scale = 1.0 / std::sqrt(squared);
                return framework::Vector3d{v.x * scale, v.y * scale, v.z * scale};
            };
            if (dot(a, a) < 1e-12 || dot(b, b) < 1e-12)
                return LslRotation{};
            a = normalize(a);
            b = normalize(b);
            const auto cosine = dot(a, b);
            if (cosine > 0.99999)
                return LslRotation{};
            if (cosine < -0.99999) {
                const auto projection = a.x / dot(a, a);
                const framework::Vector3d axis{1.0 - projection * a.x, 0.0 - projection * a.y,
                                               0.0 - projection * a.z};
                if (dot(axis, axis) < 1e-12)
                    return LslRotation{0, 0, 1, 0};
                return LslRotation{static_cast<float>(axis.x), static_cast<float>(axis.y),
                                   static_cast<float>(axis.z), 0};
            }
            const auto axis =
                normalize({a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x});
            const auto half = std::acos(cosine) * 0.5;
            const auto sine = std::sin(half);
            return LslRotation{axis.x * sine, axis.y * sine, axis.z * sine, std::cos(half)};
        }
        case Expression::Op::RotationBasis: {
            auto [x, y, z, w] = std::get<LslRotation>(evaluate(*e.left));
            const auto squared = x * x + y * y + z * z + w * w;
            if (std::abs(1.0 - squared) > 0.000001) {
                const auto scale = 1.0 / std::sqrt(squared);
                x *= scale;
                y *= scale;
                z *= scale;
                w *= scale;
            }
            if (e.slot == 0)
                return framework::Vector3d{x * x - y * y - z * z + w * w, 2 * (x * y + z * w),
                                           2 * (x * z - y * w)};
            if (e.slot == 1)
                return framework::Vector3d{2 * (x * y - z * w), -x * x + y * y - z * z + w * w,
                                           2 * (x * w + y * z)};
            return framework::Vector3d{2 * (x * z + y * w), 2 * (-x * w + y * z),
                                       -x * x - y * y + z * z + w * w};
        }
        case Expression::Op::RotationAngleBetween: {
            const auto b = std::get<LslRotation>(evaluate(*e.right));
            const auto a = std::get<LslRotation>(evaluate(*e.left));
            const auto dot = [](const LslRotation& lhs, const LslRotation& rhs) {
                return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z + lhs.w * rhs.w;
            };
            const auto lengths = dot(a, a) * dot(b, b);
            if (lengths == 0.0)
                return 0.0;
            const auto product = dot(a, b);
            const auto quotient = product * product / lengths;
            return quotient >= 1.0 ? 0.0 : std::acos(2.0 * quotient - 1.0);
        }
        case Expression::Op::RotationToAxis:
        case Expression::Op::RotationToAngle: {
            auto [x, y, z, w] = std::get<LslRotation>(evaluate(*e.left));
            const auto squared = x * x + y * y + z * z + w * w;
            if (squared < std::numeric_limits<float>::denorm_min()) {
                x = y = z = 0.;
                w = 1.;
            } else {
                const auto scale = 1.0 / std::sqrt(squared);
                x *= scale;
                y *= scale;
                z *= scale;
                w *= scale;
            }
            if (e.op == Expression::Op::RotationToAngle) {
                constexpr double pi = 3.14159265358979323846;
                const auto angle = 2.0 * std::acos(w);
                return angle > pi ? 2.0 * pi - angle : angle;
            }
            const auto sine = std::sqrt(1.0 - w * w);
            if (sine < 1e-8)
                return framework::Vector3d{};
            const auto scale = (w < 0 ? -1.0 : 1.0) / sine;
            return framework::Vector3d{x * scale, y * scale, z * scale};
        }
        case Expression::Op::ColorConversion: {
            const auto vector = std::get<framework::Vector3d>(evaluate(*e.left));
            const auto convert = [to_linear = e.slot != 0](float channel) {
                if (to_linear)
                    return channel < 0.04045f ? channel * 0.07739938f
                                              : std::pow((channel + 0.055f) / 1.055f, 2.4f);
                return channel <= 0.0031308f ? channel * 12.92f
                                             : 1.055f * std::pow(channel, 0.4166667f) - 0.055f;
            };
            return framework::Vector3d{convert(static_cast<float>(vector.x)),
                                       convert(static_cast<float>(vector.y)),
                                       convert(static_cast<float>(vector.z))};
        }
        case Expression::Op::VectorMagnitude:
        case Expression::Op::VectorNormalize:
        case Expression::Op::VectorDistance: {
            using Op = Expression::Op;
            const auto other =
                e.right ? std::get<framework::Vector3d>(evaluate(*e.right)) : framework::Vector3d{};
            auto vector = std::get<framework::Vector3d>(evaluate(*e.left));
            if (e.op == Op::VectorDistance) {
                vector.x -= other.x;
                vector.y -= other.y;
                vector.z -= other.z;
            }
            const auto squared = vector.x * vector.x + vector.y * vector.y + vector.z * vector.z;
            if (e.op != Op::VectorNormalize)
                return std::sqrt(squared);
            if (!(squared > std::numeric_limits<float>::denorm_min()))
                return framework::Vector3d{};
            const auto scale = 1. / std::sqrt(squared);
            return framework::Vector3d{vector.x * scale, vector.y * scale, vector.z * scale};
        }
        case Expression::Op::Abs: {
            const auto value = std::get<std::int32_t>(evaluate(*e.left));
            return value == std::numeric_limits<std::int32_t>::min() ? value : std::abs(value);
        }
        case Expression::Op::HttpRequest: {
            auto body = std::get<std::string>(evaluate(*e.z));
            const auto values = std::get<std::shared_ptr<const LslList>>(evaluate(*e.right));
            auto url = std::get<std::string>(evaluate(*e.left));
            if (!host.http_request)
                return std::string{};
            std::vector<network::ScriptHttpOption> options;
            options.reserve(values->items.size());
            for (const auto& value : values->items) {
                const auto* number = std::get_if<std::int32_t>(&value);
                options.push_back({number ? std::optional{*number} : std::nullopt, value_text(value)});
            }
            auto result = host.http_request(context.object, context.item, std::move(url), options, std::move(body));
            for (auto& message : result.errors)
                script_error(std::move(message), 1000, LslChatType::Shout);
            if (result.invalid_flag)
                delay(200);
            return std::move(result.key);
        }
        case Expression::Op::SendRemoteData: {
            auto text = std::get<std::string>(evaluate(*e.elements[3]));
            const auto integer = std::get<std::int32_t>(evaluate(*e.elements[2]));
            auto destination = std::get<std::string>(evaluate(*e.elements[1]));
            auto channel = std::get<std::string>(evaluate(*e.elements[0]));
            const auto milliseconds = 3000.f * host.script_delay_factor;
            if (milliseconds >= 10 && double(milliseconds) <= std::numeric_limits<std::int32_t>::max())
                delay(static_cast<std::int32_t>(milliseconds));
            return host.send_remote_data
                ? host.send_remote_data(context.item, std::move(channel), std::move(destination),
                                        integer, std::move(text)).str()
                : std::string{};
        }
        case Expression::Op::ManageEstateAccess: {
            const auto avatar = std::get<std::string>(evaluate(*e.right));
            const auto action = std::get<std::int32_t>(evaluate(*e.left));
            framework::Uuid target;
            return std::int32_t(host.manage_estate_access &&
                framework::try_parse_sdk_uuid(avatar, target) && !target.is_zero() &&
                host.manage_estate_access(context.object, action, target));
        }
        case Expression::Op::SetMemoryLimit:
            evaluate(*e.left);
            return std::int32_t{0};
        case Expression::Op::GetUsedMemory:
            return heap_bytes_used();
        case Expression::Op::GetFreeMemory:
            return wrap(static_cast<std::uint32_t>(host.heap_limit_bytes) -
                        static_cast<std::uint32_t>(heap_bytes_used()));
        case Expression::Op::RequestUrl:
        case Expression::Op::RequestSecureUrl: {
            const bool secure = e.op == Expression::Op::RequestSecureUrl;
            bool allow_xss = false;
            if (e.left) {
                const auto options = std::get<std::shared_ptr<const LslList>>(evaluate(*e.left));
                const auto function = secure ? "osRequestSecureURL" : "osRequestURL";
                check_ossl_permission(function);
                allow_xss = std::any_of(options->items.begin(), options->items.end(), [](const auto& option) {
                    const auto* name = std::get_if<std::string>(&option);
                    return name && *name == "allowXss";
                });
            }
            return (host.request_url
                        ? host.request_url(context.object, context.item, secure, allow_xss)
                        : framework::Uuid{}).str();
        }
        case Expression::Op::ReleaseUrl: {
            const auto url = std::get<std::string>(evaluate(*e.left));
            if (host.release_url)
                host.release_url(context.item, url);
            return std::int32_t{0};
        }
        case Expression::Op::FreeUrls:
            return host.free_urls ? host.free_urls() : std::int32_t{0};
        case Expression::Op::SimulatorHostname:
            return host.simulator_hostname;
        case Expression::Op::RegionSize:
            check_safe_ossl_permission();
            return context.region_size;
        case Expression::Op::ScriptEngineName:
            check_ossl_permission("osGetScriptEngineName");
            return std::string("NativeLSL");
        case Expression::Op::GridName:
            return host.grid_name;
        case Expression::Op::GridNick:
            return host.grid_nick;
        case Expression::Op::GridInfo: {
            constexpr std::array functions{
                "osGetGridLoginURI", "osGetGridHomeURI", "osGetGridGatekeeperURI", "osGetGridCustom",
            };
            const auto query = static_cast<LslGridQuery>(e.slot);
            const auto key = e.left ? std::get<std::string>(evaluate(*e.left)) : std::string{};
            check_ossl_permission(functions.at(e.slot));
            if (host.grid_info)
                return host.grid_info(query, key);
            return std::string(query == LslGridQuery::Login || query == LslGridQuery::Custom
                                   ? "Configuration Error!" : "");
        }
        case Expression::Op::EnvironmentValue: {
            const auto name = std::get<std::string>(evaluate(*e.left));
            const auto key = framework::utf8(framework::invariant_lower_utf16(framework::utf16_replacing(name)));
            if (key == "simulator_hostname")
                return host.simulator_hostname;
            return host.region_environment_value ? host.region_environment_value(key) : std::string{};
        }
        case Expression::Op::HttpHeader:
        case Expression::Op::HttpResponse: {
            const bool respond = e.op == Expression::Op::HttpResponse;
            auto text = std::get<std::string>(evaluate(respond ? *e.z : *e.right));
            const auto status = respond ? std::get<std::int32_t>(evaluate(*e.right)) : 0;
            const auto key = std::get<std::string>(evaluate(*e.left));
            framework::Uuid request;
            const bool valid = framework::try_parse_sdk_uuid(key, request);
            if (!respond)
                return valid && host.http_header ? host.http_header(context.item, request, text) : std::string{};
            if (valid && host.http_response)
                host.http_response(context.item, request, status, std::move(text));
            return std::int32_t{0};
        }
        case Expression::Op::HttpContentType: {
            const auto type = std::get<std::int32_t>(evaluate(*e.right));
            const auto key = std::get<std::string>(evaluate(*e.left));
            framework::Uuid request;
            if (host.http_content_type && framework::try_parse_sdk_uuid(key, request) && !request.is_zero())
                host.http_content_type(context.object, context.item, request, type);
            return std::int32_t{0};
        }
        case Expression::Op::ModPow: {
            const auto modulus = std::get<std::int32_t>(evaluate(*e.z));
            const auto exponent = std::get<std::int32_t>(evaluate(*e.right));
            const auto base = std::get<std::int32_t>(evaluate(*e.left));
            if (modulus == 0)
                throw std::runtime_error("Attempted to divide by zero.");
            const auto power = std::pow(static_cast<double>(base), static_cast<double>(exponent));
            constexpr double upper_bound = 9223372036854775808.0;
            const auto dividend = power >= -upper_bound && power < upper_bound
                                      ? static_cast<std::int64_t>(power)
                                      : std::numeric_limits<std::int64_t>::min();
            if (dividend == std::numeric_limits<std::int64_t>::min() && modulus == -1)
                throw std::runtime_error("Arithmetic operation resulted in an overflow.");
            const auto requested = 1000.f * host.script_delay_factor;
            if (requested >= 10 && requested < 2147483648.f)
                delay(static_cast<std::int32_t>(requested));
            return static_cast<std::int32_t>(dividend % modulus);
        }
        case Expression::Op::Fabs:
        case Expression::Op::Sqrt:
        case Expression::Op::Sin:
        case Expression::Op::Cos:
        case Expression::Op::Tan:
        case Expression::Op::Asin:
        case Expression::Op::Acos:
        case Expression::Op::Atan2:
        case Expression::Op::Log:
        case Expression::Op::Log10:
        case Expression::Op::Frand:
        case Expression::Op::Pow: {
            const auto number = [&](const Expression& operand) {
                const auto value = evaluate(operand);
                if (const auto* real = std::get_if<double>(&value))
                    return *real;
                return double(std::get<std::int32_t>(value));
            };
            using Op = Expression::Op;
            if (e.right) {
                const auto right = number(*e.right);
                const auto left = number(*e.left);
                return e.op == Op::Pow ? std::pow(left, right) : std::atan2(left, right);
            }
            const auto value = number(*e.left);
            switch (e.op) {
            case Op::Sin:
                return std::sin(value);
            case Op::Cos:
                return std::cos(value);
            case Op::Tan:
                return std::tan(value);
            case Op::Asin:
                return std::asin(value);
            case Op::Acos:
                return std::acos(value);
            case Op::Log:
                return std::log(value);
            case Op::Log10:
                return std::log10(value);
            case Op::Frand: {
                thread_local std::mt19937_64 engine{std::random_device{}()};
                const auto fraction = static_cast<double>(engine() >> 11) * 0x1p-53;
                return fraction * value;
            }
            case Op::Sqrt:
                return std::sqrt(value);
            default:
                return std::abs(value);
            }
        }
        case Expression::Op::Floor:
        case Expression::Op::Ceil:
        case Expression::Op::Round: {
            const auto value = evaluate(*e.left);
            const auto number = std::holds_alternative<double>(value) ? std::get<double>(value)
                                                                      : double(std::get<std::int32_t>(value));
            const auto rounded = e.op == Expression::Op::Floor  ? std::floor(number)
                                 : e.op == Expression::Op::Ceil ? std::ceil(number)
                                                                : std::round(number);
            if (!std::isfinite(rounded) || rounded < -2147483648. || rounded >= 2147483648.)
                return std::numeric_limits<std::int32_t>::min();
            return static_cast<std::int32_t>(rounded);
        }
        case Expression::Op::Local:
            return variable(e);
        case Expression::Op::InventoryNumber:
        case Expression::Op::InventoryName: {
            const auto index = e.right ? std::get<std::int32_t>(evaluate(*e.right)) : 0;
            const auto type = std::get<std::int32_t>(evaluate(*e.left));
            if (!host.inventory_names)
                throw std::runtime_error("LSL host inventory enumeration callback is required");
            auto names = host.inventory_names(context.object, type);
            if (e.op == Expression::Op::InventoryNumber)
                return static_cast<std::int32_t>(names.size());
            if (names.empty())
                return std::string{};
            if (index < 0)
                throw std::out_of_range("Inventory index is out of range");
            if (static_cast<std::size_t>(index) >= names.size())
                return std::string{};
            std::sort(names.begin(), names.end(),
                      [](const auto& a, const auto& b) { return framework::invariant_compare(a, b) < 0; });
            return names[index];
        }
        case Expression::Op::InventoryPermission:
        case Expression::Op::ObjectPermission: {
            std::optional<std::uint32_t> permission;
            if (e.op == Expression::Op::InventoryPermission) {
                const auto selector = std::get<std::int32_t>(evaluate(*e.right));
                const auto name = std::get<std::string>(evaluate(*e.left));
                if (!host.inventory_permission)
                    throw std::runtime_error("LSL host inventory permission callback is required");
                permission = host.inventory_permission(context.object, name, selector);
            } else {
                const auto selector = std::get<std::int32_t>(evaluate(*e.left));
                if (host.object_permission)
                    permission = host.object_permission(context.object, selector);
            }
            if (!permission)
                return std::int32_t{-1};
            constexpr std::uint32_t full_permissions = 0x8e000;
            const auto mask = *permission & full_permissions;
            return static_cast<std::int32_t>(mask == full_permissions ? 0x7fffffff : mask);
        }
        case Expression::Op::ObjectLinkKey: {
            const auto number = std::get<std::int32_t>(evaluate(*e.right));
            const auto text = std::get<std::string>(evaluate(*e.left));
            framework::Uuid id;
            if (!host.link_key || !framework::try_parse_sdk_uuid(text, id) || id.is_zero())
                return framework::Uuid{}.str();
            return host.link_key(id, number).str();
        }
        case Expression::Op::SitTargetAvatar: {
            const auto link = e.left ? std::get<std::int32_t>(evaluate(*e.left)) : -4;
            if (!host.sit_target_avatar || (link < 1 && link != -4))
                return framework::Uuid{}.str();
            return host.sit_target_avatar(context.object, link).str();
        }
        case Expression::Op::SitFlags: {
            const auto link = std::get<std::int32_t>(evaluate(*e.left));
            return host.sit_flags ? host.sit_flags(context.object, link) : std::int32_t{0};
        }
        case Expression::Op::SitActiveRange: {
            const auto link = e.left ? std::get<std::int32_t>(evaluate(*e.left)) : -4;
            return host.sit_active_range ? double(host.sit_active_range(context.object, link))
                                         : double(std::numeric_limits<std::int32_t>::min());
        }
        case Expression::Op::StandTarget: {
            const auto link = e.left ? std::get<std::int32_t>(evaluate(*e.left)) : -4;
            return host.stand_target ? host.stand_target(context.object, link) : framework::Vector3d{};
        }
        case Expression::Op::InventoryDescription:
        case Expression::Op::InventoryCreator:
        case Expression::Op::InventoryAcquireTime: {
            using Op = Expression::Op;
            const auto name = std::get<std::string>(evaluate(*e.left));
            if (!host.inventory_metadata)
                throw std::runtime_error("LSL host inventory metadata callback is required");
            if (auto item = host.inventory_metadata(context.object, name)) {
                if (e.op == Op::InventoryDescription)
                    return std::move(item->description);
                if (e.op == Op::InventoryCreator)
                    return item->creator.str();
                auto timestamp =
                    utc_text(621355968000000000LL + std::int64_t(item->creation_date) * 10000000, true);
                timestamp.resize(19);
                return timestamp + 'Z';
            }
            script_error(e.op == Op::InventoryDescription
                             ? "llGetInventoryDesc: Item " + name + " not found"
                             : std::string(e.op == Op::InventoryCreator ? "llGetInventoryCreator"
                                                                        : "llGetInventoryAcquireTime") +
                                   ": Can't find item '" + name + "'");
            return std::string{};
        }
        case Expression::Op::InventoryKey:
        case Expression::Op::InventoryType: {
            const auto name = std::get<std::string>(evaluate(*e.left));
            if (e.op == Expression::Op::InventoryKey) {
                if (!host.inventory_key)
                    throw std::runtime_error("LSL host inventory key callback is required");
                return host.inventory_key(context.object, name).str();
            }
            if (!host.inventory_type)
                throw std::runtime_error("LSL host inventory callback is required");
            return host.inventory_type(context.object, name);
        }
        case Expression::Op::ScriptName:
            return context.script_name;
        case Expression::Op::ScriptRunning: {
            const auto name = std::get<std::string>(evaluate(*e.left));
            const auto item = host.find_script ? host.find_script(context.object, name) : framework::Uuid{};
            if (item.is_zero())
                script_error("llGetScriptState: Can't find script '" + name + "'");
            return std::int32_t(!item.is_zero() && host.script_running && host.script_running(item));
        }
        case Expression::Op::SendLinkMessage: {
            const auto id = std::get<std::string>(evaluate(*e.w));
            const auto message = std::get<std::string>(evaluate(*e.z));
            const auto number = std::get<std::int32_t>(evaluate(*e.right));
            const auto link = std::get<std::int32_t>(evaluate(*e.left));
            if (host.linked_prim_parts && host.link_message)
                for (const auto part : host.linked_prim_parts(context.object, link))
                    host.link_message(context.object, part, number, message, id);
            return std::int32_t{0};
        }
        case Expression::Op::PhysicsMaterial: {
            auto result = std::make_shared<LslList>();
            if (host.physics_material)
                for (const auto value : host.physics_material(context.object))
                    result->items.emplace_back(double(value));
            return std::shared_ptr<const LslList>(std::move(result));
        }
        case Expression::Op::SetPhysicsMaterial: {
            std::array<float, 4> values;
            for (auto i = values.size(); i > 0; --i)
                values[i - 1] = static_cast<float>(list_float(evaluate(*e.elements[i])));
            const auto mask = std::get<std::int32_t>(evaluate(*e.elements[0]));
            if (host.set_physics_material)
                host.set_physics_material(context.object, mask, values);
            return std::int32_t{0};
        }
        case Expression::Op::LookAt: {
            std::optional<framework::Quaternion> rotation;
            double strength = 0.;
            double damping = 0.;
            const bool point_target = e.left && e.left->type == Type::Vector;
            if (e.left) {
                damping = list_float(evaluate(*e.z));
                strength = list_float(evaluate(*e.right));
                const auto value = evaluate(*e.left);
                LslRotation target;
                if (point_target) {
                    const auto position = std::get<framework::Vector3d>(value);
                    const auto normalize = [](framework::Vector3d v) {
                        const double length = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
                        return length > 0. ? framework::Vector3d{v.x / length, v.y / length, v.z / length}
                                           : framework::Vector3d{};
                    };
                    const auto from = context.position;
                    const auto forward = normalize({position.x - from.x, position.y - from.y, position.z - from.z});
                    const auto left = normalize({-forward.y, forward.x, 0.});
                    const framework::Vector3d up{-forward.z * left.y, forward.z * left.x,
                                                  forward.x * left.y - forward.y * left.x};
                    const auto [x, y, z, w] = axes_to_rotation(forward, left, up);
                    constexpr double half_turn = .707107;
                    target = {half_turn * (x - z), half_turn * (y + w), half_turn * (z + x), half_turn * (w - y)};
                } else {
                    target = std::get<LslRotation>(value);
                }
                rotation = framework::Quaternion{static_cast<float>(target.x), static_cast<float>(target.y),
                                                  static_cast<float>(target.z), static_cast<float>(target.w)};
            }
            if (host.look_at && host.look_at(context.object, rotation, strength, damping, point_target))
                delay(200);
            return std::int32_t{0};
        }
        case Expression::Op::MoveToTarget: {
            framework::PrimMoveTarget target;
            if (e.left) {
                target.tau = static_cast<float>(list_float(evaluate(*e.right)));
                const auto position = std::get<framework::Vector3d>(evaluate(*e.left));
                target.position = {static_cast<float>(position.x), static_cast<float>(position.y),
                                   static_cast<float>(position.z)};
            }
            if (host.move_to_target)
                host.move_to_target(context.object, target);
            return std::int32_t{0};
        }
        case Expression::Op::GroundRepel: {
            const double height = list_float(evaluate(*e.left));
            const bool water = std::get<std::int32_t>(evaluate(*e.right)) != 0;
            const float tau = static_cast<float>(list_float(evaluate(*e.z)));
            if (host.ground_repel)
                host.ground_repel(context.object, height, water, tau);
            return std::int32_t{0};
        }
        case Expression::Op::SetHover: {
            framework::PrimHover hover;
            if (e.left) {
                hover.tau = static_cast<float>(list_float(evaluate(*e.z)));
                hover.above_water = std::get<std::int32_t>(evaluate(*e.right)) != 0;
                hover.height = static_cast<float>(list_float(evaluate(*e.left)));
            }
            if (host.set_hover)
                host.set_hover(context.object, hover);
            return std::int32_t{0};
        }
        case Expression::Op::SetBuoyancy: {
            const float buoyancy = static_cast<float>(list_float(evaluate(*e.left)));
            if (host.set_buoyancy)
                host.set_buoyancy(context.object, buoyancy);
            return std::int32_t{0};
        }
        case Expression::Op::SetVelocity:
        case Expression::Op::SetAngularVelocity:
        case Expression::Op::ApplyImpulse:
        case Expression::Op::ApplyRotationalImpulse: {
            const bool local = std::get<std::int32_t>(evaluate(*e.right)) != 0;
            const auto vector = std::get<framework::Vector3d>(evaluate(*e.left));
            const bool impulse = e.op == Expression::Op::ApplyImpulse ||
                                 e.op == Expression::Op::ApplyRotationalImpulse;
            const bool angular = e.op == Expression::Op::SetAngularVelocity ||
                                 e.op == Expression::Op::ApplyRotationalImpulse;
            const auto& operation = impulse ? host.apply_impulse : host.set_velocity;
            if (operation)
                operation(context.object, {static_cast<float>(vector.x), static_cast<float>(vector.y),
                                           static_cast<float>(vector.z)}, local, angular);
            return std::int32_t{0};
        }
        case Expression::Op::PushObject: {
            const bool local = std::get<std::int32_t>(evaluate(*e.elements[3])) != 0;
            evaluate(*e.elements[2]);
            const auto impulse = std::get<framework::Vector3d>(evaluate(*e.elements[1]));
            const auto key = std::get<std::string>(evaluate(*e.elements[0]));
            framework::Uuid target;
            if (host.push_object && framework::try_parse_sdk_uuid(key, target) && !target.is_zero()) {
                host.push_object(context.object, target, {static_cast<float>(impulse.x),
                    static_cast<float>(impulse.y), static_cast<float>(impulse.z)}, local);
            }
            return std::int32_t{0};
        }
        case Expression::Op::SetForce:
        case Expression::Op::SetTorque:
        case Expression::Op::SetForceAndTorque: {
            const bool local = std::get<std::int32_t>(evaluate(*e.right)) != 0;
            const auto narrow_vector = [](framework::Vector3d vector) {
                return framework::Vector3{static_cast<float>(vector.x), static_cast<float>(vector.y),
                                          static_cast<float>(vector.z)};
            };
            framework::Vector3 torque;
            if (e.z)
                torque = narrow_vector(std::get<framework::Vector3d>(evaluate(*e.z)));
            auto vector = std::get<framework::Vector3d>(evaluate(*e.left));
            if (e.op == Expression::Op::SetTorque) {
                torque = narrow_vector(vector);
            } else if (host.set_force) {
                if (local)
                    vector = rotate_vector(vector, object_rotation(LslRotationSpace::World));
                host.set_force(context.object, narrow_vector(vector));
            }
            if (e.op != Expression::Op::SetForce && host.set_torque)
                host.set_torque(context.object, torque, local);
            return std::int32_t{0};
        }
        case Expression::Op::GetForce:
        case Expression::Op::GetTorque: {
            const auto& getter = e.op == Expression::Op::GetForce ? host.get_force : host.get_torque;
            const auto vector = getter ? getter(context.object) : framework::Vector3{};
            return framework::Vector3d{vector.x, vector.y, vector.z};
        }
        case Expression::Op::SetTextureAnimation: {
            std::array<LslValue, 8> args;
            for (auto i = e.elements.size(); i > 0; --i)
                args[i - 1] = evaluate(*e.elements[i - 1]);
            const bool linked = e.elements.size() == 8;
            const auto* values = args.data() + (linked ? 1 : 0);
            std::vector<std::uint8_t> bytes;
            if (std::get<std::int32_t>(values[0]) != 0) {
                bytes.reserve(16);
                for (int i = 0; i < 4; ++i)
                    bytes.push_back(static_cast<std::uint8_t>(std::get<std::int32_t>(values[i])));
                for (int i = 4; i < 7; ++i) {
                    auto bits = std::bit_cast<std::uint32_t>(static_cast<float>(list_float(values[i])));
                    for (int byte = 0; byte < 4; ++byte) {
                        bytes.push_back(static_cast<std::uint8_t>(bits));
                        bits >>= 8;
                    }
                }
            }
            if (host.set_texture_animation) {
                if (!linked)
                    host.set_texture_animation(context.object, bytes);
                else if (host.linked_prim_parts)
                    for (const auto part : host.linked_prim_parts(context.object, std::get<std::int32_t>(args[0])))
                        host.set_texture_animation(part, bytes);
            }
            return std::int32_t{0};
        }
        case Expression::Op::SetParticleSystem: {
            const auto rules = std::get<std::shared_ptr<const LslList>>(evaluate(*e.right));
            const bool timed = std::get<std::int32_t>(e.value) != 0;
            if (!e.left)
                set_particle_rules(context.object, *rules, timed ? "osParticleSystem" : "llParticleSystem", timed);
            else {
                const auto link = std::get<std::int32_t>(evaluate(*e.left));
                if (host.linked_prim_parts)
                    for (const auto part : host.linked_prim_parts(context.object, link))
                        set_particle_rules(part, *rules,
                                           timed ? "osLinkParticleSystem" : "llLinkParticleSystem", timed);
            }
            return std::int32_t{0};
        }
        case Expression::Op::SetTextureProperty:
        case Expression::Op::SetLinkedTextureProperty: {
            using Property = LslTextureProperty;
            const bool linked = e.op == Expression::Op::SetLinkedTextureProperty;
            const auto property = static_cast<Property>(std::get<std::int32_t>(e.value));
            const auto face = std::get<std::int32_t>(evaluate(e.z ? *e.z : *e.right));
            LslValue value;
            if (linked) {
                value = evaluate(*e.right);
            } else if (e.z) {
                const auto v = list_float(evaluate(*e.right));
                value = framework::Vector3d{list_float(evaluate(*e.left)), v, 0.};
            } else {
                value = evaluate(*e.left);
            }
            if (property == Property::Alpha || property == Property::Rotation)
                value = list_float(value);
            if (linked) {
                const auto link = std::get<std::int32_t>(evaluate(*e.left));
                if (host.linked_prim_parts && host.set_texture_property)
                    for (const auto id : host.linked_prim_parts(context.object, link))
                        host.set_texture_property(context.object, id, face, property, value);
            } else if (host.set_texture_property) {
                host.set_texture_property(context.object, context.object, face, property, value);
            }
            if (property != Property::Color && property != Property::Alpha) {
                const auto requested = 200.f * host.script_delay_factor;
                if (requested >= 10.f && requested < 2147483648.f)
                    delay(static_cast<std::int32_t>(requested));
            }
            return std::int32_t{0};
        }
        case Expression::Op::TextureProperty: {
            const auto face = std::get<std::int32_t>(evaluate(*e.left));
            const auto property = static_cast<LslTextureProperty>(std::get<std::int32_t>(e.value));
            if (host.texture_property) {
                if (auto value = host.texture_property(context.object, face, property))
                    return std::move(*value);
            }
            if (e.type == Type::Vector)
                return framework::Vector3d{};
            if (e.type == Type::String)
                return framework::Uuid{}.str();
            return 0.;
        }
        case Expression::Op::ObjectName:
            return context.object_name;
        case Expression::Op::ObjectDescription:
            return context.object_description;
        case Expression::Op::StartString:
            return context.start_string;
        case Expression::Op::StartParameter:
            return context.start_parameter;
        case Expression::Op::AnimationOverride: {
            const auto state = std::get<std::string>(evaluate(*e.left));
            return host.animation_override ? host.animation_override(context.item, state) : std::string{};
        }
        case Expression::Op::AnimationState: {
            framework::Uuid agent;
            const auto key = std::get<std::string>(evaluate(*e.left));
            if (!host.avatar_movement_state || !framework::try_parse_sdk_uuid(key, agent) || agent.is_zero())
                return std::string{};
            return host.avatar_movement_state(agent);
        }
        case Expression::Op::ObjectAnimationNames: {
            LslList result;
            if (host.object_animation_names)
                for (auto& name : host.object_animation_names(context.object))
                    result.items.emplace_back(std::move(name));
            return std::make_shared<const LslList>(std::move(result));
        }
        case Expression::Op::AnimationList: {
            framework::Uuid agent;
            LslList result;
            const auto key = std::get<std::string>(evaluate(*e.left));
            if (host.avatar_animations && framework::try_parse_sdk_uuid(key, agent) && !agent.is_zero())
                for (const auto id : host.avatar_animations(agent))
                    result.items.emplace_back(id.str());
            return std::make_shared<const LslList>(std::move(result));
        }
        case Expression::Op::ScriptPermissions:
        case Expression::Op::ScriptPermissionsKey: {
            const auto permissions = host.script_permissions
                                         ? host.script_permissions(context.object, context.item)
                                         : std::pair<framework::Uuid, std::int32_t>{};
            if (e.op == Expression::Op::ScriptPermissionsKey)
                return permissions.first.str();
            return permissions.second;
        }
        case Expression::Op::WorldPositionToHud: {
            const auto target = std::get<framework::Vector3d>(evaluate(*e.left));
            const auto point = context.attachment_point;
            if (point < 31 || point > 38)
                return framework::Vector3d{};
            const auto [granter, mask] = host.script_permissions
                                            ? host.script_permissions(context.object, context.item)
                                            : std::pair<framework::Uuid, std::int32_t>{};
            constexpr std::int32_t track_camera = 0x400;
            if (granter != context.owner || (mask & track_camera) == 0) {
                script_error("llGetCameraPos: No permissions to track the camera");
                return framework::Vector3d{};
            }
            const auto camera = host.camera ? host.camera(context.owner) : std::nullopt;
            if (!camera)
                return framework::Vector3d{};

            const framework::Vector3 delta{static_cast<float>(target.x) - camera->position.x,
                                           static_cast<float>(target.y) - camera->position.y,
                                           static_cast<float>(target.z) - camera->position.z};
            const auto dot = [&](framework::Vector3 axis) {
                return delta.x * axis.x + delta.y * axis.y + delta.z * axis.z;
            };
            const float at = dot(camera->at_axis);
            float left = dot(camera->left_axis);
            float up = dot(camera->up_axis);
            const float view_scale = at * std::tan(camera->fov * .5f);
            if (view_scale > 0) {
                left *= .45f / view_scale;
                up *= .45f / view_scale;
            }
            // HUD top right, top center and top left occupy points 32 through 34.
            if (point >= 32 && point <= 34)
                up -= .5f;
            if (point >= 36)
                up += .5f;
            if (point == 34 || point == 36)
                left -= .5f * camera->aspect;
            if (point == 32 || point == 38)
                left += .5f * camera->aspect;
            return framework::Vector3d{at > 0 ? 1.0 : -1.0, left, up};
        }
        case Expression::Op::CameraPosition:
        case Expression::Op::CameraRotation:
        case Expression::Op::CameraFov:
        case Expression::Op::CameraAspect: {
            const auto [granter, mask] = host.script_permissions
                                            ? host.script_permissions(context.object, context.item)
                                            : std::pair<framework::Uuid, std::int32_t>{};
            constexpr std::int32_t track_camera = 0x400;
            const bool permitted = !granter.is_zero() && (mask & track_camera) != 0;
            if (!granter.is_zero() && !permitted)
                script_error(std::get<std::string>(e.value) + ": No permissions to track the camera");

            const auto camera = permitted && host.camera ? host.camera(granter) : std::nullopt;
            if (e.op == Expression::Op::CameraRotation)
                return camera ? camera->rotation : LslRotation{};
            if (e.op == Expression::Op::CameraFov) {
                const float fov = camera ? camera->fov : (permitted ? 1.4f : 0.f);
                return static_cast<double>(fov);
            }
            if (e.op == Expression::Op::CameraAspect)
                return static_cast<double>(camera ? camera->aspect : 1.f);
            const auto position = camera ? camera->position : framework::Vector3{};
            return framework::Vector3d{position.x, position.y, position.z};
        }
        case Expression::Op::LinksetDataList:
        case Expression::Op::LinksetDataFind:
        case Expression::Op::LinksetDataCountFound:
        case Expression::Op::LinksetDataDeleteFound: {
            const bool count_only = e.op == Expression::Op::LinksetDataCountFound;
            const bool deleting = e.op == Expression::Op::LinksetDataDeleteFound;
            const bool filtered = e.op != Expression::Op::LinksetDataList;
            const auto pattern = filtered ? std::get<std::string>(evaluate(*e.left)) : std::string{};
            const auto start =
                count_only || deleting ? 0 : std::max(0, std::get<std::int32_t>(evaluate(*e.right)));
            const auto count = count_only || deleting ? 0 : std::get<std::int32_t>(evaluate(*e.z));
            auto keys =
                host.linkset_data_keys ? host.linkset_data_keys(context.object) : std::vector<std::string>{};
            if (filtered) {
                try {
                    if (pattern.empty())
                        keys.clear();
                    else if (!keys.empty()) {
                        const auto expression =
                            std::make_shared<const std::u16string>(framework::utf16_replacing(pattern));
                        const auto culture = []() { return std::u16string{}; };
                        const auto stack = []() { return framework::sufficient_execution_stack(); };
                        auto parsed = framework::dotnet_regex_parse(expression, 512, culture, stack);
                        framework::DotnetRegexInterpreter regex(std::move(parsed), expression, stack);
                        std::erase_if(keys, [&](const auto& key) {
                            const auto input =
                                std::make_shared<const std::u16string>(framework::utf16_replacing(key));
                            return !regex
                                        .scan(input, 0, static_cast<std::int32_t>(input->size()), 0, -1,
                                              framework::DotnetRegexRunnerMode::FullMatchRequired, 100000)
                                        .success;
                        });
                    }
                } catch (const std::exception&) {
                    keys.clear();
                }
            }
            if (count_only)
                return static_cast<std::int32_t>(keys.size());
            if (deleting) {
                const auto password = std::get<std::string>(evaluate(*e.right));
                return std::make_shared<const LslList>(
                    host.linkset_data_delete_keys
                        ? host.linkset_data_delete_keys(context.object, keys, password)
                        : LslList{{0, 0}});
            }
            std::stable_sort(keys.begin(), keys.end(), [](const auto& left, const auto& right) {
                return framework::invariant_compare(left, right) < 0;
            });
            LslList result;
            const auto first = std::min(static_cast<std::size_t>(start), keys.size());
            const auto size = count < 1 ? keys.size() - first
                                        : std::min(static_cast<std::size_t>(count), keys.size() - first);
            for (auto i = first; i < first + size; ++i)
                result.items.emplace_back(std::move(keys[i]));
            return std::make_shared<const LslList>(std::move(result));
        }
        case Expression::Op::LinksetDataCount:
        case Expression::Op::LinksetDataAvailable: {
            const bool available = e.op == Expression::Op::LinksetDataAvailable;
            return host.linkset_data_size ? host.linkset_data_size(context.object, available)
                                          : std::int32_t(available ? 32 * 1024 : 0);
        }
        case Expression::Op::LinksetDataWrite: {
            auto key = std::get<std::string>(evaluate(*e.left));
            auto value = e.right ? std::get<std::string>(evaluate(*e.right)) : std::string{};
            auto pass = e.z ? std::get<std::string>(evaluate(*e.z)) : std::string{};
            return host.linkset_data_write
                       ? host.linkset_data_write(context.object, std::move(key), std::move(value),
                                                 std::move(pass), bool(e.z))
                       : std::int32_t{4};
        }
        case Expression::Op::LinksetDataRead: {
            const auto key = std::get<std::string>(evaluate(*e.left));
            const auto pass = e.right ? std::get<std::string>(evaluate(*e.right)) : std::string{};
            return host.linkset_data_read ? host.linkset_data_read(context.object, key, pass) : std::string{};
        }
        case Expression::Op::RegionTimeDilation:
            return double(context.time_dilation);
        case Expression::Op::RegionFPS:
            return host.region_fps ? double(host.region_fps()) : 0.;
        case Expression::Op::SimulatorStats: {
            const auto selector = std::get<std::int32_t>(evaluate(*e.left));
            // LSL selectors map to StatsID values. Negative entries have no backing statistic.
            constexpr std::array stat_ids{
                -1, 2, 3, 4, 5, 6, 7, 8, 9, 10,       // Timing and agent updates
                13, 14, 15, 17, 18, -1, -1, 24,        // Agents, scripts and network
                27, 28, 29, 31, 32, 33, 34, 35, 38,   // Physics, script and simulator timing
            };
            if (selector < 0 || static_cast<std::size_t>(selector) >= stat_ids.size())
                return 0.;
            const auto id = stat_ids[static_cast<std::size_t>(selector)];
            return id >= 0 && host.simulator_stat ? double(host.simulator_stat(id)) : 0.;
        }
        case Expression::Op::RegionName:
            return context.region_name;
        case Expression::Op::ParcelMusicUrl:
            return host.parcel_music_url ? host.parcel_music_url(context.object) : std::string{};
        case Expression::Op::ParcelMediaQuery: {
            const auto selectors = std::get<std::shared_ptr<const LslList>>(evaluate(*e.left));
            const auto media = host.parcel_media ? host.parcel_media(context.object) : std::nullopt;
            if (!media)
                return std::make_shared<const LslList>();

            LslList result;
            for (const auto& selector : selectors->items) {
                const auto command = media_rule_integer(value_text(selector));
                switch (command) {
                case 4:
                    result.items.emplace_back(media->texture.str());
                    break;
                case 5:
                    result.items.emplace_back(media->url);
                    break;
                case 10:
                    result.items.emplace_back(media->type);
                    break;
                case 11:
                    result.items.emplace_back(std::to_string(media->width));
                    result.items.emplace_back(std::to_string(media->height));
                    break;
                case 12:
                    result.items.emplace_back(media->description);
                    break;
                default: {
                    constexpr std::array names{"Stop", "Pause", "Play", "Loop", "Texture", "Url", "Time",
                                               "Agent", "Unload", "AutoAlign", "Type", "Size", "Desc"};
                    const auto name = command >= 0 && static_cast<std::size_t>(command) < names.size()
                                          ? std::string(names[command])
                                          : std::to_string(command);
                    script_error("llParcelMediaQuery: Command not implemented - Parameter not supported yet: " + name);
                    break;
                }
                }
            }
            const auto requested_delay = 2000.f * host.script_delay_factor;
            if (requested_delay >= 10 && requested_delay < 2147483648.f)
                delay(static_cast<std::int32_t>(requested_delay));
            return std::make_shared<const LslList>(std::move(result));
        }
        case Expression::Op::ParcelPrimOwners: {
            const auto point = std::get<framework::Vector3d>(evaluate(*e.left));
            const framework::Vector3 position{float(point.x), float(point.y), float(point.z)};
            auto owners = host.parcel_prim_owners ? host.parcel_prim_owners(position) : LslList{};
            const auto requested = 2000.f * host.script_delay_factor;
            if (requested >= 10.f && requested < 2147483648.f)
                delay(static_cast<std::int32_t>(requested));
            return std::make_shared<const LslList>(std::move(owners));
        }
        case Expression::Op::ParcelMaxPrims:
        case Expression::Op::ParcelPrimCount: {
            const bool capacity = e.op == Expression::Op::ParcelMaxPrims;
            const bool sim_wide = std::get<std::int32_t>(evaluate(capacity ? *e.right : *e.z)) != 0;
            const auto category = capacity ? 0 : std::get<std::int32_t>(evaluate(*e.right));
            const auto point = std::get<framework::Vector3d>(evaluate(*e.left));
            const framework::Vector3 position{float(point.x), float(point.y), float(point.z)};
            if (capacity)
                return host.parcel_max_prims ? host.parcel_max_prims(position, sim_wide) : std::int32_t{0};
            return host.parcel_prim_count ? host.parcel_prim_count(position, category, sim_wide) : std::int32_t{0};
        }
        case Expression::Op::ParcelDetails: {
            const auto selectors = std::get<std::shared_ptr<const LslList>>(evaluate(*e.right));
            const auto position = std::get<framework::Vector3d>(evaluate(*e.left));
            if (!host.parcel_details)
                return std::make_shared<const LslList>(LslList{{std::int32_t{0}}});
            return std::make_shared<const LslList>(host.parcel_details(
                {float(position.x), float(position.y), float(position.z)}, context.group, *selectors));
        }
        case Expression::Op::ScriptDanger: {
            const auto position = std::get<framework::Vector3d>(evaluate(*e.left));
            return std::int32_t(
                !host.script_danger ||
                host.script_danger({float(position.x), float(position.y), float(position.z)}, context.group));
        }
        case Expression::Op::ParcelFlags: {
            const auto position = std::get<framework::Vector3d>(evaluate(*e.left));
            return host.parcel_flags
                       ? host.parcel_flags({float(position.x), float(position.y), float(position.z)})
                       : std::int32_t{0};
        }
        case Expression::Op::LandOwnerAt: {
            const auto position = std::get<framework::Vector3d>(evaluate(*e.left));
            if (host.parcel_owner) {
                const auto owner = host.parcel_owner({float(position.x), float(position.y), 0.f});
                if (owner)
                    return owner->str();
            }
            return framework::Uuid{}.str();
        }
        case Expression::Op::OverMyLand: {
            const auto text = std::get<std::string>(evaluate(*e.left));
            framework::Uuid id;
            if (!framework::try_parse_sdk_uuid(text, id) || id.is_zero() ||
                !host.entity_position || !host.parcel_owner)
                return std::int32_t{0};
            try {
                if (const auto position = host.entity_position(id))
                    return std::int32_t(host.parcel_owner(*position) == context.owner);
            } catch (...) {
                // The original returns false if the entity or land lookup fails.
            }
            return std::int32_t{0};
        }
        case Expression::Op::AgentList: {
            evaluate(*e.right);
            const auto scope = std::get<std::int32_t>(evaluate(*e.left)) & ~0x4000000;
            auto result = std::make_shared<LslList>();
            if (scope != 1 && scope != 2 && scope != 4)
                result->items.emplace_back(std::string("INVALID_SCOPE"));
            else if (host.region_agents) {
                for (const auto& id : host.region_agents(scope, context.position))
                    result->items.emplace_back(id.str());
            }
            return std::shared_ptr<const LslList>(std::move(result));
        }
        case Expression::Op::InertiaData: {
            check_safe_ossl_permission();
            auto result = std::make_shared<LslList>();
            const auto data = host.inertia_data ? host.inertia_data(context.object) : std::nullopt;
            if (data) {
                const auto vector = [](framework::Vector3 value, float scale) {
                    return framework::Vector3d{value.x * scale, value.y * scale, value.z * scale};
                };
                const float scale = data->mass > 0.f ? 1.f / data->mass : 1.f;
                result->items = {double(data->mass), vector(data->center, 1.f),
                                 vector(data->diagonal, scale), vector(data->products, scale)};
            }
            return std::shared_ptr<const LslList>(std::move(result));
        }
        case Expression::Op::RegionAgentNames: {
            check_ossl_permission("osGetAgents");
            auto result = std::make_shared<LslList>();
            if (host.region_agent_names) {
                for (auto& name : host.region_agent_names())
                    result->items.emplace_back(std::move(name));
            }
            return std::shared_ptr<const LslList>(std::move(result));
        }
        case Expression::Op::ApparentSunHour:
        case Expression::Op::ApparentTime:
        case Expression::Op::ApparentTimeString: {
            const bool format24 = e.left && std::get<std::int32_t>(evaluate(*e.left)) != 0;
            check_safe_ossl_permission();
            const auto fraction = host.region_day_fraction ? host.region_day_fraction() : 0.f;
            if (e.op == Expression::Op::ApparentTime)
                return static_cast<double>(86400.f * fraction);
            auto hours = 24.f * fraction;
            if (e.op == Expression::Op::ApparentSunHour)
                return static_cast<double>(hours);
            const auto whole = [](float value) {
                if (value >= -2147483648.f && value < 2147483648.f)
                    return static_cast<int>(value);
                return std::numeric_limits<int>::min();
            };
            const auto hour = whole(hours);
            auto minutes = (hours - hour) * 60.f;
            const auto minute = whole(minutes);
            const auto second = whole((minutes - minute) * 60.f);
            char text[64];
            if (format24)
                std::snprintf(text, sizeof text, "%02d:%02d:%02d", hour, minute, second);
            else
                std::snprintf(text, sizeof text, "%d:%02d:%02d %s", hour > 12 ? hour - 12 : hour,
                              minute, second, hour >= 12 ? "PM" : "AM");
            return std::string(text);
        }
        case Expression::Op::SunParameter: {
            const auto parameter = std::get<std::string>(evaluate(*e.left));
            if (e.slot != 0) {
                check_ossl_permission("osSunGetParam");
                script_error("Use of function osSunGetParam is deprecated. Use osGetSunParam instead.");
            } else {
                check_safe_ossl_permission();
            }
            const auto name = framework::invariant_lower_utf16(framework::utf16(parameter));
            if (name == u"day_length")
                return static_cast<double>(host.environment_timing
                    ? host.environment_timing(current_world_position(), false).first : 14400);
            if (name == u"year_length")
                return 365.;
            if (name == u"update_interval")
                return 0.1;
            if (name == u"day_time_sun_hour_scale")
                return 1.;
            return 0.;
        }
        case Expression::Op::EnvironmentQuery: {
            const auto& query = lsl_environment_queries[e.slot];
            if (query.type == Type::Integer) {
                const auto [length, offset] = host.environment_timing
                    ? host.environment_timing(query.region ? context.position : current_world_position(), query.region)
                    : std::pair<std::int32_t, std::int32_t>{14400, 57600};
                return query.field == EnvironmentField::DayLength ? length : offset;
            }
            const bool moon = query.field == EnvironmentField::MoonDirection ||
                              query.field == EnvironmentField::MoonRotation;
            const auto rotation = host.environment_rotation
                ? host.environment_rotation(current_world_position(), query.region, moon) : std::nullopt;
            if (query.type == Type::Rotation)
                return rotation.value_or(LslRotation{});
            if (!rotation)
                return framework::Vector3d{};
            auto x = static_cast<float>(rotation->x), y = static_cast<float>(rotation->y);
            auto z = static_cast<float>(rotation->z), w = static_cast<float>(rotation->w);
            const auto squared_length = framework::sdk_quaternion_length_squared({x, y, z, w});
            if (!(squared_length >= 1e-6f))
                return framework::Vector3d{1, 0, 0};
            const auto length = std::sqrt(squared_length);
            if (framework::sdk_sse41_supported()) {
                x /= length;
                y /= length;
                z /= length;
                w /= length;
            } else {
                const auto inverse = 1.f / length;
                x *= inverse;
                y *= inverse;
                z *= inverse;
                w *= inverse;
            }
            return framework::Vector3d{2.f * (x*x + w*w) - 1.f, 2.f * (x*y + z*w), 2.f * (x*z - y*w)};
        }
        case Expression::Op::ReplaceAgentEnvironment: {
            const auto cycle = std::get<std::string>(evaluate(*e.elements[2]));
            const auto transition = std::get<std::int32_t>(evaluate(*e.elements[1]));
            const auto avatar = std::get<std::string>(evaluate(*e.elements[0]));
            return host.replace_agent_environment
                ? host.replace_agent_environment(context.object, context.item, avatar, transition, cycle)
                : std::int32_t{-2};
        }
        case Expression::Op::ReplaceParcelEnvironment: {
            const auto cycle = std::get<std::string>(evaluate(*e.elements[1]));
            const auto transition = std::get<std::int32_t>(evaluate(*e.elements[0]));
            return host.replace_parcel_environment ? host.replace_parcel_environment(context.object, transition, cycle)
                                                    : std::int32_t{-1};
        }
        case Expression::Op::ReplaceRegionEnvironment: {
            LslRegionEnvironmentEdit update;
            for (std::size_t i = update.altitudes.size(); i > 0; --i)
                update.altitudes[i - 1] = list_float(evaluate(*e.elements[i + 3]));
            update.day_offset = list_float(evaluate(*e.elements[3]));
            update.day_length = list_float(evaluate(*e.elements[2]));
            update.day_cycle = std::get<std::string>(evaluate(*e.elements[1]));
            update.transition = std::get<std::int32_t>(evaluate(*e.elements[0]));
            return host.replace_region_environment ? host.replace_region_environment(context.object, update)
                                                   : std::int32_t{-3};
        }
        case Expression::Op::DrawingCommand:
            return evaluate_drawing_command(e);
        case Expression::Op::LegacyCommand: {
            const auto& command = legacy_commands[e.slot];
            if (command.name == "llRemoteDataReply") {
                const auto integer = std::get<std::int32_t>(evaluate(*e.elements[3]));
                auto text = std::get<std::string>(evaluate(*e.elements[2]));
                auto message = std::get<std::string>(evaluate(*e.elements[1]));
                auto channel = std::get<std::string>(evaluate(*e.elements[0]));
                if (host.reply_remote_data)
                    host.reply_remote_data(channel, message, std::move(text), integer);
            } else {
                for (const auto& argument : e.elements)
                    evaluate(*argument);
            }
            if (!command.report_deprecation) {
                const auto milliseconds = command.script_delay * host.script_delay_factor;
                if (milliseconds >= 10 && double(milliseconds) <= std::numeric_limits<std::int32_t>::max())
                    delay(static_cast<std::int32_t>(milliseconds));
                return std::int32_t{0};
            }
            auto message = std::string(command.name) + ": Command deprecated";
            if (!command.replacement.empty())
                message += " - Use " + std::string(command.replacement) + " instead";
            // YEngine replaces the error delay when the command schedules its own sleep.
            const auto requested_delay = command.script_delay * host.script_delay_factor;
            const auto sleep_ms = requested_delay >= 10 &&
                    double(requested_delay) <= std::numeric_limits<std::int32_t>::max()
                ? static_cast<std::int32_t>(requested_delay) : 1000;
            script_error(std::move(message), sleep_ms);
            return std::int32_t{0};
        }
        case Expression::Op::Listen: {
            const auto channel = std::get<std::int32_t>(evaluate(*e.left));
            auto name = std::get<std::string>(evaluate(*e.right));
            const auto key = std::get<std::string>(evaluate(*e.z));
            auto message = std::get<std::string>(evaluate(*e.w));
            const bool regex = !e.elements.empty();
            const auto flags = regex ? std::get<std::int32_t>(evaluate(*e.elements.front())) : 0;
            if (regex)
                check_ossl_permission("osListenRegex");
            if (!host.add_listener)
                return std::int32_t{-1};
            framework::Uuid sender;
            if (!framework::try_parse_sdk_uuid(key, sender) && regex)
                return std::int32_t{-1};

            if (regex) {
                const auto valid_pattern = [](std::string_view pattern) {
                    using namespace framework;
                    try {
                        dotnet_regex_is_match({}, pattern, 100000);
                        return true;
                    } catch (const DotnetRegexClassError&) {
                        return false;
                    }
                };
                if ((flags & listen_regex_name) && !valid_pattern(name)) {
                    script_error("Name regex is invalid.");
                    return std::int32_t{-1};
                }
                if ((flags & listen_regex_message) && !valid_pattern(message)) {
                    script_error("Message regex is invalid.");
                    return std::int32_t{-1};
                }
            }
            return std::int32_t(host.add_listener(context.item, context.object, channel, std::move(name),
                                                  sender, std::move(message), flags));
        }
        case Expression::Op::Sensor: {
            if (e.elements.empty()) {
                if (host.clear_sensors)
                    host.clear_sensors(context.item);
                return std::int32_t{0};
            }
            LslSensorQuery query{};
            query.name = std::get<std::string>(evaluate(*e.elements[0]));
            const auto key = std::get<std::string>(evaluate(*e.elements[1]));
            framework::try_parse_sdk_uuid(key, query.key);
            query.type = std::get<std::int32_t>(evaluate(*e.elements[2]));
            query.range = list_float(evaluate(*e.elements[3]));
            query.arc = list_float(evaluate(*e.elements[4]));
            if (e.elements.size() == 6)
                query.interval = list_float(evaluate(*e.elements[5]));
            if (host.sensor)
                host.sensor(context.object, context.item, std::move(query));
            return std::int32_t{0};
        }
        case Expression::Op::CreateLink: {
            const auto target = std::get<std::string>(evaluate(*e.left));
            const auto parent = std::get<std::int32_t>(evaluate(*e.right)) != 0;
            if (!host.create_link)
                return std::int32_t{0};
            switch (host.create_link(context.object, context.item, target, parent)) {
            case LslLinkResult::Completed: {
                const float milliseconds = 1000.f * host.script_delay_factor;
                if (milliseconds >= 10.f && milliseconds < 2147483648.f)
                    delay(static_cast<std::int32_t>(milliseconds));
                break;
            }
            case LslLinkResult::MissingPermission:
                script_error("llCreateLink: PERMISSION_CHANGE_LINKS required");
                break;
            case LslLinkResult::WrongGranter:
                script_error("llCreateLink: PERMISSION_CHANGE_LINKS not set by script owner");
                break;
            case LslLinkResult::Ignored:
                break;
            }
            return std::int32_t{0};
        }
        case Expression::Op::BreakLink: {
            std::optional<std::int32_t> link;
            if (e.left)
                link = std::get<std::int32_t>(evaluate(*e.left));
            if (host.break_link &&
                host.break_link(context.object, context.item, link) == LslLinkResult::MissingPermission) {
                script_error(link ? "llBreakLink: PERMISSION_CHANGE_LINKS permission not set" :
                    "llBreakAllLinks: Script trying to link but PERMISSION_CHANGE_LINKS permission not set!");
            }
            return std::int32_t{0};
        }
        case Expression::Op::AttachToAvatar: {
            const auto point = std::get<std::int32_t>(evaluate(*e.left));
            if (host.attach_to_avatar)
                host.attach_to_avatar(context.object, context.item, point, e.slot != 0);
            return std::int32_t{0};
        }
        case Expression::Op::DetachFromAvatar:
            if (host.detach_from_avatar)
                host.detach_from_avatar(context.object, context.item);
            return std::int32_t{0};
        case Expression::Op::SetRemoteScriptPin: {
            const auto pin = std::get<std::int32_t>(evaluate(*e.left));
            if (host.set_remote_script_pin)
                host.set_remote_script_pin(context.object, pin);
            return std::int32_t{0};
        }
        case Expression::Op::RemoteLoadScript: {
            const auto target_text = std::get<std::string>(evaluate(*e.elements[0]));
            const auto name = std::get<std::string>(evaluate(*e.elements[1]));
            const auto pin = std::get<std::int32_t>(evaluate(*e.elements[2]));
            const auto running = std::get<std::int32_t>(evaluate(*e.elements[3])) != 0;
            const auto start_parameter = std::get<std::int32_t>(evaluate(*e.elements[4]));
            framework::Uuid target;
            if (!framework::try_parse_sdk_uuid(target_text, target) || target.is_zero()) {
                script_error("llRemoteLoadScriptPin: invalid key '" + target_text + "'");
            } else if (target != context.object && host.remote_load_script) {
                switch (host.remote_load_script(context.object, target, name, pin, running, start_parameter)) {
                case LslRemoteScriptLoad::MissingScript:
                    script_error("llRemoteLoadScriptPin: Can't find script '" + name + "'");
                    break;
                case LslRemoteScriptLoad::NoCopy:
                    script_error("llRemoteLoadScriptPin: No copy rights");
                    break;
                case LslRemoteScriptLoad::Attempted:
                    delay(3000);
                    break;
                }
            }
            return std::int32_t{0};
        }
        case Expression::Op::RequestEmail: {
            auto address = std::get<std::string>(evaluate(*e.left));
            auto subject = std::get<std::string>(evaluate(*e.right));
            if (host.request_email)
                host.request_email(context.object, std::move(address), std::move(subject));
            else
                script_error("llGetNextEmail: Email module not configured");
            return std::int32_t{0};
        }
        case Expression::Op::SendEmail: {
            const auto recipient_value = evaluate(*e.left);
            auto subject = std::get<std::string>(evaluate(*e.right));
            auto message = std::get<std::string>(evaluate(*e.z));
            if (!host.send_email) {
                script_error(e.slot ? "llTargetedEmail: Email module not configured" :
                                      "llEmail: Email module not configured");
                return std::int32_t{0};
            }
            if (e.slot && framework::utf16(subject).size() + framework::utf16(message).size() > 4096) {
                script_error("llTargetedEmail: Message is too large");
                return std::int32_t{0};
            }
            std::variant<std::string, std::int32_t> recipient;
            if (e.slot)
                recipient = std::get<std::int32_t>(recipient_value);
            else
                recipient = std::get<std::string>(recipient_value);
            host.send_email(context.object, context.item, std::move(recipient), std::move(subject), std::move(message));
            delay(host.email_delay_ms);
            return std::int32_t{0};
        }
        case Expression::Op::ListenControl:
        case Expression::Op::ListenRemove: {
            const auto handle = std::get<std::int32_t>(evaluate(*e.left));
            const auto active = e.right
                ? std::optional(std::get<std::int32_t>(evaluate(*e.right)) != 0) : std::nullopt;
            if (host.change_listener)
                host.change_listener(context.item, handle, active);
            return std::int32_t{0};
        }
        case Expression::Op::RegionSayTo: {
            const auto key = std::get<std::string>(evaluate(*e.left));
            const auto channel = std::get<std::int32_t>(evaluate(*e.right));
            const auto text = std::get<std::string>(evaluate(*e.z));
            framework::Uuid target;
            if (host.direct_chat && channel != std::numeric_limits<std::int32_t>::max() &&
                framework::try_parse_sdk_uuid(key, target) && !target.is_zero())
                host.direct_chat(context.object, target, channel, framework::truncate_utf16(text, 1023));
            return std::int32_t{0};
        }
        case Expression::Op::AgentIpAddress: {
            const auto key = std::get<std::string>(evaluate(*e.left));
            check_ossl_permission("osGetAgentIP");
            framework::Uuid agent;
            if (!host.avatar_ip_address || !framework::try_parse_sdk_uuid(key, agent))
                return std::string{};
            return host.avatar_ip_address(context.object, agent);
        }
        case Expression::Op::SameGroup:
        case Expression::Op::IsFriend: {
            const auto text = std::get<std::string>(evaluate(*e.left));
            framework::Uuid id;
            if (!framework::try_parse_sdk_uuid(text, id) || id.is_zero())
                return std::int32_t{0};
            if (e.op == Expression::Op::IsFriend) {
                if (!host.object_friendship)
                    return std::int32_t{0};
                if (const auto result = host.object_friendship(context.object, id))
                    return std::int32_t(*result);
            }
            if (id == context.group || (id == context.object && context.attachment_point == 0))
                return std::int32_t{1};
            if (host.object_group) {
                if (const auto group = host.object_group(id))
                    return std::int32_t(*group == context.group);
            }
            return std::int32_t{0};
        }
        case Expression::Op::AgentLanguage: {
            const auto text = std::get<std::string>(evaluate(*e.left));
            framework::Uuid id;
            if (!host.agent_language)
                script_error("llGetAgentLanguage: No AgentPreferencesService present");
            else if (framework::try_parse_sdk_uuid(text, id) && !id.is_zero())
                return host.agent_language(id);
            return std::string("en-us");
        }
        case Expression::Op::RequestAgentData: {
            const auto text = std::get<std::string>(evaluate(*e.left));
            const auto selector = std::get<std::int32_t>(evaluate(*e.right));
            if (selector < 1 || selector > 8)
                return std::string{};
            framework::Uuid id;
            if (!framework::try_parse_sdk_uuid(text, id) || id.is_zero()) {
                script_error("llRequestAgentData: Invalid UUID passed to llRequestAgentData.");
                return std::string{};
            }
            if (selector == 5 || selector == 6 || !host.request_agent_data)
                return std::string{};
            const auto query = host.request_agent_data(context.object, context.item, id, selector);
            const auto requested_delay = 100.f * host.script_delay_factor;
            if (requested_delay >= 10 && requested_delay < 2147483648.f)
                delay(static_cast<std::int32_t>(requested_delay));
            return query.str();
        }
        case Expression::Op::RequestSimulatorData:
        case Expression::Op::RequestInventoryData: {
            auto name = std::get<std::string>(evaluate(*e.left));
            framework::Uuid query;
            if (e.op == Expression::Op::RequestSimulatorData) {
                const auto selector = std::get<std::int32_t>(evaluate(*e.right));
                if (host.request_simulator_data)
                    query = host.request_simulator_data(context.object, context.item, std::move(name), selector);
            } else if (host.request_inventory_data) {
                query = host.request_inventory_data(context.object, context.item, std::move(name));
            }
            const auto requested_delay = 1000.f * host.script_delay_factor;
            if (requested_delay >= 10 && requested_delay < 2147483648.f)
                delay(static_cast<std::int32_t>(requested_delay));
            return query.str();
        }
        case Expression::Op::NotecardLineSync:
        case Expression::Op::NotecardLine:
        case Expression::Op::NotecardLineCount: {
            using Op = Expression::Op;
            const auto name = std::get<std::string>(evaluate(*e.left));
            std::optional<std::int32_t> line;
            if (e.right)
                line = std::get<std::int32_t>(evaluate(*e.right));
            const bool sync = e.op == Op::NotecardLineSync;
            constexpr auto nak = "\n\x15\n";
            if (sync && *line < 0)
                return std::string(nak);
            framework::Uuid parsed;
            std::optional<framework::Uuid> asset;
            if (framework::try_parse_sdk_uuid(name, parsed))
                asset = parsed;
            else if (host.notecard_asset)
                asset = host.notecard_asset(context.object, name, sync);
            if (!asset || (!sync && asset->is_zero())) {
                const auto api = sync ? "llGetNotecardLineSync"
                                     : e.right ? "llGetNotecardLine" : "llGetNumberOfNotecardLines";
                script_error(std::string(api) + ": Can't find notecard '" + name + "'");
                return sync ? std::string(nak) : framework::Uuid{}.str();
            }
            if (sync)
                return host.cached_notecard_line ? host.cached_notecard_line(*asset, *line) : std::string(nak);
            if (!host.request_notecard)
                return framework::Uuid{}.str();
            const auto query = host.request_notecard(context.object, context.item, *asset, line);
            const auto requested_delay = 100.f * host.script_delay_factor;
            if (requested_delay >= 10 && requested_delay < 2147483648.f)
                delay(static_cast<std::int32_t>(requested_delay));
            return query.str();
        }
        case Expression::Op::RequestUsername:
        case Expression::Op::RequestDisplayName: {
            const auto text = std::get<std::string>(evaluate(*e.left));
            framework::Uuid id;
            if (!framework::try_parse_sdk_uuid(text, id) || id.is_zero() || !host.request_identity_name)
                return std::string{};
            const bool username = e.op == Expression::Op::RequestUsername;
            const auto [query, immediate] = host.request_identity_name(
                context.object, context.item, id, username ? username_from_name : nullptr);
            const auto requested_delay = 100.f * host.script_delay_factor;
            if (username && !immediate && requested_delay >= 10 && requested_delay < 2147483648.f)
                delay(static_cast<std::int32_t>(requested_delay));
            return query.str();
        }
        case Expression::Op::KeyToName:
        case Expression::Op::Username:
        case Expression::Op::DisplayName: {
            using Op = Expression::Op;
            const auto text = std::get<std::string>(evaluate(*e.left));
            framework::Uuid id;
            if (!framework::try_parse_sdk_uuid(text, id) || id.is_zero())
                return std::string{};
            auto name = host.avatar_name ? host.avatar_name(id) : std::nullopt;
            if (!name && e.op != Op::DisplayName && host.object_name)
                name = host.object_name(id);
            if (!name)
                return std::string{};
            return e.op == Op::Username ? username_from_name(*name) : *name;
        }
        case Expression::Op::RequestUserKey:
        case Expression::Op::NameToKey: {
            auto name = parse_avatar_name(std::get<std::string>(evaluate(*e.left)));
            if (!name)
                return framework::Uuid{}.str();
            if (e.op == Expression::Op::NameToKey && host.avatar_key_by_name)
                return host.avatar_key_by_name(name->scene_name()).str();
            if (e.op == Expression::Op::RequestUserKey && host.request_identity_key) {
                const auto [query, immediate] = host.request_identity_key(
                    context.object, context.item, std::move(*name));
                const auto requested_delay = 100.f * host.script_delay_factor;
                if (!immediate && requested_delay >= 10 && requested_delay < 2147483648.f)
                    delay(static_cast<std::int32_t>(requested_delay));
                return query.str();
            }
            return framework::Uuid{}.str();
        }
        case Expression::Op::VisualParams: {
            const auto selectors = std::get<std::shared_ptr<const LslList>>(evaluate(*e.right));
            const auto key = std::get<std::string>(evaluate(*e.left));
            auto result = std::make_shared<LslList>();
            framework::Uuid id;
            if (selectors->items.empty() || !host.agent_visual_params || !framework::try_parse_sdk_uuid(key, id))
                return std::shared_ptr<const LslList>(std::move(result));
            const auto parameters = host.agent_visual_params(id);
            if (!parameters)
                return std::shared_ptr<const LslList>(std::move(result));

            using Param = framework::AvatarVisualParameter;
            struct Selector {
                std::string_view id;
                std::string_view name;
                Param index;
            };
            constexpr Selector supported[]{
                {"33", "height", Param::SHAPE_HEIGHT},
                {"38", "torso_length", Param::SHAPE_TORSO_LENGTH},
                {"80", "male", Param::SHAPE_MALE},
                {"198", "heel_height", Param::SHOES_HEEL_HEIGHT},
                {"503", "platform_height", Param::SHOES_PLATFORM_HEIGHT},
                {"616", "shoe_height", Param::SHOES_SHOE_HEIGHT},
                {"675", "hand_size", Param::SHAPE_HAND_SIZE},
                {"682", "head_size", Param::SHAPE_HEAD_SIZE},
                {"692", "leg_length", Param::SHAPE_LEG_LENGTH},
                {"693", "arm_length", Param::SHAPE_ARM_LENGTH},
                {"756", "neck_length", Param::SHAPE_NECK_LENGTH},
                {"814", "waist_height", Param::PANTS_WAIST_HEIGHT},
                {"842", "hip_length", Param::SHAPE_HIP_LENGTH},
                {"11001", "hover", Param::SHAPE_HOVER},
            };
            result->items.reserve(selectors->items.size());
            for (const auto& selector : selectors->items) {
                const auto text = value_text(selector);
                const auto match = std::find_if(std::begin(supported), std::end(supported), [&](const auto& entry) {
                    return text == entry.id || text == entry.name;
                });
                if (match == std::end(supported)) {
                    result->items.emplace_back(std::string{});
                    continue;
                }
                const auto byte = parameters->at(static_cast<std::size_t>(match->index));
                const float value = std::nearbyint(byte * .0039215686f * 1000000.f) / 1000000.f;
                char buffer[16];
                const auto encoded = std::to_chars(buffer, buffer + sizeof buffer, value, std::chars_format::fixed, 6);
                std::string formatted(buffer, encoded.ptr);
                formatted.erase(formatted.find_last_not_of('0') + 1);
                if (formatted.back() == '.')
                    formatted.pop_back();
                result->items.emplace_back(std::move(formatted));
            }
            return std::shared_ptr<const LslList>(std::move(result));
        }
        case Expression::Op::BoundingBox: {
            const auto text = std::get<std::string>(evaluate(*e.left));
            framework::Uuid id;
            std::array<framework::Vector3d, 2> bounds{};
            if (host.bounding_box && framework::try_parse_sdk_uuid(text, id) && !id.is_zero())
                bounds = host.bounding_box(id);
            return std::make_shared<const LslList>(LslList{{bounds[0], bounds[1]}});
        }
        case Expression::Op::AgentHealRate: {
            const auto key = std::get<std::string>(evaluate(*e.left));
            check_ossl_permission("osGetHealRate");
            framework::Uuid id;
            if (host.agent_heal_rate && framework::try_parse_sdk_uuid(key, id))
                return static_cast<double>(host.agent_heal_rate(id));
            return 0.0;
        }
        case Expression::Op::AgentHealth: {
            const auto key = std::get<std::string>(evaluate(*e.left));
            framework::Uuid id;
            if (host.agent_health && framework::try_parse_sdk_uuid(key, id)) {
                if (const auto health = host.agent_health(id))
                    return static_cast<double>(*health);
            }
            return -1.0;
        }
        case Expression::Op::AgentInfo: {
            const auto text = std::get<std::string>(evaluate(*e.left));
            framework::Uuid id;
            if (host.agent_info && framework::try_parse_sdk_uuid(text, id) && !id.is_zero())
                return host.agent_info(id);
            return std::int32_t{0};
        }
        case Expression::Op::AgentSize: {
            const auto text = std::get<std::string>(evaluate(*e.left));
            framework::Uuid id;
            if (!host.agent_size || !framework::try_parse_sdk_uuid(text, id) || id.is_zero())
                return framework::Vector3d{};
            const auto size = host.agent_size(id);
            return framework::Vector3d{size.x, size.y, size.z};
        }
        case Expression::Op::LookupOwnerKey: {
            const auto text = std::get<std::string>(evaluate(*e.left));
            framework::Uuid id;
            if (!framework::try_parse_sdk_uuid(text, id))
                return framework::Uuid{}.str();
            if (id == context.object && !id.is_zero())
                return context.owner.str();
            if (!id.is_zero() && host.object_owner) {
                if (const auto owner = host.object_owner(id))
                    return owner->str();
            }
            return text;
        }
        case Expression::Op::AttachedList: {
            const auto text = std::get<std::string>(evaluate(*e.left));
            framework::Uuid id;
            if (host.attached_list && framework::try_parse_sdk_uuid(text, id) && !id.is_zero())
                return std::make_shared<const LslList>(host.attached_list(id));
            auto result = std::make_shared<LslList>();
            result->items.emplace_back(std::string("NOT_FOUND"));
            return std::shared_ptr<const LslList>(std::move(result));
        }
        case Expression::Op::ObjectPrimCount: {
            const auto text = std::get<std::string>(evaluate(*e.left));
            framework::Uuid id;
            if (host.prim_count && framework::try_parse_sdk_uuid(text, id) && !id.is_zero())
                return host.prim_count(id, false);
            return std::int32_t{0};
        }
        case Expression::Op::ObjectMass: {
            const auto key = std::get<std::string>(evaluate(*e.left));
            framework::Uuid id;
            if (host.object_mass && framework::try_parse_sdk_uuid(key, id) && !id.is_zero())
                return double(host.object_mass(id));
            return 0.;
        }
        case Expression::Op::ScriptMass:
        case Expression::Op::ScriptMassMks: {
            const float mass = host.script_mass ? host.script_mass(context.object) : 0.f;
            return double(e.op == Expression::Op::ScriptMassMks ? 100.f * mass : mass);
        }
        case Expression::Op::PrimCount:
            return host.prim_count ? host.prim_count(context.object, true) : std::int32_t{1};
        case Expression::Op::RenderMaterial: {
            const auto face = std::get<std::int32_t>(evaluate(*e.left));
            return host.render_material ? host.render_material(context.object, face) : framework::Uuid{}.str();
        }
        case Expression::Op::LinkHasRenderMaterial: {
            const auto link = std::get<std::int32_t>(evaluate(*e.left));
            const auto face = std::get<std::int32_t>(evaluate(*e.right));
            const auto part = link == -4 ? context.object
                             : link >= 0 && host.link_key ? host.link_key(context.object, link)
                                                         : framework::Uuid{};
            return std::int32_t(!part.is_zero() && host.has_render_material &&
                                host.has_render_material(part, face));
        }
        case Expression::Op::LinkSides: {
            const auto number = e.left ? std::get<std::int32_t>(evaluate(*e.left)) : -4;
            if (!host.face_count)
                return std::int32_t{0};
            if (number == -4)
                return host.face_count(context.object);
            if (number >= 0 && host.link_key) {
                const auto id = host.link_key(context.object, number);
                if (!id.is_zero())
                    return host.face_count(id);
            }
            return std::int32_t{0};
        }
        case Expression::Op::LinkName: {
            const auto number = std::get<std::int32_t>(evaluate(*e.left));
            if (number == -4)
                return context.object_name;
            if (number >= 0 && host.link_key && host.object_name) {
                const auto id = host.link_key(context.object, number);
                if (!id.is_zero()) {
                    if (const auto name = host.object_name(id))
                        return *name;
                    if (host.avatar_name)
                        if (const auto name = host.avatar_name(id))
                            return *name;
                }
            }
            return framework::Uuid{}.str();
        }
        case Expression::Op::SetPrimMedia:
        case Expression::Op::ClearPrimMedia:
        case Expression::Op::PrimMedia: {
            const auto rules = e.right ? std::get<std::shared_ptr<const LslList>>(evaluate(*e.right)) : nullptr;
            const auto face = std::get<std::int32_t>(evaluate(*e.left));
            const auto link = e.z ? std::get<std::int32_t>(evaluate(*e.z)) : -4;
            const auto requested_delay = 1000.f * host.script_delay_factor;
            if (requested_delay >= 10 && requested_delay < 2147483648.f)
                delay(static_cast<std::int32_t>(requested_delay));
            if (e.op != Expression::Op::PrimMedia) {
                if (!host.edit_prim_media)
                    return static_cast<std::int32_t>(LslMediaStatus::NotSupported);
                LslMediaEdit edit;
                if (rules)
                    edit = [&](framework::ShapeMediaEntry& entry) { return apply_media_rules(entry, *rules); };
                return static_cast<std::int32_t>(host.edit_prim_media(context.object, link, face, edit));
            }
            const auto media = host.prim_media ? host.prim_media(context.object, link, face) : nullptr;
            if (!media)
                return std::make_shared<const LslList>();

            LslList result;
            for (const auto& rule : rules->items) {
                auto& items = result.items;
                switch (media_rule_integer(rule)) {
                case 0:
                    items.emplace_back(0);
                    break;
                case 1:
                    items.emplace_back(std::int32_t(media->controls != 0));
                    break;
                case 2:
                    items.emplace_back(framework::utf8(media->current_url.value_or(u"")));
                    break;
                case 3:
                    items.emplace_back(framework::utf8(media->home_url.value_or(u"")));
                    break;
                case 4:
                    items.emplace_back(std::int32_t(media->auto_loop));
                    break;
                case 5:
                    items.emplace_back(std::int32_t(media->auto_play));
                    break;
                case 6:
                    items.emplace_back(std::int32_t(media->auto_scale));
                    break;
                case 7:
                    items.emplace_back(std::int32_t(media->auto_zoom));
                    break;
                case 8:
                    items.emplace_back(std::int32_t(media->interact_on_first_click));
                    break;
                case 9:
                    items.emplace_back(media->width);
                    break;
                case 10:
                    items.emplace_back(media->height);
                    break;
                case 11:
                    items.emplace_back(std::int32_t(media->enable_whitelist));
                    break;
                case 12: {
                    if (!media->whitelist)
                        throw std::runtime_error("Media whitelist is missing");
                    std::u16string urls;
                    for (std::size_t i = 0; i < media->whitelist->size(); ++i) {
                        const auto& url = (*media->whitelist)[i];
                        if (!url)
                            throw std::runtime_error("Media whitelist entry is missing");
                        if (i)
                            urls += u", ";
                        urls += framework::config_uri_escape(*url, false, framework::ConfigUriEscapeSet::Unreserved);
                    }
                    items.emplace_back(framework::utf8(urls));
                    break;
                }
                case 13:
                    items.emplace_back(std::int32_t(media->interact_permissions));
                    break;
                case 14:
                    items.emplace_back(std::int32_t(media->control_permissions));
                    break;
                default:
                    return std::make_shared<const LslList>(LslList{{std::int32_t{1000}}});
                }
            }
            return std::make_shared<const LslList>(std::move(result));
        }
        case Expression::Op::LinkKey: {
            const auto number = std::get<std::int32_t>(evaluate(*e.left));
            if (number == -4)
                return context.object.str();
            if (number >= 0 && host.link_key)
                return host.link_key(context.object, number).str();
            return framework::Uuid{}.str();
        }
        case Expression::Op::LinkNumber:
            return host.link_number ? host.link_number(context.object) : std::int32_t{0};
        case Expression::Op::AttachmentPoint:
            return host.attachment_point ? host.attachment_point(context.object) : context.attachment_point;
        case Expression::Op::DerezObject: {
            const auto temporary = std::get<std::int32_t>(evaluate(*e.right)) != 0;
            const auto key = std::get<std::string>(evaluate(*e.left));
            framework::Uuid target;
            return std::int32_t(host.derez_object && framework::try_parse_sdk_uuid(key, target) &&
                                !target.is_zero() && host.derez_object(context.object, target, temporary));
        }
        case Expression::Op::RegionFlags:
            return host.region_flags ? host.region_flags() : std::int32_t{67108864};
        case Expression::Op::RegionAgentCount:
            return host.region_agent_count ? host.region_agent_count() : std::int32_t{0};
        case Expression::Op::RegionCorner:
            return context.region_corner;
        case Expression::Op::ObjectVelocity:
        case Expression::Op::ObjectOmega:
        case Expression::Op::ObjectAcceleration: {
            const auto& vector = e.op == Expression::Op::ObjectVelocity ? context.velocity
                                 : e.op == Expression::Op::ObjectOmega  ? context.omega
                                                                        : context.acceleration;
            return framework::Vector3d{vector.x, vector.y, vector.z};
        }
        case Expression::Op::ObjectScale: {
            const auto scale = host.object_scale ? host.object_scale(context.object) : std::nullopt;
            const auto current = scale.value_or(context.object_scale);
            return framework::Vector3d{current.x, current.y, current.z};
        }
        case Expression::Op::GroupMinScaleFactor:
        case Expression::Op::GroupMaxScaleFactor:
            return host.group_scale_limit
                ? host.group_scale_limit(context.object, e.op == Expression::Op::GroupMaxScaleFactor) : 1.;
        case Expression::Op::ScaleGroup: {
            const auto factor = evaluate(*e.left);
            const double value = std::holds_alternative<double>(factor) ? std::get<double>(factor)
                                                                       : std::get<std::int32_t>(factor);
            return std::int32_t(host.scale_group && host.scale_group(context.object, value));
        }
        case Expression::Op::ObjectWorldRotation:
            return object_rotation(LslRotationSpace::World);
        case Expression::Op::ObjectRootRotation:
            return object_rotation(LslRotationSpace::Root);
        case Expression::Op::ObjectLocalRotation:
            return object_rotation(LslRotationSpace::Local);
        case Expression::Op::CenterOfMass:
        case Expression::Op::GeometricCenter: {
            const auto& query = e.op == Expression::Op::CenterOfMass ? host.center_of_mass : host.geometric_center;
            const auto center = query ? query(context.object) : framework::Vector3{};
            return framework::Vector3d{center.x, center.y, center.z};
        }
        case Expression::Op::SetRegionPosition: {
            const auto position = std::get<framework::Vector3d>(evaluate(*e.left));
            return std::int32_t(host.set_object_position && host.set_object_position(context.object, position, true));
        }
        case Expression::Op::ObjectStatus: {
            const auto status = std::get<std::int32_t>(evaluate(*e.left));
            return std::int32_t(host.object_status && host.object_status(context.object, status));
        }
        case Expression::Op::ObjectLocalPosition: {
            if (host.object_position)
                if (const auto value = host.object_position(context.object, true, false))
                    return framework::Vector3d{value->x, value->y, value->z};
            const auto& position = (context.attachment_point || context.root_position)
                                       ? context.attachment_offset
                                       : context.position;
            return framework::Vector3d{position.x, position.y, position.z};
        }
        case Expression::Op::ObjectRootPosition: {
            if (host.object_position)
                if (const auto value = host.object_position(context.object, false, true))
                    return framework::Vector3d{value->x, value->y, value->z};
            const auto position = context.root_position.value_or(context.position);
            return framework::Vector3d{position.x, position.y, position.z};
        }
        case Expression::Op::ObjectPosition: {
            const auto position = current_world_position();
            return framework::Vector3d{position.x, position.y, position.z};
        }
        case Expression::Op::ObjectKey:
            return context.object.str();
        case Expression::Op::GenerateKey:
            return framework::Uuid::random().str();
        case Expression::Op::CreatorKey:
            return context.creator.str();
        case Expression::Op::OwnerKey:
            if (host.object_owner)
                if (const auto owner = host.object_owner(context.object))
                    return owner->str();
            return context.owner.str();
        case Expression::Op::DetectedKey:
        case Expression::Op::DetectedName:
        case Expression::Op::DetectedOwner:
        case Expression::Op::DetectedType:
        case Expression::Op::DetectedPosition:
        case Expression::Op::DetectedVelocity:
        case Expression::Op::DetectedGrab:
        case Expression::Op::DetectedRotation:
        case Expression::Op::DetectedGroup:
        case Expression::Op::DetectedLinkNumber:
        case Expression::Op::DetectedTouchFace:
        case Expression::Op::DetectedTouchPos:
        case Expression::Op::DetectedTouchNormal:
        case Expression::Op::DetectedTouchBinormal:
        case Expression::Op::DetectedTouchST:
        case Expression::Op::DetectedTouchUV: {
            const auto index = std::get<std::int32_t>(evaluate(*e.left));
            const auto* detector =
                event && index >= 0 && static_cast<std::size_t>(index) < event->detectors.size()
                    ? &event->detectors[index]
                    : nullptr;
            using Op = Expression::Op;
            const LslDetector missing;
            const auto& data = detector ? *detector : missing;
            switch (e.op) {
            case Op::DetectedKey:
                return detector ? data.key.str() : std::string{};
            case Op::DetectedName:
                return data.name;
            case Op::DetectedOwner:
                return detector ? data.owner.str() : std::string{};
            case Op::DetectedType:
                return data.type;
            case Op::DetectedPosition:
                return data.position;
            case Op::DetectedVelocity:
                return data.velocity;
            case Op::DetectedGrab:
                return data.grab;
            case Op::DetectedRotation:
                return data.rotation;
            case Op::DetectedGroup:
                return std::int32_t(detector && data.group == context.group);
            case Op::DetectedLinkNumber:
                return data.link_number;
            case Op::DetectedTouchFace:
                return data.touch_face;
            case Op::DetectedTouchPos:
                return data.touch_pos;
            case Op::DetectedTouchNormal:
                return data.touch_normal;
            case Op::DetectedTouchBinormal:
                return data.touch_binormal;
            case Op::DetectedTouchST:
                return data.touch_st;
            default:
                return data.touch_uv;
            }
        }
        case Expression::Op::Assign: {
            auto value = evaluate(*e.right);
            if (e.type == Type::Float) {
                if (const auto* integer = std::get_if<std::int32_t>(&value))
                    value = double(*integer);
            }
            if (e.left->op == Expression::Op::Component)
                writable_component(*e.left) = std::get<double>(value);
            else {
                auto& slot = variable(*e.left);
                check_heap_store(slot, value, e.left->type, e.left->global);
                slot = value;
            }
            return value;
        }
        case Expression::Op::Update: {
            if (e.left->op == Expression::Op::Component) {
                auto& component = writable_component(*e.left);
                const auto previous = component;
                component += e.delta;
                return e.postfix ? previous : component;
            }
            auto& variable = this->variable(*e.left);
            const auto previous = variable;
            if (auto* integer = std::get_if<std::int32_t>(&variable))
                *integer = wrap(static_cast<std::uint32_t>(*integer) + static_cast<std::uint32_t>(e.delta));
            else
                std::get<double>(variable) += e.delta;
            return e.postfix ? previous : variable;
        }
        case Expression::Op::UnixTime:
            if (!host.unix_time)
                throw std::runtime_error("LSL host Unix time callback is required");
            return host.unix_time();
        case Expression::Op::Date:
        case Expression::Op::Timestamp:
            if (!host.clock_ticks)
                throw std::runtime_error("LSL host clock callback is required");
            return utc_text(host.clock_ticks(), e.op == Expression::Op::Timestamp);
        case Expression::Op::GmtClock:
        case Expression::Op::Wallclock:
        case Expression::Op::TimeOfDay: {
            if (!host.clock_ticks)
                throw std::runtime_error("LSL host clock callback is required");
            const auto seconds = seconds_of_day(host.clock_ticks(), e.op != Expression::Op::GmtClock);
            return e.op == Expression::Op::TimeOfDay ? std::fmod(seconds, 14400.) : seconds;
        }
        case Expression::Op::Elapsed:
        case Expression::Op::ElapsedReset: {
            const auto current = timer_now();
            const auto milliseconds = (current - timer_start) / 10000;
            const auto remainder = (current - timer_start) % 10000;
            if (e.op == Expression::Op::ElapsedReset)
                timer_start = current;
            const bool round_up = remainder > 5000 || (remainder == 5000 && milliseconds % 2 != 0);
            return (milliseconds + round_up) / 1000.;
        }
        case Expression::Op::EdgeOfWorld: {
            const auto position = std::get<framework::Vector3d>(evaluate(*e.left));
            const auto direction = std::get<framework::Vector3d>(evaluate(*e.right));
            return std::int32_t(!host.edge_of_world || host.edge_of_world(position, direction));
        }
        case Expression::Op::RegisterTarget: {
            const auto target_value = evaluate(*e.left);
            const auto tolerance = static_cast<float>(list_float(evaluate(*e.right)));
            if (e.slot)
                return host.register_rotation_target
                    ? host.register_rotation_target(context.object, context.item,
                        std::get<LslRotation>(target_value), tolerance) : std::int32_t{0};
            const auto target = std::get<framework::Vector3d>(target_value);
            return host.register_position_target
                ? host.register_position_target(context.object, context.item,
                    {static_cast<float>(target.x), static_cast<float>(target.y), static_cast<float>(target.z)}, tolerance)
                : std::int32_t{0};
        }
        case Expression::Op::RemoveTarget: {
            const auto handle = std::get<std::int32_t>(evaluate(*e.left));
            const auto& remove = e.slot ? host.remove_rotation_target : host.remove_position_target;
            if (remove)
                remove(context.object, handle);
            return std::int32_t{0};
        }
        case Expression::Op::Wind: {
            const auto offset = std::get<framework::Vector3d>(evaluate(*e.left));
            if (!host.wind)
                return framework::Vector3d{};
            const auto coordinate = [](double value) {
                if (value >= -2147483648. && value < 2147483648.)
                    return static_cast<std::int32_t>(value);
                return std::numeric_limits<std::int32_t>::min();
            };
            const auto position = current_world_position();
            const auto wind = host.wind(coordinate(position.x + offset.x),
                                        coordinate(position.y + offset.y));
            return framework::Vector3d{wind.x, wind.y, 0.};
        }
        case Expression::Op::ModifyLand: {
            const auto action = std::get<std::int32_t>(evaluate(*e.left));
            const auto brush = std::get<std::int32_t>(evaluate(*e.right));
            if (host.modify_land)
                host.modify_land(context.object, static_cast<std::uint8_t>(action), static_cast<std::uint8_t>(brush));
            return std::int32_t{0};
        }
        case Expression::Op::Water:
        case Expression::Op::Cloud:
        case Expression::Op::Ground: {
            const auto offset = std::get<framework::Vector3d>(evaluate(*e.left));
            if (e.op == Expression::Op::Water)
                return host.region_water_height ? host.region_water_height() : context.water_height;
            if (e.op == Expression::Op::Cloud)
                return 0.;
            if (!host.ground_height)
                throw std::runtime_error("LSL host ground height callback is required");
            const auto position = current_world_position();
            return double(host.ground_height(position.x + static_cast<float>(offset.x),
                                             position.y + static_cast<float>(offset.y)));
        }
        case Expression::Op::GroundNormal:
        case Expression::Op::GroundSlope:
        case Expression::Op::GroundContour: {
            using Op = Expression::Op;
            const auto offset = std::get<framework::Vector3d>(evaluate(*e.left));
            if (!host.ground_normal)
                throw std::runtime_error("LSL host ground normal callback is required");
            const auto position = current_world_position();
            const auto normal = host.ground_normal(position.x, position.y,
                                                   static_cast<float>(offset.x), static_cast<float>(offset.y));
            if (e.op == Op::GroundNormal)
                return framework::Vector3d{normal.x, normal.y, normal.z};

            const double x = normal.x;
            const double y = normal.y;
            const float z = static_cast<float>((x * x + y * y) / -normal.z);
            const float squared_length = normal.x * normal.x + normal.y * normal.y + z * z;
            // OpenMetaverse.Vector3.Normalize zeros vectors below this squared magnitude.
            if (squared_length < 1e-6f)
                return framework::Vector3d{};
            const float inverse_length = 1.f / std::sqrt(squared_length);
            if (e.op == Op::GroundContour)
                return framework::Vector3d{-normal.y * inverse_length, normal.x * inverse_length, 0.};
            return framework::Vector3d{normal.x * inverse_length, normal.y * inverse_length, z * inverse_length};
        }
        case Expression::Op::Not:
            return std::int32_t(!truth(evaluate(*e.left), e.left->type));
        case Expression::Op::BitNot:
            return ~std::get<std::int32_t>(evaluate(*e.left));
        case Expression::Op::BitAnd:
        case Expression::Op::BitOr:
        case Expression::Op::BitXor:
        case Expression::Op::ShiftLeft:
        case Expression::Op::ShiftRight: {
            using Op = Expression::Op;
            const auto right = std::get<std::int32_t>(evaluate(*e.right));
            const auto left = std::get<std::int32_t>(evaluate(*e.left));
            const auto shift = static_cast<std::uint32_t>(right) & 31u;
            if (e.op == Op::BitAnd)
                return left & right;
            if (e.op == Op::BitOr)
                return left | right;
            if (e.op == Op::BitXor)
                return left ^ right;
            if (e.op == Op::ShiftRight)
                return left >> shift;
            return wrap(static_cast<std::uint32_t>(left) << shift);
        }
        case Expression::Op::ShortAnd:
            return std::int32_t(truth(evaluate(*e.left), e.left->type) &&
                                truth(evaluate(*e.right), e.right->type));
        case Expression::Op::ShortOr:
            return std::int32_t(truth(evaluate(*e.left), e.left->type) ||
                                truth(evaluate(*e.right), e.right->type));
        case Expression::Op::And:
        case Expression::Op::Or: {
            const bool right = truth(evaluate(*e.right), e.right->type);
            const bool left = truth(evaluate(*e.left), e.left->type);
            return std::int32_t(e.op == Expression::Op::And ? (left && right) : (left || right));
        }
        case Expression::Op::Negate: {
            const auto value = evaluate(*e.left);
            if (const auto* vector = std::get_if<framework::Vector3d>(&value))
                return framework::Vector3d{-vector->x, -vector->y, -vector->z};
            if (const auto* real = std::get_if<double>(&value))
                return -*real;
            return wrap(0u - static_cast<std::uint32_t>(std::get<std::int32_t>(value)));
        }
        case Expression::Op::EncodeInteger64: {
            const auto value = static_cast<std::uint32_t>(std::get<std::int32_t>(evaluate(*e.left)));
            auto bytes = std::make_shared<std::vector<std::uint8_t>>();
            for (int shift = 24; shift >= 0; shift -= 8)
                bytes->push_back(static_cast<std::uint8_t>(value >> shift));
            return network::binary_base64(network::osd_from_binary(bytes));
        }
        case Expression::Op::DecodeInteger64: {
            const auto text = framework::utf16_replacing(std::get<std::string>(evaluate(*e.left)));
            if (text.size() < 2 || text.size() > 8)
                return std::int32_t{0};
            constexpr std::u16string_view alphabet =
                u"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
            const auto digit = [&](std::size_t i) -> std::optional<std::uint32_t> {
                if (i >= text.size())
                    return {};
                const auto index = alphabet.find(text[i]);
                if (index == alphabet.npos)
                    return {};
                return static_cast<std::uint32_t>(index);
            };
            const auto a = digit(0), b = digit(1);
            if (!a || !b)
                return std::int32_t{0};
            auto value = *a << 26;
            if (text.size() == 2)
                return wrap(value | ((*b & 0x30) << 20));
            const auto c = digit(2);
            if (!c)
                return wrap(value);
            value |= *b << 20;
            if (text.size() == 3)
                return wrap(value | ((*c & 0x3c) << 14));
            const auto d = digit(3);
            if (!d)
                return wrap(value);
            value |= (*c << 14) | (*d << 8);
            const auto e5 = digit(4), f = digit(5);
            if (e5 && f)
                value |= (*e5 << 2) | (*f >> 4);
            return wrap(value);
        }
        case Expression::Op::EncodeBase64: {
            const auto text = std::get<std::string>(evaluate(*e.left));
            const auto bytes = std::make_shared<std::vector<std::uint8_t>>(text.begin(), text.end());
            return network::binary_base64(network::osd_from_binary(bytes));
        }
        case Expression::Op::XorBase64: {
            auto key = std::get<std::string>(evaluate(*e.right));
            auto source = std::get<std::string>(evaluate(*e.left));
            if (e.slot == 2) {
                const auto requested = 300.f * host.script_delay_factor;
                if (requested >= 10 && requested < 2147483648.f)
                    delay(static_cast<std::int32_t>(requested));
            }
            if (source.empty() || key.empty())
                return source;
            if (e.slot == 1) {
                const auto truncate = [](std::string& text) {
                    constexpr std::string_view alphabet =
                        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
                    const auto end = text.find_first_not_of(alphabet);
                    if (end != std::string::npos)
                        text.resize(end);
                    const auto remainder = text.size() % 4;
                    if (remainder == 1)
                        text.pop_back();
                    else if (remainder > 1)
                        text.append(4 - remainder, '=');
                };
                truncate(source);
                if (source.empty())
                    return source;
                truncate(key);
                if (key.empty())
                    return source;
            } else if (framework::utf16(key).size() % 4 != 0) {
                const auto last = key.find_last_not_of('=');
                if (last == std::string::npos && e.slot != 2)
                    return source;
                key.resize(last == std::string::npos ? 0 : last + 1);
                const auto remainder = framework::utf16(key).size() % 4;
                if (remainder == 1) {
                    auto units = framework::utf16(key);
                    units.pop_back();
                    key = framework::utf8(units);
                } else if (remainder > 1)
                    key.append(4 - remainder, '=');
            }
            const auto decode = [](const std::string& text) {
                network::Value encoded(text);
                encoded.kind = network::Value::Kind::Binary;
                return network::osd_binary(encoded);
            };
            try {
                auto bytes = decode(source);
                const auto mask = decode(key);
                if (e.slot == 2) {
                    constexpr std::string_view alphabet =
                        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
                    const auto source_size = source.find_last_not_of('=') + 1;
                    const auto key_size = key.find_last_not_of('=') + 1;
                    if (source_size && key_size == 0)
                        throw std::runtime_error("empty Base64 XOR mask");
                    const auto digit = [&](char ch) {
                        const auto value = alphabet.find(ch);
                        return value == std::string_view::npos ? std::size_t{0} : value;
                    };
                    const auto source_units = framework::utf16(source);
                    const auto key_units = framework::utf16(key);
                    std::string result;
                    for (std::size_t i = 0; i < source_size; ++i)
                        result += alphabet[digit(static_cast<char>(source_units[i])) ^
                                           digit(static_cast<char>(key_units[i % key_size]))];
                    result.append(source.size() - source_size, '=');
                    return result;
                }
                if (mask->empty())
                    return source;
                for (std::size_t i = 0; i < bytes->size(); ++i)
                    (*bytes)[i] ^= (*mask)[i % mask->size()];
                return network::binary_base64(network::osd_from_binary(bytes));
            } catch (const std::invalid_argument&) {
                return std::string{};
            }
        }
        case Expression::Op::DecodeBase64: {
            network::Value encoded(std::get<std::string>(evaluate(*e.left)));
            encoded.kind = network::Value::Kind::Binary;
            try {
                const auto bytes = network::osd_binary(encoded);
                return framework::utf8(framework::utf16_replacing(std::string(bytes->begin(), bytes->end())));
            } catch (const std::invalid_argument&) {
                return std::string{};
            }
        }
        case Expression::Op::ComputeHash:
        case Expression::Op::Hmac: {
            const bool hmac = e.op == Expression::Op::Hmac;
            const auto algorithm = std::get<std::string>(evaluate(hmac ? *e.z : *e.right));
            const auto source = std::get<std::string>(evaluate(hmac ? *e.right : *e.left));
            if (!hmac)
                return compute_hash(source, algorithm);
            auto key = std::get<std::string>(evaluate(*e.left));
            if (key.empty() || source.empty() || algorithm == "md5_sha1")
                return std::string{};
            const std::size_t block_size = algorithm == "sha384" || algorithm == "sha512" ? 128 : 64;
            // OpenSimulator's SHA-224 implementation retains the first block of a long key.
            if (key.size() > block_size && algorithm != "sha224")
                key = digest_bytes(compute_hash(key, algorithm));
            key.resize(block_size, 0);
            auto inner = key;
            for (std::size_t i = 0; i < block_size; ++i) {
                inner[i] ^= 0x36;
                key[i] ^= 0x5c;
            }
            const auto digest = compute_hash(inner + source, algorithm);
            return digest.empty() ? std::string{} : compute_hash(key + digest_bytes(digest), algorithm);
        }
        case Expression::Op::Md5: {
            const auto nonce = std::get<std::int32_t>(evaluate(*e.right));
            const auto source = std::get<std::string>(evaluate(*e.left));
            return framework::md5(source + ":" + std::to_string(nonce));
        }
        case Expression::Op::Sha256:
            return framework::sha256_hex(std::get<std::string>(evaluate(*e.left)));
        case Expression::Op::Sha1: {
            const auto source = std::get<std::string>(evaluate(*e.left));
            return framework::sha1_hex({reinterpret_cast<const std::uint8_t*>(source.data()), source.size()});
        }
        case Expression::Op::EscapeUrl:
        case Expression::Op::UnescapeUrl: {
            const auto units = framework::utf16_replacing(std::get<std::string>(evaluate(*e.left)));
            if (e.op == Expression::Op::EscapeUrl)
                return framework::utf8(
                    framework::config_uri_escape(units, false, framework::ConfigUriEscapeSet::Unreserved));
            constexpr int unescape_all = 2 | 8;
            return framework::utf8(
                framework::config_uri_unescape(units, 0xffff, 0xffff, 0xffff, unescape_all, false, false));
        }
        case Expression::Op::Upper:
        case Expression::Op::Lower: {
            const auto units = framework::utf16_replacing(std::get<std::string>(evaluate(*e.left)));
            return framework::utf8(e.op == Expression::Op::Upper ? framework::invariant_upper_utf16(units)
                                                                 : framework::invariant_lower_utf16(units));
        }
        case Expression::Op::Char: {
            const auto code = std::get<std::int32_t>(evaluate(*e.left));
            if (code == 0)
                return std::string{};
            if (code < 0 || code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff))
                return framework::utf8(u"\ufffd");
            if (code <= 0xffff)
                return framework::utf8(std::u16string(1, static_cast<char16_t>(code)));
            return framework::utf8(
                std::u16string{static_cast<char16_t>(0xd800 + ((code - 0x10000) >> 10)),
                               static_cast<char16_t>(0xdc00 + ((code - 0x10000) & 0x3ff))});
        }
        case Expression::Op::Hash: {
            const auto units = framework::utf16(std::get<std::string>(evaluate(*e.left)));
            std::uint32_t hash = 0;
            for (std::size_t i = 0; i < units.size(); ++i) {
                std::uint32_t code = units[i];
                if (code >= 0xd800 && code <= 0xdbff) {
                    if (++i == units.size() || units[i] < 0xdc00 || units[i] > 0xdfff)
                        return std::int32_t{0};
                    code = 0x10000 + ((code - 0xd800) << 10) + (units[i] - 0xdc00);
                } else if (code >= 0xdc00 && code <= 0xdfff)
                    return std::int32_t{0};
                hash = hash * 65599u + code;
            }
            return wrap(hash);
        }
        case Expression::Op::Ord: {
            std::int64_t index = std::get<std::int32_t>(evaluate(*e.right));
            const auto units = framework::utf16_replacing(std::get<std::string>(evaluate(*e.left)));
            const auto size = static_cast<std::int64_t>(units.size());
            if (index < 0)
                index += size;
            if (index < 0 || index >= size)
                return std::int32_t{0};
            const auto unit = units[index];
            if (unit < 0xd800 || unit > 0xdfff)
                return static_cast<std::int32_t>(unit);
            if (unit >= 0xdc00)
                --index;
            if (index < 0 || index + 1 >= size)
                return std::int32_t{0};
            const auto high = units[index], low = units[index + 1];
            if (high < 0xd800 || high > 0xdbff || low < 0xdc00 || low > 0xdfff)
                return std::int32_t{0};
            return std::int32_t(0x10000 + ((high - 0xd800) << 10) + (low - 0xdc00));
        }
        case Expression::Op::StringTrim: {
            const auto mode = std::get<std::int32_t>(evaluate(*e.right));
            const auto text = std::get<std::string>(evaluate(*e.left));
            if (mode < 1 || mode > 3)
                return text;
            const auto units = framework::utf16_replacing(text);
            std::size_t first = 0, last = units.size();
            if (mode & 1)
                while (first < last && framework::unicode_whitespace(units[first]))
                    ++first;
            if (mode & 2)
                while (last > first && framework::unicode_whitespace(units[last - 1]))
                    --last;
            return framework::utf8(std::u16string_view(units).substr(first, last - first));
        }
        case Expression::Op::RegexIsMatch: {
            const auto text = std::get<std::string>(evaluate(*e.left));
            const auto pattern = std::get<std::string>(evaluate(*e.right));
            check_ossl_permission("osRegexIsMatch");
            try {
                return std::int32_t(framework::dotnet_regex_is_match(text, pattern, 100000));
            } catch (const framework::DotnetRegexClassError&) {
                script_error("Possible invalid regular expression detected.");
                return std::int32_t{0};
            }
        }
        case Expression::Op::RegexMatchString: {
            using namespace framework;
            const auto input = regex_text(std::get<std::string>(evaluate(*e.left)));
            const auto pattern = std::get<std::string>(evaluate(*e.right));
            const auto start = regex_start(input->size(), std::get<std::int32_t>(evaluate(*e.z)));
            check_ossl_permission("osMatchString");
            auto result = std::make_shared<LslList>();
            if (!start)
                return std::shared_ptr<const LslList>(std::move(result));

            const auto expression = regex_text(pattern);
            const auto culture = [] { return std::u16string{}; };
            DotnetRegexInterpreter regex(
                dotnet_regex_parse(expression, 0, culture, sufficient_execution_stack),
                expression, sufficient_execution_stack);
            auto position = *start;
            int previous = -1;
            for (;;) {
                const auto match = regex.scan(input, 0, static_cast<std::int32_t>(input->size()),
                                              position, previous, DotnetRegexRunnerMode::FullMatchRequired,
                                              100000);
                if (!match.success)
                    break;
                for (const auto& group : match.groups) {
                    if (!group.success)
                        continue;
                    result->items.emplace_back(utf8(std::u16string_view(*input).substr(group.index, group.length)));
                    result->items.emplace_back(group.index);
                }
                position = match.text_position;
                previous = match.length;
            }
            return std::shared_ptr<const LslList>(std::move(result));
        }
        case Expression::Op::RegexReplaceString: {
            const auto source = std::get<std::string>(evaluate(*e.left));
            const auto pattern = std::get<std::string>(evaluate(*e.right));
            const auto replacement = std::get<std::string>(evaluate(*e.z));
            const auto count = std::get<std::int32_t>(evaluate(*e.w));
            const auto offset = std::get<std::int32_t>(evaluate(*e.elements.front()));
            check_ossl_permission("osReplaceString");
            const auto input = regex_text(source);
            const auto start = regex_start(input->size(), offset);
            if (!start)
                return source;
            if (count < -1)
                throw std::invalid_argument("Specified argument was out of the range of valid values. (Parameter 'count')");
            return replace_regex(input, pattern, replacement, count, *start, 0);
        }
        case Expression::Op::ReplaceSubstring: {
            const auto count = std::get<std::int32_t>(evaluate(*e.w));
            const auto replacement = std::get<std::string>(evaluate(*e.z));
            const auto pattern = std::get<std::string>(evaluate(*e.right));
            const auto source = std::get<std::string>(evaluate(*e.left));
            return replace_substring(source, pattern, replacement, count);
        }
        case Expression::Op::InsertString: {
            const auto inserted = std::get<std::string>(evaluate(*e.z));
            std::int64_t index = std::get<std::int32_t>(evaluate(*e.right));
            const auto destination = std::get<std::string>(evaluate(*e.left));
            if (inserted.empty())
                return destination;
            auto units = framework::utf16_replacing(destination);
            const auto size = static_cast<std::int64_t>(units.size());
            const bool from_end = index < 0;
            if (from_end)
                index += size;
            if (index >= 0 && index < size && units[index] >= 0xdc00 && units[index] <= 0xdfff)
                index += from_end ? -1 : 1;
            units.insert(static_cast<std::size_t>(std::clamp(index, std::int64_t{0}, size)),
                         framework::utf16_replacing(inserted));
            return framework::utf8(units);
        }
        case Expression::Op::GetSubstring:
        case Expression::Op::DeleteSubstring: {
            std::int64_t end = std::get<std::int32_t>(evaluate(*e.z));
            std::int64_t start = std::get<std::int32_t>(evaluate(*e.right));
            auto source = framework::utf16_replacing(std::get<std::string>(evaluate(*e.left)));
            const auto size = static_cast<std::int64_t>(source.size());
            if (start < 0)
                start += size;
            if (end < 0)
                end += size;
            const bool remove = e.op == Expression::Op::DeleteSubstring;
            if (start <= end) {
                if (end < 0 || start >= size)
                    return remove ? framework::utf8(source) : std::string{};
                const auto first = std::max<std::int64_t>(0, start);
                const auto count = std::min(end, size - 1) - first + 1;
                if (remove) {
                    source.erase(first, count);
                    return framework::utf8(source);
                }
                return framework::utf8(source.substr(first, count));
            }
            if (start < 0 || end >= size)
                return remove ? std::string{} : framework::utf8(source);
            const auto tail = std::min(start, size);
            if (remove) {
                const auto first = end > 0 ? end + 1 : 0;
                return framework::utf8(source.substr(first, tail - first));
            }
            return framework::utf8(source.substr(0, std::max<std::int64_t>(end + 1, 0)) +
                                   source.substr(tail));
        }
        case Expression::Op::SubstringIndex: {
            const auto pattern = std::get<std::string>(evaluate(*e.right));
            const auto source = std::get<std::string>(evaluate(*e.left));
            if (source.empty())
                return std::int32_t{-1};
            if (pattern.empty())
                return std::int32_t{0};
            return static_cast<std::int32_t>(framework::invariant_index_of(
                framework::utf16_replacing(source), framework::utf16_replacing(pattern)));
        }
        case Expression::Op::StringLength: {
            const auto value = std::get<std::string>(evaluate(*e.left));
            return static_cast<std::int32_t>(framework::utf16_replacing(value).size());
        }
        case Expression::Op::VectorCast:
            return list_vector(evaluate(*e.left));
        case Expression::Op::RotationCast:
            return list_rotation(evaluate(*e.left));
        case Expression::Op::StringCast:
            return value_text(evaluate(*e.left));
        case Expression::Op::Equal:
        case Expression::Op::NotEqual:
        case Expression::Op::Less:
        case Expression::Op::LessEqual:
        case Expression::Op::Greater:
        case Expression::Op::GreaterEqual: {
            using Op = Expression::Op;
            const auto right = evaluate(*e.right);
            const auto left = evaluate(*e.left);
            if (const auto* rotation = std::get_if<LslRotation>(&left)) {
                const bool equal = *rotation == std::get<LslRotation>(right);
                return std::int32_t(e.op == Op::Equal ? equal : !equal);
            }
            if (const auto* vector = std::get_if<framework::Vector3d>(&left)) {
                const bool equal = *vector == std::get<framework::Vector3d>(right);
                return std::int32_t(e.op == Op::Equal ? equal : !equal);
            }
            if (const auto* list = std::get_if<std::shared_ptr<const LslList>>(&left)) {
                const auto left_length = static_cast<std::uint32_t>((*list)->items.size());
                const auto right_length =
                    static_cast<std::uint32_t>(std::get<std::shared_ptr<const LslList>>(right)->items.size());
                return e.op == Op::Equal ? std::int32_t(left_length == right_length)
                                         : wrap(left_length - right_length);
            }
            const auto compare = [&](const auto& x, const auto& y) {
                switch (e.op) {
                case Op::Equal:
                    return std::int32_t(x == y);
                case Op::NotEqual:
                    return std::int32_t(x != y);
                case Op::Less:
                    return std::int32_t(x < y);
                case Op::LessEqual:
                    return std::int32_t(x <= y);
                case Op::Greater:
                    return std::int32_t(x > y);
                default:
                    return std::int32_t(x >= y);
                }
            };
            if (const auto* text = std::get_if<std::string>(&left))
                return compare(framework::utf16_replacing(*text),
                               framework::utf16_replacing(std::get<std::string>(right)));
            const auto number = [](const LslValue& value) {
                if (const auto* real = std::get_if<double>(&value))
                    return *real;
                return static_cast<double>(std::get<std::int32_t>(value));
            };
            return compare(number(left), number(right));
        }
        case Expression::Op::Add:
        case Expression::Op::Subtract:
        case Expression::Op::Multiply:
        case Expression::Op::Divide:
        case Expression::Op::Remainder: {
            using Op = Expression::Op;
            const auto right = evaluate(*e.right);
            const auto left = evaluate(*e.left);
            if (e.type == Type::List) {
                auto result = std::make_shared<LslList>();
                const auto append = [&](const LslValue& value) {
                    if (const auto* list = std::get_if<std::shared_ptr<const LslList>>(&value))
                        result->items.insert(result->items.end(), (*list)->items.begin(),
                                             (*list)->items.end());
                    else
                        result->items.push_back(value);
                };
                append(left);
                append(right);
                return std::shared_ptr<const LslList>(std::move(result));
            }
            if (e.type == Type::Rotation) {
                const auto b = std::get<LslRotation>(left);
                auto a = std::get<LslRotation>(right);
                if (e.op == Op::Add)
                    return LslRotation{b.x + a.x, b.y + a.y, b.z + a.z, b.w + a.w};
                if (e.op == Op::Subtract)
                    return LslRotation{b.x - a.x, b.y - a.y, b.z - a.z, b.w - a.w};
                if (e.op == Op::Divide) {
                    a.x = -a.x;
                    a.y = -a.y;
                    a.z = -a.z;
                }
                // LSL composition multiplies the right quaternion by the left quaternion.
                return LslRotation{a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
                                   a.w * b.y + a.y * b.w + a.z * b.x - a.x * b.z,
                                   a.w * b.z + a.z * b.w + a.x * b.y - a.y * b.x,
                                   a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
            }
            const auto* a = std::get_if<framework::Vector3d>(&left);
            const auto* b = std::get_if<framework::Vector3d>(&right);
            if (const auto* rotation = std::get_if<LslRotation>(&right); a && rotation) {
                auto transform = *rotation;
                if (e.op == Op::Divide)
                    transform.w = -transform.w;
                return rotate_vector(*a, transform);
            }
            if (a && b) {
                if (e.op == Op::Add)
                    return framework::Vector3d{a->x + b->x, a->y + b->y, a->z + b->z};
                if (e.op == Op::Subtract)
                    return framework::Vector3d{a->x - b->x, a->y - b->y, a->z - b->z};
                if (e.op == Op::Multiply)
                    return a->x * b->x + a->y * b->y + a->z * b->z;
                return framework::Vector3d{a->y * b->z - a->z * b->y, a->z * b->x - a->x * b->z,
                                           a->x * b->y - a->y * b->x};
            }
            if (a || b) {
                const auto& vector = a ? *a : *b;
                const auto& scalar = a ? right : left;
                const auto scale = std::holds_alternative<double>(scalar)
                                       ? std::get<double>(scalar)
                                       : double(std::get<std::int32_t>(scalar));
                if (e.op == Op::Multiply)
                    return framework::Vector3d{vector.x * scale, vector.y * scale, vector.z * scale};
                if (scale == 0.)
                    throw std::runtime_error("Vector division by zero");
                const framework::Vector3d result{vector.x / scale, vector.y / scale, vector.z / scale};
                if (!std::isfinite(result.x) || !std::isfinite(result.y) || !std::isfinite(result.z))
                    throw std::runtime_error("Vector division by zero");
                return result;
            }
            if (e.type == Type::String)
                return std::get<std::string>(left) + std::get<std::string>(right);
            if (e.type == Type::Float) {
                const auto numeric = [](const LslValue& value) {
                    if (const auto* real = std::get_if<double>(&value))
                        return *real;
                    return static_cast<double>(std::get<std::int32_t>(value));
                };
                const auto x = numeric(left), y = numeric(right);
                if (e.op == Op::Add)
                    return x + y;
                if (e.op == Op::Subtract)
                    return x - y;
                if (e.op == Op::Multiply)
                    return x * y;
                if (y == 0.)
                    throw std::runtime_error("Division by Zero");
                const auto result = e.op == Op::Divide ? x / y : std::fmod(x, y);
                if (!std::isfinite(result))
                    throw std::runtime_error("Division by Zero");
                return result;
            }
            const std::int64_t x = std::get<std::int32_t>(left), y = std::get<std::int32_t>(right);
            if (e.op == Op::Add)
                return wrap(static_cast<std::uint32_t>(x + y));
            if (e.op == Op::Subtract)
                return wrap(static_cast<std::uint32_t>(x - y));
            if (e.op == Op::Multiply)
                return wrap(static_cast<std::uint32_t>(x * y));
            if (y == 0)
                throw std::runtime_error("Division by Zero");
            return wrap(static_cast<std::uint32_t>(e.op == Op::Divide ? x / y : x % y));
        }
        }
        throw std::logic_error("invalid native LSL expression");
    }
};
}
struct ScriptState {
    std::map<std::string, Handler> handlers;
    std::uint64_t event_mask = 0;
};
class LslProgram {
  public:
    std::map<std::string, ScriptState> states;
    std::vector<std::string> state_order{"default"};
    std::vector<Instruction> globals;
    std::string source_hash;
};
namespace {
class Parser {
    Lexer lexer;
    Token token;
    Handler* handler = nullptr;
    std::size_t depth = 0;
    struct Local {
        Type type;
        std::size_t slot;
        bool global = false;
    };
    std::vector<std::map<std::string, Local>> scopes;
    std::optional<Local> local(const std::string& name) const {
        for (auto scope = scopes.rbegin(); scope != scopes.rend(); ++scope) {
            const auto found = scope->find(name);
            if (found != scope->end())
                return found->second;
        }
        return {};
    }
    struct Label {
        std::size_t instruction, block;
    };
    struct PendingJump {
        Token name;
        std::size_t instruction;
        std::vector<std::size_t> blocks;
    };
    std::map<std::string, Label> labels;
    std::vector<PendingJump> jumps;
    std::vector<Token> state_targets;
    std::vector<std::size_t> blocks;
    std::size_t next_block = 0;
    void resolve_jumps() {
        for (const auto& jump : jumps) {
            const auto label = labels.find(jump.name.text);
            if (label == labels.end())
                fail(jump.name, "undefined label");
            if (std::find(jump.blocks.begin(), jump.blocks.end(), label->second.block) == jump.blocks.end())
                fail(jump.name, "no lateral jumps allowed");
            handler->instructions[jump.instruction].jump = label->second.instruction;
        }
        labels.clear();
        jumps.clear();
    }
    void advance() {
        token = lexer.next();
    }
    bool is(std::string_view text) const {
        return token.text == text && token.kind != Kind::String;
    }
    void require(std::string_view text) {
        if (!is(text))
            fail(token, "expected " + std::string(text));
        advance();
    }
    struct Depth {
        Parser& p;
        explicit Depth(Parser& v) : p(v) {
            if (++p.depth > 128)
                fail(p.token, "native LSL nesting limit exceeded");
        }
        ~Depth() {
            --p.depth;
        }
    };
    bool update_operator() const {
        return is("++") || is("--");
    }
    std::unique_ptr<Expression> update(std::unique_ptr<Expression> target, int delta, bool postfix) {
        if (!writable(*target) || !numeric(target->type))
            fail(token, "increment and decrement require a writable numeric value");
        auto e = std::make_unique<Expression>();
        e->op = Expression::Op::Update;
        e->type = target->type;
        e->delta = delta;
        e->postfix = postfix;
        e->height = target->height + 1;
        if (e->height > 128)
            fail(token, "native LSL expression depth exceeded");
        e->left = std::move(target);
        return e;
    }
    std::unique_ptr<Expression> component_access(std::unique_ptr<Expression> value) {
        if (!is("."))
            return value;
        const bool rotation = value->type == Type::Rotation;
        if (value->type != Type::Vector && !rotation)
            fail(token, "component access requires a vector or rotation");
        advance();
        if (!is("x") && !is("y") && !is("z") && !(rotation && is("s")))
            fail(token, rotation ? "expected rotation component x, y, z, or s"
                                 : "expected vector component x, y, or z");
        auto component = std::make_unique<Expression>();
        component->op = Expression::Op::Component;
        component->type = Type::Float;
        if (is("x"))
            component->slot = 0;
        else if (is("y"))
            component->slot = 1;
        else if (is("z"))
            component->slot = 2;
        else
            component->slot = 3;
        component->height = value->height + 1;
        if (component->height > 128)
            fail(token, "native LSL expression depth exceeded");
        component->left = std::move(value);
        advance();
        return component;
    }
    std::unique_ptr<Expression> unary() {
        Depth guard(*this);
        auto e = std::make_unique<Expression>();
        if (update_operator()) {
            const int delta = is("++") ? 1 : -1;
            advance();
            return update(unary(), delta, false);
        }
        if (is("!") || is("~")) {
            const bool bitwise = is("~");
            advance();
            e->op = bitwise ? Expression::Op::BitNot : Expression::Op::Not;
            e->left = unary();
            if (bitwise && e->left->type != Type::Integer)
                fail(token, "bitwise complement requires integer");
            e->height = e->left->height + 1;
            if (e->height > 128)
                fail(token, "native LSL expression depth exceeded");
            return e;
        }
        if (is("-") || is("+")) {
            bool minus = is("-");
            advance();
            auto value = unary();
            if (!numeric(value->type) && value->type != Type::Vector)
                fail(token, "unary +/- requires numeric value");
            if (!minus)
                return value;
            if (value->height >= 128)
                fail(token, "native LSL expression depth exceeded");
            e->height = value->height + 1;
            e->op = Expression::Op::Negate;
            e->type = value->type;
            e->left = std::move(value);
            return e;
        }
        if (is("(")) {
            advance();
            if (is("string") || is("key") || is("vector") || is("rotation") || is("quaternion")) {
                const bool rotation = is("rotation") || is("quaternion");
                const bool vector = is("vector");
                e->type = rotation    ? Type::Rotation
                          : vector    ? Type::Vector
                          : is("key") ? Type::Key
                                      : Type::String;
                e->op = rotation ? Expression::Op::RotationCast
                        : vector ? Expression::Op::VectorCast
                                 : Expression::Op::StringCast;
                advance();
                require(")");
                e->left = unary();
                if ((rotation || vector) && e->left->type != Type::String && e->left->type != e->type)
                    fail(token, rotation ? "rotation cast requires a string or rotation"
                                         : "vector cast requires a string or vector");
                if (e->left->type == Type::List)
                    fail(token, "unsupported list cast");
                if (e->left->height >= 128)
                    fail(token, "native LSL expression depth exceeded");
                e->height = e->left->height + 1;
                return e;
            }
            auto value = expression();
            require(")");
            return component_access(std::move(value));
        }
        if (is("[")) {
            advance();
            e->op = Expression::Op::ListLiteral;
            e->type = Type::List;
            if (!is("]")) {
                for (;;) {
                    auto element = expression();
                    if (element->type == Type::List)
                        fail(token, "nested lists are unsupported");
                    e->height = std::max(e->height, element->height + 1);
                    if (e->height > 128)
                        fail(token, "native LSL expression depth exceeded");
                    e->elements.push_back(std::move(element));
                    if (!is(","))
                        break;
                    advance();
                }
            }
            require("]");
            return e;
        }
        if (is("llListReplaceList") || is("llListInsertList")) {
            const bool insert = is("llListInsertList");
            e->op = insert ? Expression::Op::ListInsert : Expression::Op::ListReplace;
            e->type = Type::List;
            advance();
            require("(");
            e->left = expression();
            require(",");
            e->right = expression();
            require(",");
            e->z = expression();
            if (!insert) {
                require(",");
                e->w = expression();
            }
            require(")");
            if (e->left->type != Type::List || e->right->type != Type::List || e->z->type != Type::Integer ||
                (e->w && e->w->type != Type::Integer))
                fail(token, "list replacement requires two lists and integer bounds");
            e->height =
                1 + std::max({e->left->height, e->right->height, e->z->height, e->w ? e->w->height : 0u});
            if (e->height > 128)
                fail(token, "native LSL expression depth exceeded");
            return e;
        }
        if (is("llList2List") || is("llDeleteSubList") || is("llList2ListStrided") ||
            is("llList2ListSlice")) {
            const bool slice = is("llList2ListSlice");
            const bool strided = slice || is("llList2ListStrided");
            e->op = strided             ? Expression::Op::ListStrided
                    : is("llList2List") ? Expression::Op::ListSlice
                                        : Expression::Op::ListDelete;
            e->type = Type::List;
            advance();
            require("(");
            e->left = expression();
            require(",");
            e->right = expression();
            require(",");
            e->z = expression();
            if (strided) {
                require(",");
                e->w = expression();
                if (e->w->type != Type::Integer)
                    fail(token, "list stride requires an integer");
            }
            if (slice) {
                require(",");
                auto offset = expression();
                if (offset->type != Type::Integer)
                    fail(token, "list stride offset requires an integer");
                e->elements.push_back(std::move(offset));
            }
            require(")");
            if (e->left->type != Type::List || e->right->type != Type::Integer || e->z->type != Type::Integer)
                fail(token, "list ranges require a list and integer bounds");
            e->height = 1 + std::max({e->left->height, e->right->height, e->z->height,
                                      e->w ? e->w->height : 0u, slice ? e->elements.front()->height : 0u});
            if (e->height > 128)
                fail(token, "native LSL expression depth exceeded");
            return e;
        }
        if (is("llListFindStrided")) {
            e->op = Expression::Op::ListFindStrided;
            advance();
            require("(");
            e->left = expression();
            require(",");
            e->right = expression();
            require(",");
            e->z = expression();
            require(",");
            e->w = expression();
            require(",");
            auto stride = expression();
            require(")");
            if (e->left->type != Type::List || e->right->type != Type::List || e->z->type != Type::Integer ||
                e->w->type != Type::Integer || stride->type != Type::Integer)
                fail(token, "llListFindStrided requires two lists and three integers");
            e->height =
                1 + std::max({e->left->height, e->right->height, e->z->height, e->w->height, stride->height});
            if (e->height > 128)
                fail(token, "native LSL expression depth exceeded");
            e->elements.push_back(std::move(stride));
            return e;
        }
        if (is("llListFindList") || is("llListFindListNext")) {
            const bool next = is("llListFindListNext");
            e->op = Expression::Op::ListFind;
            advance();
            require("(");
            e->left = expression();
            require(",");
            e->right = expression();
            if (next) {
                require(",");
                e->z = expression();
                if (e->z->type != Type::Integer)
                    fail(token, "list search occurrence requires an integer");
            }
            require(")");
            if (e->left->type != Type::List || e->right->type != Type::List)
                fail(token, "llListFindList requires two lists");
            e->height = 1 + std::max({e->left->height, e->right->height, e->z ? e->z->height : 0u});
            if (e->height > 128)
                fail(token, "native LSL expression depth exceeded");
            return e;
        }
        if (is("llList2CSV") || is("llCSV2List")) {
            const bool decode = is("llCSV2List");
            e->op = decode ? Expression::Op::CsvToList : Expression::Op::ListJoin;
            e->type = decode ? Type::List : Type::String;
            advance();
            require("(");
            e->left = expression();
            require(")");
            const auto type = e->left->type;
            if (decode ? (type != Type::String && type != Type::Key) : type != Type::List)
                fail(token, "invalid CSV argument");
            e->height = e->left->height + 1;
            if (e->height > 128)
                fail(token, "native LSL expression depth exceeded");
            return e;
        }
        if (is("llParseString2List") || is("llParseStringKeepNulls")) {
            using Op = Expression::Op;
            e->op = is("llParseStringKeepNulls") ? Op::ParseStringKeepNulls : Op::ParseString;
            e->type = Type::List;
            advance();
            require("(");
            e->left = expression();
            require(",");
            e->right = expression();
            require(",");
            e->z = expression();
            require(")");
            if ((e->left->type != Type::String && e->left->type != Type::Key) ||
                e->right->type != Type::List || e->z->type != Type::List)
                fail(token, "string parsing requires text, a separator list, and a spacer list");
            e->height = 1 + std::max({e->left->height, e->right->height, e->z->height});
            if (e->height > 128)
                fail(token, "native LSL expression depth exceeded");
            return e;
        }
        if (is("llDumpList2String")) {
            e->op = Expression::Op::ListJoin;
            e->type = Type::String;
            advance();
            require("(");
            e->left = expression();
            require(",");
            e->right = expression();
            require(")");
            if (e->left->type != Type::List ||
                (e->right->type != Type::String && e->right->type != Type::Key))
                fail(token, "llDumpList2String requires a list and text separator");
            e->height = 1 + std::max(e->left->height, e->right->height);
            if (e->height > 128)
                fail(token, "native LSL expression depth exceeded");
            return e;
        }
        if (is("llList2String") || is("llList2Key") || is("llGetListEntryType") || is("llList2Integer") ||
            is("llList2Float") || is("llList2Vector") || is("llList2Rot")) {
            const bool key = is("llList2Key");
            const bool entry_type = is("llGetListEntryType");
            const bool integer = is("llList2Integer");
            const bool real = is("llList2Float");
            const bool vector = is("llList2Vector");
            const bool rotation = is("llList2Rot");
            e->op = rotation     ? Expression::Op::ListRotation
                    : vector     ? Expression::Op::ListVector
                    : real       ? Expression::Op::ListFloat
                    : integer    ? Expression::Op::ListInteger
                    : entry_type ? Expression::Op::ListEntryType
                    : key        ? Expression::Op::ListKey
                                 : Expression::Op::ListString;
            e->type = rotation                  ? Type::Rotation
                      : vector                  ? Type::Vector
                      : real                    ? Type::Float
                      : (entry_type || integer) ? Type::Integer
                      : key                     ? Type::Key
                                                : Type::String;
            advance();
            require("(");
            e->left = expression();
            require(",");
            e->right = expression();
            require(")");
            if (e->left->type != Type::List || e->right->type != Type::Integer)
                fail(token, "list access requires a list and integer index");
            e->height = 1 + std::max(e->left->height, e->right->height);
            if (e->height > 128)
                fail(token, "native LSL expression depth exceeded");
            return component_access(std::move(e));
        }
        if (is("llJsonSetValue")) {
            e->op = Expression::Op::JsonSet;
            e->type = Type::String;
            advance();
            require("(");
            e->left = expression();
            require(",");
            e->right = expression();
            require(",");
            e->z = expression();
            require(")");
            if ((e->left->type != Type::String && e->left->type != Type::Key) ||
                e->right->type != Type::List || (e->z->type != Type::String && e->z->type != Type::Key))
                fail(token, "llJsonSetValue requires text, a list path and replacement text");
            e->height = 1 + std::max({e->left->height, e->right->height, e->z->height});
            if (e->height > 128)
                fail(token, "native LSL expression depth exceeded");
            return e;
        }
        if (is("llJsonGetValue") || is("llJsonValueType")) {
            e->slot = is("llJsonValueType");
            e->op = Expression::Op::JsonLookup;
            e->type = Type::String;
            advance();
            require("(");
            e->left = expression();
            require(",");
            e->right = expression();
            require(")");
            if ((e->left->type != Type::String && e->left->type != Type::Key) || e->right->type != Type::List)
                fail(token, "JSON lookup requires text and a list path");
            e->height = 1 + std::max(e->left->height, e->right->height);
            if (e->height > 128)
                fail(token, "native LSL expression depth exceeded");
            return e;
        }
        if (is("llJson2List")) {
            e->op = Expression::Op::JsonToList;
            e->type = Type::List;
            advance();
            require("(");
            e->left = expression();
            require(")");
            if (e->left->type != Type::String && e->left->type != Type::Key)
                fail(token, "llJson2List requires text");
            e->height = e->left->height + 1;
            if (e->height > 128)
                fail(token, "native LSL expression depth exceeded");
            return e;
        }
        if (is("llList2Json")) {
            e->op = Expression::Op::ListToJson;
            e->type = Type::String;
            advance();
            require("(");
            e->left = expression();
            require(",");
            e->right = expression();
            require(")");
            if (e->left->type != Type::String || e->right->type != Type::List)
                fail(token, "llList2Json requires a string type and list");
            e->height = 1 + std::max(e->left->height, e->right->height);
            if (e->height > 128)
                fail(token, "native LSL expression depth exceeded");
            return e;
        }
        if (is("llListRandomize")) {
            e->op = Expression::Op::ListRandomize;
            e->type = Type::List;
            advance();
            require("(");
            e->left = expression();
            require(",");
            e->right = expression();
            require(")");
            if (e->left->type != Type::List || e->right->type != Type::Integer)
                fail(token, "llListRandomize requires a list and integer stride");
            e->height = 1 + std::max(e->left->height, e->right->height);
            if (e->height > 128)
                fail(token, "native LSL expression depth exceeded");
            return e;
        }
        if (is("llListSort") || is("llListSortStrided")) {
            const bool indexed = is("llListSortStrided");
            e->op = Expression::Op::ListSort;
            e->type = Type::List;
            advance();
            require("(");
            e->left = expression();
            require(",");
            e->right = expression();
            require(",");
            if (indexed) {
                e->w = expression();
                require(",");
            }
            e->z = expression();
            require(")");
            if (e->left->type != Type::List || e->right->type != Type::Integer ||
                e->z->type != Type::Integer || (e->w && e->w->type != Type::Integer))
                fail(token, "list sorting requires a list and integer arguments");
            e->height =
                1 + std::max({e->left->height, e->right->height, e->z->height, e->w ? e->w->height : 0u});
            if (e->height > 128)
                fail(token, "native LSL expression depth exceeded");
            return e;
        }
        if (is("llListStatistics")) {
            e->op = Expression::Op::ListStatistics;
            e->type = Type::Float;
            advance();
            require("(");
            e->left = expression();
            require(",");
            e->right = expression();
            require(")");
            if (e->left->type != Type::Integer || e->right->type != Type::List)
                fail(token, "llListStatistics requires an integer operation and a list");
            e->height = 1 + std::max(e->left->height, e->right->height);
            if (e->height > 128)
                fail(token, "native LSL expression depth exceeded");
            return e;
        }
        if (is("llGetListLength")) {
            e->op = Expression::Op::ListLength;
            advance();
            require("(");
            e->left = expression();
            require(")");
            if (e->left->type != Type::List)
                fail(token, "llGetListLength requires a list");
            e->height = e->left->height + 1;
            if (e->height > 128)
                fail(token, "native LSL expression depth exceeded");
            return e;
        }
        if (is("ZERO_ROTATION")) {
            e->type = Type::Rotation;
            e->value = LslRotation{};
            advance();
            return e;
        }
        if (is("ZERO_VECTOR")) {
            e->type = Type::Vector;
            e->value = framework::Vector3d{};
            advance();
            return e;
        }
        if (is("<")) {
            advance();
            e->op = Expression::Op::VectorLiteral;
            e->type = Type::Vector;
            e->left = expression();
            require(",");
            e->right = expression();
            require(",");
            e->z = expression(0, true);
            if (is(",")) {
                advance();
                e->w = expression(0, true);
                e->type = Type::Rotation;
            }
            require(">");
            if (!numeric(e->left->type) || !numeric(e->right->type) || !numeric(e->z->type) ||
                (e->w && !numeric(e->w->type)))
                fail(token, "vector and rotation components must be numeric");
            e->height = std::max({e->left->height, e->right->height, e->z->height}) + 1;
            if (e->height > 128)
                fail(token, "native LSL expression depth exceeded");
            return e;
        }
        if (token.kind == Kind::Float) {
            e->type = Type::Float;
            e->value = token.real;
            advance();
            return e;
        }
        if (token.kind == Kind::Integer) {
            e->value = token.integer;
            advance();
            return e;
        }
        if (token.kind == Kind::String) {
            e->type = Type::String;
            e->value = token.text;
            advance();
            return e;
        }
        using Op = Expression::Op;
        struct DetectorFunction {
            std::string_view name;
            Op op;
            Type type;
        };
        static constexpr DetectorFunction detector_functions[] = {
            {"llDetectedKey", Op::DetectedKey, Type::Key},
            {"llDetectedName", Op::DetectedName, Type::String},
            {"llDetectedOwner", Op::DetectedOwner, Type::Key},
            {"llDetectedType", Op::DetectedType, Type::Integer},
            {"llDetectedPos", Op::DetectedPosition, Type::Vector},
            {"llDetectedVel", Op::DetectedVelocity, Type::Vector},
            {"llDetectedGrab", Op::DetectedGrab, Type::Vector},
            {"llDetectedRot", Op::DetectedRotation, Type::Rotation},
            {"llDetectedGroup", Op::DetectedGroup, Type::Integer},
            {"llDetectedLinkNumber", Op::DetectedLinkNumber, Type::Integer},
            {"llDetectedTouchFace", Op::DetectedTouchFace, Type::Integer},
            {"llDetectedTouchPos", Op::DetectedTouchPos, Type::Vector},
            {"llDetectedTouchNormal", Op::DetectedTouchNormal, Type::Vector},
            {"llDetectedTouchBinormal", Op::DetectedTouchBinormal, Type::Vector},
            {"llDetectedTouchST", Op::DetectedTouchST, Type::Vector},
            {"llDetectedTouchUV", Op::DetectedTouchUV, Type::Vector}};
        for (const auto& [name, op, type] : detector_functions) {
            if (!is(name))
                continue;
            e->op = op;
            e->type = type;
            advance();
            require("(");
            e->left = expression();
            if (e->left->type != Type::Integer)
                fail(token, "detector index must be integer");
            e->height = e->left->height + 1;
            if (e->height > 128)
                fail(token, "native LSL expression depth exceeded");
            require(")");
            return component_access(std::move(e));
        }
        if (token.kind == Kind::Name && is_drawing_function(token.text)) {
            const auto function = token;
            advance();
            require("(");
            do {
                if (e->elements.size() == 5)
                    fail(function, "too many drawing command arguments");
                auto argument = expression();
                e->height = std::max(e->height, argument->height + 1);
                if (e->height > 128)
                    fail(function, "native LSL expression depth exceeded");
                e->elements.push_back(std::move(argument));
                if (!is(","))
                    break;
                advance();
            } while (true);
            require(")");
            const auto accepts = [](Type parameter, Type argument) {
                return parameter == argument || (parameter == Type::String && argument == Type::Key) ||
                       (parameter == Type::Float && argument == Type::Integer);
            };
            const auto command = std::ranges::find_if(drawing_commands, [&](const auto& candidate) {
                return candidate.function == function.text &&
                       candidate.parameters.size() + 1 == e->elements.size() &&
                       accepts(Type::String, e->elements.front()->type) &&
                       std::ranges::equal(candidate.parameters, e->elements | std::views::drop(1),
                                          [&](Type parameter, const auto& argument) {
                                              return accepts(parameter, argument->type);
                                          });
            });
            if (command == std::end(drawing_commands))
                fail(function, "invalid drawing command arguments");
            e->slot = static_cast<std::size_t>(command - std::begin(drawing_commands));
            e->op = Op::DrawingCommand;
            e->type = Type::String;
            return e;
        }
        if (is("osRegexIsMatch") || is("osMatchString")) {
            const auto command = token;
            const bool capture = is("osMatchString");
            e->op = capture ? Op::RegexMatchString : Op::RegexIsMatch;
            e->type = capture ? Type::List : Type::Integer;
            advance();
            require("(");
            e->left = expression();
            require(",");
            e->right = expression();
            if (capture) {
                require(",");
                e->z = expression();
                if (e->z->type != Type::Integer)
                    fail(command, "osMatchString start must be an integer");
                e->height = e->z->height + 1;
            }
            require(")");
            const auto text = [](Type type) { return type == Type::String || type == Type::Key; };
            if (!text(e->left->type) || !text(e->right->type))
                fail(command, "regex matching requires text arguments");
            e->height = std::max(e->height, 1 + std::max(e->left->height, e->right->height));
            if (e->height > 128)
                fail(command, "native LSL expression depth exceeded");
            return e;
        }
        if (is("llReplaceSubString") || is("osReplaceString")) {
            const auto command = token;
            const bool has_start = is("osReplaceString");
            e->op = has_start ? Op::RegexReplaceString : Op::ReplaceSubstring;
            e->type = Type::String;
            advance();
            require("(");
            e->left = expression();
            require(",");
            e->right = expression();
            require(",");
            e->z = expression();
            require(",");
            e->w = expression();
            if (has_start) {
                require(",");
                auto start = expression();
                if (start->type != Type::Integer)
                    fail(command, "osReplaceString start must be an integer");
                e->height = start->height + 1;
                e->elements.push_back(std::move(start));
            }
            require(")");
            const auto text = [](Type type) { return type == Type::String || type == Type::Key; };
            if (!text(e->left->type) || !text(e->right->type) || !text(e->z->type) ||
                e->w->type != Type::Integer)
                fail(command, "substring replacement requires text arguments and integer count");
            e->height = std::max(e->height, 1 + std::max({e->left->height, e->right->height,
                                                       e->z->height, e->w->height}));
            if (e->height > 128)
                fail(token, "native LSL expression depth exceeded");
            return e;
        }
        if (is("llGetSubString") || is("llDeleteSubString") || is("llInsertString")) {
            const bool insert = is("llInsertString");
            e->op = insert ? Op::InsertString : is("llGetSubString") ? Op::GetSubstring : Op::DeleteSubstring;
            e->type = Type::String;
            advance();
            require("(");
            e->left = expression();
            require(",");
            e->right = expression();
            require(",");
            e->z = expression();
            require(")");
            const auto text = [](Type type) { return type == Type::String || type == Type::Key; };
            if (!text(e->left->type) || e->right->type != Type::Integer ||
                (insert ? !text(e->z->type) : e->z->type != Type::Integer))
                fail(token, "invalid string range arguments");
            e->height = 1 + std::max({e->left->height, e->right->height, e->z->height});
            if (e->height > 128)
                fail(token, "native LSL expression depth exceeded");
            return e;
        }
        if (is("llIntegerToBase64") || is("llBase64ToInteger")) {
            const bool encode = is("llIntegerToBase64");
            e->op = encode ? Op::EncodeInteger64 : Op::DecodeInteger64;
            e->type = encode ? Type::String : Type::Integer;
            advance();
            require("(");
            e->left = expression();
            require(")");
            const auto type = e->left->type;
            if (encode ? type != Type::Integer : (type != Type::String && type != Type::Key))
                fail(token, "invalid integer Base64 argument");
            e->height = e->left->height + 1;
            if (e->height > 128)
                fail(token, "native LSL expression depth exceeded");
            return e;
        }
        if (is("llXorBase64StringsCorrect") || is("llXorBase64") || is("llXorBase64Strings")) {
            e->slot = is("llXorBase64Strings") ? 2 : is("llXorBase64") ? 1 : 0;
            e->op = Op::XorBase64;
            e->type = Type::String;
            advance();
            require("(");
            e->left = expression();
            require(",");
            e->right = expression();
            require(")");
            const auto text = [](Type type) { return type == Type::String || type == Type::Key; };
            if (!text(e->left->type) || !text(e->right->type))
                fail(token, "Base64 XOR requires two strings");
            e->height = 1 + std::max(e->left->height, e->right->height);
            if (e->height > 128)
                fail(token, "native LSL expression depth exceeded");
            return e;
        }
        if (is("llComputeHash") || is("llHMAC")) {
            const bool hmac = is("llHMAC");
            e->op = hmac ? Op::Hmac : Op::ComputeHash;
            e->type = Type::String;
            advance();
            require("(");
            e->left = expression();
            require(",");
            e->right = expression();
            if (hmac) {
                require(",");
                e->z = expression();
            }
            require(")");
            const auto text = [](Type type) { return type == Type::String || type == Type::Key; };
            if (!text(e->left->type) || !text(e->right->type) || (hmac && !text(e->z->type)))
                fail(token, "hash function requires string arguments");
            e->height = 1 + std::max({e->left->height, e->right->height, hmac ? e->z->height : 0u});
            if (e->height > 128)
                fail(token, "native LSL expression depth exceeded");
            return e;
        }
        if (is("llMD5String") || is("llSHA1String") || is("llSHA256String") || is("osSHA256")) {
            const bool md5 = is("llMD5String");
            e->op = md5 ? Op::Md5 : is("llSHA1String") ? Op::Sha1 : Op::Sha256;
            e->type = Type::String;
            advance();
            require("(");
            e->left = expression();
            if (md5) {
                require(",");
                e->right = expression();
            }
            require(")");
            if ((e->left->type != Type::String && e->left->type != Type::Key) ||
                (e->right && e->right->type != Type::Integer))
                fail(token, "invalid string hash arguments");
            e->height = 1 + std::max(e->left->height, e->right ? e->right->height : 0u);
            if (e->height > 128)
                fail(token, "native LSL expression depth exceeded");
            return e;
        }
        if (is("llHash")) {
            e->op = Op::Hash;
            advance();
            require("(");
            e->left = expression();
            require(")");
            if (e->left->type != Type::String && e->left->type != Type::Key)
                fail(token, "llHash requires text");
            e->height = e->left->height + 1;
            if (e->height > 128)
                fail(token, "native LSL expression depth exceeded");
            return e;
        }
        if (is("llChar") || is("llOrd")) {
            const bool ordinal = is("llOrd");
            e->op = ordinal ? Op::Ord : Op::Char;
            e->type = ordinal ? Type::Integer : Type::String;
            advance();
            require("(");
            e->left = expression();
            if (ordinal) {
                require(",");
                e->right = expression();
            }
            require(")");
            if (ordinal ? ((e->left->type != Type::String && e->left->type != Type::Key) ||
                           e->right->type != Type::Integer)
                        : e->left->type != Type::Integer)
                fail(token, "invalid Unicode query arguments");
            e->height = 1 + std::max(e->left->height, e->right ? e->right->height : 0u);
            if (e->height > 128)
                fail(token, "native LSL expression depth exceeded");
            return e;
        }
        if (is("llStringTrim")) {
            e->op = Op::StringTrim;
            e->type = Type::String;
            advance();
            require("(");
            e->left = expression();
            require(",");
            e->right = expression();
            require(")");
            if ((e->left->type != Type::String && e->left->type != Type::Key) ||
                e->right->type != Type::Integer)
                fail(token, "string trim requires text and an integer mode");
            e->height = 1 + std::max(e->left->height, e->right->height);
            if (e->height > 128)
                fail(token, "native LSL expression depth exceeded");
            return e;
        }
        if (is("llSubStringIndex")) {
            e->op = Op::SubstringIndex;
            advance();
            require("(");
            e->left = expression();
            require(",");
            e->right = expression();
            require(")");
            const auto text = [](Type type) { return type == Type::String || type == Type::Key; };
            if (!text(e->left->type) || !text(e->right->type))
                fail(token, "substring search requires text");
            e->height = 1 + std::max(e->left->height, e->right->height);
            if (e->height > 128)
                fail(token, "native LSL expression depth exceeded");
            return e;
        }
        if (is("llStringLength") || is("llToUpper") || is("llToLower") || is("llEscapeURL") ||
            is("llUnescapeURL") || is("llStringToBase64") || is("llBase64ToString")) {
            const bool length = is("llStringLength");
            e->op = length                   ? Op::StringLength
                    : is("llToUpper")        ? Op::Upper
                    : is("llToLower")        ? Op::Lower
                    : is("llEscapeURL")      ? Op::EscapeUrl
                    : is("llUnescapeURL")    ? Op::UnescapeUrl
                    : is("llStringToBase64") ? Op::EncodeBase64
                                             : Op::DecodeBase64;
            e->type = length ? Type::Integer : Type::String;
            advance();
            require("(");
            e->left = expression();
            require(")");
            if (e->left->type != Type::String && e->left->type != Type::Key)
                fail(token, "string query requires text");
            e->height = e->left->height + 1;
            if (e->height > 128)
                fail(token, "native LSL expression depth exceeded");
            return e;
        }
        if (is("llGenerateKey")) {
            e->op = Op::GenerateKey;
            e->type = Type::Key;
            advance();
            require("(");
            require(")");
            return e;
        }
        if (is("llSameGroup") || is("llIsFriend")) {
            e->op = is("llIsFriend") ? Expression::Op::IsFriend : Expression::Op::SameGroup;
            advance();
            require("(");
            e->left = expression();
            require(")");
            if (e->left->type != Type::String && e->left->type != Type::Key)
                fail(token, e->op == Expression::Op::IsFriend ? "llIsFriend requires text"
                                                             : "llSameGroup requires text");
            e->height = e->left->height + 1;
            if (e->height > 128)
                fail(token, "native LSL expression depth exceeded");
            return e;
        }
        if (is("llRequestAgentData") || is("llRequestSimulatorData")) {
            e->op = is("llRequestAgentData") ? Expression::Op::RequestAgentData
                                             : Expression::Op::RequestSimulatorData;
            e->type = Type::Key;
            advance();
            require("(");
            e->left = expression();
            require(",");
            e->right = expression();
            require(")");
            if ((e->left->type != Type::String && e->left->type != Type::Key) ||
                e->right->type != Type::Integer)
                fail(token, "data request requires text and an integer selector");
            e->height = 1 + std::max(e->left->height, e->right->height);
            if (e->height > 128)
                fail(token, "native LSL expression depth exceeded");
            return e;
        }
        if (is("llRequestInventoryData")) {
            e->op = Expression::Op::RequestInventoryData;
            e->type = Type::Key;
            advance();
            require("(");
            e->left = expression();
            require(")");
            if (e->left->type != Type::String && e->left->type != Type::Key)
                fail(token, "llRequestInventoryData requires an inventory name");
            e->height = e->left->height + 1;
            if (e->height > 128)
                fail(token, "native LSL expression depth exceeded");
            return e;
        }
        if (is("llGetNotecardLineSync") || is("llGetNotecardLine") || is("llGetNumberOfNotecardLines")) {
            using Op = Expression::Op;
            const bool sync = is("llGetNotecardLineSync");
            const bool count = is("llGetNumberOfNotecardLines");
            e->op = sync ? Op::NotecardLineSync : count ? Op::NotecardLineCount : Op::NotecardLine;
            e->type = sync ? Type::String : Type::Key;
            advance();
            require("(");
            e->left = expression();
            if (!count) {
                require(",");
                e->right = expression();
            }
            require(")");
            if ((e->left->type != Type::String && e->left->type != Type::Key) ||
                (e->right && e->right->type != Type::Integer))
                fail(token, "notecard query requires a name or key and an integer line when applicable");
            e->height = 1 + std::max(e->left->height, e->right ? e->right->height : 0u);
            if (e->height > 128)
                fail(token, "native LSL expression depth exceeded");
            return e;
        }
        if (is("llKey2Name") || is("llName2Key") || is("llGetUsername") || is("llGetDisplayName") ||
            is("llRequestUsername") || is("llRequestDisplayName") || is("llRequestUserKey")) {
            using Op = Expression::Op;
            const bool name_to_key = is("llName2Key");
            const bool request_username = is("llRequestUsername");
            const bool request_display = is("llRequestDisplayName");
            const bool request_key = is("llRequestUserKey");
            e->op = name_to_key             ? Op::NameToKey
                    : request_key          ? Op::RequestUserKey
                    : request_username     ? Op::RequestUsername
                    : request_display      ? Op::RequestDisplayName
                    : is("llKey2Name")      ? Op::KeyToName
                    : is("llGetUsername")   ? Op::Username
                                            : Op::DisplayName;
            e->type = name_to_key || request_username || request_display || request_key ? Type::Key : Type::String;
            advance();
            require("(");
            e->left = expression();
            require(")");
            if (e->left->type != Type::String && e->left->type != Type::Key)
                fail(token, "name lookup requires text");
            e->height = e->left->height + 1;
            if (e->height > 128)
                fail(token, "native LSL expression depth exceeded");
            return e;
        }
        if (is("osGetHealRate")) {
            e->op = Op::AgentHealRate;
            e->type = Type::Float;
            advance();
            require("(");
            e->left = expression();
            if (!assignable(Type::Key, e->left->type))
                fail(token, "osGetHealRate requires an avatar key");
            require(")");
            return e;
        }
        if (is("llGetAgentInfo") || is("llGetHealth")) {
            const bool health = is("llGetHealth");
            e->op = health ? Op::AgentHealth : Op::AgentInfo;
            if (health)
                e->type = Type::Float;
            advance();
            require("(");
            e->left = expression();
            require(")");
            if (e->left->type != Type::Key && e->left->type != Type::String)
                fail(token, health ? "llGetHealth requires a key" : "llGetAgentInfo requires a key");
            e->height = e->left->height + 1;
            if (e->height > 128)
                fail(token, "native LSL expression depth exceeded");
            return e;
        }
        if (is("llGetBoundingBox")) {
            e->op = Op::BoundingBox;
            e->type = Type::List;
            advance();
            require("(");
            e->left = expression();
            require(")");
            if (e->left->type != Type::Key && e->left->type != Type::String)
                fail(token, "llGetBoundingBox requires a key");
            e->height = e->left->height + 1;
            if (e->height > 128)
                fail(token, "native LSL expression depth exceeded");
            return e;
        }
        if (is("llGetVisualParams")) {
            e->op = Op::VisualParams;
            e->type = Type::List;
            advance();
            require("(");
            e->left = expression();
            require(",");
            e->right = expression();
            require(")");
            if ((e->left->type != Type::String && e->left->type != Type::Key) || e->right->type != Type::List)
                fail(token, "llGetVisualParams requires a key and list");
            e->height = std::max(e->left->height, e->right->height) + 1;
            if (e->height > 128)
                fail(token, "native LSL expression depth exceeded");
            return e;
        }
        if (is("llGetOwnerKey") || is("llGetAgentSize") || is("llGetAgentLanguage")) {
            const bool size = is("llGetAgentSize");
            const bool language = is("llGetAgentLanguage");
            e->op = language ? Op::AgentLanguage : size ? Op::AgentSize : Op::LookupOwnerKey;
            e->type = language ? Type::String : size ? Type::Vector : Type::Key;
            advance();
            require("(");
            e->left = expression();
            require(")");
            if (e->left->type != Type::String && e->left->type != Type::Key)
                fail(token, size ? "llGetAgentSize requires a key" : "llGetOwnerKey requires text");
            e->height = e->left->height + 1;
            if (e->height > 128)
                fail(token, "native LSL expression depth exceeded");
            return component_access(std::move(e));
        }
        if (is("llGetKey") || is("llGetCreator") || is("llGetOwner")) {
            e->op = is("llGetKey")       ? Expression::Op::ObjectKey
                    : is("llGetCreator") ? Expression::Op::CreatorKey
                                         : Expression::Op::OwnerKey;
            e->type = Type::Key;
            advance();
            require("(");
            require(")");
            return e;
        }
        if (is("llGetInventoryNumber") || is("llGetInventoryName")) {
            const bool name = is("llGetInventoryName");
            e->op = name ? Op::InventoryName : Op::InventoryNumber;
            e->type = name ? Type::String : Type::Integer;
            advance();
            require("(");
            e->left = expression();
            if (name) {
                require(",");
                e->right = expression();
            }
            require(")");
            if (e->left->type != Type::Integer || (e->right && e->right->type != Type::Integer))
                fail(token, "inventory type and index require integers");
            e->height = 1 + std::max(e->left->height, e->right ? e->right->height : 0u);
            if (e->height > 128)
                fail(token, "native LSL expression depth exceeded");
            return e;
        }
        if (is("llGetInventoryPermMask") || is("llGetObjectLinkKey")) {
            const bool link = is("llGetObjectLinkKey");
            e->op = link ? Op::ObjectLinkKey : Op::InventoryPermission;
            e->type = link ? Type::Key : Type::Integer;
            advance();
            require("(");
            e->left = expression();
            require(",");
            e->right = expression();
            require(")");
            if ((e->left->type != Type::String && e->left->type != Type::Key) ||
                e->right->type != Type::Integer)
                fail(token, link ? "llGetObjectLinkKey requires a key and integer link number"
                                 : "inventory permission query requires a name and integer mask selector");
            e->height = 1 + std::max(e->left->height, e->right->height);
            if (e->height > 128)
                fail(token, "native LSL expression depth exceeded");
            return e;
        }
        if (is("llGetObjectPermMask")) {
            e->op = Op::ObjectPermission;
            advance();
            require("(");
            e->left = expression();
            require(")");
            if (e->left->type != Type::Integer)
                fail(token, "llGetObjectPermMask requires an integer mask selector");
            e->height = e->left->height + 1;
            if (e->height > 128)
                fail(token, "native LSL expression depth exceeded");
            return e;
        }
        if (is("llGetScriptState")) {
            e->op = Op::ScriptRunning;
            e->type = Type::Integer;
            advance();
            require("(");
            e->left = expression();
            require(")");
            if (e->left->type != Type::String && e->left->type != Type::Key)
                fail(token, "script name requires text");
            e->height = e->left->height + 1;
            if (e->height > 128)
                fail(token, "native LSL expression depth exceeded");
            return e;
        }
        if (is("llGetInventoryType") || is("llGetInventoryKey") || is("llGetInventoryDesc") ||
            is("llGetInventoryCreator") || is("llGetInventoryAcquireTime")) {
            const bool key = is("llGetInventoryKey");
            const bool description = is("llGetInventoryDesc");
            const bool creator = is("llGetInventoryCreator");
            const bool acquire_time = is("llGetInventoryAcquireTime");
            e->op = creator        ? Op::InventoryCreator
                    : acquire_time ? Op::InventoryAcquireTime
                    : description  ? Op::InventoryDescription
                    : key          ? Op::InventoryKey
                                   : Op::InventoryType;
            e->type = description || acquire_time ? Type::String : key || creator ? Type::Key : Type::Integer;
            advance();
            require("(");
            e->left = expression();
            require(")");
            if (e->left->type != Type::String && e->left->type != Type::Key)
                fail(token, "inventory name requires text");
            e->height = e->left->height + 1;
            if (e->height > 128)
                fail(token, "native LSL expression depth exceeded");
            return e;
        }
        if (is("llLinksetDataWrite") || is("llLinksetDataWriteProtected") || is("llLinksetDataDelete") ||
            is("llLinksetDataDeleteProtected")) {
            const bool write = is("llLinksetDataWrite") || is("llLinksetDataWriteProtected");
            const bool protected_call =
                is("llLinksetDataWriteProtected") || is("llLinksetDataDeleteProtected");
            e->op = Op::LinksetDataWrite;
            e->type = Type::Integer;
            advance();
            require("(");
            e->left = expression();
            if (write) {
                require(",");
                e->right = expression();
            }
            if (protected_call) {
                require(",");
                e->z = expression();
            }
            require(")");
            for (const auto* argument : {e->left.get(), e->right.get(), e->z.get()}) {
                if (!argument)
                    continue;
                if (argument->type != Type::String)
                    fail(token, "Linkset data mutation requires string arguments");
                e->height = std::max(e->height, 1 + argument->height);
            }
            if (e->height > 128)
                fail(token, "native LSL expression depth exceeded");
            return e;
        }
        if (is("llLinksetDataDeleteFound")) {
            e->op = Op::LinksetDataDeleteFound;
            e->type = Type::List;
            advance();
            require("(");
            e->left = expression();
            require(",");
            e->right = expression();
            require(")");
            if (e->left->type != Type::String || e->right->type != Type::String)
                fail(token, "Linkset data bulk deletion requires a pattern and password");
            e->height = 1 + std::max(e->left->height, e->right->height);
            if (e->height > 128)
                fail(token, "native LSL expression depth exceeded");
            return e;
        }
        if (is("llLinksetDataListKeys") || is("llLinksetDataFindKeys") || is("llLinksetDataCountFound")) {
            const bool count = is("llLinksetDataCountFound");
            const bool filtered = !is("llLinksetDataListKeys");
            e->op = count ? Op::LinksetDataCountFound : filtered ? Op::LinksetDataFind : Op::LinksetDataList;
            e->type = count ? Type::Integer : Type::List;
            advance();
            require("(");
            if (filtered) {
                e->left = expression();
                if (e->left->type != Type::String)
                    fail(token, "Linkset data pattern must be a string");
                if (!count)
                    require(",");
            }
            if (!count) {
                e->right = expression();
                require(",");
                e->z = expression();
                if (e->right->type != Type::Integer || e->z->type != Type::Integer)
                    fail(token, "Linkset data pagination requires integers");
            }
            require(")");
            for (const auto* argument : {e->left.get(), e->right.get(), e->z.get()})
                if (argument)
                    e->height = std::max(e->height, 1 + argument->height);
            if (e->height > 128)
                fail(token, "native LSL expression depth exceeded");
            return e;
        }
        if (is("llLinksetDataCountKeys") || is("llLinksetDataAvailable")) {
            e->op = is("llLinksetDataAvailable") ? Op::LinksetDataAvailable : Op::LinksetDataCount;
            e->type = Type::Integer;
            advance();
            require("(");
            require(")");
            return e;
        }
        if (is("llLinksetDataRead") || is("llLinksetDataReadProtected")) {
            const bool protected_read = is("llLinksetDataReadProtected");
            e->op = Op::LinksetDataRead;
            e->type = Type::String;
            advance();
            require("(");
            e->left = expression();
            if (protected_read) {
                require(",");
                e->right = expression();
            }
            require(")");
            if (e->left->type != Type::String || (e->right && e->right->type != Type::String))
                fail(token, "Linkset data read requires string arguments");
            e->height = 1 + std::max(e->left->height, e->right ? e->right->height : 0);
            if (e->height > 128)
                fail(token, "native LSL expression depth exceeded");
            return e;
        }
        if (is("llGetAnimationOverride")) {
            e->op = Op::AnimationOverride;
            e->type = Type::String;
            advance();
            require("(");
            e->left = expression();
            require(")");
            if (e->left->type != Type::String)
                fail(token, "Animation override requires a movement state string");
            e->height = 1 + e->left->height;
            if (e->height > 128)
                fail(token, "native LSL expression depth exceeded");
            return e;
        }
        if (is("llGetObjectAnimationNames")) {
            e->op = Op::ObjectAnimationNames;
            e->type = Type::List;
            advance();
            require("(");
            require(")");
            return e;
        }
        if (is("llGetAnimationList") || is("llGetAnimation")) {
            const bool list = is("llGetAnimationList");
            e->op = list ? Op::AnimationList : Op::AnimationState;
            e->type = list ? Type::List : Type::String;
            advance();
            require("(");
            e->left = expression();
            require(")");
            if (e->left->type != Type::Key && e->left->type != Type::String)
                fail(token, "Animation list requires an agent key");
            e->height = 1 + e->left->height;
            if (e->height > 128)
                fail(token, "native LSL expression depth exceeded");
            return e;
        }
        if (is("llWorldPosToHUD")) {
            e->op = Op::WorldPositionToHud;
            e->type = Type::Vector;
            advance();
            require("(");
            e->left = expression();
            if (e->left->type != Type::Vector)
                fail(token, "llWorldPosToHUD requires a world position vector");
            require(")");
            return e;
        }
        if (is("llGetCameraPos") || is("llGetCameraRot") || is("llGetCameraFOV") || is("llGetCameraAspect")) {
            if (is("llGetCameraPos")) {
                e->op = Op::CameraPosition;
                e->type = Type::Vector;
            } else if (is("llGetCameraRot")) {
                e->op = Op::CameraRotation;
                e->type = Type::Rotation;
            } else {
                e->op = is("llGetCameraFOV") ? Op::CameraFov : Op::CameraAspect;
                e->type = Type::Float;
            }
            e->value = token.text;
            advance();
            require("(");
            require(")");
            return e;
        }
        if (is("llGetPermissions") || is("llGetPermissionsKey")) {
            const bool key = is("llGetPermissionsKey");
            e->op = key ? Op::ScriptPermissionsKey : Op::ScriptPermissions;
            e->type = key ? Type::Key : Type::Integer;
            advance();
            require("(");
            require(")");
            return e;
        }
        if (is("llGetStartString") || is("llGetStartParameter")) {
            const bool string_parameter = is("llGetStartString");
            e->op = string_parameter ? Op::StartString : Op::StartParameter;
            e->type = string_parameter ? Type::String : Type::Integer;
            advance();
            require("(");
            require(")");
            return e;
        }
        if (is("llRegionSayTo")) {
            const auto command = token;
            e->op = Op::RegionSayTo;
            advance();
            require("(");
            e->left = expression();
            require(",");
            e->right = expression();
            require(",");
            e->z = expression();
            require(")");
            if (!assignable(Type::Key, e->left->type) || e->right->type != Type::Integer ||
                !assignable(Type::String, e->z->type))
                fail(command, "llRegionSayTo requires a target key, an integer channel, and text");
            e->height = 1 + std::max({e->left->height, e->right->height, e->z->height});
            if (e->height > 128)
                fail(command, "native LSL expression depth exceeded");
            return e;
        }
        if (is("llListen") || is("osListenRegex") || is("llListenControl") || is("llListenRemove")) {
            const bool regex = is("osListenRegex");
            const bool add = regex || is("llListen");
            const bool control = is("llListenControl");
            const auto command = token;
            e->op = add ? Op::Listen : control ? Op::ListenControl : Op::ListenRemove;
            advance();
            require("(");
            e->left = expression();
            if (e->left->type != Type::Integer)
                fail(command, "listen channel or handle must be an integer");
            if (add || control) {
                require(",");
                e->right = expression();
                const auto type = e->right->type;
                if (control ? type != Type::Integer : type != Type::String && type != Type::Key)
                    fail(command, "invalid listen argument type");
            }
            if (add) {
                require(",");
                e->z = expression();
                require(",");
                e->w = expression();
                if ((e->z->type != Type::String && e->z->type != Type::Key) ||
                    (e->w->type != Type::String && e->w->type != Type::Key))
                    fail(command, "listen filters must be text");
            }
            if (regex) {
                require(",");
                auto flags = expression();
                if (flags->type != Type::Integer)
                    fail(command, "osListenRegex flags must be an integer");
                e->height = std::max(e->height, flags->height + 1);
                e->elements.push_back(std::move(flags));
            }
            require(")");
            for (const auto* argument : {e->left.get(), e->right.get(), e->z.get(), e->w.get()})
                if (argument)
                    e->height = std::max(e->height, argument->height + 1);
            if (e->height > 128)
                fail(command, "native LSL expression depth exceeded");
            return e;
        }
        if (is("llSensor") || is("llSensorRepeat") || is("llSensorRemove")) {
            const int arguments = is("llSensorRemove") ? 0 : is("llSensorRepeat") ? 6 : 5;
            advance();
            require("(");
            for (int index = 0; index < arguments; ++index) {
                if (index)
                    require(",");
                auto argument = expression();
                const auto type = argument->type;
                const bool valid = index == 0 ? type == Type::String
                    : index == 1 ? type == Type::String || type == Type::Key
                    : index == 2 ? type == Type::Integer
                    : numeric(type);
                if (!valid)
                    fail(token, "Invalid sensor arguments");
                e->height = std::max(e->height, argument->height + 1);
                e->elements.push_back(std::move(argument));
            }
            require(")");
            if (e->height > 128)
                fail(token, "native LSL expression depth exceeded");
            e->op = Op::Sensor;
            e->type = Type::Integer;
            return e;
        }
        if (is("llCreateLink") || is("llBreakLink") || is("llBreakAllLinks")) {
            const bool create = is("llCreateLink");
            const bool all = is("llBreakAllLinks");
            advance();
            require("(");
            if (!all) {
                e->left = expression();
                e->height = e->left->height + 1;
            }
            if (create) {
                require(",");
                e->right = expression();
                e->height = std::max(e->height, e->right->height + 1);
                if ((e->left->type != Type::String && e->left->type != Type::Key) ||
                    e->right->type != Type::Integer)
                    fail(token, "llCreateLink requires a key and an integer");
            } else if (!all && e->left->type != Type::Integer) {
                fail(token, "llBreakLink requires an integer link number");
            }
            require(")");
            if (e->height > 128)
                fail(token, "native LSL expression depth exceeded");
            e->op = create ? Op::CreateLink : Op::BreakLink;
            e->type = Type::Integer;
            return e;
        }
        if (is("llAttachToAvatar") || is("llAttachToAvatarTemp")) {
            e->slot = is("llAttachToAvatarTemp");
            advance();
            require("(");
            e->left = expression();
            require(")");
            if (e->left->type != Type::Integer)
                fail(token, "Attachment point must be an integer");
            e->op = Op::AttachToAvatar;
            e->type = Type::Integer;
            e->height = e->left->height + 1;
            if (e->height > 128)
                fail(token, "native LSL expression depth exceeded");
            return e;
        }
        if (is("llDetachFromAvatar")) {
            advance();
            require("(");
            require(")");
            e->op = Op::DetachFromAvatar;
            e->type = Type::Integer;
            return e;
        }
        if (is("llSetRemoteScriptAccessPin")) {
            advance();
            require("(");
            e->left = expression();
            require(")");
            if (e->left->type != Type::Integer)
                fail(token, "llSetRemoteScriptAccessPin requires an integer PIN");
            e->op = Op::SetRemoteScriptPin;
            e->type = Type::Integer;
            e->height = e->left->height + 1;
            if (e->height > 128)
                fail(token, "native LSL expression depth exceeded");
            return e;
        }
        if (is("llRemoteLoadScriptPin")) {
            advance();
            require("(");
            for (int index = 0; index < 5; ++index) {
                if (index)
                    require(",");
                auto argument = expression();
                const bool valid = index == 0 ? (argument->type == Type::String || argument->type == Type::Key)
                                            : argument->type == (index == 1 ? Type::String : Type::Integer);
                if (!valid)
                    fail(token, "Invalid llRemoteLoadScriptPin arguments");
                e->height = std::max(e->height, argument->height + 1);
                e->elements.push_back(std::move(argument));
            }
            require(")");
            if (e->height > 128)
                fail(token, "native LSL expression depth exceeded");
            e->op = Op::RemoteLoadScript;
            e->type = Type::Integer;
            return e;
        }
        if (is("llGetNextEmail") || is("llEmail") || is("llTargetedEmail")) {
            const auto command = token;
            const bool targeted = is("llTargetedEmail");
            const bool sending = is("llEmail") || targeted;
            advance();
            require("(");
            e->left = expression();
            require(",");
            e->right = expression();
            if (sending) {
                require(",");
                e->z = expression();
            }
            require(")");
            const auto is_text = [](Type type) { return type == Type::String || type == Type::Key; };
            if ((targeted ? e->left->type != Type::Integer : !is_text(e->left->type)) ||
                !is_text(e->right->type) ||
                (sending && !is_text(e->z->type)))
                fail(command, "email arguments must be strings");
            e->height = std::max(e->left->height, e->right->height) + 1;
            if (sending)
                e->height = std::max(e->height, e->z->height + 1);
            if (e->height > 128)
                fail(command, "native LSL expression depth exceeded");
            e->op = sending ? Op::SendEmail : Op::RequestEmail;
            e->slot = targeted;
            e->type = Type::Integer;
            return e;
        }
        if (is("llMessageLinked")) {
            e->op = Op::SendLinkMessage;
            advance();
            require("(");
            e->left = expression();
            require(",");
            e->right = expression();
            require(",");
            e->z = expression();
            require(",");
            e->w = expression();
            require(")");
            const auto is_text = [](Type type) { return type == Type::String || type == Type::Key; };
            if (e->left->type != Type::Integer || e->right->type != Type::Integer ||
                !is_text(e->z->type) || !is_text(e->w->type))
                fail(token, "llMessageLinked requires two integers and two strings");
            e->height = 1 + std::max({e->left->height, e->right->height, e->z->height, e->w->height});
            if (e->height > 128)
                fail(token, "native LSL expression depth exceeded");
            return e;
        }
        if (is("llGetPhysicsMaterial") || is("llSetPhysicsMaterial")) {
            const bool set = is("llSetPhysicsMaterial");
            e->op = set ? Op::SetPhysicsMaterial : Op::PhysicsMaterial;
            e->type = set ? Type::Integer : Type::List;
            advance();
            require("(");
            if (set) {
                for (int i = 0; i < 5; ++i) {
                    if (i)
                        require(",");
                    auto argument = expression();
                    if (i == 0 ? argument->type != Type::Integer : !numeric(argument->type))
                        fail(token, "Invalid physics material arguments");
                    e->height = std::max(e->height, argument->height + 1);
                    e->elements.push_back(std::move(argument));
                }
            }
            require(")");
            if (e->height > 128)
                fail(token, "native LSL expression depth exceeded");
            return e;
        }
        if (is("llParticleSystem") || is("llLinkParticleSystem") ||
            is("osParticleSystem") || is("osLinkParticleSystem")) {
            const bool linked = is("llLinkParticleSystem") || is("osLinkParticleSystem");
            e->op = Op::SetParticleSystem;
            e->value = std::int32_t(is("osParticleSystem") || is("osLinkParticleSystem"));
            advance();
            require("(");
            if (linked) {
                e->left = expression();
                if (e->left->type != Type::Integer)
                    fail(token, "Particle link selector must be an integer");
                require(",");
            }
            e->right = expression();
            if (e->right->type != Type::List)
                fail(token, "Particle rules must be a list");
            require(")");
            e->height = 1 + std::max(e->right->height, e->left ? e->left->height : 0u);
            if (e->height > 128)
                fail(token, "native LSL expression depth exceeded");
            return e;
        }
        if (is("llSetTextureAnim") || is("llSetLinkTextureAnim")) {
            const int integers = is("llSetLinkTextureAnim") ? 5 : 4;
            e->op = Op::SetTextureAnimation;
            advance();
            require("(");
            for (int i = 0; i < integers + 3; ++i) {
                if (i)
                    require(",");
                auto argument = expression();
                if (i < integers ? argument->type != Type::Integer : !numeric(argument->type))
                    fail(token, "Invalid texture animation arguments");
                e->height = std::max(e->height, argument->height + 1);
                e->elements.push_back(std::move(argument));
            }
            require(")");
            if (e->height > 128)
                fail(token, "native LSL expression depth exceeded");
            return e;
        }
        if (is("llSetLinkTexture") || is("llSetLinkColor") || is("llSetLinkAlpha")) {
            using Property = LslTextureProperty;
            const auto property = is("llSetLinkTexture") ? Property::Name
                : is("llSetLinkColor") ? Property::Color : Property::Alpha;
            e->op = Op::SetLinkedTextureProperty;
            e->value = static_cast<std::int32_t>(property);
            advance();
            require("(");
            e->left = expression();
            require(",");
            e->right = expression();
            require(",");
            e->z = expression();
            require(")");
            const auto type = e->right->type;
            const bool valid_value = property == Property::Name ? type == Type::String || type == Type::Key
                : property == Property::Color ? type == Type::Vector : numeric(type);
            if (e->left->type != Type::Integer || !valid_value || e->z->type != Type::Integer)
                fail(token, "Invalid linked texture setter arguments");
            e->height = 1 + std::max({e->left->height, e->right->height, e->z->height});
            if (e->height > 128)
                fail(token, "native LSL expression depth exceeded");
            return e;
        }
        if (is("llSetTexture") || is("llSetColor") || is("llSetAlpha") ||
            is("llOffsetTexture") || is("llScaleTexture") || is("llRotateTexture")) {
            using Property = LslTextureProperty;
            auto property = Property::Rotation;
            if (is("llSetTexture"))
                property = Property::Name;
            else if (is("llSetColor"))
                property = Property::Color;
            else if (is("llSetAlpha"))
                property = Property::Alpha;
            else if (is("llOffsetTexture"))
                property = Property::Offset;
            else if (is("llScaleTexture"))
                property = Property::Repeat;
            e->op = Op::SetTextureProperty;
            e->value = static_cast<std::int32_t>(property);
            advance();
            require("(");
            e->left = expression();
            require(",");
            e->right = expression();
            if (property == Property::Offset || property == Property::Repeat) {
                require(",");
                e->z = expression();
            }
            require(")");
            const auto first = e->left->type;
            const bool valid_value = property == Property::Name ? first == Type::String || first == Type::Key
                : property == Property::Color ? first == Type::Vector : numeric(first);
            if (!valid_value || (e->z ? !numeric(e->right->type) || e->z->type != Type::Integer
                                      : e->right->type != Type::Integer))
                fail(token, "Invalid texture setter arguments");
            e->height = 1 + std::max({e->left->height, e->right->height, e->z ? e->z->height : 0u});
            if (e->height > 128)
                fail(token, "native LSL expression depth exceeded");
            return e;
        }
        if (is("llGetTexture") || is("llGetColor") || is("llGetAlpha") ||
            is("llGetTextureOffset") || is("llGetTextureScale") || is("llGetTextureRot")) {
            using Property = LslTextureProperty;
            auto property = Property::Rotation;
            if (is("llGetTexture"))
                property = Property::Name;
            else if (is("llGetColor"))
                property = Property::Color;
            else if (is("llGetAlpha"))
                property = Property::Alpha;
            else if (is("llGetTextureOffset"))
                property = Property::Offset;
            else if (is("llGetTextureScale"))
                property = Property::Repeat;
            e->op = Op::TextureProperty;
            e->value = static_cast<std::int32_t>(property);
            e->type = property == Property::Name ? Type::String
                : property == Property::Alpha || property == Property::Rotation ? Type::Float : Type::Vector;
            advance();
            require("(");
            e->left = expression();
            require(")");
            if (e->left->type != Type::Integer)
                fail(token, "Texture query requires an integer face");
            e->height = e->left->height + 1;
            if (e->height > 128)
                fail(token, "native LSL expression depth exceeded");
            return e;
        }
        if (is("llGetObjectName") || is("llGetObjectDesc") || is("llGetScriptName")) {
            e->op = is("llGetObjectName")   ? Op::ObjectName
                    : is("llGetScriptName") ? Op::ScriptName
                                            : Op::ObjectDescription;
            e->type = Type::String;
            advance();
            require("(");
            require(")");
            return e;
        }
        if (is("llGetVel") || is("llGetOmega") || is("llGetAccel")) {
            e->op = is("llGetVel")     ? Op::ObjectVelocity
                    : is("llGetOmega") ? Op::ObjectOmega
                                       : Op::ObjectAcceleration;
            e->type = Type::Vector;
            advance();
            require("(");
            require(")");
            return component_access(std::move(e));
        }
        if (is("llGetEnergy")) {
            e->type = Type::Float;
            e->value = 1.;
            advance();
            require("(");
            require(")");
            return e;
        }
        if (is("URL_REQUEST_GRANTED") || is("URL_REQUEST_DENIED")) {
            e->type = Type::String;
            e->value = token.text;
            advance();
            return e;
        }
        if (is("llGetEnv")) {
            e->type = Type::String;
            e->op = Op::EnvironmentValue;
            advance();
            require("(");
            e->left = expression();
            require(")");
            if (e->left->type != Type::String && e->left->type != Type::Key)
                fail(token, "llGetEnv requires text");
            e->height = e->left->height + 1;
            if (e->height > 128)
                fail(token, "native LSL expression depth exceeded");
            return e;
        }
        if (is("osGetGridLoginURI") || is("osGetGridHomeURI") || is("osGetGridGatekeeperURI") ||
            is("osGetGridCustom")) {
            const auto query = is("osGetGridLoginURI") ? LslGridQuery::Login
                             : is("osGetGridHomeURI") ? LslGridQuery::Home
                             : is("osGetGridGatekeeperURI") ? LslGridQuery::Gatekeeper
                             : LslGridQuery::Custom;
            const auto command = token;
            e->op = Op::GridInfo;
            e->type = Type::String;
            e->slot = static_cast<std::size_t>(query);
            advance();
            require("(");
            if (query == LslGridQuery::Custom) {
                e->left = expression();
                if (e->left->type != Type::String && e->left->type != Type::Key)
                    fail(command, "osGetGridCustom requires a text key");
                e->height = 1 + e->left->height;
                if (e->height > 128)
                    fail(command, "native LSL expression depth exceeded");
            }
            require(")");
            return e;
        }
        if (is("llGetSimulatorHostname") || is("osGetRegionSize") || is("osGetScriptEngineName") ||
            is("osGetGridName") || is("osGetGridNick")) {
            const bool region_size = is("osGetRegionSize");
            e->type = region_size ? Type::Vector : Type::String;
            e->op = region_size ? Op::RegionSize
                    : is("osGetScriptEngineName") ? Op::ScriptEngineName
                    : is("osGetGridName") ? Op::GridName
                    : is("osGetGridNick") ? Op::GridNick : Op::SimulatorHostname;
            advance();
            require("(");
            require(")");
            return e;
        }
        if (is("llRequestURL") || is("llRequestSecureURL") || is("llGetFreeURLs") ||
            is("osRequestURL") || is("osRequestSecureURL")) {
            const bool free = is("llGetFreeURLs");
            const bool extended = is("osRequestURL") || is("osRequestSecureURL");
            const bool secure = is("llRequestSecureURL") || is("osRequestSecureURL");
            e->type = free ? Type::Integer : Type::Key;
            e->op = free ? Op::FreeUrls : secure ? Op::RequestSecureUrl : Op::RequestUrl;
            advance();
            require("(");
            if (extended) {
                e->left = expression();
                if (e->left->type != Type::List)
                    fail(token, "OSSL URL options must be a list");
            }
            require(")");
            return e;
        }
        if (is("llSendRemoteData")) {
            const auto command = token;
            advance();
            require("(");
            for (const auto type : {Type::String, Type::String, Type::Integer, Type::String}) {
                if (!e->elements.empty())
                    require(",");
                auto argument = expression();
                if (!assignable(type, argument->type))
                    fail(command, "invalid argument type for llSendRemoteData");
                e->height = std::max(e->height, argument->height + 1);
                e->elements.push_back(std::move(argument));
            }
            require(")");
            if (e->height > 128)
                fail(command, "native LSL expression depth exceeded");
            e->op = Op::SendRemoteData;
            e->type = Type::Key;
            return e;
        }
        if (is("llHTTPRequest")) {
            e->op = Op::HttpRequest;
            e->type = Type::Key;
            advance();
            require("(");
            e->left = expression();
            require(",");
            e->right = expression();
            require(",");
            e->z = expression();
            require(")");
            if ((e->left->type != Type::String && e->left->type != Type::Key) || e->right->type != Type::List ||
                (e->z->type != Type::String && e->z->type != Type::Key))
                fail(token, "llHTTPRequest requires a URL, options list, and body");
            e->height = 1 + std::max({e->left->height, e->right->height, e->z->height});
            if (e->height > 128)
                fail(token, "native LSL expression depth exceeded");
            return e;
        }
        if (is("llReleaseURL") || is("llGetHTTPHeader") || is("llHTTPResponse") || is("llSetContentType")) {
            const bool release = is("llReleaseURL");
            const bool respond = is("llHTTPResponse");
            const bool content_type = is("llSetContentType");
            e->op = release ? Op::ReleaseUrl : respond ? Op::HttpResponse
                    : content_type ? Op::HttpContentType : Op::HttpHeader;
            e->type = release || respond || content_type ? Type::Integer : Type::String;
            advance();
            require("(");
            e->left = expression();
            if (!release) {
                require(",");
                e->right = expression();
                if (respond) {
                    require(",");
                    e->z = expression();
                }
            }
            require(")");
            if ((e->left->type != Type::Key && e->left->type != Type::String) ||
                (respond && (e->right->type != Type::Integer || e->z->type != Type::String)) ||
                (!release && !respond && e->right->type != (content_type ? Type::Integer : Type::String)))
                fail(token, "invalid inbound HTTP arguments");
            return e;
        }
        if (is("llGetFreeMemory") || is("llGetUsedMemory")) {
            e->op = is("llGetFreeMemory") ? Op::GetFreeMemory : Op::GetUsedMemory;
            advance();
            require("(");
            require(")");
            return e;
        }
        if (is("llGetMemoryLimit") || is("llGetSPMaxMemory")) {
            e->value = std::int32_t{65536};
            advance();
            require("(");
            require(")");
            return e;
        }
        if (is("llManageEstateAccess")) {
            e->op = Op::ManageEstateAccess;
            advance();
            require("(");
            e->left = expression();
            require(",");
            e->right = expression();
            require(")");
            if (e->left->type != Type::Integer ||
                (e->right->type != Type::Key && e->right->type != Type::String))
                fail(token, "llManageEstateAccess requires an integer action and a key");
            e->height = 1 + std::max(e->left->height, e->right->height);
            if (e->height > 128)
                fail(token, "native LSL expression depth exceeded");
            return e;
        }
        if (is("llSetMemoryLimit")) {
            e->op = Op::SetMemoryLimit;
            advance();
            require("(");
            e->left = expression();
            require(")");
            if (e->left->type != Type::Integer)
                fail(token, "llSetMemoryLimit requires an integer");
            e->height = e->left->height + 1;
            if (e->height > 128)
                fail(token, "native LSL expression depth exceeded");
            return e;
        }
        if (is("llParcelMediaQuery")) {
            e->op = Op::ParcelMediaQuery;
            e->type = Type::List;
            advance();
            require("(");
            e->left = expression();
            require(")");
            if (e->left->type != Type::List)
                fail(token, "llParcelMediaQuery requires a list of commands");
            e->height = e->left->height + 1;
            if (e->height > 128)
                fail(token, "native LSL expression depth exceeded");
            return e;
        }
        if (is("llGetParcelMusicURL")) {
            e->op = Op::ParcelMusicUrl;
            e->type = Type::String;
            advance();
            require("(");
            require(")");
            return e;
        }
        if (is("llGetParcelPrimOwners")) {
            e->op = Op::ParcelPrimOwners;
            e->type = Type::List;
            advance();
            require("(");
            e->left = expression();
            require(")");
            if (e->left->type != Type::Vector)
                fail(token, "llGetParcelPrimOwners requires a vector");
            e->height = e->left->height + 1;
            if (e->height > 128)
                fail(token, "native LSL expression depth exceeded");
            return e;
        }
        if (is("llGetParcelMaxPrims") || is("llGetParcelPrimCount")) {
            const bool capacity = is("llGetParcelMaxPrims");
            e->op = capacity ? Op::ParcelMaxPrims : Op::ParcelPrimCount;
            advance();
            require("(");
            e->left = expression();
            require(",");
            e->right = expression();
            if (!capacity) {
                require(",");
                e->z = expression();
            }
            require(")");
            if (e->left->type != Type::Vector || e->right->type != Type::Integer ||
                (e->z && e->z->type != Type::Integer))
                fail(token, "Parcel prim query requires a vector and integer selectors");
            e->height = 1 + std::max({e->left->height, e->right->height, e->z ? e->z->height : 0u});
            if (e->height > 128)
                fail(token, "native LSL expression depth exceeded");
            return e;
        }
        if (is("llGetParcelDetails")) {
            e->op = Expression::Op::ParcelDetails;
            e->type = Type::List;
            advance();
            require("(");
            e->left = expression();
            require(",");
            e->right = expression();
            require(")");
            if (e->left->type != Type::Vector || e->right->type != Type::List)
                fail(token, "llGetParcelDetails requires a vector and list");
            e->height = 1 + std::max(e->left->height, e->right->height);
            if (e->height > 128)
                fail(token, "native LSL expression depth exceeded");
            return e;
        }
        if (is("llGetParcelFlags") || is("llScriptDanger") || is("llGetLandOwnerAt") || is("llOverMyLand")) {
            using Op = Expression::Op;
            const bool over_my_land = is("llOverMyLand");
            const bool land_owner = is("llGetLandOwnerAt");
            e->op = over_my_land ? Op::OverMyLand : land_owner ? Op::LandOwnerAt
                          : is("llScriptDanger") ? Op::ScriptDanger : Op::ParcelFlags;
            e->type = land_owner ? Type::Key : Type::Integer;
            advance();
            require("(");
            e->left = expression();
            require(")");
            if (over_my_land && e->left->type != Type::String && e->left->type != Type::Key)
                fail(token, "llOverMyLand requires a key");
            if (!over_my_land && e->left->type != Type::Vector)
                fail(token, "parcel query requires a vector");
            e->height = e->left->height + 1;
            if (e->height > 128)
                fail(token, "native LSL expression depth exceeded");
            return e;
        }
        if (is("osGetAgents") || is("osGetInertiaData")) {
            e->op = is("osGetInertiaData") ? Op::InertiaData : Op::RegionAgentNames;
            advance();
            require("(");
            require(")");
            e->type = Type::List;
            return e;
        }
        if (is("osReplaceAgentEnvironment")) {
            advance();
            require("(");
            for (std::size_t i = 0; i < 3; ++i) {
                if (i != 0)
                    require(",");
                auto argument = expression();
                const auto type = argument->type;
                if (i == 1 ? type != Type::Integer : type != Type::String && type != Type::Key)
                    fail(token, "Invalid agent environment replacement argument");
                e->height = std::max(e->height, argument->height + 1);
                if (e->height > 128)
                    fail(token, "native LSL expression depth exceeded");
                e->elements.push_back(std::move(argument));
            }
            require(")");
            e->op = Op::ReplaceAgentEnvironment;
            e->type = Type::Integer;
            return e;
        }
        if (is("osReplaceRegionEnvironment") || is("osReplaceParcelEnvironment")) {
            const bool parcel = is("osReplaceParcelEnvironment");
            advance();
            require("(");
            const std::size_t count = parcel ? 2 : 7;
            for (std::size_t i = 0; i < count; ++i) {
                if (i != 0)
                    require(",");
                auto argument = expression();
                const auto type = argument->type;
                if ((i == 0 && type != Type::Integer) ||
                    (i == 1 && type != Type::String && type != Type::Key) ||
                    (i >= 2 && !numeric(type)))
                    fail(token, "Invalid environment replacement argument");
                e->height = std::max(e->height, argument->height + 1);
                if (e->height > 128)
                    fail(token, "native LSL expression depth exceeded");
                e->elements.push_back(std::move(argument));
            }
            require(")");
            e->op = parcel ? Op::ReplaceParcelEnvironment : Op::ReplaceRegionEnvironment;
            e->type = Type::Integer;
            return e;
        }
        if (token.kind == Kind::Name && is_environment_query(token.text)) {
            const bool time_string = is("osGetApparentTimeString") || is("osGetApparentRegionTimeString");
            const bool sun_parameter = is("osGetSunParam") || is("osSunGetParam");
            e->op = time_string ? Op::ApparentTimeString : sun_parameter ? Op::SunParameter
                  : is("osGetCurrentSunHour") ? Op::ApparentSunHour : Op::ApparentTime;
            e->type = time_string ? Type::String : Type::Float;
            e->slot = is("osSunGetParam") ? 1 : 0;
            advance();
            require("(");
            if (time_string || sun_parameter) {
                e->left = expression();
                if (time_string && e->left->type != Type::Integer)
                    fail(token, "Apparent time format flag must be an integer");
                if (sun_parameter && e->left->type != Type::String && e->left->type != Type::Key)
                    fail(token, "Sun parameter name must be a string");
                e->height = e->left->height + 1;
                if (e->height > 128)
                    fail(token, "native LSL expression depth exceeded");
            }
            require(")");
            return e;
        }
        if (token.kind == Kind::Name && token.text.starts_with("llGet")) {
            const auto query = std::ranges::find(lsl_environment_queries, token.text, &LslEnvironmentQuery::name);
            if (query != std::end(lsl_environment_queries)) {
                advance();
                require("(");
                require(")");
                e->op = Op::EnvironmentQuery;
                e->type = query->type;
                e->slot = static_cast<std::size_t>(query - std::begin(lsl_environment_queries));
                return e;
            }
        }
        if (is("osGetAgentIP")) {
            advance();
            require("(");
            e->left = expression();
            require(")");
            if (e->left->type != Type::String && e->left->type != Type::Key)
                fail(token, "osGetAgentIP requires an avatar key");
            e->height = e->left->height + 1;
            if (e->height > 128)
                fail(token, "native LSL expression depth exceeded");
            e->op = Op::AgentIpAddress;
            e->type = Type::String;
            return e;
        }
        if (is("llGetAgentList")) {
            e->op = Expression::Op::AgentList;
            e->type = Type::List;
            advance();
            require("(");
            e->left = expression();
            require(",");
            e->right = expression();
            require(")");
            if (e->left->type != Type::Integer || e->right->type != Type::List)
                fail(token, "llGetAgentList requires an integer scope and list options");
            e->height = 1 + std::max(e->left->height, e->right->height);
            if (e->height > 128)
                fail(token, "native LSL expression depth exceeded");
            return e;
        }
        if (is("llGetNumberOfSides")) {
            e->op = Op::LinkSides;
            advance();
            require("(");
            require(")");
            return e;
        }
        if (is("llGetRenderMaterial")) {
            e->op = Op::RenderMaterial;
            e->type = Type::String;
            advance();
            require("(");
            e->left = expression();
            require(")");
            if (e->left->type != Type::Integer)
                fail(token, "llGetRenderMaterial requires an integer face number");
            e->height = 1 + e->left->height;
            if (e->height > 128)
                fail(token, "native LSL expression depth exceeded");
            return e;
        }
        if (is("llIsLinkGLTFMaterial")) {
            e->op = Op::LinkHasRenderMaterial;
            e->type = Type::Integer;
            advance();
            require("(");
            e->left = expression();
            require(",");
            e->right = expression();
            require(")");
            if (e->left->type != Type::Integer || e->right->type != Type::Integer)
                fail(token, "llIsLinkGLTFMaterial requires integer link and face numbers");
            e->height = 1 + std::max(e->left->height, e->right->height);
            if (e->height > 128)
                fail(token, "native LSL expression depth exceeded");
            return e;
        }
        if (is("llGetLinkNumberOfSides")) {
            e->op = Op::LinkSides;
            advance();
            require("(");
            e->left = expression();
            require(")");
            if (e->left->type != Type::Integer)
                fail(token, "llGetLinkNumberOfSides requires an integer");
            e->height = e->left->height + 1;
            if (e->height > 128)
                fail(token, "native LSL expression depth exceeded");
            return e;
        }
        if (is("llGetLinkSitFlags")) {
            e->op = Op::SitFlags;
            advance();
            require("(");
            e->left = expression();
            require(")");
            if (e->left->type != Type::Integer)
                fail(token, "llGetLinkSitFlags requires an integer link number");
            e->height = e->left->height + 1;
            if (e->height > 128)
                fail(token, "native LSL expression depth exceeded");
            return e;
        }
        if (is("llAvatarOnSitTarget") || is("llAvatarOnLinkSitTarget") ||
            is("osGetSitActiveRange") || is("osGetLinkSitActiveRange") ||
            is("osGetStandTarget") || is("osGetLinkStandTarget")) {
            const bool stand = is("osGetStandTarget") || is("osGetLinkStandTarget");
            const bool range = is("osGetSitActiveRange") || is("osGetLinkSitActiveRange");
            const bool linked = is("llAvatarOnLinkSitTarget") || is("osGetLinkSitActiveRange") ||
                                is("osGetLinkStandTarget");
            e->op = stand ? Op::StandTarget : range ? Op::SitActiveRange : Op::SitTargetAvatar;
            e->type = stand ? Type::Vector : range ? Type::Float : Type::Key;
            advance();
            require("(");
            if (linked) {
                e->left = expression();
                if (e->left->type != Type::Integer)
                    fail(token, "sit query requires an integer link number");
                e->height = e->left->height + 1;
                if (e->height > 128)
                    fail(token, "native LSL expression depth exceeded");
            }
            require(")");
            return e;
        }
        if (is("llGetPrimMediaParams") || is("llGetLinkMedia") || is("llSetPrimMediaParams") ||
            is("llSetLinkMedia") || is("llClearPrimMedia") || is("llClearLinkMedia")) {
            const bool linked = is("llGetLinkMedia") || is("llSetLinkMedia") || is("llClearLinkMedia");
            const bool clear = is("llClearPrimMedia") || is("llClearLinkMedia");
            const bool set = is("llSetPrimMediaParams") || is("llSetLinkMedia");
            e->op = clear ? Op::ClearPrimMedia : set ? Op::SetPrimMedia : Op::PrimMedia;
            e->type = clear || set ? Type::Integer : Type::List;
            advance();
            require("(");
            if (linked) {
                e->z = expression();
                require(",");
            }
            e->left = expression();
            if (!clear) {
                require(",");
                e->right = expression();
            }
            require(")");
            if ((e->z && e->z->type != Type::Integer) || e->left->type != Type::Integer ||
                (e->right && e->right->type != Type::List))
                fail(token, "Invalid prim media arguments");
            e->height = 1 + std::max({e->left->height, e->right ? e->right->height : 0, e->z ? e->z->height : 0});
            if (e->height > 128)
                fail(token, "native LSL expression depth exceeded");
            return e;
        }
        if (is("llGetLinkKey") || is("llGetLinkName")) {
            const bool name = is("llGetLinkName");
            e->op = name ? Op::LinkName : Op::LinkKey;
            e->type = name ? Type::String : Type::Key;
            advance();
            require("(");
            e->left = expression();
            require(")");
            if (e->left->type != Type::Integer)
                fail(token, name ? "llGetLinkName requires an integer" : "llGetLinkKey requires an integer");
            e->height = e->left->height + 1;
            if (e->height > 128)
                fail(token, "native LSL expression depth exceeded");
            return e;
        }
        if (is("llGetMass") || is("llGetMassMKS")) {
            e->op = is("llGetMassMKS") ? Op::ScriptMassMks : Op::ScriptMass;
            e->type = Type::Float;
            advance();
            require("(");
            require(")");
            return e;
        }
        if (is("llGetObjectMass")) {
            e->op = Op::ObjectMass;
            e->type = Type::Float;
            advance();
            require("(");
            e->left = expression();
            require(")");
            if (e->left->type != Type::Key && e->left->type != Type::String)
                fail(token, "llGetObjectMass requires a key");
            e->height = e->left->height + 1;
            if (e->height > 128)
                fail(token, "native LSL expression depth exceeded");
            return e;
        }
        if (is("llGetAttachedList") || is("llGetObjectPrimCount")) {
            const bool list = is("llGetAttachedList");
            e->op = list ? Op::AttachedList : Op::ObjectPrimCount;
            e->type = list ? Type::List : Type::Integer;
            advance();
            require("(");
            e->left = expression();
            require(")");
            if (e->left->type != Type::Key && e->left->type != Type::String)
                fail(token,
                     list ? "llGetAttachedList requires a key" : "llGetObjectPrimCount requires a key");
            e->height = e->left->height + 1;
            if (e->height > 128)
                fail(token, "native LSL expression depth exceeded");
            return e;
        }
        if (is("llGetAttached") || is("llGetLinkNumber") || is("llGetNumberOfPrims")) {
            e->op = is("llGetAttached")     ? Op::AttachmentPoint
                    : is("llGetLinkNumber") ? Op::LinkNumber
                                            : Op::PrimCount;
            advance();
            require("(");
            require(")");
            return e;
        }
        if (is("llDerezObject")) {
            e->op = Op::DerezObject;
            e->type = Type::Integer;
            advance();
            require("(");
            e->left = expression();
            require(",");
            e->right = expression();
            require(")");
            if ((e->left->type != Type::Key && e->left->type != Type::String) ||
                e->right->type != Type::Integer)
                fail(token, "Invalid object derez arguments");
            e->height = 1 + std::max(e->left->height, e->right->height);
            if (e->height > 128)
                fail(token, "native LSL expression depth exceeded");
            return e;
        }
        if (is("llGetRegionAgentCount") || is("llGetRegionFlags")) {
            e->op = is("llGetRegionFlags") ? Expression::Op::RegionFlags : Expression::Op::RegionAgentCount;
            advance();
            require("(");
            require(")");
            return e;
        }
        if (is("llGetSimStats")) {
            e->op = Op::SimulatorStats;
            e->type = Type::Float;
            advance();
            require("(");
            e->left = expression();
            require(")");
            if (e->left->type != Type::Integer)
                fail(token, "llGetSimStats requires an integer");
            e->height = e->left->height + 1;
            if (e->height > 128)
                fail(token, "native LSL expression depth exceeded");
            return e;
        }
        if (is("llGetRegionTimeDilation") || is("llGetRegionFPS")) {
            e->op = is("llGetRegionFPS") ? Op::RegionFPS : Op::RegionTimeDilation;
            e->type = Type::Float;
            advance();
            require("(");
            require(")");
            return e;
        }
        if (is("llGetLocalRot") || is("llGetRot") || is("llGetRootRotation")) {
            e->op = is("llGetLocalRot") ? Op::ObjectLocalRotation
                    : is("llGetRot")    ? Op::ObjectWorldRotation
                                        : Op::ObjectRootRotation;
            e->type = Type::Rotation;
            advance();
            require("(");
            require(")");
            return component_access(std::move(e));
        }
        if (is("llGetGeometricCenter") || is("llGetCenterOfMass") || is("llGetForce") || is("llGetTorque")) {
            if (is("llGetCenterOfMass"))
                e->op = Op::CenterOfMass;
            else if (is("llGetGeometricCenter"))
                e->op = Op::GeometricCenter;
            else
                e->op = is("llGetForce") ? Op::GetForce : Op::GetTorque;
            e->type = Type::Vector;
            advance();
            require("(");
            require(")");
            return component_access(std::move(e));
        }
        if (is("llGetStatus")) {
            e->op = Op::ObjectStatus;
            e->type = Type::Integer;
            advance();
            require("(");
            e->left = expression();
            require(")");
            if (e->left->type != Type::Integer)
                fail(token, "llGetStatus requires an integer");
            e->height = 1 + e->left->height;
            if (e->height > 128)
                fail(token, "native LSL expression depth exceeded");
            return e;
        }
        if (is("llSetRegionPos")) {
            e->op = Op::SetRegionPosition;
            e->type = Type::Integer;
            advance();
            require("(");
            e->left = expression();
            require(")");
            if (e->left->type != Type::Vector)
                fail(token, "Region position must be a vector");
            e->height = 1 + e->left->height;
            if (e->height > 128)
                fail(token, "native LSL expression depth exceeded");
            return e;
        }
        if (is("llGetMinScaleFactor") || is("llGetMaxScaleFactor") || is("llScaleByFactor")) {
            const bool resize = is("llScaleByFactor");
            e->op = resize ? Op::ScaleGroup : is("llGetMaxScaleFactor") ? Op::GroupMaxScaleFactor
                                                                        : Op::GroupMinScaleFactor;
            e->type = resize ? Type::Integer : Type::Float;
            advance();
            require("(");
            if (resize) {
                e->left = expression();
                if (e->left->type != Type::Integer && e->left->type != Type::Float)
                    fail(token, "llScaleByFactor requires a numeric factor");
            }
            require(")");
            return e;
        }
        if (is("llGetPos") || is("llGetRootPosition") || is("llGetLocalPos") || is("llGetScale") ||
            is("llGetRegionName") || is("llGetRegionCorner")) {
            const bool name = is("llGetRegionName");
            e->op = name                      ? Op::RegionName
                    : is("llGetRegionCorner") ? Op::RegionCorner
                    : is("llGetScale")        ? Op::ObjectScale
                    : is("llGetLocalPos")     ? Op::ObjectLocalPosition
                    : is("llGetRootPosition") ? Op::ObjectRootPosition
                                              : Op::ObjectPosition;
            e->type = name ? Type::String : Type::Vector;
            advance();
            require("(");
            require(")");
            return component_access(std::move(e));
        }
        if (is("llModPow")) {
            e->op = Op::ModPow;
            advance();
            require("(");
            e->left = expression();
            require(",");
            e->right = expression();
            require(",");
            e->z = expression();
            require(")");
            if (e->left->type != Type::Integer || e->right->type != Type::Integer ||
                e->z->type != Type::Integer)
                fail(token, "llModPow requires three integer arguments");
            e->height = 1 + std::max({e->left->height, e->right->height, e->z->height});
            if (e->height > 128)
                fail(token, "native LSL expression depth exceeded");
            return e;
        }
        static constexpr std::pair<std::string_view, Op> scalar_math[] = {
            {"llAbs", Op::Abs},    {"llFabs", Op::Fabs},   {"llSqrt", Op::Sqrt}, {"llPow", Op::Pow},
            {"llSin", Op::Sin},    {"llCos", Op::Cos},     {"llTan", Op::Tan},   {"llAsin", Op::Asin},
            {"llAcos", Op::Acos},  {"llAtan2", Op::Atan2}, {"llLog", Op::Log},   {"llLog10", Op::Log10},
            {"llFrand", Op::Frand}};
        for (const auto& [name, op] : scalar_math) {
            if (!is(name))
                continue;
            const bool integer = op == Op::Abs;
            const bool binary = op == Op::Pow || op == Op::Atan2;
            e->op = op;
            e->type = integer ? Type::Integer : Type::Float;
            advance();
            require("(");
            e->left = expression();
            if (binary) {
                require(",");
                e->right = expression();
            }
            require(")");
            if ((integer && e->left->type != Type::Integer) || !numeric(e->left->type) ||
                (e->right && !numeric(e->right->type)))
                fail(token, "invalid math argument type");
            e->height = 1 + std::max(e->left->height, e->right ? e->right->height : 0u);
            if (e->height > 128)
                fail(token, "native LSL expression depth exceeded");
            return e;
        }
        if (is("llFloor") || is("llCeil") || is("llRound")) {
            e->op = is("llFloor") ? Op::Floor : is("llCeil") ? Op::Ceil : Op::Round;
            advance();
            require("(");
            e->left = expression();
            require(")");
            if (!numeric(e->left->type))
                fail(token, "rounding requires a numeric argument");
            e->height = e->left->height + 1;
            if (e->height > 128)
                fail(token, "native LSL expression depth exceeded");
            return e;
        }
        if (is("llAxes2Rot")) {
            e->op = Op::AxesToRotation;
            e->type = Type::Rotation;
            advance();
            require("(");
            e->left = expression();
            require(",");
            e->right = expression();
            require(",");
            e->z = expression();
            require(")");
            if (e->left->type != Type::Vector || e->right->type != Type::Vector || e->z->type != Type::Vector)
                fail(token, "llAxes2Rot requires three vectors");
            e->height = 1 + std::max({e->left->height, e->right->height, e->z->height});
            if (e->height > 128)
                fail(token, "native LSL expression depth exceeded");
            return component_access(std::move(e));
        }
        if (is("llEuler2Rot") || is("llRot2Euler")) {
            const bool to_rotation = is("llEuler2Rot");
            e->op = to_rotation ? Op::EulerToRotation : Op::RotationToEuler;
            e->type = to_rotation ? Type::Rotation : Type::Vector;
            advance();
            require("(");
            e->left = expression();
            require(")");
            if (e->left->type != (to_rotation ? Type::Vector : Type::Rotation))
                fail(token,
                     to_rotation ? "llEuler2Rot requires a vector" : "llRot2Euler requires a rotation");
            e->height = e->left->height + 1;
            if (e->height > 128)
                fail(token, "native LSL expression depth exceeded");
            return component_access(std::move(e));
        }
        if (is("llRotBetween")) {
            e->op = Op::RotationBetweenVectors;
            e->type = Type::Rotation;
            advance();
            require("(");
            e->left = expression();
            require(",");
            e->right = expression();
            require(")");
            if (e->left->type != Type::Vector || e->right->type != Type::Vector)
                fail(token, "llRotBetween requires two vectors");
            e->height = 1 + std::max(e->left->height, e->right->height);
            if (e->height > 128)
                fail(token, "native LSL expression depth exceeded");
            return component_access(std::move(e));
        }
        if (is("llRot2Fwd") || is("llRot2Left") || is("llRot2Up")) {
            e->op = Op::RotationBasis;
            e->type = Type::Vector;
            e->slot = is("llRot2Fwd") ? 0 : is("llRot2Left") ? 1 : 2;
            advance();
            require("(");
            e->left = expression();
            require(")");
            if (e->left->type != Type::Rotation)
                fail(token, "rotation direction requires a rotation");
            e->height = e->left->height + 1;
            if (e->height > 128)
                fail(token, "native LSL expression depth exceeded");
            return component_access(std::move(e));
        }
        if (is("llAxisAngle2Rot") || is("llRot2Axis") || is("llRot2Angle") || is("llAngleBetween")) {
            const bool from_axis = is("llAxisAngle2Rot");
            const bool to_axis = is("llRot2Axis");
            const bool between = is("llAngleBetween");
            e->op = from_axis ? Op::AxisAngleToRotation
                    : to_axis ? Op::RotationToAxis
                    : between ? Op::RotationAngleBetween
                              : Op::RotationToAngle;
            e->type = from_axis ? Type::Rotation : to_axis ? Type::Vector : Type::Float;
            advance();
            require("(");
            e->left = expression();
            if (from_axis) {
                require(",");
                e->right = expression();
                if (e->left->type != Type::Vector || !numeric(e->right->type))
                    fail(token, "llAxisAngle2Rot requires a vector and numeric angle");
            } else {
                if (between) {
                    require(",");
                    e->right = expression();
                }
                if (e->left->type != Type::Rotation || (between && e->right->type != Type::Rotation))
                    fail(token, "rotation query requires rotation arguments");
            }
            require(")");
            e->height = 1 + std::max(e->left->height, e->right ? e->right->height : 0u);
            if (e->height > 128)
                fail(token, "native LSL expression depth exceeded");
            return component_access(std::move(e));
        }
        if (is("llLinear2sRGB") || is("llsRGB2Linear")) {
            e->op = Op::ColorConversion;
            e->type = Type::Vector;
            e->slot = is("llsRGB2Linear");
            advance();
            require("(");
            e->left = expression();
            require(")");
            if (e->left->type != Type::Vector)
                fail(token, "color conversion requires a vector");
            e->height = e->left->height + 1;
            if (e->height > 128)
                fail(token, "native LSL expression depth exceeded");
            return component_access(std::move(e));
        }
        if (is("llVecMag") || is("llVecNorm") || is("llVecDist")) {
            const bool distance = is("llVecDist");
            const bool normalize = is("llVecNorm");
            e->op = distance ? Op::VectorDistance : normalize ? Op::VectorNormalize : Op::VectorMagnitude;
            e->type = normalize ? Type::Vector : Type::Float;
            advance();
            require("(");
            e->left = expression();
            if (distance) {
                require(",");
                e->right = expression();
            }
            require(")");
            if (e->left->type != Type::Vector || (e->right && e->right->type != Type::Vector))
                fail(token, "vector query requires vector arguments");
            e->height = 1 + std::max(e->left->height, e->right ? e->right->height : 0u);
            if (e->height > 128)
                fail(token, "native LSL expression depth exceeded");
            return component_access(std::move(e));
        }
        if (is("llGetTime") || is("llGetAndResetTime")) {
            e->op = is("llGetTime") ? Expression::Op::Elapsed : Expression::Op::ElapsedReset;
            e->type = Type::Float;
            advance();
            require("(");
            require(")");
            return e;
        }
        if (is("llGetGMTclock") || is("llGetWallclock") || is("llGetTimeOfDay")) {
            e->op = is("llGetGMTclock")    ? Expression::Op::GmtClock
                    : is("llGetWallclock") ? Expression::Op::Wallclock
                                           : Expression::Op::TimeOfDay;
            e->type = Type::Float;
            advance();
            require("(");
            require(")");
            return e;
        }
        if (is("llGetDate") || is("llGetTimestamp")) {
            e->op = is("llGetDate") ? Expression::Op::Date : Expression::Op::Timestamp;
            e->type = Type::String;
            advance();
            require("(");
            require(")");
            return e;
        }
        if (is("llGetUnixTime")) {
            advance();
            require("(");
            require(")");
            e->op = Expression::Op::UnixTime;
            return e;
        }
        if (is("llTarget") || is("llTargetRemove") || is("llRotTarget") || is("llRotTargetRemove")) {
            const auto command = token;
            const bool registering = is("llTarget") || is("llRotTarget");
            const bool rotation = is("llRotTarget") || is("llRotTargetRemove");
            e->slot = rotation;
            e->op = registering ? Op::RegisterTarget : Op::RemoveTarget;
            e->type = Type::Integer;
            advance();
            require("(");
            e->left = expression();
            if (registering) {
                require(",");
                e->right = expression();
                if (e->left->type != (rotation ? Type::Rotation : Type::Vector) || !numeric(e->right->type))
                    fail(command, "target registration requires a matching target type and numeric tolerance");
                e->height = std::max(e->left->height, e->right->height) + 1;
            } else {
                if (e->left->type != Type::Integer)
                    fail(command, "target removal requires an integer handle");
                e->height = e->left->height + 1;
            }
            require(")");
            if (e->height > 128)
                fail(command, "native LSL expression depth exceeded");
            return e;
        }
        if (is("llModifyLand")) {
            advance();
            require("(");
            e->left = expression();
            require(",");
            e->right = expression();
            require(")");
            if (e->left->type != Type::Integer || e->right->type != Type::Integer)
                fail(token, "llModifyLand requires an integer action and brush");
            e->height = std::max(e->left->height, e->right->height) + 1;
            if (e->height > 128)
                fail(token, "native LSL expression depth exceeded");
            e->op = Op::ModifyLand;
            e->type = Type::Integer;
            return e;
        }
        if (is("llEdgeOfWorld")) {
            advance();
            require("(");
            e->left = expression();
            require(",");
            e->right = expression();
            require(")");
            if (e->left->type != Type::Vector || e->right->type != Type::Vector)
                fail(token, "llEdgeOfWorld requires two vectors");
            e->op = Op::EdgeOfWorld;
            e->type = Type::Integer;
            return e;
        }
        if (is("llWater") || is("llGround") || is("llCloud") || is("llWind") ||
            is("llGroundNormal") || is("llGroundSlope") || is("llGroundContour")) {
            e->type = Type::Float;
            if (is("llGround"))
                e->op = Op::Ground;
            else if (is("llCloud"))
                e->op = Op::Cloud;
            else if (is("llWater"))
                e->op = Op::Water;
            else {
                e->type = Type::Vector;
                if (is("llWind"))
                    e->op = Op::Wind;
                else if (is("llGroundNormal"))
                    e->op = Op::GroundNormal;
                else if (is("llGroundSlope"))
                    e->op = Op::GroundSlope;
                else
                    e->op = Op::GroundContour;
            }
            advance();
            require("(");
            e->left = expression();
            if (e->left->type != Type::Vector)
                fail(token, "terrain query requires a vector");
            e->height = e->left->height + 1;
            if (e->height > 128)
                fail(token, "native LSL expression depth exceeded");
            require(")");
            return e;
        }
        if (is("JSON_APPEND")) {
            e->type = Type::String;
            e->value = std::string("-1");
            advance();
            return e;
        }
        constexpr std::string_view json_constants[] = {"JSON_INVALID", "JSON_OBJECT", "JSON_ARRAY",
                                                       "JSON_NUMBER",  "JSON_STRING", "JSON_NULL",
                                                       "JSON_TRUE",    "JSON_FALSE",  "JSON_DELETE"};
        for (std::size_t i = 0; i < std::size(json_constants); ++i) {
            if (token.text != json_constants[i] || token.kind != Kind::Name)
                continue;
            e->type = Type::String;
            e->value = json_marker(static_cast<char16_t>(0xFDD0 + i));
            advance();
            return e;
        }
        if (is("EOF") || is("NAK")) {
            e->type = Type::String;
            e->value = std::string(is("EOF") ? "\n\n\n" : "\n\x15\n");
            advance();
            return e;
        }
        if (is("NULL_KEY")) {
            e->type = Type::Key;
            e->value = framework::Uuid{}.str();
            advance();
            return e;
        }
        static constexpr std::pair<std::string_view, float> math_constants[] = {
            {"PI", 3.14159274f},        {"TWO_PI", 6.28318548f},
            {"PI_BY_TWO", 1.57079637f}, {"DEG_TO_RAD", 0.01745329238f},
            {"RAD_TO_DEG", 57.29578f},  {"SQRT2", 1.414213538f}};
        for (const auto& [name, value] : math_constants) {
            if (!is(name))
                continue;
            e->type = Type::Float;
            e->value = double(value);
            advance();
            return e;
        }
        for (std::size_t rule = 0; rule < std::size(particle_rules); ++rule) {
            const auto name = particle_rules[rule].name;
            if (name.empty() || !is(name))
                continue;
            e->type = Type::Integer;
            e->value = static_cast<std::int32_t>(rule);
            advance();
            return e;
        }
        static constexpr std::pair<std::string_view, std::int32_t> integer_constants[] = {
            {"CONTENT_TYPE_TEXT", 0}, {"CONTENT_TYPE_HTML", 1}, {"CONTENT_TYPE_XML", 2},
            {"CONTENT_TYPE_XHTML", 3}, {"CONTENT_TYPE_ATOM", 4}, {"CONTENT_TYPE_JSON", 5},
            {"CONTENT_TYPE_LLSD", 6}, {"CONTENT_TYPE_FORM", 7}, {"CONTENT_TYPE_RSS", 8},
            {"FALSE", 0},
            {"TRUE", 1},
            {"DEBUG_CHANNEL", std::numeric_limits<std::int32_t>::max()},
            {"PUBLIC_CHANNEL", 0},
            {"LINK_SET", -1},
            {"LINK_ALL_OTHERS", -2},
            {"SOUND_PLAY", 0},
            {"SOUND_LOOP", 1},
            {"SOUND_TRIGGER", 2},
            {"SOUND_SYNC", 4},
            {"LINK_ALL_CHILDREN", -3},
            {"LINK_THIS", -4},
            {"TARGETED_EMAIL_ROOT_CREATOR", 1},
            {"TARGETED_EMAIL_OBJECT_OWNER", 2},
            {"ALL_SIDES", -1},
            {"ANIM_ON", 1}, {"LOOP", 2}, {"REVERSE", 4}, {"PING_PONG", 8},
            {"PSYS_PART_INTERP_COLOR_MASK", 1}, {"PSYS_PART_INTERP_SCALE_MASK", 2},
            {"PSYS_PART_BOUNCE_MASK", 4}, {"PSYS_PART_WIND_MASK", 8}, {"PSYS_PART_FOLLOW_SRC_MASK", 16},
            {"PSYS_PART_FOLLOW_VELOCITY_MASK", 32}, {"PSYS_PART_TARGET_POS_MASK", 64},
            {"PSYS_PART_TARGET_LINEAR_MASK", 128}, {"PSYS_PART_EMISSIVE_MASK", 256}, {"PSYS_PART_RIBBON_MASK", 1024},
            {"PSYS_SRC_PATTERN_DROP", 1}, {"PSYS_SRC_PATTERN_EXPLODE", 2}, {"PSYS_SRC_PATTERN_ANGLE", 4},
            {"PSYS_SRC_PATTERN_ANGLE_CONE", 8}, {"PSYS_SRC_PATTERN_ANGLE_CONE_EMPTY", 16},
            {"PSYS_PART_BF_ONE", 0}, {"PSYS_PART_BF_ZERO", 1}, {"PSYS_PART_BF_DEST_COLOR", 2},
            {"PSYS_PART_BF_SOURCE_COLOR", 3}, {"PSYS_PART_BF_ONE_MINUS_DEST_COLOR", 4},
            {"PSYS_PART_BF_ONE_MINUS_SOURCE_COLOR", 5}, {"PSYS_PART_BF_SOURCE_ALPHA", 7},
            {"PSYS_PART_BF_ONE_MINUS_SOURCE_ALPHA", 9},
            {"SMOOTH", 16}, {"ROTATE", 32}, {"SCALE", 64},
            {"BEACON_MAP", 1}, {"BEACON_SHOW_MAP", 1}, {"BEACON_FOCUS_MAP", 2},
            {"DENSITY", 1}, {"FRICTION", 2}, {"RESTITUTION", 4}, {"GRAVITY_MULTIPLIER", 8},
            {"SIT_FLAG_SIT_TARGET", 0x01},
            {"SIT_FLAG_ALLOW_UNSIT", 0x02},
            {"SIT_FLAG_SCRIPTED_ONLY", 0x04},
            {"SIT_FLAG_NO_COLLIDE", 0x10},
            {"SIT_FLAG_NO_DAMAGE", 0x20},
            {"SIT_FLAG_OPENSIMFORCED", 0x32},
            {"LINK_ROOT", 1},
            {"PAY_HIDE", -1},
            {"PAY_DEFAULT", -2},
            {"PERMISSION_DEBIT", 0x02},
            {"PERMISSION_TAKE_CONTROLS", 0x04},
            {"CONTROL_FWD", 1},
            {"CONTROL_BACK", 2},
            {"CONTROL_LEFT", 4},
            {"CONTROL_RIGHT", 8},
            {"CONTROL_UP", 16},
            {"CONTROL_DOWN", 32},
            {"CONTROL_ROT_LEFT", 256},
            {"CONTROL_ROT_RIGHT", 512},
            {"CONTROL_LBUTTON", 0x10000000},
            {"CONTROL_ML_LBUTTON", 0x40000000},
            {"PERMISSION_REMAP_CONTROLS", 0x08},
            {"PERMISSION_TRIGGER_ANIMATION", 0x010},
            {"AGENT", 1},
            {"ACTIVE", 2},
            {"PASSIVE", 4},
            {"OS_LISTEN_REGEX_NAME", listen_regex_name},
            {"OS_LISTEN_REGEX_MESSAGE", listen_regex_message},
            {"OS_LTPAG_NONE", 0x0},
            {"OS_LTPAG_USEVEL", 0x1},
            {"OS_LTPAG_USELOOKAT", 0x2},
            {"OS_LTPAG_ALGNLV", 0x4},
            {"OS_LTPAG_FORCEFLY", 0x8},
            {"OS_LTPAG_FORCENOFLY", 0x16},
            {"SCRIPTED", 8},
            {"AGENT_BY_USERNAME", 0x10},
            {"NPC", 0x20},
            {"PERMISSION_ATTACH", 0x20},
            {"ATTACH_CHEST", 1},
            {"ATTACH_HEAD", 2},
            {"ATTACH_LSHOULDER", 3},
            {"ATTACH_RSHOULDER", 4},
            {"ATTACH_LHAND", 5},
            {"ATTACH_RHAND", 6},
            {"ATTACH_LFOOT", 7},
            {"ATTACH_RFOOT", 8},
            {"ATTACH_BACK", 9},
            {"ATTACH_PELVIS", 10},
            {"ATTACH_MOUTH", 11},
            {"ATTACH_CHIN", 12},
            {"ATTACH_LEAR", 13},
            {"ATTACH_REAR", 14},
            {"ATTACH_LEYE", 15},
            {"ATTACH_REYE", 16},
            {"ATTACH_NOSE", 17},
            {"ATTACH_RUARM", 18},
            {"ATTACH_RLARM", 19},
            {"ATTACH_LUARM", 20},
            {"ATTACH_LLARM", 21},
            {"ATTACH_RHIP", 22},
            {"ATTACH_RULEG", 23},
            {"ATTACH_RLLEG", 24},
            {"ATTACH_LHIP", 25},
            {"ATTACH_LULEG", 26},
            {"ATTACH_LLLEG", 27},
            {"ATTACH_BELLY", 28},
            {"ATTACH_RPEC", 29},
            {"ATTACH_LPEC", 30},
            {"ATTACH_LEFT_PEC", 29},
            {"ATTACH_RIGHT_PEC", 30},
            {"ATTACH_HUD_CENTER_2", 31},
            {"ATTACH_HUD_TOP_RIGHT", 32},
            {"ATTACH_HUD_TOP_CENTER", 33},
            {"ATTACH_HUD_TOP_LEFT", 34},
            {"ATTACH_HUD_CENTER_1", 35},
            {"ATTACH_HUD_BOTTOM_LEFT", 36},
            {"ATTACH_HUD_BOTTOM", 37},
            {"ATTACH_HUD_BOTTOM_RIGHT", 38},
            {"ATTACH_NECK", 39},
            {"ATTACH_AVATAR_CENTER", 40},
            {"ATTACH_LHAND_RING1", 41},
            {"ATTACH_RHAND_RING1", 42},
            {"ATTACH_TAIL_BASE", 43},
            {"ATTACH_TAIL_TIP", 44},
            {"ATTACH_LWING", 45},
            {"ATTACH_RWING", 46},
            {"ATTACH_FACE_JAW", 47},
            {"ATTACH_FACE_LEAR", 48},
            {"ATTACH_FACE_REAR", 49},
            {"ATTACH_FACE_LEYE", 50},
            {"ATTACH_FACE_REYE", 51},
            {"ATTACH_FACE_TONGUE", 52},
            {"ATTACH_GROIN", 53},
            {"ATTACH_HIND_LFOOT", 54},
            {"ATTACH_HIND_RFOOT", 55},
            {"PERMISSION_RELEASE_OWNERSHIP", 0x40},
            {"PERMISSION_CHANGE_LINKS", 0x80},
            {"PERMISSION_CHANGE_JOINTS", 0x100},
            {"PERMISSION_CHANGE_PERMISSIONS", 0x200},
            {"PERMISSION_TRACK_CAMERA", 0x400},
            {"PERMISSION_CONTROL_CAMERA", 0x800},
            {"PERMISSION_TELEPORT", 0x1000},
            {"PERMISSION_SILENT_ESTATE_MANAGEMENT", 0x4000},
            {"PERMISSION_OVERRIDE_ANIMATIONS", 0x8000},
            {"PERMISSION_RETURN_OBJECTS", 0x10000},
            {"LINKSETDATA_RESET", 0},
            {"LINKSETDATA_UPDATE", 1},
            {"LINKSETDATA_DELETE", 2},
            {"LINKSETDATA_MULTIDELETE", 3},
            {"LINKSETDATA_OK", 0},
            {"LINKSETDATA_EMEMORY", 1},
            {"LINKSETDATA_ENOKEY", 2},
            {"LINKSETDATA_EPROTECTED", 3},
            {"LINKSETDATA_NOTFOUND", 4},
            {"LINKSETDATA_NOUPDATE", 5},
            {"TYPE_INVALID", 0},
            {"TYPE_INTEGER", 1},
            {"TYPE_FLOAT", 2},
            {"TYPE_STRING", 3},
            {"PARCEL_DETAILS_PRIM_CAPACITY", 7},
            {"PARCEL_MEDIA_COMMAND_STOP", 0},
            {"PARCEL_MEDIA_COMMAND_PAUSE", 1},
            {"PARCEL_MEDIA_COMMAND_PLAY", 2},
            {"PARCEL_MEDIA_COMMAND_LOOP", 3},
            {"PARCEL_MEDIA_COMMAND_TEXTURE", 4},
            {"PARCEL_MEDIA_COMMAND_URL", 5},
            {"PARCEL_MEDIA_COMMAND_TIME", 6},
            {"PARCEL_MEDIA_COMMAND_AGENT", 7},
            {"PARCEL_MEDIA_COMMAND_UNLOAD", 8},
            {"PARCEL_MEDIA_COMMAND_AUTO_ALIGN", 9},
            {"PARCEL_MEDIA_COMMAND_TYPE", 10},
            {"PARCEL_MEDIA_COMMAND_SIZE", 11},
            {"PARCEL_MEDIA_COMMAND_DESC", 12},
            {"PARCEL_DETAILS_PRIM_USED", 8},
            {"PARCEL_DETAILS_SCRIPT_DANGER", 13},
            {"PARCEL_DETAILS_GEOMETRICCENTER", 66},
            {"PARCEL_DETAILS_SEE_AVATARS", 6},
            {"PARCEL_DETAILS_DWELL", 64},
            {"PARCEL_DETAILS_GETCLAIMDATE", 65},
            {"PARCEL_DETAILS_LANDING_POINT", 9},
            {"PARCEL_DETAILS_LANDING_LOOKAT", 10},
            {"PARCEL_DETAILS_TP_ROUTING", 11},
            {"PARCEL_DETAILS_NAME", 0},
            {"PARCEL_DETAILS_DESC", 1},
            {"PARCEL_DETAILS_GROUP", 3},
            {"PARCEL_DETAILS_OWNER", 2},
            {"PARCEL_DETAILS_AREA", 4},
            {"PARCEL_DETAILS_ID", 5},
            {"PARCEL_DETAILS_FLAGS", 12},
            {"REGION_FLAG_ALLOW_DAMAGE", 0x1},
            {"REGION_FLAG_FIXED_SUN", 0x10},
            {"REGION_FLAG_BLOCK_TERRAFORM", 0x40},
            {"REGION_FLAG_SANDBOX", 0x100},
            {"REGION_FLAG_DISABLE_COLLISIONS", 0x1000},
            {"REGION_FLAG_DISABLE_PHYSICS", 0x4000},
            {"REGION_FLAG_BLOCK_FLY", 0x80000},
            {"REGION_FLAG_ALLOW_DIRECT_TELEPORT", 0x100000},
            {"REGION_FLAG_RESTRICT_PUSHOBJECT", 0x400000},
            {"PARCEL_FLAG_ALLOW_FLY", 0x1},
            {"PARCEL_FLAG_ALLOW_SCRIPTS", 0x2},
            {"PARCEL_FLAG_ALLOW_LANDMARK", 0x8},
            {"PARCEL_FLAG_ALLOW_TERRAFORM", 0x10},
            {"PARCEL_FLAG_ALLOW_DAMAGE", 0x20},
            {"PARCEL_FLAG_ALLOW_CREATE_OBJECTS", 0x40},
            {"PARCEL_FLAG_USE_ACCESS_GROUP", 0x100},
            {"PARCEL_FLAG_USE_ACCESS_LIST", 0x200},
            {"PARCEL_FLAG_USE_BAN_LIST", 0x400},
            {"LAND_LEVEL", 0},
            {"HTTP_METHOD", 0},
            {"HTTP_MIMETYPE", 1},
            {"HTTP_BODY_MAXLENGTH", 2},
            {"HTTP_VERIFY_CERT", 3},
            {"HTTP_VERBOSE_THROTTLE", 4},
            {"HTTP_CUSTOM_HEADER", 5},
            {"HTTP_PRAGMA_NO_CACHE", 6},
            {"REMOTE_DATA_CHANNEL", 1},
            {"REMOTE_DATA_REQUEST", 2},
            {"REMOTE_DATA_REPLY", 3},
            {"ESTATE_ACCESS_ALLOWED_AGENT_ADD", 0},
            {"ESTATE_ACCESS_ALLOWED_AGENT_REMOVE", 1},
            {"ESTATE_ACCESS_ALLOWED_GROUP_ADD", 2},
            {"ESTATE_ACCESS_ALLOWED_GROUP_REMOVE", 3},
            {"ESTATE_ACCESS_BANNED_AGENT_ADD", 4},
            {"ESTATE_ACCESS_BANNED_AGENT_REMOVE", 5},
            {"LAND_RAISE", 1},
            {"LAND_LOWER", 2},
            {"LAND_SMOOTH", 3},
            {"LAND_NOISE", 4},
            {"LAND_REVERT", 5},
            {"LAND_SMALL_BRUSH", 1},
            {"LAND_MEDIUM_BRUSH", 2},
            {"LAND_LARGE_BRUSH", 3},
            {"PARCEL_FLAG_USE_LAND_PASS_LIST", 0x800},
            {"PARCEL_FLAG_LOCAL_SOUND_ONLY", 0x8000},
            {"PARCEL_FLAG_RESTRICT_PUSHOBJECT", 0x200000},
            {"PARCEL_FLAG_ALLOW_GROUP_SCRIPTS", 0x2000000},
            {"PARCEL_FLAG_ALLOW_CREATE_GROUP_OBJECTS", 0x4000000},
            {"PARCEL_FLAG_ALLOW_ALL_OBJECT_ENTRY", 0x8000000},
            {"PARCEL_FLAG_ALLOW_GROUP_OBJECT_ENTRY", 0x10000000},
            {"AGENT_LIST_PARCEL", 1},
            {"AGENT_LIST_PARCEL_OWNER", 2},
            {"AGENT_LIST_REGION", 4},
            {"AGENT_FLYING", 0x1}, {"AGENT_ATTACHMENTS", 0x2}, {"AGENT_SCRIPTED", 0x4},
            {"STATUS_PHYSICS", object_status::physics}, {"STATUS_PHANTOM", object_status::phantom},
            {"STATUS_ROTATE_X", object_status::rotate_x}, {"STATUS_ROTATE_Y", object_status::rotate_y},
            {"STATUS_ROTATE_Z", object_status::rotate_z},
            {"STATUS_SANDBOX", object_status::sandbox},
            {"STATUS_DIE_AT_EDGE", object_status::die_at_edge},
            {"STATUS_RETURN_AT_EDGE", object_status::return_at_edge},
            {"STATUS_CAST_SHADOWS", object_status::cast_shadows},
            {"STATUS_BLOCK_GRAB", object_status::block_grab},
            {"STATUS_BLOCK_GRAB_OBJECT", object_status::block_grab_object},
            {"AGENT_MOUSELOOK", 0x8}, {"AGENT_SITTING", 0x10}, {"AGENT_ON_OBJECT", 0x20},
            {"AGENT_AWAY", 0x40}, {"AGENT_WALKING", 0x80}, {"AGENT_IN_AIR", 0x100},
            {"AGENT_TYPING", 0x200}, {"AGENT_CROUCHING", 0x400}, {"AGENT_BUSY", 0x800},
            {"AGENT_ALWAYS_RUN", 0x1000}, {"AGENT_AUTOPILOT", 0x2000}, {"AGENT_MALE", 0x40000000},
            {"DATA_ONLINE", 1},
            {"DATA_NAME", 2},
            {"DATA_BORN", 3},
            {"DATA_RATING", 4},
            {"DATA_SIM_POS", 5},
            {"DATA_SIM_STATUS", 6},
            {"DATA_SIM_RATING", 7},
            {"DATA_PAYINFO", 8},
            {"DATA_SIM_RELEASE", 128},
            {"PARCEL_COUNT_TOTAL", 0},
            {"PARCEL_COUNT_OWNER", 1},
            {"PARCEL_COUNT_GROUP", 2},
            {"PARCEL_COUNT_OTHER", 3},
            {"PARCEL_COUNT_SELECTED", 4},
            {"PARCEL_COUNT_TEMP", 5},
            {"SIM_STAT_PCT_CHARS_STEPPED", 0},
            {"SIM_STAT_PHYSICS_FPS", 1},
            {"SIM_STAT_AGENT_UPDATES", 2},
            {"SIM_STAT_FRAME_MS", 3},
            {"SIM_STAT_NET_MS", 4},
            {"SIM_STAT_OTHER_MS", 5},
            {"SIM_STAT_PHYSICS_MS", 6},
            {"SIM_STAT_AGENT_MS", 7},
            {"SIM_STAT_IMAGE_MS", 8},
            {"SIM_STAT_SCRIPT_MS", 9},
            {"SIM_STAT_AGENT_COUNT", 10},
            {"SIM_STAT_CHILD_AGENT_COUNT", 11},
            {"SIM_STAT_ACTIVE_SCRIPT_COUNT", 12},
            {"SIM_STAT_PACKETS_IN", 13},
            {"SIM_STAT_PACKETS_OUT", 14},
            {"SIM_STAT_ASSET_DOWNLOADS", 15},
            {"SIM_STAT_ASSET_UPLOADS", 16},
            {"SIM_STAT_UNACKED_BYTES", 17},
            {"SIM_STAT_PHYSICS_STEP_MS", 18},
            {"SIM_STAT_PHYSICS_SHAPE_MS", 19},
            {"SIM_STAT_PHYSICS_OTHER_MS", 20},
            {"SIM_STAT_SCRIPT_EPS", 21},
            {"SIM_STAT_SPARE_MS", 22},
            {"SIM_STAT_SLEEP_MS", 23},
            {"SIM_STAT_IO_PUMP_MS", 24},
            {"SIM_STAT_SCRIPT_RUN_PCT", 25},
            {"SIM_STAT_AI_MS", 26},
            {"AGENT_LIST_EXCLUDENPC", 0x4000000},
            {"LIST_STAT_RANGE", 0},
            {"LIST_STAT_MIN", 1},
            {"LIST_STAT_MAX", 2},
            {"LIST_STAT_MEAN", 3},
            {"LIST_STAT_MEDIAN", 4},
            {"LIST_STAT_STD_DEV", 5},
            {"LIST_STAT_SUM", 6},
            {"LIST_STAT_SUM_SQUARES", 7},
            {"LIST_STAT_NUM_COUNT", 8},
            {"LIST_STAT_GEOMETRIC_MEAN", 9},
            {"LIST_STAT_HARMONIC_MEAN", 100},
            {"TYPE_KEY", 4},
            {"TYPE_VECTOR", 5},
            {"TYPE_ROTATION", 6},
            {"STRING_TRIM_HEAD", 1},
            {"STRING_TRIM_TAIL", 2},
            {"STRING_TRIM", 3},
            {"CLICK_ACTION_NONE", 0},
            {"CLICK_ACTION_TOUCH", 0},
            {"CLICK_ACTION_SIT", 1},
            {"CLICK_ACTION_BUY", 2},
            {"CLICK_ACTION_PAY", 3},
            {"CLICK_ACTION_OPEN", 4},
            {"CLICK_ACTION_PLAY", 5},
            {"CLICK_ACTION_OPEN_MEDIA", 6},
            {"CLICK_ACTION_ZOOM", 7},
            {"CLICK_ACTION_DISABLED", 8},
            {"CLICK_ACTION_IGNORE", 9},
            {"MASK_BASE", 0},
            {"MASK_OWNER", 1},
            {"MASK_GROUP", 2},
            {"MASK_EVERYONE", 3},
            {"MASK_NEXT", 4},
            {"PERM_TRANSFER", 0x2000},
            {"PERM_MODIFY", 0x4000},
            {"PERM_COPY", 0x8000},
            {"PERM_MOVE", 0x80000},
            {"PERM_ALL", 0x7fffffff},
            {"INVENTORY_ALL", -1},
            {"INVENTORY_NONE", -1},
            {"INVENTORY_TEXTURE", 0},
            {"INVENTORY_SOUND", 1},
            {"INVENTORY_LANDMARK", 3},
            {"INVENTORY_CLOTHING", 5},
            {"INVENTORY_OBJECT", 6},
            {"INVENTORY_NOTECARD", 7},
            {"INVENTORY_SCRIPT", 10},
            {"INVENTORY_BODYPART", 13},
            {"INVENTORY_ANIMATION", 20},
            {"INVENTORY_GESTURE", 21},
            {"INVENTORY_SETTING", 56},
            {"INVENTORY_MATERIAL", 57},
            {"CHANGED_INVENTORY", 1},
            {"CHANGED_COLOR", 2},
            {"CHANGED_SHAPE", 4},
            {"CHANGED_SCALE", 8},
            {"CHANGED_TEXTURE", 16},
            {"CHANGED_LINK", 32},
            {"CHANGED_ALLOWED_DROP", 64},
            {"CHANGED_OWNER", 128},
            {"CHANGED_REGION", 256},
            {"CHANGED_TELEPORT", 512},
            {"CHANGED_REGION_RESTART", 1024},
            {"CHANGED_REGION_START", 1024},
            {"CHANGED_MEDIA", 2048},
            {"PRIM_MEDIA_ALT_IMAGE_ENABLE", 0},
            {"PRIM_MEDIA_CONTROLS", 1},
            {"PRIM_MEDIA_CURRENT_URL", 2},
            {"PRIM_MEDIA_HOME_URL", 3},
            {"PRIM_MEDIA_AUTO_LOOP", 4},
            {"PRIM_MEDIA_AUTO_PLAY", 5},
            {"PRIM_MEDIA_AUTO_SCALE", 6},
            {"PRIM_MEDIA_AUTO_ZOOM", 7},
            {"PRIM_MEDIA_FIRST_CLICK_INTERACT", 8},
            {"PRIM_MEDIA_WIDTH_PIXELS", 9},
            {"PRIM_MEDIA_HEIGHT_PIXELS", 10},
            {"PRIM_MEDIA_WHITELIST_ENABLE", 11},
            {"PRIM_MEDIA_WHITELIST", 12},
            {"PRIM_MEDIA_PERMS_INTERACT", 13},
            {"PRIM_MEDIA_PERMS_CONTROL", 14},
            {"PRIM_MEDIA_CONTROLS_STANDARD", 0},
            {"PRIM_MEDIA_CONTROLS_MINI", 1},
            {"PRIM_MEDIA_PERM_NONE", 0},
            {"PRIM_MEDIA_PERM_OWNER", 1},
            {"PRIM_MEDIA_PERM_GROUP", 2},
            {"PRIM_MEDIA_PERM_ANYONE", 4},
            {"LSL_STATUS_OK", 0},
            {"LSL_STATUS_MALFORMED_PARAMS", 1000},
            {"LSL_STATUS_TYPE_MISMATCH", 1001},
            {"LSL_STATUS_BOUNDS_ERROR", 1002},
            {"LSL_STATUS_NOT_FOUND", 1003},
            {"LSL_STATUS_NOT_SUPPORTED", 1004},
            {"LSL_STATUS_INTERNAL_ERROR", 1999},
            {"LSL_STATUS_WHITELIST_FAILED", 2001},
            {"CHANGED_RENDER_MATERIAL", 4096},
            {"CHANGED_ANIMATION", 16384},
            {"CHANGED_POSITION", 32768}};
        for (const auto& [name, value] : integer_constants) {
            if (!is(name))
                continue;
            e->value = value;
            advance();
            return e;
        }
        if (token.kind == Kind::Name) {
            if (const auto variable = local(token.text)) {
                e->op = Expression::Op::Local;
                e->slot = variable->slot;
                e->global = variable->global;
                e->type = variable->type;
                advance();
                auto value = component_access(std::move(e));
                if (update_operator()) {
                    const int delta = is("++") ? 1 : -1;
                    advance();
                    return update(std::move(value), delta, true);
                }
                return value;
            }
        }
        fail(token, "unsupported expression or undefined name");
    }
    struct BinaryOperator {
        std::string_view token;
        int precedence;
        Expression::Op op;
        bool compound = false;
    };
    static const BinaryOperator* binary(const Token& token, bool end_vector) {
        using Op = Expression::Op;
        static constexpr BinaryOperator operators[] = {{"=", 0, Op::Assign},
                                                       {"+=", 0, Op::Add, true},
                                                       {"-=", 0, Op::Subtract, true},
                                                       {"*=", 0, Op::Multiply, true},
                                                       {"/=", 0, Op::Divide, true},
                                                       {"%=", 0, Op::Remainder, true},
                                                       {"&=", 0, Op::BitAnd, true},
                                                       {"|=", 0, Op::BitOr, true},
                                                       {"^=", 0, Op::BitXor, true},
                                                       {"<<=", 0, Op::ShiftLeft, true},
                                                       {">>=", 0, Op::ShiftRight, true},
                                                       {"|||", 1, Op::ShortOr},
                                                       {"&&&", 2, Op::ShortAnd},
                                                       {"&&", 3, Op::And},
                                                       {"||", 3, Op::Or},
                                                       {"|", 4, Op::BitOr},
                                                       {"^", 5, Op::BitXor},
                                                       {"&", 6, Op::BitAnd},
                                                       {"==", 7, Op::Equal},
                                                       {"!=", 7, Op::NotEqual},
                                                       {"<", 8, Op::Less},
                                                       {"<=", 8, Op::LessEqual},
                                                       {">", 8, Op::Greater},
                                                       {">=", 8, Op::GreaterEqual},
                                                       {"<<", 9, Op::ShiftLeft},
                                                       {">>", 9, Op::ShiftRight},
                                                       {"+", 10, Op::Add},
                                                       {"-", 10, Op::Subtract},
                                                       {"*", 11, Op::Multiply},
                                                       {"/", 11, Op::Divide},
                                                       {"%", 11, Op::Remainder}};
        if (token.kind != Kind::Symbol || (end_vector && token.text == ">"))
            return nullptr;
        for (const auto& op : operators)
            if (op.token == token.text)
                return &op;
        return nullptr;
    }
    std::unique_ptr<Expression> expression(int minimum = 0, bool end_vector = false) {
        Depth guard(*this);
        using Op = Expression::Op;
        auto left = unary();
        while (const auto* binary_op = binary(token, end_vector)) {
            const auto level = binary_op->precedence;
            if (level < minimum)
                break;
            const auto at = token;
            advance();
            const auto op = binary_op->op;
            const bool assign = level == 0;
            auto right = expression(level + !assign, end_vector);
            const bool list_operand = left->type == Type::List || right->type == Type::List;
            const bool list_add = list_operand && op == Op::Add;
            const bool list_compare = left->type == Type::List && right->type == Type::List &&
                                      (op == Op::Equal || op == Op::NotEqual);
            if (list_operand && !list_add && !list_compare && (binary_op->compound || level > 3))
                fail(at, "unsupported list operator");
            if (binary_op->compound) {
                if (!writable(*left))
                    fail(at, "compound assignment requires a writable variable");
                const bool bitwise = op == Op::BitAnd || op == Op::BitOr || op == Op::BitXor ||
                                     op == Op::ShiftLeft || op == Op::ShiftRight;
                const auto spatial_type = spatial_binary_type(op, left->type, right->type);
                if (!list_add && !spatial_type &&
                    ((bitwise && (left->type != Type::Integer || right->type != Type::Integer)) ||
                     ((!numeric(left->type) || !numeric(right->type)) &&
                      (op != Op::Add || left->type != Type::String || right->type != Type::String))))
                    fail(at, "compound assignment operand type mismatch");
                auto operation = std::make_unique<Expression>();
                operation->op = op;
                operation->type =
                    list_add ? Type::List
                             : spatial_type.value_or(left->type == Type::Float || right->type == Type::Float
                                                         ? Type::Float
                                                         : left->type);
                operation->height = std::max(left->height, right->height) + 1;
                operation->left = copy_target(*left);
                operation->right = std::move(right);
                right = std::move(operation);
            }
            const auto spatial_type = spatial_binary_type(op, left->type, right->type);
            if (!list_add && !spatial_type && !assign && level > 3 &&
                (left->type == Type::Vector || right->type == Type::Vector || left->type == Type::Rotation ||
                 right->type == Type::Rotation))
                fail(at, "unsupported vector or rotation operator");
            if (assign && (!writable(*left) || !assignable(left->type, right->type)))
                fail(at, "assignment requires a writable variable and compatible value");
            if (((level >= 4 && level <= 6) || level == 9) &&
                (left->type != Type::Integer || right->type != Type::Integer))
                fail(at, "bitwise operators require integers");
            if (!list_add && !spatial_type && level > 3 && (!numeric(left->type) || !numeric(right->type)) &&
                ((left->type == Type::Key || right->type == Type::Key)
                     ? (numeric(left->type) || numeric(right->type) ||
                        (op != Op::Equal && op != Op::NotEqual))
                     : (left->type != right->type || (level > 9 && op != Op::Add))))
                fail(at, "operator operand type mismatch");
            auto result = std::make_unique<Expression>();
            result->op = assign ? Op::Assign : op;
            result->type =
                assign     ? left->type
                : list_add ? Type::List
                           : spatial_type.value_or(level <= 9 ? Type::Integer
                                                   : left->type == Type::Float || right->type == Type::Float
                                                       ? Type::Float
                                                       : left->type);
            result->height = std::max(left->height, right->height) + 1;
            if (result->height > 128)
                fail(at, "native LSL expression depth exceeded");
            result->left = std::move(left);
            result->right = std::move(right);
            left = std::move(result);
        }
        return left;
    }
    static bool assignable(Type target, Type source) {
        return target == source ||
               ((target == Type::String || target == Type::Key) &&
                (source == Type::String || source == Type::Key)) ||
               (target == Type::Float && source == Type::Integer);
    }
    Instruction store(Instruction ins, Local variable) {
        if (ins.text && !assignable(variable.type, ins.text->type))
            fail(token, "assignment operand type mismatch");
        ins.store = variable.slot;
        ins.global = variable.global;
        ins.store_type = variable.type;
        return ins;
    }
    Instruction expression_statement() {
        Instruction ins;
        if (is("llLookAt") || is("llRotLookAt") || is("llStopLookAt")) {
            const bool stop = is("llStopLookAt");
            const auto target_type = is("llLookAt") ? Type::Vector : Type::Rotation;
            auto call = std::make_unique<Expression>();
            call->op = Expression::Op::LookAt;
            advance();
            require("(");
            if (!stop) {
                call->left = expression();
                require(",");
                call->right = expression();
                require(",");
                call->z = expression();
                if (call->left->type != target_type || !numeric(call->right->type) || !numeric(call->z->type))
                    fail(token, "Look-at requires a target, strength and damping");
                call->height = 1 + std::max({call->left->height, call->right->height, call->z->height});
                if (call->height > 128)
                    fail(token, "native LSL expression depth exceeded");
            }
            require(")");
            ins.text = std::move(call);
        } else if (is("llMoveToTarget") || is("llStopMoveToTarget")) {
            const bool start = is("llMoveToTarget");
            auto call = std::make_unique<Expression>();
            call->op = Expression::Op::MoveToTarget;
            advance();
            require("(");
            if (start) {
                call->left = expression();
                require(",");
                call->right = expression();
                if (call->left->type != Type::Vector || !numeric(call->right->type))
                    fail(token, "llMoveToTarget requires a vector and time constant");
                call->height = 1 + std::max(call->left->height, call->right->height);
                if (call->height > 128)
                    fail(token, "native LSL expression depth exceeded");
            }
            require(")");
            ins.text = std::move(call);
        } else if (is("llSetHoverHeight") || is("llStopHover") || is("llGroundRepel")) {
            const bool repel = is("llGroundRepel");
            const bool start = repel || is("llSetHoverHeight");
            auto call = std::make_unique<Expression>();
            call->op = repel ? Expression::Op::GroundRepel : Expression::Op::SetHover;
            advance();
            require("(");
            if (start) {
                call->left = expression();
                require(",");
                call->right = expression();
                require(",");
                call->z = expression();
                if (!numeric(call->left->type) || call->right->type != Type::Integer ||
                    !numeric(call->z->type))
                    fail(token, "llSetHoverHeight requires a height, integer water flag and time constant");
                call->height = 1 + std::max({call->left->height, call->right->height, call->z->height});
                if (call->height > 128)
                    fail(token, "native LSL expression depth exceeded");
            }
            require(")");
            ins.text = std::move(call);
        } else if (is("llPushObject")) {
            auto call = std::make_unique<Expression>();
            call->op = Expression::Op::PushObject;
            advance();
            require("(");
            constexpr std::array types{Type::String, Type::Vector, Type::Vector, Type::Integer};
            for (std::size_t index = 0; index < types.size(); ++index) {
                if (index != 0)
                    require(",");
                auto argument = expression();
                if (argument->type != types[index] && !(index == 0 && argument->type == Type::Key))
                    fail(token, "llPushObject requires a key, two vectors, and an integer");
                call->height = std::max(call->height, 1 + argument->height);
                call->elements.push_back(std::move(argument));
            }
            require(")");
            if (call->height > 128)
                fail(token, "native LSL expression depth exceeded");
            ins.text = std::move(call);
        } else if (is("llSetBuoyancy") || is("llSetForce") || is("llSetTorque") || is("llSetForceAndTorque") ||
            is("llSetVelocity") || is("llSetAngularVelocity") ||
            is("llApplyImpulse") || is("llApplyRotationalImpulse")) {
            const bool buoyancy = is("llSetBuoyancy");
            const bool combined = is("llSetForceAndTorque");
            auto call = std::make_unique<Expression>();
            if (buoyancy)
                call->op = Expression::Op::SetBuoyancy;
            else if (combined)
                call->op = Expression::Op::SetForceAndTorque;
            else if (is("llSetVelocity"))
                call->op = Expression::Op::SetVelocity;
            else if (is("llSetAngularVelocity"))
                call->op = Expression::Op::SetAngularVelocity;
            else if (is("llApplyImpulse"))
                call->op = Expression::Op::ApplyImpulse;
            else if (is("llApplyRotationalImpulse"))
                call->op = Expression::Op::ApplyRotationalImpulse;
            else
                call->op = is("llSetForce") ? Expression::Op::SetForce : Expression::Op::SetTorque;
            advance();
            require("(");
            call->left = expression();
            if (!buoyancy) {
                require(",");
                if (combined) {
                    call->z = expression();
                    require(",");
                }
                call->right = expression();
                if (call->left->type != Type::Vector || call->right->type != Type::Integer ||
                    (call->z && call->z->type != Type::Vector))
                    fail(token, "Physics setter requires vector arguments followed by an integer");
            } else if (!numeric(call->left->type)) {
                fail(token, "llSetBuoyancy requires a numeric argument");
            }
            require(")");
            call->height = 1 + std::max({call->left->height, call->right ? call->right->height : 0u,
                                        call->z ? call->z->height : 0u});
            if (call->height > 128)
                fail(token, "native LSL expression depth exceeded");
            ins.text = std::move(call);
        } else {
            ins.text = expression();
        }
        ins.discard = true;
        return ins;
    }
    void for_expressions(std::vector<Instruction>& code, std::string_view end) {
        if (is(end))
            return;
        for (;;) {
            code.push_back(expression_statement());
            if (!is(","))
                return;
            advance();
        }
    }
    Instruction declaration(std::size_t slot, bool global) {
        const auto type = is("integer")                          ? Type::Integer
                          : is("float")                          ? Type::Float
                          : is("key")                            ? Type::Key
                          : is("vector")                         ? Type::Vector
                          : (is("rotation") || is("quaternion")) ? Type::Rotation
                          : is("list")                           ? Type::List
                                                                 : Type::String;
        advance();
        if (token.kind != Kind::Name)
            fail(token, "expected variable name");
        const auto name = token;
        advance();
        const Local variable{type, slot, global};
        if (scopes.back().contains(name.text))
            fail(name, "duplicate variable");
        Instruction ins;
        if (is("=")) {
            advance();
            ins.text = expression();
        }
        scopes.back().emplace(name.text, variable);
        require(";");
        return store(std::move(ins), variable);
    }
    void block(bool event_body = false) {
        require("{");
        if (!event_body)
            scopes.emplace_back();
        blocks.push_back(next_block++);
        while (!is("}")) {
            if (token.kind == Kind::End)
                fail(token, "eof parsing event body");
            statement();
        }
        require("}");
        blocks.pop_back();
        if (!event_body)
            scopes.pop_back();
    }
    void embedded_statement() {
        scopes.emplace_back();
        statement();
        scopes.pop_back();
    }
    void statement() {
        Depth guard(*this);
        if (is("{")) {
            block();
            return;
        }
        if (is(";")) {
            advance();
            return;
        }
        Instruction ins;
        if (is("jump") || is("@")) {
            const bool jump = is("jump");
            advance();
            if (token.kind != Kind::Name)
                fail(token, "expected label name");
            const auto name = token;
            advance();
            require(";");
            auto& code = handler->instructions;
            if (jump) {
                jumps.push_back({name, code.size(), blocks});
                code.emplace_back();
            } else if (!labels.emplace(name.text, Label{code.size(), blocks.back()}).second)
                fail(name, "duplicate label");
            return;
        }
        if (is("integer") || is("float") || is("string") || is("key") || is("vector") || is("rotation") ||
            is("quaternion") || is("list")) {
            auto instruction = declaration(handler->local_types.size(), false);
            handler->local_types.push_back(instruction.store_type);
            handler->instructions.push_back(std::move(instruction));
            return;
        }
        if (token.kind == Kind::Name && local(token.text)) {
            auto instruction = expression_statement();
            require(";");
            handler->instructions.push_back(std::move(instruction));
            return;
        }
        if (is("state")) {
            advance();
            if (token.kind != Kind::Name)
                fail(token, "expected state name");
            state_targets.push_back(token);
            ins.target_state = token.text;
            advance();
            require(";");
            handler->instructions.push_back(std::move(ins));
            return;
        }
        if (is("return")) {
            advance();
            require(";");
            ins.return_event = true;
            handler->instructions.push_back(std::move(ins));
            return;
        }
        if (is("for")) {
            advance();
            require("(");
            auto& code = handler->instructions;
            for_expressions(code, ";");
            require(";");
            const auto start = code.size();
            const bool conditional = !is(";");
            if (conditional) {
                ins.condition = expression();
                code.push_back(std::move(ins));
            }
            require(";");
            std::vector<Instruction> updates;
            for_expressions(updates, ")");
            require(")");
            embedded_statement();
            for (auto& update : updates)
                code.push_back(std::move(update));
            Instruction repeat;
            repeat.jump = start;
            code.push_back(std::move(repeat));
            if (conditional)
                code[start].jump = code.size();
            return;
        }
        if (is("do")) {
            advance();
            auto& code = handler->instructions;
            const auto start = code.size();
            embedded_statement();
            require("while");
            require("(");
            ins.condition = expression();
            require(")");
            require(";");
            ins.jump = start;
            ins.jump_when_true = true;
            code.push_back(std::move(ins));
            return;
        }
        if (is("if") || is("while")) {
            const bool loop = is("while");
            advance();
            require("(");
            ins.condition = expression();
            require(")");
            auto& code = handler->instructions;
            const auto branch = code.size();
            code.push_back(std::move(ins));
            embedded_statement();
            if (loop) {
                Instruction repeat;
                repeat.jump = branch;
                code.push_back(std::move(repeat));
                code[branch].jump = code.size();
            } else if (is("else")) {
                advance();
                const auto skip = code.size();
                code.emplace_back();
                code[branch].jump = code.size();
                embedded_statement();
                code[skip].jump = code.size();
            } else
                code[branch].jump = code.size();
            return;
        }
        if (is("llUnSit")) {
            advance();
            require("(");
            ins.unsit_avatar = expression();
            if (ins.unsit_avatar->type != Type::Key && ins.unsit_avatar->type != Type::String)
                fail(token, "llUnSit requires an avatar key");
            require(")");
            require(";");
            handler->instructions.push_back(std::move(ins));
            return;
        }
        if (is("osForceOtherSit")) {
            advance();
            require("(");
            ins.force_sit_avatar = expression();
            if (ins.force_sit_avatar->type != Type::String && ins.force_sit_avatar->type != Type::Key)
                fail(token, "osForceOtherSit requires an avatar key");
            if (is(",")) {
                advance();
                ins.force_sit_target = expression();
                if (ins.force_sit_target->type != Type::String && ins.force_sit_target->type != Type::Key)
                    fail(token, "osForceOtherSit requires an object key");
            }
            require(")");
            require(";");
            handler->instructions.push_back(std::move(ins));
            return;
        }
        if (is("osSetRegionSunSettings") || is("osSetEstateSunSettings")) {
            ins.estate_sun = is("osSetEstateSunSettings");
            const auto function = token;
            advance();
            require("(");
            if (!ins.estate_sun) {
                ins.sun_use_estate = expression();
                require(",");
            }
            ins.sun_fixed = expression();
            require(",");
            ins.sun_hour = expression();
            require(")");
            require(";");
            if ((ins.sun_use_estate && ins.sun_use_estate->type != Type::Integer) ||
                ins.sun_fixed->type != Type::Integer || !numeric(ins.sun_hour->type))
                fail(function, "Sun settings require integer flags and a numeric hour");
            handler->instructions.push_back(std::move(ins));
            return;
        }
        if (is("llParcelMediaCommandList")) {
            advance();
            require("(");
            ins.parcel_media_commands = expression();
            if (ins.parcel_media_commands->type != Type::List)
                fail(token, "llParcelMediaCommandList requires a list of commands");
            require(")");
            require(";");
            handler->instructions.push_back(std::move(ins));
            return;
        }
        if (is("llSetParcelMusicURL")) {
            advance();
            require("(");
            ins.parcel_music_url = expression();
            if (ins.parcel_music_url->type != Type::String && ins.parcel_music_url->type != Type::Key)
                fail(token, "llSetParcelMusicURL requires a string URL");
            require(")");
            require(";");
            handler->instructions.push_back(std::move(ins));
            return;
        }
        if (is("osSetRegionWaterHeight")) {
            advance();
            require("(");
            ins.region_water_height = expression();
            if (!numeric(ins.region_water_height->type))
                fail(token, "osSetRegionWaterHeight requires a numeric height");
            require(")");
            require(";");
            handler->instructions.push_back(std::move(ins));
            return;
        }
        if (is("llSetLinkSitFlags")) {
            advance();
            require("(");
            ins.sit_flags_link = expression();
            require(",");
            ins.sit_flags_value = expression();
            if (ins.sit_flags_link->type != Type::Integer || ins.sit_flags_value->type != Type::Integer)
                fail(token, "llSetLinkSitFlags requires an integer link number and flags");
            require(")");
            require(";");
            handler->instructions.push_back(std::move(ins));
            return;
        }
        if (is("llSetCameraEyeOffset") || is("llSetCameraAtOffset") ||
            is("llSetLinkCamera") || is("llForceMouselook")) {
            const bool linked = is("llSetLinkCamera");
            const bool eye = is("llSetCameraEyeOffset");
            const bool mouselook = is("llForceMouselook");
            advance();
            require("(");
            if (linked) {
                ins.seat_camera_link = expression();
                if (ins.seat_camera_link->type != Type::Integer)
                    fail(token, "llSetLinkCamera requires an integer link number");
                require(",");
                ins.seat_camera_eye = expression();
                if (ins.seat_camera_eye->type != Type::Vector)
                    fail(token, "camera eye offset requires a vector");
                require(",");
            }
            auto& value = mouselook ? ins.seat_camera_mouselook : eye ? ins.seat_camera_eye : ins.seat_camera_at;
            value = expression();
            if (value->type != (mouselook ? Type::Integer : Type::Vector))
                fail(token, mouselook ? "llForceMouselook requires an integer" : "camera offset requires a vector");
            require(")");
            require(";");
            handler->instructions.push_back(std::move(ins));
            return;
        }
        if (is("osSetSitActiveRange") || is("osSetLinkSitActiveRange") ||
            is("osSetStandTarget") || is("osSetLinkStandTarget")) {
            const bool stand = is("osSetStandTarget") || is("osSetLinkStandTarget");
            const bool linked = is("osSetLinkSitActiveRange") || is("osSetLinkStandTarget");
            advance();
            require("(");
            if (linked) {
                auto& link = stand ? ins.stand_target_link : ins.sit_range_link;
                link = expression();
                if (link->type != Type::Integer)
                    fail(token, "seat configuration requires an integer link number");
                require(",");
            }
            auto& value = stand ? ins.stand_target_value : ins.sit_range_value;
            value = expression();
            if (stand ? value->type != Type::Vector : !numeric(value->type))
                fail(token, stand ? "stand target requires a vector" : "sit range requires a number");
            require(")");
            require(";");
            handler->instructions.push_back(std::move(ins));
            return;
        }
        if (is("llSitTarget") || is("llLinkSitTarget")) {
            const bool linked = is("llLinkSitTarget");
            advance();
            require("(");
            if (linked) {
                ins.sit_target_link = expression();
                if (ins.sit_target_link->type != Type::Integer)
                    fail(token, "llLinkSitTarget requires an integer link number");
                require(",");
            }
            ins.sit_target_position = expression();
            require(",");
            ins.sit_target_rotation = expression();
            if (ins.sit_target_position->type != Type::Vector || ins.sit_target_rotation->type != Type::Rotation)
                fail(token, "sit target requires a vector and rotation");
            require(")");
            require(";");
            handler->instructions.push_back(std::move(ins));
            return;
        }
        if (is("llSetStatus")) {
            advance();
            require("(");
            ins.status_mask = expression();
            require(",");
            ins.status_value = expression();
            require(")");
            require(";");
            if (ins.status_mask->type != Type::Integer || ins.status_value->type != Type::Integer)
                fail(token, "llSetStatus requires two integers");
            handler->instructions.push_back(std::move(ins));
            return;
        }
        if (is("osClearInertia") || is("osSetInertia") || is("osSetInertiaAsBox") ||
            is("osSetInertiaAsSphere") || is("osSetInertiaAsCylinder")) {
            using Shape = Instruction::InertiaShape;
            std::span<const Type> types;
            constexpr std::array vector_shape{Type::Float, Type::Vector, Type::Vector, Type::Rotation};
            constexpr std::array sphere{Type::Float, Type::Float, Type::Vector};
            constexpr std::array cylinder{Type::Float, Type::Float, Type::Float, Type::Vector, Type::Rotation};
            if (is("osClearInertia")) {
                ins.inertia_shape = Shape::Clear;
            } else if (is("osSetInertiaAsSphere")) {
                ins.inertia_shape = Shape::Sphere;
                types = sphere;
            } else if (is("osSetInertiaAsCylinder")) {
                ins.inertia_shape = Shape::Cylinder;
                types = cylinder;
            } else {
                ins.inertia_shape = is("osSetInertia") ? Shape::Principal : Shape::Box;
                types = vector_shape;
            }
            const auto name = token.text;
            advance();
            require("(");
            for (const auto type : types) {
                if (!ins.inertia_arguments.empty())
                    require(",");
                auto argument = expression();
                if (type == Type::Float ? !numeric(argument->type) : argument->type != type)
                    fail(token, "Invalid argument for " + name);
                ins.inertia_arguments.push_back(std::move(argument));
            }
            require(")");
            require(";");
            handler->instructions.push_back(std::move(ins));
            return;
        }
        if (is("osSetHealRate")) {
            advance();
            require("(");
            ins.heal_agent = expression();
            require(",");
            ins.heal_rate = expression();
            if (!assignable(Type::Key, ins.heal_agent->type) || !numeric(ins.heal_rate->type))
                fail(token, "osSetHealRate requires an avatar key and numeric rate");
            require(")");
            require(";");
            handler->instructions.push_back(std::move(ins));
            return;
        }
        if (is("llTeleportAgent") || is("llTeleportAgentGlobalCoords")) {
            ins.teleport_viewer = true;
            const bool global = is("llTeleportAgentGlobalCoords");
            advance();
            require("(");
            auto& args = ins.teleport_arguments;
            for (std::size_t i = 0; i < 4; ++i) {
                if (i)
                    require(",");
                args.push_back(expression());
            }
            if (!assignable(Type::String, args[0]->type) ||
                (global ? args[1]->type != Type::Vector : !assignable(Type::String, args[1]->type)) ||
                args[2]->type != Type::Vector || args[3]->type != Type::Vector)
                fail(token, "Teleport requires an avatar key, a destination and two vectors");
            require(")");
            require(";");
            handler->instructions.push_back(std::move(ins));
            return;
        }
        if (is("osTeleportAgent") || is("osTeleportOwner")) {
            ins.teleport_owner = is("osTeleportOwner");
            advance();
            require("(");
            auto& args = ins.teleport_arguments;
            args.push_back(expression());
            while (is(",") && args.size() < 5) {
                advance();
                args.push_back(expression());
            }
            const std::size_t first = ins.teleport_owner ? 0 : 1;
            if (args.size() < first + 2 || args.size() > first + 4 ||
                (!ins.teleport_owner && !assignable(Type::String, args[0]->type)) ||
                args[args.size() - 2]->type != Type::Vector || args.back()->type != Type::Vector)
                fail(token, "Teleport requires an optional destination followed by position and look vectors");
            const auto destination_args = args.size() - first - 2;
            if ((destination_args == 1 && !assignable(Type::String, args[first]->type)) ||
                (destination_args == 2 && (args[first]->type != Type::Integer ||
                                           args[first + 1]->type != Type::Integer)))
                fail(token, "Teleport destination requires a region name or two integer grid coordinates");
            require(")");
            require(";");
            handler->instructions.push_back(std::move(ins));
            return;
        }
        if (is("osLocalTeleportAgent")) {
            advance();
            require("(");
            auto& args = ins.local_teleport_arguments;
            for (std::size_t i = 0; i < args.size(); ++i) {
                if (i)
                    require(",");
                args[i] = expression();
            }
            if (!assignable(Type::Key, args[0]->type) || args[1]->type != Type::Vector ||
                args[2]->type != Type::Vector || args[3]->type != Type::Vector || args[4]->type != Type::Integer)
                fail(token, "osLocalTeleportAgent requires an avatar key, three vectors and integer flags");
            require(")");
            require(";");
            handler->instructions.push_back(std::move(ins));
            return;
        }
        if (is("llTeleportAgentHome") || is("llEjectFromLand")) {
            ins.eject_from_land = is("llEjectFromLand");
            advance();
            require("(");
            ins.relocate_agent = expression();
            if (!assignable(Type::Key, ins.relocate_agent->type))
                fail(token, ins.eject_from_land ? "llEjectFromLand requires an avatar key"
                                               : "llTeleportAgentHome requires an avatar key");
            require(")");
            require(";");
            handler->instructions.push_back(std::move(ins));
            return;
        }
        if (is("llSetRenderMaterial")) {
            advance();
            require("(");
            ins.render_material_name = expression();
            require(",");
            ins.render_material_face = expression();
            if (!assignable(Type::String, ins.render_material_name->type) ||
                ins.render_material_face->type != Type::Integer)
                fail(token, "llSetRenderMaterial requires a material and an integer face");
            require(")");
            require(";");
            handler->instructions.push_back(std::move(ins));
            return;
        }
        if (is("llSetDamage")) {
            advance();
            require("(");
            ins.damage = expression();
            if (!numeric(ins.damage->type))
                fail(token, "llSetDamage requires a numeric damage value");
            require(")");
            require(";");
            handler->instructions.push_back(std::move(ins));
            return;
        }
        if (is("llVolumeDetect") || is("osVolumeDetect")) {
            const auto function = token.text;
            ins.volume_detect_part = is("osVolumeDetect");
            advance();
            require("(");
            ins.volume_detect = expression();
            require(")");
            require(";");
            if (ins.volume_detect->type != Type::Integer)
                fail(token, function + " requires an integer");
            handler->instructions.push_back(std::move(ins));
            return;
        }
        if (is("llCollisionFilter")) {
            advance();
            require("(");
            auto& filter = ins.collision_filter.emplace();
            filter.name = expression();
            require(",");
            filter.key = expression();
            require(",");
            filter.accept = expression();
            require(")");
            require(";");
            const auto is_text = [](Type type) { return type == Type::String || type == Type::Key; };
            if (!is_text(filter.name->type) || !is_text(filter.key->type) || filter.accept->type != Type::Integer)
                fail(token, "llCollisionFilter requires a string, a key and an integer");
            handler->instructions.push_back(std::move(ins));
            return;
        }
        if (is("llPassTouches") || is("llAllowInventoryDrop") || is("llPassCollisions")) {
            const bool allow_drop = is("llAllowInventoryDrop");
            const bool collisions = is("llPassCollisions");
            advance();
            require("(");
            auto& value = allow_drop ? ins.allow_inventory_drop : collisions ? ins.pass_collisions : ins.pass_touches;
            value = expression();
            if (value->type != Type::Integer)
                fail(token, allow_drop ? "llAllowInventoryDrop requires an integer"
                                       : collisions ? "llPassCollisions requires an integer"
                                                    : "llPassTouches requires an integer");
            require(")");
            require(";");
            handler->instructions.push_back(std::move(ins));
            return;
        }
        if (is("llSetClickAction")) {
            advance();
            require("(");
            ins.click_action = expression();
            if (ins.click_action->type != Type::Integer)
                fail(token, "llSetClickAction requires an integer");
            require(")");
            require(";");
            handler->instructions.push_back(std::move(ins));
            return;
        }
        if (is("llSetScale")) {
            advance();
            require("(");
            ins.scale = expression();
            if (ins.scale->type != Type::Vector)
                fail(token, "llSetScale requires a vector");
            require(")");
            require(";");
            handler->instructions.push_back(std::move(ins));
            return;
        }
        if (is("llAdjustSoundVolume") || is("llLinkAdjustSoundVolume") ||
            is("llSetSoundRadius") || is("llLinkSetSoundRadius") ||
            is("llSetSoundQueueing") || is("llLinkSetSoundQueueing")) {
            const bool radius = is("llSetSoundRadius") || is("llLinkSetSoundRadius");
            const bool queueing = is("llSetSoundQueueing") || is("llLinkSetSoundQueueing");
            const bool linked = token.text.starts_with("llLink");
            advance();
            require("(");
            if (linked) {
                ins.sound_link = expression();
                if (ins.sound_link->type != Type::Integer)
                    fail(token, "linked sound requires an integer link selector");
                require(",");
            }
            auto& value = queueing ? ins.sound_queueing : radius ? ins.sound_radius : ins.adjust_sound_gain;
            value = expression();
            if (queueing && value->type != Type::Integer)
                fail(token, "llSetSoundQueueing requires an integer");
            if (!queueing && !numeric(value->type))
                fail(token, "sound setting requires a numeric value");
            require(")");
            require(";");
            handler->instructions.push_back(std::move(ins));
            return;
        }
        if (is("llSetPayPrice")) {
            advance();
            require("(");
            ins.pay_price = expression();
            require(",");
            ins.pay_buttons = expression();
            if (ins.pay_price->type != Type::Integer || ins.pay_buttons->type != Type::List)
                fail(token, "llSetPayPrice requires an integer price and a list");
            require(")");
            require(";");
            handler->instructions.push_back(std::move(ins));
            return;
        }
        if (is("llScriptProfiler")) {
            advance();
            require("(");
            ins.text = expression();
            if (ins.text->type != Type::Integer)
                fail(token, "llScriptProfiler requires an integer");
            require(")");
            require(";");
            ins.discard = true;
            handler->instructions.push_back(std::move(ins));
            return;
        }
        if (is("llPreloadSound")) {
            advance();
            require("(");
            ins.preload_sound = expression();
            if (ins.preload_sound->type != Type::String && ins.preload_sound->type != Type::Key)
                fail(token, "llPreloadSound requires an asset name or key");
            require(")");
            require(";");
            handler->instructions.push_back(std::move(ins));
            return;
        }
        if (is("llLoopSound") || is("llLoopSoundMaster") || is("llLoopSoundSlave") || is("llStopSound") ||
            is("llPlaySound") || is("llPlaySoundSlave") || is("llTriggerSound") ||
            is("llTriggerSoundLimited") || is("llCollisionSound") || is("osCollisionSound") || is("llLinkPlaySound") ||
            is("llLinkStopSound")) {
            ins.sound = true;
            const bool linked_play = is("llLinkPlaySound");
            const bool linked = linked_play || is("llLinkStopSound");
            const bool limited = is("llTriggerSoundLimited");
            ins.trigger_sound = is("llTriggerSound") || limited;
            ins.ossl_collision_sound = is("osCollisionSound");
            ins.collision_sound = is("llCollisionSound") || ins.ossl_collision_sound;
            ins.play_sound = is("llPlaySound") || is("llPlaySoundSlave") || ins.trigger_sound;
            const bool has_sound = !is("llStopSound") && !is("llLinkStopSound");
            ins.sound_sync_flags = is("llLoopSoundMaster")                              ? 2
                                   : (is("llLoopSoundSlave") || is("llPlaySoundSlave")) ? 4
                                                                                        : 0;
            advance();
            require("(");
            if (linked) {
                ins.sound_link = expression();
                if (ins.sound_link->type != Type::Integer)
                    fail(token, "linked sound requires an integer link selector");
                if (linked_play)
                    require(",");
            }
            if (has_sound) {
                ins.text = expression();
                require(",");
                ins.sound_gain = expression();
                if ((ins.text->type != Type::String && ins.text->type != Type::Key) ||
                    !numeric(ins.sound_gain->type))
                    fail(token, "llLoopSound requires an asset name or key and numeric gain");
                if (limited) {
                    require(",");
                    ins.sound_max = expression();
                    require(",");
                    ins.sound_min = expression();
                    if (ins.sound_max->type != Type::Vector || ins.sound_min->type != Type::Vector)
                        fail(token, "llTriggerSoundLimited requires two vector corners");
                }
                if (linked_play && is(",")) {
                    advance();
                    ins.sound_mode = expression();
                    if (ins.sound_mode->type != Type::Integer)
                        fail(token, "llLinkPlaySound requires integer flags");
                }
            }
            require(")");
            require(";");
            handler->instructions.push_back(std::move(ins));
            return;
        }
        if (is("llSetText")) {
            advance();
            require("(");
            ins.text = expression();
            require(",");
            ins.text_color = expression();
            require(",");
            ins.text_alpha = expression();
            if ((ins.text->type != Type::String && ins.text->type != Type::Key) ||
                ins.text_color->type != Type::Vector || !numeric(ins.text_alpha->type))
                fail(token, "llSetText requires text, vector color and numeric alpha");
            require(")");
            require(";");
            handler->instructions.push_back(std::move(ins));
            return;
        }
        if (is("llDialog") || is("llTextBox")) {
            const bool text_box = is("llTextBox");
            advance();
            require("(");
            const auto parse_argument = [&](std::size_t i, Type type) {
                if (i)
                    require(",");
                auto argument = expression();
                if (!assignable(type, argument->type))
                    fail(token, text_box ? "llTextBox requires an avatar key, message and channel"
                                         : "llDialog requires an avatar key, message, button list and channel");
                ins.dialog_arguments[i] = std::move(argument);
            };
            parse_argument(0, Type::String);
            parse_argument(1, Type::String);
            if (!text_box)
                parse_argument(2, Type::List);
            parse_argument(3, Type::Integer);
            require(")");
            require(";");
            handler->instructions.push_back(std::move(ins));
            return;
        }
        if (is("llGiveInventoryList")) {
            advance();
            require("(");
            constexpr std::array types{Type::Key, Type::String, Type::List};
            for (std::size_t i = 0; i < types.size(); ++i) {
                if (i)
                    require(",");
                auto argument = expression();
                if (!assignable(types[i], argument->type))
                    fail(token, "llGiveInventoryList requires a destination key, folder name and list");
                ins.give_inventory_list_arguments[i] = std::move(argument);
            }
            require(")");
            require(";");
            handler->instructions.push_back(std::move(ins));
            return;
        }
        if (is("llInstantMessage") || is("llGiveInventory")) {
            ins.give_inventory = is("llGiveInventory");
            advance();
            require("(");
            ins.instant_message_arguments[0] = expression();
            require(",");
            ins.instant_message_arguments[1] = expression();
            for (const auto& argument : ins.instant_message_arguments)
                if (!assignable(Type::String, argument->type))
                    fail(token, ins.give_inventory ? "llGiveInventory requires a destination key and inventory name"
                                                   : "llInstantMessage requires an avatar key and message");
            require(")");
            require(";");
            handler->instructions.push_back(std::move(ins));
            return;
        }
        if (is("llLoadURL")) {
            advance();
            require("(");
            for (std::size_t i = 0; i < ins.load_url_arguments.size(); ++i) {
                if (i)
                    require(",");
                auto argument = expression();
                if (!assignable(Type::String, argument->type))
                    fail(token, "llLoadURL requires an avatar key, message and URL");
                ins.load_url_arguments[i] = std::move(argument);
            }
            require(")");
            require(";");
            handler->instructions.push_back(std::move(ins));
            return;
        }
        if (is("llMapDestination") || is("llMapBeacon")) {
            ins.map_beacon = is("llMapBeacon");
            advance();
            require("(");
            for (std::size_t i = 0; i < ins.map_arguments.size(); ++i) {
                if (i)
                    require(",");
                ins.map_arguments[i] = expression();
            }
            require(")");
            require(";");
            const auto name_type = ins.map_arguments[0]->type;
            if ((name_type != Type::String && name_type != Type::Key) ||
                ins.map_arguments[1]->type != Type::Vector ||
                ins.map_arguments[2]->type != (ins.map_beacon ? Type::List : Type::Vector))
                fail(token, "Invalid map destination arguments");
            handler->instructions.push_back(std::move(ins));
            return;
        }
        if (is("llDie")) {
            advance();
            require("(");
            require(")");
            require(";");
            ins.die = true;
            handler->instructions.push_back(std::move(ins));
            return;
        }
        if (is("llSetObjectPermMask")) {
            advance();
            require("(");
            ins.object_permission_args[0] = expression();
            require(",");
            ins.object_permission_args[1] = expression();
            require(")");
            require(";");
            if (ins.object_permission_args[0]->type != Type::Integer ||
                ins.object_permission_args[1]->type != Type::Integer)
                fail(token, "llSetObjectPermMask requires an integer selector and mask");
            handler->instructions.push_back(std::move(ins));
            return;
        }
        if (is("llSetInventoryPermMask")) {
            advance();
            require("(");
            for (std::size_t i = 0; i < ins.inventory_permission_args.size(); ++i) {
                if (i)
                    require(",");
                ins.inventory_permission_args[i] = expression();
            }
            require(")");
            require(";");
            const auto name_type = ins.inventory_permission_args[0]->type;
            if ((name_type != Type::String && name_type != Type::Key) ||
                ins.inventory_permission_args[1]->type != Type::Integer ||
                ins.inventory_permission_args[2]->type != Type::Integer)
                fail(token, "llSetInventoryPermMask requires a name, selector and integer mask");
            handler->instructions.push_back(std::move(ins));
            return;
        }
        if (is("llRemoveInventory")) {
            advance();
            require("(");
            ins.inventory_remove_name = expression();
            require(")");
            require(";");
            if (ins.inventory_remove_name->type != Type::String && ins.inventory_remove_name->type != Type::Key)
                fail(token, "llRemoveInventory requires an inventory name");
            handler->instructions.push_back(std::move(ins));
            return;
        }
        if (is("llSetObjectName") || is("llSetObjectDesc") || is("llSetTouchText") || is("llSetSitText")) {
            if (is("llSetTouchText") || is("llSetSitText"))
                ins.sit_label = is("llSetSitText");
            else
                ins.set_metadata = true;
            ins.description = is("llSetObjectDesc");
            advance();
            require("(");
            ins.text = expression();
            if (ins.text->type != Type::String && ins.text->type != Type::Key)
                fail(token, "object metadata requires text");
            require(")");
            require(";");
            handler->instructions.push_back(std::move(ins));
            return;
        }
        if (is("llAddToLandBanList") || is("llAddToLandPassList") ||
            is("llRemoveFromLandBanList") || is("llRemoveFromLandPassList") ||
            is("llResetLandBanList") || is("llResetLandPassList")) {
            const bool add = is("llAddToLandBanList") || is("llAddToLandPassList");
            const bool reset = is("llResetLandBanList") || is("llResetLandPassList");
            ins.parcel_list_action = reset ? LslParcelListAction::reset :
                                     add ? LslParcelListAction::add : LslParcelListAction::remove;
            ins.parcel_ban_list = is("llAddToLandBanList") || is("llRemoveFromLandBanList") ||
                                  is("llResetLandBanList");
            advance();
            require("(");
            if (!reset) {
                ins.parcel_agent = expression();
                if (ins.parcel_agent->type != Type::String && ins.parcel_agent->type != Type::Key)
                    fail(token, "parcel list edits require an avatar key");
            }
            if (add) {
                require(",");
                ins.parcel_hours = expression();
                if (!numeric(ins.parcel_hours->type))
                    fail(token, "parcel list duration must be numeric");
            }
            require(")");
            require(";");
            handler->instructions.push_back(std::move(ins));
            return;
        }
        if (is("llMinEventDelay")) {
            advance();
            require("(");
            ins.event_delay = expression();
            if (!numeric(ins.event_delay->type))
                fail(token, "llMinEventDelay requires a numeric interval");
            require(")");
            require(";");
            handler->instructions.push_back(std::move(ins));
            return;
        }
        if (is("llSetTimerEvent")) {
            advance();
            require("(");
            ins.timer = expression();
            if (!numeric(ins.timer->type))
                fail(token, "llSetTimerEvent requires a numeric interval");
            require(")");
            require(";");
            handler->instructions.push_back(std::move(ins));
            return;
        }
        if (is("llSleep")) {
            advance();
            require("(");
            ins.sleep = expression();
            if (!numeric(ins.sleep->type))
                fail(token, "llSleep requires a numeric duration");
            require(")");
            require(";");
            handler->instructions.push_back(std::move(ins));
            return;
        }
        if (is("llResetOtherScript") || is("llSetScriptState")) {
            const bool set_running = is("llSetScriptState");
            advance();
            require("(");
            ins.script_control_name = expression();
            if (ins.script_control_name->type != Type::String && ins.script_control_name->type != Type::Key)
                fail(token, "script name requires text");
            if (set_running) {
                require(",");
                ins.script_control_run = expression();
                if (ins.script_control_run->type != Type::Integer)
                    fail(token, "script running state requires an integer");
            }
            require(")");
            require(";");
            handler->instructions.push_back(std::move(ins));
            return;
        }
        if (is("llResetScript")) {
            advance();
            require("(");
            require(")");
            require(";");
            ins.reset_script = true;
            handler->instructions.push_back(std::move(ins));
            return;
        }
        if (is("llSetAnimationOverride") || is("llResetAnimationOverride")) {
            const bool set = is("llSetAnimationOverride");
            advance();
            require("(");
            ins.override_state = expression();
            if (set) {
                require(",");
                ins.override_animation = expression();
            }
            require(")");
            require(";");
            if (ins.override_state->type != Type::String ||
                (ins.override_animation && ins.override_animation->type != Type::String))
                fail(token, "Animation override requires string arguments");
            handler->instructions.push_back(std::move(ins));
            return;
        }
        if (is("llSetRegionPos")) {
            auto position = expression_statement();
            require(";");
            handler->instructions.push_back(std::move(position));
            return;
        }
        if (is("llRezObject") || is("llRezAtRoot")) {
            ins.rez_at_root = is("llRezAtRoot");
            advance();
            require("(");
            constexpr std::array types{Type::String, Type::Vector, Type::Vector, Type::Rotation, Type::Integer};
            for (std::size_t i = 0; i < types.size(); ++i) {
                if (i)
                    require(",");
                auto& argument = ins.rez_arguments[i];
                argument = expression();
                if (argument->type != types[i] && !(i == 0 && argument->type == Type::Key))
                    fail(token, "Invalid object rez argument");
            }
            require(")");
            require(";");
            handler->instructions.push_back(std::move(ins));
            return;
        }
        if (is("llSetRot") || is("llSetLocalRot")) {
            ins.local_object_rotation = is("llSetLocalRot");
            advance();
            require("(");
            ins.object_rotation = expression();
            require(")");
            require(";");
            if (ins.object_rotation->type != Type::Rotation)
                fail(token, "Object rotation must be a rotation");
            handler->instructions.push_back(std::move(ins));
            return;
        }
        if (is("llSetPos")) {
            advance();
            require("(");
            ins.object_position = expression();
            require(")");
            require(";");
            if (ins.object_position->type != Type::Vector)
                fail(token, "Object position must be a vector");
            handler->instructions.push_back(std::move(ins));
            return;
        }
        if (is("llStartAnimation") || is("llStopAnimation") ||
            is("llStartObjectAnimation") || is("llStopObjectAnimation") ||
            is("osAvatarPlayAnimation") || is("osAvatarStopAnimation")) {
            const bool explicit_avatar = is("osAvatarPlayAnimation") || is("osAvatarStopAnimation");
            ins.object_animation = is("llStartObjectAnimation") || is("llStopObjectAnimation");
            ins.start_animation = is("llStartAnimation") || is("osAvatarPlayAnimation") ||
                                  is("llStartObjectAnimation");
            advance();
            require("(");
            if (explicit_avatar) {
                ins.animation_avatar = expression();
                if (ins.animation_avatar->type != Type::Key && ins.animation_avatar->type != Type::String)
                    fail(token, "Avatar animation requires an avatar key");
                require(",");
            }
            ins.animation_name = expression();
            require(")");
            require(";");
            if (ins.animation_name->type != Type::String &&
                !(explicit_avatar && ins.animation_name->type == Type::Key))
                fail(token, "Animation name must be a string");
            handler->instructions.push_back(std::move(ins));
            return;
        }
        if (is("llTakeControls") || is("llReleaseControls")) {
            ins.release_controls = is("llReleaseControls");
            advance();
            require("(");
            if (!ins.release_controls) {
                for (std::size_t index = 0; index < ins.taken_controls.size(); ++index) {
                    if (index)
                        require(",");
                    auto argument = expression();
                    if (argument->type != Type::Integer)
                        fail(token, "llTakeControls requires integer arguments");
                    ins.taken_controls[index] = std::move(argument);
                }
            }
            require(")");
            require(";");
            handler->instructions.push_back(std::move(ins));
            return;
        }
        if (is("llRequestPermissions")) {
            advance();
            require("(");
            ins.permission_agent = expression();
            require(",");
            ins.permission_mask = expression();
            require(")");
            require(";");
            if ((ins.permission_agent->type != Type::String && ins.permission_agent->type != Type::Key) ||
                ins.permission_mask->type != Type::Integer)
                fail(token, "Permission request requires an agent key and integer mask");
            handler->instructions.push_back(std::move(ins));
            return;
        }
        if (is("llLinksetDataReset")) {
            advance();
            require("(");
            require(")");
            require(";");
            ins.reset_linkset_data = true;
            handler->instructions.push_back(std::move(ins));
            return;
        }
        if (is("llResetTime")) {
            advance();
            require("(");
            require(")");
            require(";");
            ins.reset_time = true;
            handler->instructions.push_back(std::move(ins));
            return;
        }
        if (is("llTargetOmega")) {
            advance();
            require("(");
            ins.omega[0] = expression();
            if (ins.omega[0]->type != Type::Vector)
                fail(token, "llTargetOmega requires a vector axis");
            for (std::size_t i = 1; i < ins.omega.size(); ++i) {
                require(",");
                ins.omega[i] = expression();
                if (!numeric(ins.omega[i]->type))
                    fail(token, "llTargetOmega requires numeric spin and gain");
            }
            require(")");
            require(";");
            handler->instructions.push_back(std::move(ins));
            return;
        }
        if (is("llOpenRemoteDataChannel")) {
            advance();
            require("(");
            require(")");
            require(";");
            ins.open_remote_data_channel = true;
            handler->instructions.push_back(std::move(ins));
            return;
        }
        if (is("llCloseRemoteDataChannel")) {
            const auto command = token;
            advance();
            require("(");
            ins.close_remote_data_channel = expression();
            if (!assignable(Type::String, ins.close_remote_data_channel->type))
                fail(command, "invalid argument type for llCloseRemoteDataChannel");
            require(")");
            require(";");
            handler->instructions.push_back(std::move(ins));
            return;
        }
        const auto legacy = std::ranges::find(legacy_commands, token.text, &LegacyCommand::name);
        if (legacy != std::end(legacy_commands)) {
            const auto command = token;
            advance();
            require("(");
            ins.text = std::make_unique<Expression>();
            ins.text->op = Expression::Op::LegacyCommand;
            ins.text->slot = static_cast<std::size_t>(legacy - std::begin(legacy_commands));
            for (const auto parameter : legacy->parameters) {
                if (!ins.text->elements.empty())
                    require(",");
                auto argument = expression();
                if (!assignable(parameter, argument->type))
                    fail(command, "invalid argument type for " + command.text);
                ins.text->elements.push_back(std::move(argument));
            }
            require(")");
            require(";");
            ins.discard = true;
            handler->instructions.push_back(std::move(ins));
            return;
        }
        if (is("llWhisper"))
            ins.chat_type = LslChatType::Whisper;
        else if (is("llShout"))
            ins.chat_type = LslChatType::Shout;
        else if (is("llRegionSay"))
            ins.chat_type = LslChatType::Region;
        else if (is("llOwnerSay"))
            ins.owner = true;
        else if (!is("llSay")) {
            auto instruction = expression_statement();
            require(";");
            handler->instructions.push_back(std::move(instruction));
            return;
        }
        advance();
        require("(");
        if (!ins.owner) {
            ins.channel = expression();
            if (ins.channel->type != Type::Integer)
                fail(token, "llSay channel must be integer");
            require(",");
        }
        ins.text = expression();
        if (ins.text->type != Type::String && ins.text->type != Type::Key)
            fail(token, "chat text must be string");
        require(")");
        require(";");
        handler->instructions.push_back(std::move(ins));
    }

  public:
    explicit Parser(std::string_view s) : lexer(s) {
        advance();
    }
    std::shared_ptr<LslProgram> parse() {
        auto program = std::make_shared<LslProgram>();
        scopes.emplace_back();
        while (is("integer") || is("float") || is("string") || is("key") || is("vector") || is("rotation") ||
               is("quaternion") || is("list"))
            program->globals.push_back(declaration(program->globals.size(), true));
        require("default");
        parse_state(program->states["default"]);
        while (is("state")) {
            advance();
            if (token.kind != Kind::Name)
                fail(token, "expected state name");
            const auto name = token;
            advance();
            auto [it, inserted] = program->states.try_emplace(name.text);
            if (!inserted)
                fail(name, "duplicate state");
            program->state_order.push_back(name.text);
            parse_state(it->second);
        }
        if (token.kind != Kind::End)
            fail(token, "expected named state");
        for (const auto& target : state_targets)
            if (!program->states.contains(target.text))
                fail(target, "undefined state");
        return program;
    }
    void parse_state(ScriptState& state) {
        require("{");
        while (!is("}")) {
            if (token.kind != Kind::Name)
                fail(token, "expected event handler");
            auto event = token.text;
            auto at = token;
            const auto mask_bit = std::ranges::find(event_mask_bits, event,
                                                    &std::pair<std::string_view, unsigned>::first);
            if (mask_bit == std::ranges::end(event_mask_bits))
                fail(at, "unsupported event in native LSL subset");
            advance();
            if (state.handlers.contains(event))
                fail(at, "duplicate event handler");
            auto [it, inserted] = state.handlers.emplace(event, Handler{});
            handler = &it->second;
            scopes.emplace_back();
            require("(");
            const auto parameter = [&](Type type) {
                require(type == Type::Integer ? "integer" : type == Type::Key ? "key"
                                               : type == Type::Vector ? "vector"
                                               : type == Type::Rotation ? "rotation"
                                               : type == Type::List ? "list" : "string");
                if (token.kind != Kind::Name)
                    fail(token, "expected event parameter name");
                if (std::string_view(
                        " default do else float for if integer list jump key return rotation state string vector while llSay llOwnerSay ")
                        .find(" " + token.text + " ") != std::string_view::npos)
                    fail(token, "unsupported/reserved event parameter name");
                if (!scopes.back().emplace(token.text, Local{type, handler->local_types.size()}).second)
                    fail(token, "duplicate event parameter");
                handler->local_types.push_back(type);
                advance();
            };
            if (event == "control") {
                parameter(Type::Key);
                require(",");
                parameter(Type::Integer);
                require(",");
                parameter(Type::Integer);
            } else if (event == "dataserver" || event == "http_request") {
                parameter(Type::Key);
                require(",");
                parameter(Type::String);
                if (event == "http_request") {
                    require(",");
                    parameter(Type::String);
                }
            } else if (event == "remote_data") {
                parameter(Type::Integer);
                require(",");
                parameter(Type::Key);
                require(",");
                parameter(Type::Key);
                require(",");
                parameter(Type::String);
                require(",");
                parameter(Type::Integer);
                require(",");
                parameter(Type::String);
            } else if (event == "http_response") {
                parameter(Type::Key);
                require(",");
                parameter(Type::Integer);
                require(",");
                parameter(Type::List);
                require(",");
                parameter(Type::String);
            } else if (event == "attach" || event == "object_rez") {
                parameter(Type::Key);
            } else if (event == "email") {
                parameter(Type::String);
                for (int field = 0; field < 3; ++field) {
                    require(",");
                    parameter(Type::String);
                }
                require(",");
                parameter(Type::Integer);
            } else if (event == "listen") {
                parameter(Type::Integer);
                require(",");
                parameter(Type::String);
                require(",");
                parameter(Type::Key);
                require(",");
                parameter(Type::String);
            } else if (is_land_collision_event(event)) {
                parameter(Type::Vector);
            } else if (event != "state_entry" && event != "timer" && event != "state_exit" &&
                       event != "not_at_target" && event != "not_at_rot_target" && event != "no_sensor") {
                parameter(Type::Integer);
            }
            if (event == "at_target" || event == "at_rot_target") {
                const auto type = event == "at_target" ? Type::Vector : Type::Rotation;
                require(",");
                parameter(type);
                require(",");
                parameter(type);
            }
            if (event == "linkset_data") {
                require(",");
                parameter(Type::String);
                require(",");
                parameter(Type::String);
            }
            if (event == "link_message") {
                require(",");
                parameter(Type::Integer);
                require(",");
                parameter(Type::String);
                require(",");
                parameter(Type::Key);
            }
            require(")");
            handler->parameter_count = handler->local_types.size();
            block(true);
            resolve_jumps();
            scopes.pop_back();
            handler = nullptr;
            state.event_mask |= 1ull << mask_bit->second;
        }
        require("}");
        state.handlers.try_emplace("state_entry");
        state.handlers.try_emplace("state_exit");
    }
};
// SDK StringToBytesNoTerm excludes one existing trailing NUL and truncates at
// a UTF-8 scalar boundary. Runtime strings use UTF-8; no byte splitting occurs.
std::string say_text(std::string text) {
    if (auto zero = text.find(char(0)); zero != std::string::npos)
        text.resize(zero);
    if (text.size() <= 1023)
        return text;
    std::size_t n = 1023;
    while (n > 0 && (static_cast<unsigned char>(text[n]) & 0xc0) == 0x80)
        --n;
    text.resize(n);
    return text;
}
std::int64_t ticks() {
    using Tick = std::chrono::duration<std::int64_t, std::ratio<1, 10000000>>;
    return std::chrono::duration_cast<Tick>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

std::uint32_t heap_value_bytes(const LslValue& value, bool list_entry = false) {
    if (std::holds_alternative<std::int32_t>(value))
        return 4;
    if (std::holds_alternative<double>(value))
        return 8;
    if (std::holds_alternative<framework::Vector3d>(value))
        return 24;
    if (std::holds_alternative<LslRotation>(value))
        return 32;
    if (const auto* text = std::get_if<std::string>(&value)) {
        const auto units = framework::utf16_replacing(*text);
        if (!list_entry && std::all_of(units.begin(), units.end(), framework::unicode_whitespace))
            return 0;
        return static_cast<std::uint32_t>(units.size()) * 2u;
    }
    const auto& list = std::get<std::shared_ptr<const LslList>>(value);
    if (!list)
        return 0;
    auto bytes = static_cast<std::uint32_t>(list->items.size() * sizeof(void*));
    for (const auto& item : list->items)
        bytes += heap_value_bytes(item, true);
    return bytes;
}

class OutOfHeap final : public std::runtime_error {
public:
    OutOfHeap(std::uint64_t old_bytes, std::uint64_t new_bytes, std::int32_t limit)
        : std::runtime_error("OutOfHeap: oldtotal=" + std::to_string(old_bytes) +
                             ", newtotal=" + std::to_string(new_bytes) +
                             ", limit=" + std::to_string(limit)) {}
};
}
std::string lsl_vector_string(const framework::Vector3d& value) {
    return value_text(value);
}

std::string lsl_date_from_unix(std::int32_t seconds) {
    return utc_text(621355968000000000LL + std::int64_t(seconds) * 10000000, false);
}

LslCompileResult compile_lsl(std::string_view source) {
    try {
        auto program = Parser(source).parse();
        program->source_hash = framework::md5(source);
        return {std::move(program), {}};
    } catch (const CompileFailure& e) {
        return {nullptr, {e.diagnostic}};
    }
}
struct LslInstance::Impl {
    std::shared_ptr<const LslProgram> program;
    LslContext context;
    LslHost host;
    std::deque<LslEvent> queue;
    std::map<std::string, std::int64_t> counts;
    std::optional<LslEvent> active;
    std::size_t pc = 0;
    std::vector<LslValue> locals, globals;
    const ScriptState* state = nullptr;
    const ScriptState* next_state = nullptr;
    const auto& handlers() const {
        return state->handlers;
    }
    std::size_t initialized_globals = 0;
    double minimum_event_delay = 0.;
    std::optional<double> next_event_time;
    scene::ScriptExecutionControl control;
    std::int64_t last_say_check = 0, sleep_until = 0, timer_start = 0;
    std::int64_t timer_interval = 0, next_timer = 0;
    bool sleep_requested = false;
    std::uint64_t say_count = 0;
    std::string fault;
    std::uint64_t global_heap_bytes() const {
        std::uint64_t bytes = 0;
        for (std::size_t i = 0; i < globals.size(); ++i) {
            switch (program->globals[i].store_type) {
            case Type::Integer:
                bytes += 4;
                break;
            case Type::Float:
                bytes += 8;
                break;
            case Type::Vector:
                bytes += 24;
                break;
            case Type::Rotation:
                bytes += 32;
                break;
            default:
                if (i < initialized_globals &&
                    (std::holds_alternative<std::string>(globals[i]) ||
                     std::holds_alternative<std::shared_ptr<const LslList>>(globals[i])))
                    bytes += heap_value_bytes(globals[i]);
                break;
            }
        }
        return bytes;
    }
    std::uint64_t local_heap_bytes() const {
        std::uint64_t bytes = 0;
        if (active) {
            const auto& types = handlers().at(active->name).local_types;
            for (std::size_t i = 0; i < locals.size(); ++i)
                if (types[i] == Type::String || types[i] == Type::List)
                    bytes += heap_value_bytes(locals[i]);
        }
        return bytes;
    }
    std::int32_t heap_bytes_used() const {
        return wrap(static_cast<std::uint32_t>(global_heap_bytes() + local_heap_bytes()));
    }
    void check_heap_limit(std::uint64_t old_bytes, std::uint64_t new_bytes) const {
        if (host.heap_limit_bytes < 0 || new_bytes > static_cast<std::uint64_t>(host.heap_limit_bytes))
            throw OutOfHeap(old_bytes, new_bytes, host.heap_limit_bytes);
    }
    void check_heap_store(const LslValue& slot, const LslValue& value, Type type, bool global) const {
        // YEngine stores global keys in its string array. Its local key type
        // has no heap tracker, unlike local strings and lists.
        if (type != Type::String && type != Type::List && !(global && type == Type::Key))
            return;
        const auto previous = slot.index() == value.index() ? heap_value_bytes(slot) : 0;
        const auto next = heap_value_bytes(value);
        if (next == previous)
            return;
        auto total = global_heap_bytes();
        if (!global)
            total += local_heap_bytes();
        check_heap_limit(total, total - previous + next);
    }
    void initialize_event_locals() {
        const auto& types = handlers().at(active->name).local_types;
        locals.clear();
        locals.reserve(types.size());
        for (const auto type : types)
            locals.push_back(default_value(type));
        for (std::size_t i = 0; i < active->arguments.size(); ++i) {
            const auto& argument = active->arguments[i];
            check_heap_store(locals[i], argument, types[i], false);
            locals[i] = argument;
        }
    }
    std::int64_t now() const {
        return host.clock_ticks ? host.clock_ticks() : ticks();
    }
    void reset(bool constructing = false) {
        if (!constructing && host.clear_listeners)
            host.clear_listeners(context.item);
        if (!constructing && host.clear_targets)
            host.clear_targets(context.item);
        if (!constructing && host.clear_sensors)
            host.clear_sensors(context.item);
        if (!constructing && host.clear_script_permissions)
            host.clear_script_permissions(context.item);
        state = &program->states.at("default");
        next_state = nullptr;
        initialized_globals = 0;
        minimum_event_delay = 0.;
        next_event_time.reset();
        globals.assign(program->globals.size(), std::int32_t{0});
        locals.clear();
        queue.clear();
        counts.clear();
        active.reset();
        pc = 0;
        sleep_until = 0;
        sleep_requested = false;
        timer_interval = 0;
        fault.clear();
        control.pending_events = 0;
        control.active_event = false;
        if (constructing)
            control.state = scene::ScriptExecutionState::Construct;
        else if (control.state != scene::ScriptExecutionState::Suspended)
            control.state = scene::ScriptExecutionState::Idle;
        post({"state_entry", {}, {}});
        if (constructing)
            control.state = control.running ? (queue.empty() ? scene::ScriptExecutionState::Idle
                                                             : scene::ScriptExecutionState::OnStartQueue)
                                            : scene::ScriptExecutionState::Suspended;
    }
    void set_running(bool value) {
        control.active_event = active.has_value();
        control.pending_events = queue.size();
        scene::ScriptExecutionQueues queues;
        queues.queue_yield = [] {};
        queues.queue_start = [] {};
        queues.remove_sleep = [] {};
        queues.clear_events = [this] {
            queue.clear();
            counts.clear();
        };
        scene::script_set_running(control, value, queues);
    }

    bool throttle_event(const LslEvent& event) {
        if (minimum_event_delay == 0.)
            return false;
        if (event.name == "changed") {
            constexpr int scale_or_position = 8 | 32768;
            const auto flags = std::get<std::int32_t>(event.arguments.front());
            if (flags == 0)
                return true;
            if (flags & ~scale_or_position)
                return false;
        } else if (event.name != "timer" && event.name != "touch" && event.name != "not_at_target" &&
                   event.name != "not_at_rot_target" && event.name != "sensor" && event.name != "no_sensor")
            return false;

        const auto time = (host.monotonic_ticks ? host.monotonic_ticks() : ticks()) / 10000000.;
        if (next_event_time && time < *next_event_time)
            return true;
        next_event_time = time + minimum_event_delay;
        return false;
    }
    bool post(LslEvent event) {
        control.pending_events = queue.size();
        auto gate = scene::stopped_script_event(control, handlers().contains(event.name),
                                                handlers().contains(event.name), event.name == "state_entry");
        if (gate == scene::StoppedScriptEvent::Reject)
            return false;
        if (gate == scene::StoppedScriptEvent::Ignore)
            return true;
        if (gate == scene::StoppedScriptEvent::QueueStateEntry) {
            queue.push_front(std::move(event));
            control.pending_events = queue.size();
            return true;
        }
        const bool dataserver = event.name == "dataserver";
        const bool http_request = event.name == "http_request";
        const bool http_response = event.name == "http_response";
        const bool remote_data = event.name == "remote_data";
        const bool link_message = event.name == "link_message";
        const bool listen = event.name == "listen";
        const bool email = event.name == "email";
        const bool attachment = event.name == "attach";
        const bool script_control = event.name == "control";
        const auto expected = remote_data ? 6u : email ? 5u
                        : link_message || listen || http_response ? 4u
                        : event.name == "linkset_data" || event.name == "at_target" ||
                          event.name == "at_rot_target" || http_request || script_control ? 3u
                        : dataserver ? 2u
                        : event.name == "state_entry" || event.name == "state_exit" || event.name == "timer" ||
                          event.name == "not_at_target" || event.name == "not_at_rot_target" || event.name == "no_sensor"
                            ? 0u
                            : 1u;
        if (event.arguments.size() != expected)
            return false;
        if (dataserver || http_request || attachment || event.name == "object_rez") {
            if (!std::all_of(event.arguments.begin(), event.arguments.end(), [](const auto& value) {
                    return std::holds_alternative<std::string>(value);
                }))
                return false;
        } else if (remote_data) {
            for (std::size_t i = 0; i < event.arguments.size(); ++i) {
                const auto& argument = event.arguments[i];
                if (i == 0 || i == 4) {
                    if (!std::holds_alternative<std::int32_t>(argument))
                        return false;
                } else if (!std::holds_alternative<std::string>(argument)) {
                    return false;
                }
            }
        } else if (http_response) {
            const auto* metadata = std::get_if<std::shared_ptr<const LslList>>(&event.arguments[2]);
            if (!std::holds_alternative<std::string>(event.arguments[0]) ||
                !std::holds_alternative<std::int32_t>(event.arguments[1]) ||
                !metadata || !*metadata ||
                !std::holds_alternative<std::string>(event.arguments[3]))
                return false;
        } else if (script_control) {
            if (!std::holds_alternative<std::string>(event.arguments[0]) ||
                !std::holds_alternative<std::int32_t>(event.arguments[1]) ||
                !std::holds_alternative<std::int32_t>(event.arguments[2]))
                return false;
        } else if (email) {
            if (!std::holds_alternative<std::int32_t>(event.arguments.back()) ||
                !std::all_of(event.arguments.begin(), event.arguments.end() - 1, [](const auto& value) {
                    return std::holds_alternative<std::string>(value);
                }))
                return false;
        } else if (listen) {
            if (!std::holds_alternative<std::int32_t>(event.arguments[0]) ||
                !std::all_of(event.arguments.begin() + 1, event.arguments.end(), [](const auto& value) {
                    return std::holds_alternative<std::string>(value);
                }))
                return false;
        } else if (link_message) {
            if (!std::holds_alternative<std::int32_t>(event.arguments[0]) ||
                !std::holds_alternative<std::int32_t>(event.arguments[1]) ||
                !std::holds_alternative<std::string>(event.arguments[2]) ||
                !std::holds_alternative<std::string>(event.arguments[3]))
                return false;
        } else if (event.name == "at_target") {
            if (!std::holds_alternative<std::int32_t>(event.arguments[0]) ||
                !std::holds_alternative<framework::Vector3d>(event.arguments[1]) ||
                !std::holds_alternative<framework::Vector3d>(event.arguments[2]))
                return false;
        } else if (event.name == "at_rot_target") {
            if (!std::holds_alternative<std::int32_t>(event.arguments[0]) ||
                !std::holds_alternative<LslRotation>(event.arguments[1]) ||
                !std::holds_alternative<LslRotation>(event.arguments[2]))
                return false;
        } else if (is_land_collision_event(event.name)) {
            if (!std::holds_alternative<framework::Vector3d>(event.arguments[0]))
                return false;
        } else {
            if (expected && !std::holds_alternative<std::int32_t>(event.arguments[0]))
                return false;
            if (expected == 3 && (!std::holds_alternative<std::string>(event.arguments[1]) ||
                                  !std::holds_alternative<std::string>(event.arguments[2])))
                return false;
        }
        if (throttle_event(event))
            return false;
        if (counts[event.name] >= (event.name == "timer" ? 1 : 64))
            return false;
        ++counts[event.name];
        if (event.name == "state_entry")
            queue.push_front(std::move(event));
        else if (attachment && std::get<std::string>(event.arguments.front()) == framework::Uuid{}.str()) {
            const auto position = std::ranges::find_if(queue, [](const auto& pending) {
                return pending.name != "state_entry" && pending.name != "attach";
            });
            queue.insert(position, std::move(event));
        } else
            queue.push_back(std::move(event));
        control.pending_events = queue.size();
        if (control.state == scene::ScriptExecutionState::Idle)
            control.state = scene::ScriptExecutionState::OnStartQueue;
        return true;
    }

    void set_timer(double seconds) {
        if (seconds == 0.) {
            if (timer_interval) {
                std::erase_if(queue, [](const auto& event) { return event.name == "timer"; });
                counts["timer"] = 0;
                control.pending_events = queue.size();
            }
            timer_interval = 0;
            return;
        }
        const auto ticks = std::max(seconds, double(host.min_timer_interval)) * 10000000.;
        if (!std::isfinite(ticks) || ticks < 1. || ticks >= 9223372036854775808.)
            return;
        timer_interval = static_cast<std::int64_t>(ticks);
        advance_timer(now());
    }
    void advance_timer(std::int64_t time) {
        constexpr auto limit = std::numeric_limits<std::int64_t>::max();
        next_timer = time > limit - timer_interval ? limit : time + timer_interval;
    }
    void schedule_sleep(std::int32_t delay, std::int64_t time) {
        sleep_until = time + std::int64_t(delay) * 10000;
        sleep_requested = true;
        if (host.request_sleep)
            host.request_sleep(delay);
    }
    void reset_from_script() {
        // YEngine ignores a self-reset during default state_entry, after
        // the LSL API has released the script's URLs.
        if (active->name == "state_entry" && state == &program->states.at("default")) {
            if (host.release_script_urls)
                host.release_script_urls(context.item);
            return;
        }
        reset();
    }
    void execute(const Instruction& instruction) {
        if (instruction.open_remote_data_channel) {
            if (host.open_remote_data_channel) {
                const auto channel = host.open_remote_data_channel(context.object, context.item);
                if (channel)
                    post({"remote_data", {std::int32_t{1}, channel->str(), framework::Uuid{}.str(),
                                         std::string{}, std::int32_t{0}, std::string{}}, {}});
            }
            const auto milliseconds = 1000.f * host.script_delay_factor;
            if (milliseconds >= 10 && double(milliseconds) <= std::numeric_limits<std::int32_t>::max())
                schedule_sleep(static_cast<std::int32_t>(milliseconds), now());
            return;
        }
        if (instruction.reset_script) {
            reset_from_script();
            return;
        }
        if (!instruction.target_state.empty()) {
            const auto* target = &program->states.at(instruction.target_state);
            if (active->name == "state_exit")
                next_state = target;
            else if (target != state) {
                next_state = target;
                active = LslEvent{"state_exit", {}, {}};
                pc = 0;
                initialize_event_locals();
                return;
            }
            pc = handlers().at(active->name).instructions.size();
            return;
        }
        if (instruction.return_event) {
            pc = handlers().at(active->name).instructions.size();
            return;
        }
        const Evaluator evaluator{locals,
                                  globals,
                                  active ? &*active : nullptr,
                                  context,
                                  host,
                                  timer_start,
                                  [this](std::int32_t milliseconds) { schedule_sleep(milliseconds, now()); },
                                  [this] { return heap_bytes_used(); },
                                  [this](const LslValue& slot, const LslValue& value, Type type, bool global) {
                                      check_heap_store(slot, value, type, global);
                                  }};
        if (instruction.close_remote_data_channel) {
            const auto channel = std::get<std::string>(evaluator.evaluate(*instruction.close_remote_data_channel));
            if (host.close_remote_data_channel)
                host.close_remote_data_channel(context.item, channel);
            const auto milliseconds = 1000.f * host.script_delay_factor;
            if (milliseconds >= 10 && double(milliseconds) <= std::numeric_limits<std::int32_t>::max())
                schedule_sleep(static_cast<std::int32_t>(milliseconds), now());
            return;
        }
        if (instruction.script_control_name) {
            const auto name = std::get<std::string>(evaluator.evaluate(*instruction.script_control_name));
            const auto* run = instruction.script_control_run.get();
            const bool running = run && std::get<std::int32_t>(evaluator.evaluate(*run)) != 0;
            const auto item = host.find_script ? host.find_script(context.object, name) : framework::Uuid{};
            if (item.is_zero()) {
                const auto* command = run ? "llSetScriptState" : "llResetOtherScript";
                evaluator.script_error(std::string(command) + ": Can't find script '" + name + "'");
            } else if (run) {
                if (host.set_script_running)
                    host.set_script_running(item, running);
            } else if (item == context.item) {
                reset_from_script();
            } else if (host.reset_other_script) {
                host.reset_other_script(item);
            }
            return;
        }
        if (instruction.unsit_avatar) {
            const auto key = std::get<std::string>(evaluator.evaluate(*instruction.unsit_avatar));
            framework::Uuid avatar;
            if (host.unsit && framework::try_parse_sdk_uuid(key, avatar) && !avatar.is_zero())
                host.unsit(context.object, avatar);
            return;
        }
        if (instruction.force_sit_avatar) {
            const auto avatar_key = std::get<std::string>(evaluator.evaluate(*instruction.force_sit_avatar));
            const auto target_key = instruction.force_sit_target
                ? std::get<std::string>(evaluator.evaluate(*instruction.force_sit_target))
                : std::string{};
            evaluator.check_ossl_permission("osForceOtherSit");
            if (!host.force_sit)
                return;

            auto target = context.object;
            if (instruction.force_sit_target && !framework::try_parse_sdk_uuid(target_key, target))
                return;
            framework::Uuid avatar;
            if (framework::try_parse_sdk_uuid(avatar_key, avatar) && !avatar.is_zero())
                host.force_sit(avatar, target);
            return;
        }
        if (instruction.sun_hour) {
            evaluator.evaluate(*instruction.sun_hour);
            evaluator.evaluate(*instruction.sun_fixed);
            // The current estate API evaluates its arguments and has an empty body.
            if (instruction.estate_sun)
                return;
            const bool use_estate = std::get<std::int32_t>(evaluator.evaluate(*instruction.sun_use_estate)) != 0;
            evaluator.check_ossl_permission("osSetRegionSunSettings");
            // RegionSettings ignores assignments to FixedSun and SunPosition.
            if (host.set_region_sun_settings)
                host.set_region_sun_settings(use_estate);
            return;
        }
        if (instruction.parcel_media_commands) {
            const auto commands = std::get<std::shared_ptr<const LslList>>(
                evaluator.evaluate(*instruction.parcel_media_commands));
            if (host.parcel_media_commands &&
                host.parcel_media_commands(context.object, *commands, [&](std::string message) {
                    evaluator.script_error("llParcelMediaCommandList: " + message);
                })) {
                const auto requested = 2000.f * host.script_delay_factor;
                if (requested >= 10 && requested < 2147483648.f)
                    schedule_sleep(static_cast<std::int32_t>(requested), now());
            }
            return;
        }
        if (instruction.parcel_music_url) {
            auto url = std::get<std::string>(evaluator.evaluate(*instruction.parcel_music_url));
            if (host.set_parcel_music_url && host.set_parcel_music_url(context.object, std::move(url))) {
                const auto requested = 2000.f * host.script_delay_factor;
                if (requested >= 10 && requested < 2147483648.f)
                    schedule_sleep(static_cast<std::int32_t>(requested), now());
            }
            return;
        }
        if (instruction.region_water_height) {
            const auto height = list_float(evaluator.evaluate(*instruction.region_water_height));
            evaluator.check_ossl_permission("osSetRegionWaterHeight");
            if (std::abs(height) <= std::numeric_limits<float>::max() && host.set_region_water_height) {
                const auto water_height = static_cast<float>(height);
                if (water_height >= 0)
                    host.set_region_water_height(water_height);
            }
            return;
        }
        if (instruction.sit_flags_link) {
            // The source setter ignores both values after evaluating its arguments.
            evaluator.evaluate(*instruction.sit_flags_value);
            evaluator.evaluate(*instruction.sit_flags_link);
            return;
        }
        if (instruction.seat_camera_eye || instruction.seat_camera_at || instruction.seat_camera_mouselook) {
            const auto vector = [&](const auto& expression) -> std::optional<framework::Vector3d> {
                if (!expression)
                    return std::nullopt;
                return std::get<framework::Vector3d>(evaluator.evaluate(*expression));
            };
            const auto at = vector(instruction.seat_camera_at);
            const auto eye = vector(instruction.seat_camera_eye);
            std::optional<bool> mouselook;
            if (instruction.seat_camera_mouselook)
                mouselook = std::get<std::int32_t>(evaluator.evaluate(*instruction.seat_camera_mouselook)) != 0;
            const auto link = instruction.seat_camera_link
                ? std::get<std::int32_t>(evaluator.evaluate(*instruction.seat_camera_link)) : -4;
            if (host.set_seat_camera)
                host.set_seat_camera(context.object, link, eye, at, mouselook);
            return;
        }
        if (instruction.stand_target_value) {
            const auto offset = std::get<framework::Vector3d>(evaluator.evaluate(*instruction.stand_target_value));
            const auto link = instruction.stand_target_link
                ? std::get<std::int32_t>(evaluator.evaluate(*instruction.stand_target_link)) : -4;
            if (host.set_stand_target)
                host.set_stand_target(context.object, link, offset);
            return;
        }
        if (instruction.sit_range_value) {
            const auto range = static_cast<float>(list_float(evaluator.evaluate(*instruction.sit_range_value)));
            const auto link = instruction.sit_range_link
                ? std::get<std::int32_t>(evaluator.evaluate(*instruction.sit_range_link)) : -4;
            if (host.set_sit_active_range)
                host.set_sit_active_range(context.object, link, std::min(range, 128.f));
            return;
        }
        if (instruction.sit_target_position) {
            auto rotation = std::get<LslRotation>(evaluator.evaluate(*instruction.sit_target_rotation));
            const auto position =
                std::get<framework::Vector3d>(evaluator.evaluate(*instruction.sit_target_position));
            const auto link = instruction.sit_target_link
                ? std::get<std::int32_t>(evaluator.evaluate(*instruction.sit_target_link)) : -4;
            if (rotation == LslRotation{0, 0, 0, 0})
                rotation.w = 1;
            if (host.set_sit_target)
                host.set_sit_target(context.object, link, position, rotation);
            return;
        }
        if (instruction.allow_inventory_drop) {
            const auto allowed = std::get<std::int32_t>(evaluator.evaluate(*instruction.allow_inventory_drop));
            if (host.allow_inventory_drop)
                host.allow_inventory_drop(context.object, allowed != 0);
            return;
        }
        if (instruction.status_mask) {
            const auto mask = std::get<std::int32_t>(evaluator.evaluate(*instruction.status_mask));
            const auto enabled = std::get<std::int32_t>(evaluator.evaluate(*instruction.status_value)) != 0;
            if (host.set_object_status)
                host.set_object_status(context.object, mask, enabled);
            return;
        }
        if (instruction.inertia_shape) {
            using Shape = Instruction::InertiaShape;
            const auto shape = *instruction.inertia_shape;
            std::vector<LslValue> values;
            for (const auto& argument : instruction.inertia_arguments)
                values.push_back(evaluator.evaluate(*argument));
            evaluator.check_safe_ossl_permission();
            if (!host.set_inertia)
                return;
            if (shape == Shape::Clear) {
                host.set_inertia(context.object, {});
                return;
            }

            const auto scalar = [&](std::size_t index) {
                const auto& value = values[index];
                return std::holds_alternative<double>(value) ? std::get<double>(value)
                                                             : double(std::get<std::int32_t>(value));
            };
            const auto vector = [&](std::size_t index) {
                const auto [x, y, z] = std::get<framework::Vector3d>(values[index]);
                return framework::Vector3{float(x), float(y), float(z)};
            };
            if (scalar(0) < 0.)
                return;
            framework::PrimInertiaOverride data;
            data.mass = static_cast<float>(scalar(0));
            if (shape == Shape::Principal || shape == Shape::Box) {
                const auto [x, y, z] = vector(shape == Shape::Principal ? 2 : 1);
                if (shape == Shape::Principal) {
                    const auto& scaled = std::get<framework::Vector3d>(values[2]);
                    if (scaled.x < 0. || scaled.y < 0. || scaled.z < 0.)
                        return;
                    data.diagonal = {data.mass * x, data.mass * y, data.mass * z};
                    data.center = vector(1);
                } else {
                    const float factor = data.mass / 12.f;
                    data.diagonal = {factor * (y * y + z * z), factor * (x * x + z * z),
                                     factor * (x * x + y * y)};
                    data.center = vector(2);
                }
            } else {
                const float radius = static_cast<float>(scalar(1));
                if (shape == Shape::Sphere) {
                    const float axis = .4f * data.mass * radius * radius;
                    data.diagonal = {axis, axis, axis};
                    data.center = vector(2);
                } else {
                    const float length = static_cast<float>(scalar(2));
                    const float radius_squared = radius * radius;
                    const float axis = (length * length + 3.f * radius_squared) * (8.333333e-2f * data.mass);
                    data.diagonal = {axis, axis, .5f * data.mass * radius_squared};
                    data.center = vector(3);
                }
            }
            if (shape != Shape::Sphere) {
                const auto [x, y, z, w] = std::get<LslRotation>(values.back());
                // The managed general setter uses Y for both the Y and Z rotation components.
                const float qx = float(x), qy = float(y), qz = float(shape == Shape::Principal ? y : z), qw = float(w);
                const float length = std::sqrt(qx * qx + qy * qy + qz * qz + qw * qw);
                if (length > 0.f && std::isfinite(length))
                    data.rotation = {qx / length, qy / length, qz / length, qw / length};
            }
            host.set_inertia(context.object, data);
            return;
        }
        if (instruction.heal_agent) {
            const auto key = std::get<std::string>(evaluator.evaluate(*instruction.heal_agent));
            const auto rate = list_float(evaluator.evaluate(*instruction.heal_rate));
            evaluator.check_ossl_permission("osSetHealRate");
            framework::Uuid id;
            if (host.set_agent_heal_rate && framework::try_parse_sdk_uuid(key, id))
                host.set_agent_heal_rate(id, static_cast<float>(rate));
            return;
        }
        if (instruction.teleport_viewer) {
            const auto& args = instruction.teleport_arguments;
            const auto key = std::get<std::string>(evaluator.evaluate(*args[0]));
            const auto destination = evaluator.evaluate(*args[1]);
            LslViewerTeleport request;
            if (const auto name = std::get_if<std::string>(&destination))
                request.destination = *name;
            else
                request.destination = std::get<framework::Vector3d>(destination);
            request.position = std::get<framework::Vector3d>(evaluator.evaluate(*args[2]));
            request.look_at = std::get<framework::Vector3d>(evaluator.evaluate(*args[3]));
            framework::Uuid agent;
            if (host.teleport_viewer && framework::try_parse_sdk_uuid(key, agent) && !agent.is_zero())
                host.teleport_viewer(context.object, context.item, agent, request);
            return;
        }
        if (!instruction.teleport_arguments.empty()) {
            const auto& args = instruction.teleport_arguments;
            std::size_t first = 0;
            auto agent = context.owner;
            bool valid_agent = true;
            if (!instruction.teleport_owner) {
                const auto key = std::get<std::string>(evaluator.evaluate(*args[first++]));
                valid_agent = framework::try_parse_sdk_uuid(key, agent);
            }
            LslTeleport request;
            const auto destination_args = args.size() - first - 2;
            if (destination_args == 1)
                request.destination = std::get<std::string>(evaluator.evaluate(*args[first]));
            else if (destination_args == 2)
                request.destination = std::array{
                    std::get<std::int32_t>(evaluator.evaluate(*args[first])),
                    std::get<std::int32_t>(evaluator.evaluate(*args[first + 1])),
                };
            request.position = std::get<framework::Vector3d>(evaluator.evaluate(*args[args.size() - 2]));
            request.look_at = std::get<framework::Vector3d>(evaluator.evaluate(*args.back()));
            if (instruction.teleport_owner)
                evaluator.check_ossl_permission("osTeleportOwner");
            else if (destination_args != 0)
                evaluator.check_ossl_permission("osTeleportAgent");
            if (valid_agent && host.teleport_agent) {
                const float delay = host.teleport_agent(context.object, context.item, agent, request) *
                                    host.script_delay_factor;
                if (delay >= 10.f && delay < 2147483648.f)
                    schedule_sleep(static_cast<std::int32_t>(delay), now());
            }
            return;
        }
        if (instruction.local_teleport_arguments.front()) {
            const auto& args = instruction.local_teleport_arguments;
            const auto key = std::get<std::string>(evaluator.evaluate(*args[0]));
            const LslLocalTeleport request{
                std::get<framework::Vector3d>(evaluator.evaluate(*args[1])),
                std::get<framework::Vector3d>(evaluator.evaluate(*args[2])),
                std::get<framework::Vector3d>(evaluator.evaluate(*args[3])),
                std::get<std::int32_t>(evaluator.evaluate(*args[4])),
            };
            framework::Uuid agent;
            if (host.local_teleport_agent && framework::try_parse_sdk_uuid(key, agent))
                host.local_teleport_agent(context.object, context.item, agent, request);
            return;
        }
        if (instruction.relocate_agent) {
            const auto key = std::get<std::string>(evaluator.evaluate(*instruction.relocate_agent));
            const auto& relocate = instruction.eject_from_land ? host.eject_from_land : host.teleport_agent_home;
            framework::Uuid agent;
            if (framework::try_parse_sdk_uuid(key, agent) && !agent.is_zero() &&
                (relocate ? !relocate(context.object, agent) : !instruction.eject_from_land))
                return;
            const float delay = (instruction.eject_from_land ? 1000.f : 5000.f) * host.script_delay_factor;
            if (delay >= 10.f && delay < 2147483648.f)
                schedule_sleep(static_cast<std::int32_t>(delay), now());
            return;
        }
        if (instruction.render_material_name) {
            const auto name = std::get<std::string>(evaluator.evaluate(*instruction.render_material_name));
            const auto face = std::get<std::int32_t>(evaluator.evaluate(*instruction.render_material_face));
            if (host.set_render_material && !host.set_render_material(context.object, name, face))
                evaluator.script_error("llSetRenderMaterial: material \"" + name + "\" not found");
            return;
        }
        if (instruction.damage) {
            const auto value = evaluator.evaluate(*instruction.damage);
            const float damage = std::holds_alternative<double>(value)
                                     ? static_cast<float>(std::get<double>(value))
                                     : static_cast<float>(std::get<std::int32_t>(value));
            if (host.set_damage)
                host.set_damage(context.object, damage);
            return;
        }
        if (instruction.volume_detect) {
            const auto detect = std::get<std::int32_t>(evaluator.evaluate(*instruction.volume_detect));
            if (instruction.volume_detect_part)
                evaluator.check_safe_ossl_permission();
            if (host.set_volume_detect)
                host.set_volume_detect(context.object, detect != 0, instruction.volume_detect_part);
            return;
        }
        if (instruction.collision_filter) {
            const auto& filter = *instruction.collision_filter;
            const auto name = std::get<std::string>(evaluator.evaluate(*filter.name));
            const auto key = std::get<std::string>(evaluator.evaluate(*filter.key));
            const auto accept = std::get<std::int32_t>(evaluator.evaluate(*filter.accept)) != 0;
            framework::Uuid id;
            if (!framework::try_parse_sdk_uuid(key, id))
                id = {};
            if (host.set_collision_filter) {
                auto lowered_name = framework::invariant_lower_utf16(framework::utf16(name));
                host.set_collision_filter(context.object, std::move(lowered_name), id, accept);
            }
            return;
        }
        if (instruction.pass_collisions) {
            const auto pass = std::get<std::int32_t>(evaluator.evaluate(*instruction.pass_collisions));
            if (host.set_pass_collisions)
                host.set_pass_collisions(context.object, pass == 1);
            return;
        }
        if (instruction.pass_touches) {
            const auto pass = std::get<std::int32_t>(evaluator.evaluate(*instruction.pass_touches));
            if (host.set_pass_touches)
                host.set_pass_touches(context.object, pass != 0);
            return;
        }
        if (instruction.click_action) {
            const auto action = std::get<std::int32_t>(evaluator.evaluate(*instruction.click_action));
            if (!host.set_click_action)
                throw std::runtime_error("LSL host click action callback is required");
            host.set_click_action(context.object, static_cast<std::uint8_t>(action));
            return;
        }
        if (instruction.scale) {
            const auto value = std::get<framework::Vector3d>(evaluator.evaluate(*instruction.scale));
            if (!host.set_object_scale)
                throw std::runtime_error("LSL host object scale callback is required");
            if (auto scale =
                    host.set_object_scale(context.object, {float(value.x), float(value.y), float(value.z)}))
                context.object_scale = *scale;
            return;
        }
        if (instruction.pay_price) {
            std::array<std::int32_t, 5> prices;
            prices.fill(-1);
            prices[0] = std::get<std::int32_t>(evaluator.evaluate(*instruction.pay_price));
            const auto buttons =
                std::get<std::shared_ptr<const LslList>>(evaluator.evaluate(*instruction.pay_buttons));
            if (buttons)
                for (std::size_t i = 0; i < std::min(buttons->items.size(), prices.size() - 1); ++i)
                    prices[i + 1] = list_integer(buttons->items[i]);
            if (!host.set_pay_prices)
                throw std::runtime_error("LSL host pay price callback is required");
            host.set_pay_prices(context.object, prices);
            return;
        }
        const auto sound_link = instruction.sound_link
            ? std::optional(std::get<std::int32_t>(evaluator.evaluate(*instruction.sound_link)))
            : std::nullopt;
        const auto sound_parts = [&] {
            if (!sound_link)
                return std::vector{context.object};
            return host.linked_prim_parts ? host.linked_prim_parts(context.object, *sound_link)
                                          : std::vector<framework::Uuid>{};
        };
        if (instruction.sound_queueing) {
            const auto queueing = std::get<std::int32_t>(evaluator.evaluate(*instruction.sound_queueing));
            if (sound_link && !host.set_sound_queueing)
                return;
            if (!host.set_sound_queueing)
                throw std::runtime_error("LSL host sound queueing callback is required");
            for (const auto part : sound_parts())
                host.set_sound_queueing(part, queueing == 1);
            return;
        }
        if (instruction.preload_sound) {
            const auto name = std::get<std::string>(evaluator.evaluate(*instruction.preload_sound));
            if (!host.preload_sound)
                throw std::runtime_error("LSL host sound preload callback is required");
            if (host.preload_sound(context.object, name))
                schedule_sleep(1000, now());
            return;
        }
        if (instruction.sound_radius) {
            const auto radius = evaluator.evaluate(*instruction.sound_radius);
            const double value = std::holds_alternative<double>(radius) ? std::get<double>(radius)
                                                                        : std::get<std::int32_t>(radius);
            if (!host.set_sound_radius)
                throw std::runtime_error("LSL host sound radius callback is required");
            for (const auto part : sound_parts())
                host.set_sound_radius(part, static_cast<float>(value));
            return;
        }
        if (instruction.adjust_sound_gain) {
            const auto gain = evaluator.evaluate(*instruction.adjust_sound_gain);
            const double value =
                std::holds_alternative<double>(gain) ? std::get<double>(gain) : std::get<std::int32_t>(gain);
            if (!host.adjust_sound_gain)
                throw std::runtime_error("LSL host sound gain callback is required");
            for (const auto part : sound_parts())
                host.adjust_sound_gain(part, static_cast<float>(std::clamp(value, 0., 1.)));
            schedule_sleep(100, now());
            return;
        }
        if (instruction.sound) {
            if (sound_link && instruction.sound_gain) {
                const auto name = std::get<std::string>(evaluator.evaluate(*instruction.text));
                const auto gain = evaluator.evaluate(*instruction.sound_gain);
                const auto volume = std::holds_alternative<double>(gain) ? std::get<double>(gain)
                                                                         : std::get<std::int32_t>(gain);
                const auto mode = instruction.sound_mode
                    ? std::get<std::int32_t>(evaluator.evaluate(*instruction.sound_mode)) : 0;
                framework::Uuid asset;
                if (host.resolve_sound_asset)
                    asset = host.resolve_sound_asset(context.object, name);
                else
                    framework::try_parse_sdk_uuid(name, asset);
                if (asset.is_zero())
                    return;
                const auto asset_key = asset.str();
                for (const auto part : sound_parts()) {
                    switch (mode) {
                    case 0:
                    case 2:
                    case 4:
                        if (host.play_sound)
                            host.play_sound(part, asset_key, volume, mode == 4 ? 4 : 0, mode == 2, {});
                        break;
                    case 1:
                    case 5:
                        if (host.loop_sound)
                            host.loop_sound(part, asset_key, volume, mode == 5 ? 4 : 0);
                        break;
                    }
                }
                return;
            }
            if (instruction.collision_sound) {
                const auto name = std::get<std::string>(evaluator.evaluate(*instruction.text));
                const auto gain = evaluator.evaluate(*instruction.sound_gain);
                const double volume = std::holds_alternative<double>(gain) ? std::get<double>(gain)
                                                                           : std::get<std::int32_t>(gain);
                if (!host.set_collision_sound)
                    throw std::runtime_error("LSL host collision sound callback is required");
                if (instruction.ossl_collision_sound)
                    evaluator.check_safe_ossl_permission();
                host.set_collision_sound(context.object, name, volume, instruction.ossl_collision_sound);
                return;
            }
            if (instruction.play_sound) {
                const auto name = std::get<std::string>(evaluator.evaluate(*instruction.text));
                const auto gain = evaluator.evaluate(*instruction.sound_gain);
                if (!host.play_sound)
                    throw std::runtime_error("LSL host attached sound callback is required");
                std::optional<LslHost::SoundBounds> bounds;
                if (instruction.sound_max) {
                    const auto convert = [](framework::Vector3d v) {
                        return framework::Vector3{static_cast<float>(v.x), static_cast<float>(v.y),
                                                  static_cast<float>(v.z)};
                    };
                    const auto max =
                        convert(std::get<framework::Vector3d>(evaluator.evaluate(*instruction.sound_max)));
                    const auto min =
                        convert(std::get<framework::Vector3d>(evaluator.evaluate(*instruction.sound_min)));
                    bounds.emplace(min, max);
                }
                host.play_sound(context.object, name,
                                std::holds_alternative<double>(gain) ? std::get<double>(gain)
                                                                     : std::get<std::int32_t>(gain),
                                instruction.sound_sync_flags, instruction.trigger_sound, bounds);
                return;
            }
            if (sound_link && !host.loop_sound)
                return;
            if (!host.loop_sound)
                throw std::runtime_error("LSL host sound callback is required");
            if (instruction.sound_gain) {
                const auto name = std::get<std::string>(evaluator.evaluate(*instruction.text));
                const auto gain = evaluator.evaluate(*instruction.sound_gain);
                host.loop_sound(context.object, name,
                                std::holds_alternative<double>(gain) ? std::get<double>(gain)
                                                                     : std::get<std::int32_t>(gain),
                                instruction.sound_sync_flags);
            } else {
                for (const auto part : sound_parts())
                    host.loop_sound(part, std::nullopt, 0., 0);
            }
            return;
        }
        if (instruction.text_color) {
            auto text = std::get<std::string>(evaluator.evaluate(*instruction.text));
            const auto color = std::get<framework::Vector3d>(evaluator.evaluate(*instruction.text_color));
            const auto alpha = evaluator.evaluate(*instruction.text_alpha);
            const auto channel = [](double value) {
                const auto clamped = std::clamp(static_cast<float>(value), 0.f, 1.f);
                return std::isnan(clamped) ? 0u : static_cast<std::uint32_t>(clamped * 255.f);
            };
            const auto opacity = std::holds_alternative<double>(alpha) ? std::get<double>(alpha)
                                                                       : std::get<std::int32_t>(alpha);
            if (!host.set_floating_text)
                throw std::runtime_error("LSL host floating text callback is required");
            host.set_floating_text(context.object, std::move(text),
                                   (channel(opacity) << 24) | (channel(color.x) << 16) |
                                       (channel(color.y) << 8) | channel(color.z));
            return;
        }
        if (instruction.sit_label) {
            const auto text = std::get<std::string>(evaluator.evaluate(*instruction.text));
            if (!host.set_interaction_label)
                throw std::runtime_error("LSL host interaction label callback is required");
            host.set_interaction_label(context.object, *instruction.sit_label,
                                       framework::truncate_utf16(text, 9));
            return;
        }
        if (instruction.set_metadata) {
            auto text = std::get<std::string>(evaluator.evaluate(*instruction.text));
            if (instruction.description && text.size() > 127) {
                std::size_t end = 127;
                while ((static_cast<unsigned char>(text[end]) & 0xc0) == 0x80)
                    --end;
                text.resize(end);
            }
            auto& current = instruction.description ? context.object_description : context.object_name;
            if (text != current) {
                if (!host.set_object_text)
                    throw std::runtime_error("LSL host object text callback is required");
                if (host.set_object_text(context.object, instruction.description, text))
                    current = std::move(text);
            }
            return;
        }
        if (instruction.discard) {
            evaluator.evaluate(*instruction.text);
            return;
        }
        if (instruction.store) {
            LslValue value = default_value(instruction.store_type);
            if (instruction.text)
                value = evaluator.evaluate(*instruction.text);
            if (instruction.store_type == Type::Float) {
                if (const auto* integer = std::get_if<std::int32_t>(&value))
                    value = double(*integer);
            }
            auto& slot = (instruction.global ? globals : locals).at(*instruction.store);
            check_heap_store(slot, value, instruction.store_type, instruction.global);
            slot = std::move(value);
            return;
        }
        if (instruction.jump) {
            if (!instruction.condition || truth(evaluator.evaluate(*instruction.condition),
                                                instruction.condition->type) == instruction.jump_when_true)
                pc = *instruction.jump;
            return;
        }
        if (instruction.parcel_list_action) {
            framework::Uuid avatar;
            if (instruction.parcel_agent &&
                (!framework::try_parse_sdk_uuid(
                     std::get<std::string>(evaluator.evaluate(*instruction.parcel_agent)), avatar) ||
                 avatar.is_zero()))
                return;
            double hours = 0;
            if (instruction.parcel_hours) {
                const auto value = evaluator.evaluate(*instruction.parcel_hours);
                hours = std::holds_alternative<double>(value) ? std::get<double>(value)
                                                              : std::get<std::int32_t>(value);
            }
            if (!host.edit_parcel_list ||
                host.edit_parcel_list(context.object, *instruction.parcel_list_action,
                                      instruction.parcel_ban_list, avatar, hours))
                schedule_sleep(100, now());
            return;
        }
        if (instruction.event_delay) {
            const auto value = evaluator.evaluate(*instruction.event_delay);
            const auto seconds = std::holds_alternative<double>(value)
                                     ? std::get<double>(value)
                                     : double(std::get<std::int32_t>(value));
            minimum_event_delay = seconds > 0.001 ? seconds : 0.;
            next_event_time.reset();
            return;
        }
        if (instruction.timer) {
            const auto value = evaluator.evaluate(*instruction.timer);
            set_timer(std::holds_alternative<double>(value) ? std::get<double>(value)
                                                            : double(std::get<std::int32_t>(value)));
            return;
        }
        if (instruction.sleep) {
            const auto value = evaluator.evaluate(*instruction.sleep);
            const auto seconds = std::holds_alternative<double>(value)
                                     ? std::get<double>(value)
                                     : double(std::get<std::int32_t>(value));
            const auto milliseconds = std::trunc(seconds * 1000.);
            const auto delay = milliseconds >= -2147483648. && milliseconds <= 2147483647.
                                   ? static_cast<std::int32_t>(milliseconds)
                                   : 0;
            schedule_sleep(delay, now());
            return;
        }
        if (instruction.override_state) {
            const auto state = std::get<std::string>(evaluator.evaluate(*instruction.override_state));
            std::optional<std::string> animation;
            if (instruction.override_animation)
                animation = std::get<std::string>(evaluator.evaluate(*instruction.override_animation));
            if (host.change_animation_override)
                host.change_animation_override(context.object, context.item, state, animation);
            return;
        }
        if (instruction.rez_arguments.front()) {
            const auto name = std::get<std::string>(evaluator.evaluate(*instruction.rez_arguments[0]));
            const auto position = std::get<framework::Vector3d>(evaluator.evaluate(*instruction.rez_arguments[1]));
            const auto velocity = std::get<framework::Vector3d>(evaluator.evaluate(*instruction.rez_arguments[2]));
            const auto rotation = std::get<LslRotation>(evaluator.evaluate(*instruction.rez_arguments[3]));
            const auto parameter = std::get<std::int32_t>(evaluator.evaluate(*instruction.rez_arguments[4]));
            if (host.rez_object && host.rez_object(context.object, name, position, velocity, rotation,
                                                   parameter, instruction.rez_at_root)) {
                const auto delay = 100.f * host.script_delay_factor;
                if (delay >= 1.f && delay < 2147483648.f)
                    schedule_sleep(static_cast<std::int32_t>(delay), now());
            }
            return;
        }
        if (instruction.dialog_arguments.front()) {
            const auto& args = instruction.dialog_arguments;
            const bool text_box = !args[2];
            const auto avatar = std::get<std::string>(evaluator.evaluate(*args[0]));
            const auto message = std::get<std::string>(evaluator.evaluate(*args[1]));
            const auto buttons = args[2] ? std::get<std::shared_ptr<const LslList>>(evaluator.evaluate(*args[2]))
                                         : nullptr;
            const auto channel = std::get<std::int32_t>(evaluator.evaluate(*args[3]));
            if (!host.dialog || !host.dialog_owner_name)
                return;

            framework::Uuid target;
            if (!framework::try_parse_sdk_uuid(avatar, target) || target.is_zero()) {
                evaluator.script_error(text_box ? "llTextBox: First parameter must be a valid agent key"
                                                : "llDialog: First parameter must be a valid key");
                return;
            }
            if (text_box && (message.empty() || message.size() > 512)) {
                evaluator.script_error(message.empty() ? "llTextBox: Empty message"
                                                       : "llTextBox: Message longer than 512 bytes");
                return;
            }
            const auto owner = host.dialog_owner_name(context.object);
            if (!owner)
                return;
            if (buttons && buttons->items.size() > 12) {
                evaluator.script_error("llDialog: No more than 12 buttons can be shown");
                return;
            }
            if (message.empty())
                evaluator.script_error("llDialog: Empty message");
            else if (message.size() > 512)
                evaluator.script_error("llDialog: Message longer than 512 bytes");

            std::vector<std::string> labels;
            if (buttons) {
                for (const auto& button : buttons->items) {
                    auto label = value_text(button);
                    if (label.empty()) {
                        evaluator.script_error("llDialog: Button label cannot be blank");
                        return;
                    }
                    labels.push_back(std::move(label));
                }
            }
            if (labels.empty())
                labels.emplace_back(text_box ? "!!llTextBox!!" : "Ok");
            host.dialog(context.object, target, *owner, message, labels, channel, text_box);
            const float delay = 1000.f * host.script_delay_factor;
            if (delay >= 10.f && delay < 2147483648.f)
                schedule_sleep(static_cast<std::int32_t>(delay), now());
            return;
        }
        if (instruction.give_inventory_list_arguments.front()) {
            const auto& args = instruction.give_inventory_list_arguments;
            const auto key = std::get<std::string>(evaluator.evaluate(*args[0]));
            const auto category = std::get<std::string>(evaluator.evaluate(*args[1]));
            const auto list = std::get<std::shared_ptr<const LslList>>(evaluator.evaluate(*args[2]));
            framework::Uuid destination;
            if (!host.give_inventory_list || list->items.empty() ||
                !framework::try_parse_sdk_uuid(key, destination) || destination.is_zero())
                return;
            std::vector<std::string> names;
            names.reserve(list->items.size());
            for (const auto& item : list->items)
                names.push_back(value_text(item));
            const auto status = host.give_inventory_list(context.object, destination, category, names);
            switch (status) {
            case LslInventoryGiveStatus::MissingRecipient:
                evaluator.script_error("llGiveInventoryList: Unable to give list, destination not found");
                break;
            case LslInventoryGiveStatus::MissingItem:
                evaluator.script_error("llGiveInventoryList: Unable to give list, no items found");
                break;
            case LslInventoryGiveStatus::TransferFailed:
                evaluator.script_error("llGiveInventoryList: Unable to give list");
                break;
            case LslInventoryGiveStatus::Done:
                return;
            default:
                break;
            }
            const float delay = (status == LslInventoryGiveStatus::AvatarOffer ? 3000.f : 100.f) *
                                host.script_delay_factor;
            if (delay >= 10.f && delay < 2147483648.f)
                schedule_sleep(static_cast<std::int32_t>(delay), now());
            return;
        }
        if (instruction.instant_message_arguments.front()) {
            const auto& args = instruction.instant_message_arguments;
            const auto key = std::get<std::string>(evaluator.evaluate(*args[0]));
            const auto message = std::get<std::string>(evaluator.evaluate(*args[1]));
            if (instruction.give_inventory) {
                framework::Uuid destination;
                if (!framework::try_parse_sdk_uuid(key, destination) || destination.is_zero()) {
                    evaluator.script_error("llGiveInventory: Can't parse destination key '" + key + "'");
                    return;
                }
                if (!host.give_inventory)
                    return;
                const auto result = host.give_inventory(context.object, destination, message);
                switch (result.status) {
                case LslInventoryGiveStatus::MissingItem:
                    evaluator.script_error("llGiveInventory: Can't find inventory object '" + message + "'");
                    break;
                case LslInventoryGiveStatus::MissingRecipient:
                    evaluator.script_error("llGiveInventory: Can't find destination '" + destination.str() + "'");
                    break;
                case LslInventoryGiveStatus::TransferFailed:
                    if (host.chat && !result.message.empty())
                        host.chat({LslChatType::Say, 0, result.message, context});
                    break;
                case LslInventoryGiveStatus::AvatarOffer: {
                    const float delay = 3000.f * host.script_delay_factor;
                    if (delay >= 10.f && delay < 2147483648.f)
                        schedule_sleep(static_cast<std::int32_t>(delay), now());
                    break;
                }
                case LslInventoryGiveStatus::Done:
                case LslInventoryGiveStatus::ObjectList:
                    break;
                }
                return;
            }
            if (!host.instant_message || message.empty())
                return;
            framework::Uuid recipient;
            if (!framework::try_parse_sdk_uuid(key, recipient) || recipient.is_zero())
                evaluator.script_error("llInstantMessage: An invalid key  was passed to llInstantMessage");
            else
                host.instant_message(context.object, recipient, message);
            const float delay = 2000.f * host.script_delay_factor;
            if (delay >= 10.f && delay < 2147483648.f)
                schedule_sleep(static_cast<std::int32_t>(delay), now());
            return;
        }
        if (instruction.load_url_arguments.front()) {
            const auto& args = instruction.load_url_arguments;
            const auto avatar = std::get<std::string>(evaluator.evaluate(*args[0]));
            const auto message = std::get<std::string>(evaluator.evaluate(*args[1]));
            const auto url = std::get<std::string>(evaluator.evaluate(*args[2]));
            if (context.owner == context.group)
                return;

            const auto uri = framework::native_config_uri_object_backend()->try_create(
                std::make_shared<const std::u16string>(framework::utf16_replacing(url)),
                framework::ConfigUriObjectKind::Absolute);
            const auto scheme = uri ? uri->scheme() : nullptr;
            if (!scheme || (*scheme != u"http" && *scheme != u"https")) {
                const float delay = 200.f * host.script_delay_factor;
                evaluator.script_error(uri ? "llLoadURL: Invalid url schema" : "llLoadURL: Invalid url",
                    delay >= 10.f && delay < 2147483648.f ? static_cast<std::int32_t>(delay) : 1000);
                return;
            }

            if (host.load_url)
                host.load_url(context.object, framework::construct_sdk_uuid(framework::utf16_replacing(avatar)),
                              message, url);
            const float delay = 1000.f * host.script_delay_factor;
            if (delay >= 10.f && delay < 2147483648.f)
                schedule_sleep(static_cast<std::int32_t>(delay), now());
            return;
        }
        if (instruction.map_arguments.front()) {
            const auto name = std::get<std::string>(evaluator.evaluate(*instruction.map_arguments[0]));
            const auto position = std::get<framework::Vector3d>(evaluator.evaluate(*instruction.map_arguments[1]));
            const auto rules = evaluator.evaluate(*instruction.map_arguments[2]);
            framework::Uuid target;
            if (active && !active->detectors.empty())
                target = active->detectors.front().key;
            else if (context.attachment_point)
                target = context.owner;
            else
                return;

            std::uint32_t options = 3;
            if (instruction.map_beacon) {
                options = 0;
                const auto& entries = std::get<std::shared_ptr<const LslList>>(rules)->items;
                if (!entries.empty()) {
                    if (entries.size() != 3)
                        throw std::invalid_argument("Unknown llMapBeacon rules");
                    const auto convertible = [](const LslValue& value) {
                        return std::holds_alternative<std::int32_t>(value) || std::holds_alternative<double>(value) ||
                               std::holds_alternative<std::string>(value);
                    };
                    if (!std::ranges::all_of(entries, convertible) || list_integer(entries[0]) != 1)
                        throw std::invalid_argument("Invalid llMapBeacon rule");
                    if (list_integer(entries[1]) != 0)
                        options = list_integer(entries[2]) != 0 ? 3u : 1u;
                }
            }
            if (host.map_destination)
                host.map_destination(context.object, target, name, position, options);
            const auto delay = 1000.f * host.script_delay_factor;
            if (delay >= 10.f && delay < 2147483648.f)
                schedule_sleep(static_cast<std::int32_t>(delay), now());
            return;
        }
        if (instruction.die) {
            if (!context.attachment_point)
                throw ScriptTermination{LslRunStatus::DeleteLinkset};
            return;
        }
        if (instruction.object_permission_args.front()) {
            const auto selector = std::get<std::int32_t>(evaluator.evaluate(*instruction.object_permission_args[0]));
            const auto value = std::get<std::int32_t>(evaluator.evaluate(*instruction.object_permission_args[1]));
            if (host.set_object_permission)
                host.set_object_permission(context.object, selector, value);
            return;
        }
        if (instruction.inventory_permission_args.front()) {
            const auto& args = instruction.inventory_permission_args;
            const auto name = std::get<std::string>(evaluator.evaluate(*args[0]));
            const auto selector = std::get<std::int32_t>(evaluator.evaluate(*args[1]));
            const auto mask = std::get<std::int32_t>(evaluator.evaluate(*args[2]));
            if (host.set_inventory_permission)
                host.set_inventory_permission(context.object, name, selector, mask);
            return;
        }
        if (instruction.inventory_remove_name) {
            const auto name = std::get<std::string>(evaluator.evaluate(*instruction.inventory_remove_name));
            if (host.remove_inventory && host.remove_inventory(context.object, context.item, name))
                throw ScriptTermination{LslRunStatus::DeleteSelf};
            return;
        }
        if (instruction.object_rotation) {
            const auto rotation = std::get<LslRotation>(evaluator.evaluate(*instruction.object_rotation));
            if (host.set_object_rotation)
                host.set_object_rotation(context.object, rotation, instruction.local_object_rotation);
            const auto delay = 200.f * host.script_delay_factor;
            if (delay >= 10.f && delay < 2147483648.f)
                schedule_sleep(static_cast<std::int32_t>(delay), now());
            return;
        }
        if (instruction.object_position) {
            const auto position = std::get<framework::Vector3d>(evaluator.evaluate(*instruction.object_position));
            if (host.set_object_position)
                host.set_object_position(context.object, position, false);
            const auto delay = 200.f * host.script_delay_factor;
            if (delay >= 1.f && delay < 2147483648.f)
                schedule_sleep(static_cast<std::int32_t>(delay), now());
            return;
        }
        if (instruction.animation_name) {
            if (instruction.animation_avatar) {
                const auto key = std::get<std::string>(evaluator.evaluate(*instruction.animation_avatar));
                const auto name = std::get<std::string>(evaluator.evaluate(*instruction.animation_name));
                evaluator.check_ossl_permission(
                    instruction.start_animation ? "osAvatarPlayAnimation" : "osAvatarStopAnimation");
                framework::Uuid avatar;
                if (host.ossl_avatar_animation && framework::try_parse_sdk_uuid(key, avatar))
                    host.ossl_avatar_animation(context.object, avatar, name, instruction.start_animation);
                return;
            }
            const auto name = std::get<std::string>(evaluator.evaluate(*instruction.animation_name));
            if (instruction.object_animation) {
                if (host.object_animation)
                    host.object_animation(context.object, name, instruction.start_animation);
            } else if (host.script_animation) {
                host.script_animation(context.object, context.item, name, instruction.start_animation);
            }
            return;
        }
        if (instruction.taken_controls[0]) {
            std::array<std::int32_t, 3> arguments;
            for (std::size_t index = 0; index < arguments.size(); ++index)
                arguments[index] = std::get<std::int32_t>(evaluator.evaluate(*instruction.taken_controls[index]));
            if (host.take_controls)
                host.take_controls(context.item, arguments[0], arguments[1], arguments[2]);
            return;
        }
        if (instruction.release_controls) {
            if (host.release_controls)
                host.release_controls(context.item);
            return;
        }
        if (instruction.permission_agent) {
            const auto agent = std::get<std::string>(evaluator.evaluate(*instruction.permission_agent));
            const auto permissions = std::get<std::int32_t>(evaluator.evaluate(*instruction.permission_mask));
            if (host.request_permissions)
                host.request_permissions(context.object, context.item, agent, permissions);
            return;
        }
        if (instruction.reset_linkset_data) {
            if (host.linkset_data_reset)
                host.linkset_data_reset(context.object);
            return;
        }
        if (instruction.reset_time) {
            timer_start = evaluator.timer_now();
            return;
        }
        if (instruction.omega.front()) {
            if (!host.target_omega)
                throw std::runtime_error("LSL host target omega callback is required");
            const auto number = [&](std::size_t i) {
                const auto value = evaluator.evaluate(*instruction.omega[i]);
                if (const auto* real = std::get_if<double>(&value))
                    return static_cast<float>(*real);
                return static_cast<float>(std::get<std::int32_t>(value));
            };
            const bool stop = std::abs(number(2)) < 1e-6f;
            const float spin = number(1);
            const auto axis = std::get<framework::Vector3d>(evaluator.evaluate(*instruction.omega[0]));
            const auto velocity = stop ? framework::Vector3{}
                                       : framework::Vector3{static_cast<float>(axis.x) * spin,
                                                            static_cast<float>(axis.y) * spin,
                                                            static_cast<float>(axis.z) * spin};
            host.target_omega(context.object, velocity, stop);
            context.omega = velocity;
            return;
        }
        auto text = std::get<std::string>(evaluator.evaluate(*instruction.text));
        if (instruction.owner) {
            if (context.owner == context.group)
                return;
            if (!host.chat)
                throw std::runtime_error("LSL host chat callback is required");
            host.chat({LslChatType::Owner, 0, std::move(text), context});
            return;
        }
        auto channel = std::get<std::int32_t>(evaluator.evaluate(*instruction.channel));
        if (instruction.chat_type == LslChatType::Region) {
            if (channel == 0) {
                evaluator.script_error("llRegionSay: Cannot use on channel 0");
                return;
            }
            LslChat chat{LslChatType::Region, channel, say_text(std::move(text)), context};
            if (channel == std::numeric_limits<std::int32_t>::max() && host.chat)
                host.chat(chat);
            if (host.listener)
                host.listener(chat);
            return;
        }
        auto time = now();
        if (channel == 0 && instruction.chat_type != LslChatType::Whisper) {
            if (time - last_say_check > 10000000) {
                last_say_check = time;
                say_count = 0;
            } else
                ++say_count;
        }
        if (say_count >= 11 && instruction.chat_type != LslChatType::Whisper) {
            float requested = 2000.f * host.script_delay_factor;
            if (requested >= 10 && requested < 2147483648.f) {
                auto delay = static_cast<std::int32_t>(requested);
                schedule_sleep(delay, time);
            }
        }
        if (instruction.chat_type == LslChatType::Say && host.console_channel &&
            channel == *host.console_channel) {
            if (!host.console)
                throw std::runtime_error("LSL host console callback is required");
            host.console(text);
            return;
        }
        LslChat chat{instruction.chat_type, channel, say_text(std::move(text)), context};
        if (!host.chat)
            throw std::runtime_error("LSL host chat callback is required");
        host.chat(chat);
        if (host.listener)
            host.listener(chat);
    }
};
LslInstance::LslInstance(std::shared_ptr<const LslProgram> p, LslContext c, const LslHost& h)
    : LslInstance(std::move(p), std::move(c), h, true) {
}
LslInstance::LslInstance(std::shared_ptr<const LslProgram> p, LslContext c, const LslHost& h, bool start_running)
    : impl_(std::make_unique<Impl>()) {
    if (!p)
        throw std::invalid_argument("null LSL program");
    impl_->program = std::move(p);
    impl_->context = std::move(c);
    impl_->host = h;
    impl_->last_say_check = impl_->now();
    impl_->timer_start = impl_->host.monotonic_ticks ? impl_->host.monotonic_ticks() : ticks();
    impl_->control.running = start_running;
    impl_->reset(true);
}
LslInstance::~LslInstance() = default;
LslInstance::LslInstance(LslInstance&&) noexcept = default;
LslInstance& LslInstance::operator=(LslInstance&&) noexcept = default;
void LslInstance::reset() {
    impl_->reset();
}
bool LslInstance::running() const {
    return impl_->control.running;
}
void LslInstance::running(bool value) {
    impl_->set_running(value);
}
bool LslInstance::post_event(LslEvent event) {
    return impl_->post(std::move(event));
}
std::size_t LslInstance::queued_events() const {
    return impl_->queue.size();
}
std::int32_t LslInstance::heap_bytes_used() const {
    return impl_->heap_bytes_used();
}
bool LslInstance::handles_event(std::string_view name) const {
    return impl_->handlers().contains(std::string(name));
}
std::uint64_t LslInstance::event_mask() const {
    return impl_->state->event_mask;
}
const LslEvent* LslInstance::current_event() const {
    return impl_->active ? &*impl_->active : nullptr;
}
void LslInstance::set_context(LslContext c) {
    impl_->context = std::move(c);
}
namespace {
std::int64_t ticks_between(std::int64_t earlier, std::int64_t later) {
    if (later <= earlier)
        return 0;
    if (earlier < 0 && later > std::numeric_limits<std::int64_t>::max() + earlier)
        return std::numeric_limits<std::int64_t>::max();
    return later - earlier;
}
std::int64_t ticks_after(std::int64_t now, std::int64_t interval) {
    const auto maximum = std::numeric_limits<std::int64_t>::max();
    return now > maximum - interval ? maximum : now + interval;
}
std::int64_t timer_origin(std::int64_t now, std::int64_t elapsed) {
    const auto minimum = std::numeric_limits<std::int64_t>::min();
    return now < minimum + elapsed ? minimum : now - elapsed;
}
std::string yengine_source_hash(std::string_view source) {
    Lexer lexer(source);
    std::string tokens;
    for (auto token = lexer.next(); token.kind != Kind::End; token = lexer.next()) {
        tokens.push_back('\0');
        if (token.kind == Kind::Integer) {
            tokens += std::to_string(token.integer);
        } else if (token.kind == Kind::Float) {
            std::array<char, 64> number;
            auto formatted = std::to_chars(number.data(), number.data() + number.size(), token.real,
                                           std::chars_format::scientific);
            if (formatted.ec != std::errc{})
                return {};
            const auto marker = std::find(number.data(), formatted.ptr, 'e');
            if (marker == formatted.ptr || formatted.ptr - marker < 3)
                return {};
            const bool negative = marker[1] == '-';
            int exponent = 0;
            if (std::from_chars(marker + 2, formatted.ptr, exponent).ec != std::errc{})
                return {};
            if (negative)
                exponent = -exponent;
            if (exponent >= -4 && exponent < 17) {
                formatted = std::to_chars(number.data(), number.data() + number.size(), token.real,
                                          std::chars_format::fixed);
                if (formatted.ec != std::errc{})
                    return {};
            } else {
                *marker = 'E';
            }
            tokens.append(number.data(), formatted.ptr);
        } else if (token.kind == Kind::String) {
            tokens.push_back('"');
            if (token.text.find_first_of("\"\\\n\t") == std::string::npos) {
                tokens += token.text;
                tokens.push_back('"');
            } else {
                // YEngine's escaped-string token text omits the closing quote.
                for (const auto character : token.text) {
                    if (character == '"' || character == '\\')
                        tokens.push_back('\\');
                    if (character == '\n')
                        tokens += "\\n";
                    else if (character == '\t')
                        tokens += "\\t";
                    else
                        tokens.push_back(character);
                }
            }
        } else {
            tokens += token.text;
        }
    }
    auto hash = framework::md5(tokens);
    for (auto& character : hash)
        if (character >= 'a' && character <= 'f')
            character -= 'a' - 'A';
    return hash;
}
bool checkpoint_value_matches(Type type, const LslValue& value) {
    if (type == Type::List) {
        const auto* list = std::get_if<std::shared_ptr<const LslList>>(&value);
        return list && *list;
    }
    return value.index() == default_value(type).index();
}
}
void LslInstance::restore_timer(std::int64_t interval_ticks, std::int64_t remaining_ticks) {
    if (interval_ticks <= 0)
        return;
    impl_->timer_interval = interval_ticks;
    impl_->next_timer = ticks_after(impl_->now(), std::max<std::int64_t>(remaining_ticks, 0));
}

void LslInstance::restore_script_clock(std::optional<std::int64_t> elapsed_ticks,
                                       std::optional<double> minimum_event_delay) {
    if (elapsed_ticks && *elapsed_ticks >= 0) {
        const auto now = impl_->host.monotonic_ticks ? impl_->host.monotonic_ticks() : ticks();
        impl_->timer_start = timer_origin(now, *elapsed_ticks);
    }
    if (minimum_event_delay && std::isfinite(*minimum_event_delay)) {
        impl_->minimum_event_delay = *minimum_event_delay > 0.001 ? *minimum_event_delay : 0.;
        impl_->next_event_time.reset();
    }
}

void LslInstance::restore_queued_events(std::vector<LslEvent> events) {
    auto& instance = *impl_;
    for (auto& event : events) {
        const auto handler = instance.handlers().find(event.name);
        if (handler == instance.handlers().end() ||
            event.arguments.size() != handler->second.parameter_count)
            continue;
        const auto parameters = std::span(handler->second.local_types).first(event.arguments.size());
        if (!std::ranges::equal(parameters, event.arguments, checkpoint_value_matches))
            continue;
        if (event.name == "state_entry" &&
            std::ranges::any_of(instance.queue, [](const auto& pending) { return pending.name == "state_entry"; }))
            continue;
        ++instance.counts[event.name];
        instance.queue.push_back(std::move(event));
    }
    instance.control.pending_events = instance.queue.size();
    if (!instance.queue.empty() && instance.control.state == scene::ScriptExecutionState::Idle)
        instance.control.state = scene::ScriptExecutionState::OnStartQueue;
}

bool yengine_source_matches(std::string_view source, std::string_view expected_hash) {
    return expected_hash.size() == 32 && expected_hash == yengine_source_hash(source);
}

bool LslInstance::restore_yengine_snapshot(const YEngineSnapshot& saved, std::string_view source_hash,
                                               std::string_view source) {
    const auto& program = *impl_->program;
    if (saved.state_index >= program.state_order.size() || !yengine_source_matches(source, source_hash))
        return false;
    const bool initialize_globals = saved.initialize_globals && !program.globals.empty();
    if (initialize_globals &&
        (saved.state_index != 0 || saved.next_state_index || (saved.event_code != -1 && saved.event_code != 30)))
        return false;

    // These slots follow XMRInstAbstract.SendArrays. String and key share a slot array.
    constexpr std::array<std::size_t, 7> slots{3, 2, 7, 7, 8, 6, 4};
    std::array<std::size_t, 11> consumed{};
    LslCheckpoint checkpoint;
    checkpoint.program_hash = program.source_hash;
    checkpoint.state = program.state_order[saved.state_index];
    if (saved.next_state_index) {
        if (*saved.next_state_index >= program.state_order.size() || saved.event_code != 1)
            return false;
        checkpoint.next_state = program.state_order[*saved.next_state_index];
    }
    checkpoint.running = impl_->control.running;
    checkpoint.start_parameter = impl_->context.start_parameter;
    for (const auto& global : program.globals) {
        const auto slot = slots.at(static_cast<std::size_t>(global.store_type));
        const auto index = consumed[slot]++;
        if (index >= saved.globals[slot].size() ||
            !checkpoint_value_matches(global.store_type, saved.globals[slot][index]))
            return false;
        checkpoint.globals.push_back(saved.globals[slot][index]);
    }
    for (std::size_t slot = 0; slot < consumed.size(); ++slot)
        if (consumed[slot] != saved.globals[slot].size())
            return false;
    checkpoint.initialized_globals = initialize_globals ? 0 : checkpoint.globals.size();
    if (saved.event_code >= 0) {
        const auto event = std::ranges::find(event_mask_bits, static_cast<unsigned>(saved.event_code),
                                             &std::pair<std::string_view, unsigned>::second);
        if (event == std::ranges::end(event_mask_bits))
            return false;
        const auto& handlers = program.states.at(checkpoint.state).handlers;
        const auto handler = handlers.find(std::string(event->first));
        if (handler == handlers.end() || handler->second.parameter_count != saved.event_arguments.size())
            return false;
        checkpoint.active_event = LslEvent{std::string(event->first), saved.event_arguments, saved.event_detectors};
        checkpoint.locals = saved.event_arguments;
        const auto types = std::span(handler->second.local_types).subspan(saved.event_arguments.size());
        for (const auto type : types)
            checkpoint.locals.push_back(default_value(type));
    }
    return restore_checkpoint(checkpoint);
}

std::optional<LslCheckpoint> LslInstance::checkpoint() const {
    const auto& s = *impl_;
    if (!s.fault.empty())
        return std::nullopt;
    LslCheckpoint result;
    result.program_hash = s.program->source_hash;
    for (const auto& [name, state] : s.program->states) {
        if (&state == s.state)
            result.state = name;
        if (&state == s.next_state)
            result.next_state = name;
    }
    result.globals = s.globals;
    if (s.active)
        result.locals = s.locals;
    result.queued_events.assign(s.queue.begin(), s.queue.end());
    result.active_event = s.active;
    result.initialized_globals = s.initialized_globals;
    result.instruction = s.active ? s.pc : 0;
    result.running = s.control.running;
    result.sleep_requested = s.sleep_requested;
    result.start_parameter = s.context.start_parameter;
    const auto now = s.now();
    const auto monotonic_now = s.host.monotonic_ticks ? s.host.monotonic_ticks() : ticks();
    result.sleep_remaining = ticks_between(now, s.sleep_until);
    result.timer_interval = s.timer_interval;
    result.timer_remaining = s.timer_interval ? ticks_between(now, s.next_timer) : 0;
    result.timer_elapsed = ticks_between(s.timer_start, monotonic_now);
    result.minimum_event_delay = s.minimum_event_delay;
    if (s.next_event_time)
        result.event_delay_remaining = std::max(0., *s.next_event_time - monotonic_now / 10000000.);
    return result;
}
bool LslInstance::restore_checkpoint(const LslCheckpoint& saved) {
    auto& current = *impl_;
    const auto& program = *current.program;
    if (saved.program_hash != program.source_hash || saved.globals.size() != program.globals.size() ||
        saved.initialized_globals > saved.globals.size() || saved.sleep_remaining < 0 ||
        saved.timer_interval < 0 || saved.timer_remaining < 0 || saved.timer_elapsed < 0 ||
        !std::isfinite(saved.minimum_event_delay) ||
        (saved.event_delay_remaining &&
         (!std::isfinite(*saved.event_delay_remaining) || *saved.event_delay_remaining < 0.)))
        return false;
    const auto state = program.states.find(saved.state);
    const auto next = program.states.find(saved.next_state);
    if (state == program.states.end() || (!saved.next_state.empty() && next == program.states.end()))
        return false;
    for (std::size_t i = 0; i < saved.initialized_globals; ++i)
        if (!checkpoint_value_matches(program.globals[i].store_type, saved.globals[i]))
            return false;
    const auto event_matches = [&](const LslEvent& event) {
        const auto handler = state->second.handlers.find(event.name);
        if (handler == state->second.handlers.end() || event.arguments.size() > handler->second.local_types.size())
            return false;
        for (std::size_t i = 0; i < event.arguments.size(); ++i)
            if (!checkpoint_value_matches(handler->second.local_types[i], event.arguments[i]))
                return false;
        return true;
    };
    if (saved.active_event) {
        if (!event_matches(*saved.active_event))
            return false;
        const auto& handler = state->second.handlers.at(saved.active_event->name);
        if (saved.locals.size() != handler.local_types.size() || saved.instruction > handler.instructions.size())
            return false;
        for (std::size_t i = 0; i < saved.locals.size(); ++i)
            if (!checkpoint_value_matches(handler.local_types[i], saved.locals[i]))
                return false;
    } else if (saved.instruction != 0 || !saved.locals.empty()) {
        return false;
    }
    if (!std::ranges::all_of(saved.queued_events, event_matches))
        return false;

    auto restored = std::make_unique<Impl>();
    restored->program = current.program;
    restored->context = current.context;
    restored->context.start_parameter = saved.start_parameter;
    restored->host = current.host;
    restored->state = &state->second;
    restored->next_state = saved.next_state.empty() ? nullptr : &next->second;
    restored->initialized_globals = saved.initialized_globals;
    restored->globals = saved.globals;
    restored->locals = saved.locals;
    restored->active = saved.active_event;
    restored->pc = saved.instruction;
    restored->queue.assign(saved.queued_events.begin(), saved.queued_events.end());
    for (const auto& event : restored->queue)
        ++restored->counts[event.name];
    const auto now = restored->now();
    const auto monotonic_now = restored->host.monotonic_ticks ? restored->host.monotonic_ticks() : ticks();
    restored->sleep_until = ticks_after(now, saved.sleep_remaining);
    restored->sleep_requested = saved.sleep_requested;
    restored->timer_interval = saved.timer_interval;
    restored->next_timer = ticks_after(now, saved.timer_remaining);
    restored->timer_start = timer_origin(monotonic_now, saved.timer_elapsed);
    restored->last_say_check = now;
    restored->minimum_event_delay = saved.minimum_event_delay;
    if (saved.event_delay_remaining)
        restored->next_event_time = monotonic_now / 10000000. + *saved.event_delay_remaining;
    restored->control.running = saved.running;
    restored->control.active_event = restored->active.has_value();
    restored->control.pending_events = restored->queue.size();
    restored->control.state = !saved.running ? scene::ScriptExecutionState::Suspended
                              : saved.active_event ? scene::ScriptExecutionState::OnYieldQueue
                              : restored->queue.empty() ? scene::ScriptExecutionState::Idle
                                                        : scene::ScriptExecutionState::OnStartQueue;
    try {
        restored->check_heap_limit(0, restored->global_heap_bytes() + restored->local_heap_bytes());
    } catch (const OutOfHeap&) {
        return false;
    }
    impl_ = std::move(restored);
    return true;
}
LslRunResult LslInstance::run(std::size_t budget) {
    auto& s = *impl_;
    LslRunResult out{LslRunStatus::Idle, 0, {}};
    auto paused = [&] {
        if (s.control.running)
            return false;
        s.control.state = scene::ScriptExecutionState::Suspended;
        s.control.active_event = s.active.has_value();
        s.control.pending_events = s.queue.size();
        return true;
    };
    if (paused())
        return out;
    if (!s.fault.empty())
        return {LslRunStatus::Faulted, 0, s.fault};
    try {
        if (s.initialized_globals == 0)
            s.check_heap_limit(0, s.global_heap_bytes());
        for (;;) {
            if (paused())
                return out;
            const auto time = s.now();
            if (s.timer_interval && time >= s.next_timer) {
                s.advance_timer(time);
                s.post({"timer", {}, {}});
            }
            if (std::exchange(s.sleep_requested, false) || time < s.sleep_until) {
                s.control.state = scene::ScriptExecutionState::OnSleepQueue;
                out.status = LslRunStatus::Sleeping;
                return out;
            }
            if (s.initialized_globals < s.program->globals.size()) {
                if (out.instructions >= budget) {
                    out.status = LslRunStatus::BudgetExhausted;
                    return out;
                }
                s.execute(s.program->globals[s.initialized_globals++]);
                ++out.instructions;
                continue;
            }
            if (!s.active) {
                if (s.queue.empty()) {
                    s.control.state = scene::ScriptExecutionState::Idle;
                    s.control.active_event = false;
                    s.control.pending_events = 0;
                    return out;
                }
                if (out.instructions >= budget) {
                    s.control.state = scene::ScriptExecutionState::OnStartQueue;
                    out.status = LslRunStatus::BudgetExhausted;
                    return out;
                }
                s.active = std::move(s.queue.front());
                s.queue.pop_front();
                --s.counts[s.active->name];
                s.control.pending_events = s.queue.size();
                s.control.active_event = true;
                s.pc = 0;
                s.initialize_event_locals();
            }
            auto& code = s.handlers().at(s.active->name).instructions;
            if (s.pc == code.size()) {
                if (s.next_state && s.active->name == "state_exit") {
                    if (s.host.clear_listeners)
                        s.host.clear_listeners(s.context.item);
                    if (s.host.cancel_remote_data)
                        s.host.cancel_remote_data(s.context.item);
                    if (s.host.clear_targets)
                        s.host.clear_targets(s.context.item);
                    if (s.host.clear_sensors)
                        s.host.clear_sensors(s.context.item);
                    s.state = std::exchange(s.next_state, nullptr);
                    s.queue.clear();
                    s.counts.clear();
                    s.post({"state_entry", {}, {}});
                }
                ++out.events_completed;
                s.active.reset();
                s.control.active_event = false;
                continue;
            }
            if (out.instructions >= budget) {
                s.control.state = scene::ScriptExecutionState::OnYieldQueue;
                out.status = LslRunStatus::BudgetExhausted;
                return out;
            }
            s.control.state = scene::ScriptExecutionState::Running;
            s.execute(code[s.pc++]);
            ++out.instructions;
        }
    } catch (const ScriptTermination& termination) {
        s.set_running(false);
        ++out.instructions;
        out.status = termination.status;
        return out;
    } catch (const std::exception& e) {
        s.fault = e.what();
        const bool out_of_heap = dynamic_cast<const OutOfHeap*>(&e) != nullptr;
        const bool ossl_error = dynamic_cast<const OsslPermissionError*>(&e) != nullptr;
        const bool ossl_runtime_error = dynamic_cast<const OsslRuntimeError*>(&e) != nullptr;
        if ((out_of_heap || ossl_error || ossl_runtime_error) && s.host.chat) {
            const auto& position = s.context.position;
            char location[160];
            std::snprintf(location, sizeof location, "<%.0f,%.0f,%.0f>",
                          std::floor(position.x), std::floor(position.y), std::floor(position.z));
            const auto event = s.active ? s.active->name : "None";
            auto message = "YEngine: " + s.fault + " (script: " + s.context.script_name +
                           " event: " + event + " primID: " + s.context.object.str() +
                           " at: " + location + ") Script must be Reset to re-enable.\n";
            const auto audience = out_of_heap || (ossl_error && s.host.ossl_permission_error_to_owner)
                ? LslChatType::Owner : LslChatType::Debug;
            s.host.chat({audience, std::numeric_limits<std::int32_t>::max(),
                         framework::truncate_utf16(message, 1000), s.context});
        }
        s.active.reset();
        s.locals.clear();
        s.pc = 0;
        s.control.active_event = false;
        s.control.state = scene::ScriptExecutionState::OnSleepQueue;
        out.status = LslRunStatus::Faulted;
        out.error = s.fault;
        return out;
    }
}
}
