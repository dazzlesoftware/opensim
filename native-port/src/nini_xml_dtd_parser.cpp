// Port of the synchronous .NET 8.0.31 System.Xml.DtdParser.
// Copyright (c) .NET Foundation and Contributors. MIT license:
// native-port/third_party/dotnet_collation/LICENSE.
#include "opensim/framework/nini_xml_reader_dtd.hpp"
#include "opensim/framework/nini_xml_character.hpp"
#include "opensim/framework/nini_xml_dtd_resources.hpp"
#include <algorithm>
#include <optional>
#include <unordered_map>
#include <array>
namespace opensim::framework {
namespace {
using Name = NiniXmlDtdName;
using MaybeName = std::optional<Name>;
using Entity = std::shared_ptr<NiniXmlDtdEntity>;
using Attribute = std::shared_ptr<NiniXmlDtdAttribute>;
using Element = std::shared_ptr<NiniXmlDtdAttributeList>;
static bool IsWhiteSpace(char16_t c) {
    return (nini_xml_character_properties(c) & 1) != 0;
}
static bool IsStartNCNameSingleChar(char16_t c) {
    return (nini_xml_character_properties(c) & 4) != 0;
}
static bool IsNCNameSingleChar(char16_t c) {
    return (nini_xml_character_properties(c) & 8) != 0;
}
static bool IsNameSingleChar(char16_t c) {
    return c == u':' || IsNCNameSingleChar(c);
}
static bool IsAttributeValueChar(char16_t c) {
    return (nini_xml_character_properties(c) & 128) != 0;
}
static bool IsTextChar(char16_t c) {
    return (nini_xml_character_properties(c) & 64) != 0;
}
static bool IsHighSurrogate(char16_t c) {
    return c >= 0xd800 && c <= 0xdbff;
}
static bool IsLowSurrogate(char16_t c) {
    return c >= 0xdc00 && c <= 0xdfff;
}
static void Append(std::u16string& s, char16_t c) {
    s += c;
}
static void Append(std::u16string& s, std::u16string_view v) {
    s += v;
}
// Consumed ParticleContentValidator construction state. All DTD particles are
// null, so IsUpaEnforced remains true; unexposed DFA/NFA storage is separate.
class DtdParticle {
    enum Kind { Leaf, Choice, Sequence, Star, Plus, Qmark };
    struct Node {
        Kind kind;
        Node* left = nullptr;
        Node* right = nullptr;
    };
    std::vector<std::unique_ptr<Node>> nodes;
    std::vector<Node*> stack;
    std::vector<Name> names;
    Node* content = nullptr;
    bool partial = false, mixed;
    static void nullref() {
        throw NiniXmlProviderError("System.NullReferenceException", 0x80004003u,
                                   u"Object reference not set to an instance of an object.");
    }
    Node* make(Kind k) {
        auto n = std::make_unique<Node>();
        n->kind = k;
        auto p = n.get();
        nodes.push_back(std::move(n));
        return p;
    }
    Node* pop() {
        if (stack.empty())
            throw NiniXmlProviderError("System.InvalidOperationException", 0x80131509u, u"Stack empty.");
        auto n = stack.back();
        stack.pop_back();
        return n;
    }
    static Node* interior(Node* p) {
        if (p && p->kind == Leaf)
            throw NiniXmlProviderError(
                "System.InvalidCastException", 0x80004002u,
                u"Unable to cast object of type 'System.Xml.Schema.LeafNode' to type 'System.Xml.Schema.InteriorNode'.");
        return p;
    }

  public:
    explicit DtdParticle(bool m) : mixed(m) {
    }
    void open() {
        stack.push_back(nullptr);
    }
    void close() {
        auto n = pop();
        if (!n)
            return;
        if (stack.empty()) {
            content = n;
            partial = false;
            return;
        }
        auto i = interior(pop());
        if (i) {
            i->right = n;
            n = i;
            partial = true;
        } else
            partial = false;
        stack.push_back(n);
    }
    bool exists(const Name& n) const {
        return std::find(names.begin(), names.end(), n) != names.end();
    }
    void name(Name n) {
        if (!exists(n))
            names.push_back(n);
        auto node = make(Leaf);
        if (!stack.empty()) {
            auto i = interior(pop());
            if (i) {
                i->right = node;
                node = i;
            }
        }
        stack.push_back(node);
        partial = true;
    }
    void connector(bool choice) {
        auto left = pop();
        auto n = make(choice ? Choice : Sequence);
        n->left = left;
        stack.push_back(n);
    }
    void closure(int kind) {
        auto n = make(kind == 0 ? Star : kind == 1 ? Qmark : Plus);
        if (!stack.empty()) {
            auto syntax = pop();
            auto i = syntax && syntax->kind != Leaf ? syntax : nullptr;
            if (partial && i) {
                n->left = i->right;
                i->right = n;
            } else {
                n->left = syntax;
                syntax = n;
            }
            stack.push_back(syntax);
        } else if (content) {
            n->left = content;
            content = n;
        }
    }
    bool finish() {
        if (!content)
            return !mixed;
        std::vector<Node*> pending{content};
        while (!pending.empty()) {
            auto n = pending.back();
            pending.pop_back();
            if (!n)
                nullref();
            if (n->kind == Leaf)
                continue;
            if (!n->left)
                nullref();
            pending.push_back(n->left);
            if (n->kind == Choice || n->kind == Sequence) {
                if (!n->right)
                    nullref();
                pending.push_back(n->right);
            } else if (n->right)
                pending.push_back(n->right);
        }
        return false;
    }
};
class DtdParser {
    enum class Token {
        CDATA,
        ID,
        IDREF,
        IDREFS,
        ENTITY,
        ENTITIES,
        NMTOKEN,
        NMTOKENS,
        NOTATION,
        None,
        PERef,
        AttlistDecl,
        ElementDecl,
        EntityDecl,
        NotationDecl,
        Comment,
        PI,
        CondSectionStart,
        CondSectionEnd,
        Eof,
        REQUIRED,
        IMPLIED,
        FIXED,
        QName,
        Name,
        Nmtoken,
        Quote,
        LeftParen,
        RightParen,
        GreaterThan,
        Or,
        LeftBracket,
        RightBracket,
        PUBLIC,
        SYSTEM,
        Literal,
        DOCTYPE,
        NData,
        Percent,
        Star,
        QMark,
        Plus,
        PCDATA,
        Comma,
        ANY,
        EMPTY,
        IGNORE,
        INCLUDE,
    };
    enum class ScanningFunction {
        SubsetContent,
        Name,
        QName,
        Nmtoken,
        Doctype1,
        Doctype2,
        Element1,
        Element2,
        Element3,
        Element4,
        Element5,
        Element6,
        Element7,
        Attlist1,
        Attlist2,
        Attlist3,
        Attlist4,
        Attlist5,
        Attlist6,
        Attlist7,
        Entity1,
        Entity2,
        Entity3,
        Notation1,
        CondSection1,
        CondSection2,
        CondSection3,
        Literal,
        SystemId,
        PublicId1,
        PublicId2,
        ClosingTag,
        ParamEntitySpace,
        None,
    };
    enum class LiteralType { AttributeValue, EntityReplText, SystemOrPublicID };
    NiniXmlDtdAdapter& _readerAdapter;
    std::shared_ptr<NiniXmlDtdInfo> _schemaInfo = std::make_shared<NiniXmlDtdInfo>();
    NiniWriterText _systemId = nini_writer_text(u""), _publicId = nini_writer_text(u"");
    bool _normalize = true, _validate = false, _supportNamespaces = true, _v1Compat = false;
    std::span<char16_t> _chars;
    int _charsUsed = 0, _curPos = 0;
    ScanningFunction _scanningFunction = ScanningFunction::SubsetContent,
                     _nextScanningFunction = ScanningFunction::SubsetContent,
                     _savedScanningFunction = ScanningFunction::SubsetContent;
    bool _whitespaceSeen = false;
    int _tokenStartPos = 0, _colonPos = 0;
    std::optional<std::u16string> _internalSubsetValueSb;
    int _externalEntitiesDepth = 0, _currentEntityId = 0;
    bool _freeFloatingDtd = false, _hasFreeFloatingInternalSubset = false;
    std::u16string _stringBuilder;
    int _condSectionDepth = 0;
    NiniXmlDtdLine _literalLineInfo;
    char16_t _literalQuoteChar = u'"';
    std::u16string _documentBaseUri, _externalDtdBaseUri;
    std::vector<int> _condSectionEntityIds;
    bool ParsingInternalSubset() const {
        return _externalEntitiesDepth == 0;
    }
    bool IgnoreEntityReferences() const {
        return _scanningFunction == ScanningFunction::CondSection3;
    }
    bool SaveInternalSubsetValue() const {
        return _readerAdapter.entity_stack_length() == 0 && _internalSubsetValueSb.has_value();
    }
    bool ParsingTopLevelMarkup() const {
        return _scanningFunction == ScanningFunction::SubsetContent ||
               (_scanningFunction == ScanningFunction::ParamEntitySpace &&
                _savedScanningFunction == ScanningFunction::SubsetContent);
    }
    int LineNo() const {
        return _readerAdapter.line_number();
    }
    int LinePos() const {
        return _curPos - _readerAdapter.line_start_position();
    }
    std::u16string BaseUriStr() const {
        auto p = _readerAdapter.base_uri();
        return p ? *p : std::u16string{};
    }
    std::vector<std::u16string> CharArgs(char16_t, char16_t);
    std::vector<std::u16string> CharArgs(std::span<char16_t>, int, int);
    void Throw(int, std::string_view, const std::vector<std::u16string>&);
    void Throw(int p, std::string_view r) {
        Throw(p, r, std::vector<std::u16string>{u""});
    }
    void Throw(int p, std::string_view r, std::u16string_view a) {
        Throw(p, r, std::vector<std::u16string>{std::u16string(a)});
    }
    void Throw(std::string_view, std::u16string_view, int, int);
    void ThrowInvalidChar(std::span<char16_t> s, int len, int p) {
        Throw(p, "Xml_InvalidCharacter", CharArgs(s, len, p));
    }
    void ThrowUnexpectedToken(int, std::u16string_view, std::optional<std::u16string_view> = {});
    std::u16string ParseUnexpectedToken(int);
    void OnUnexpectedError();
    void LoadParsingBuffer() {
        _chars = _readerAdapter.parsing_buffer();
        _charsUsed = _readerAdapter.parsing_buffer_length();
        _curPos = _readerAdapter.current_position();
    }
    void SaveParsingBuffer() {
        SaveParsingBuffer(_curPos);
    }
    void SaveParsingBuffer(int end) {
        if (SaveInternalSubsetValue()) {
            int p = _readerAdapter.current_position();
            if (end - p > 0)
                _internalSubsetValueSb->append(_chars.data() + p, end - p);
        }
        _readerAdapter.current_position(_curPos);
    }
    int ReadData() {
        SaveParsingBuffer();
        int n = _readerAdapter.read_data();
        LoadParsingBuffer();
        return n;
    }
    MaybeName GetNameQualified(bool);
    std::u16string GetNameString() {
        return {_chars.data() + _tokenStartPos, static_cast<std::size_t>(_curPos - _tokenStartPos)};
    }
    std::u16string GetNmtokenString() {
        return GetNameString();
    }
    std::u16string GetValue() {
        if (!_stringBuilder.empty())
            return _stringBuilder;
        int length = _curPos - _tokenStartPos - 1;
        if (length < 0) {
            auto narrow = std::to_string(length);
            std::u16string number(narrow.begin(), narrow.end());
            // System.String(char[], startIndex, length) validates before copying. Its
            // ArgumentOutOfRangeException appends ActualValue with Environment.NewLine.
            auto message = u"length ('" + number + u"') must be a non-negative value. (Parameter 'length')";
#ifdef _WIN32
            message += u"\r\n";
#else
            message += u"\n";
#endif
            message += u"Actual value was " + number + u".";
            throw NiniXmlProviderError("System.ArgumentOutOfRangeException", 0x80131502, std::move(message),
                                       u"length");
        }
        return std::u16string(_chars.data() + _tokenStartPos, static_cast<std::size_t>(length));
    }
    static std::u16string StripSpaces(std::u16string);
    std::u16string GetValueWithStrippedSpaces() {
        return StripSpaces(GetValue());
    }
    bool HandleEntityReference(bool, bool, bool);
    bool HandleEntityReference(MaybeName, bool, bool, bool);
    bool HandleEntityEnd(bool);
    Entity VerifyEntityReference(MaybeName, bool, bool, bool);
    void SendValidationEvent(int, int, std::string_view, std::optional<std::u16string>);
    struct UndeclaredNotation {
        std::u16string name;
        NiniXmlDtdLine line;
        int count;
        bool active = true;
    };
    std::vector<UndeclaredNotation> _undeclaredNotations;
    std::vector<std::size_t> _freeNotationSlots;
    std::unordered_map<NiniXmlDtdAttributeList*, std::size_t> _undeclaredElementSlots;
    std::vector<std::size_t> _freeElementSlots;
    std::size_t _nextElementSlot = 0;
    void ParseInDocumentDtd(bool);
    void ParseExternalSubset();
    void ParseSubset();
    void ParseAttlistDecl();
    void ParseElementDecl();
    void ParseEntityDecl();
    void ParseNotationDecl();
    void ParseComment();
    void ParsePI();
    void ParseCondSection();
    void ParseExternalId(Token, Token, NiniWriterText&, NiniWriterText&);
    void AddUndeclaredNotation(std::u16string);
    struct ElementMetadata {
        bool id = false, notation = false, empty = false;
    };
    std::unordered_map<NiniXmlDtdAttributeList*, ElementMetadata> _elementMetadata;
    std::unordered_map<NiniXmlDtdAttribute*, NiniXmlDtdTokenizedType> _attributeDatatype;
    Element FindElement(const Name&, bool);
    void CheckXmlSpace(const Attribute&);
    void ParseAttlistType(const Attribute&, const Element&, bool);
    void ParseAttlistDefault(const Attribute&, bool);
    void SetDefaultTypedValue(const Attribute&);
    void ParseHowMany(DtdParticle&);
    void ParseElementOnlyContent(DtdParticle&, int);
    void ParseElementMixedContent(DtdParticle&, int);

  public:
    std::shared_ptr<NiniXmlDtdInfo> Parse(bool);
    void InitializeFreeFloating(NiniWriterText, NiniWriterText, NiniWriterText, NiniWriterText,
                                NiniWriterText);
    explicit DtdParser(NiniXmlDtdAdapter& a) : _readerAdapter(a) {
        _validate = a.validation_enabled();
        _v1Compat = a.v1_compatibility();
        _normalize = a.normalization();
        _supportNamespaces = a.namespaces();
        _documentBaseUri = BaseUriStr();
    }
    Token GetToken(bool needWhiteSpace) {
        _whitespaceSeen = false;
        while (true) {
            switch (_chars[_curPos]) {
            case (char16_t)0:
                if (_curPos == _charsUsed) {
                    goto ReadData;
                } else {
                    ThrowInvalidChar(_chars, _charsUsed, _curPos);
                }
                break;
            case (char16_t)0xA:
                _whitespaceSeen = true;
                _curPos++;
                _readerAdapter.on_new_line(_curPos);
                continue;
            case (char16_t)0xD:
                _whitespaceSeen = true;
                if (_chars[_curPos + 1] == (char16_t)0xA) {
                    if (_normalize) {
                        SaveParsingBuffer();
                        _readerAdapter.current_position(_readerAdapter.current_position() + 1);
                    }
                    _curPos += 2;
                } else if (_curPos + 1 < _charsUsed || _readerAdapter.is_eof()) {
                    _chars[_curPos] = (char16_t)0xA;
                    _curPos++;
                } else {
                    goto ReadData;
                }
                _readerAdapter.on_new_line(_curPos);
                continue;
            case (char16_t)0x9:
            case (char16_t)0x20:
                _whitespaceSeen = true;
                _curPos++;
                continue;
            case u'%':
                if (_charsUsed - _curPos < 2) {
                    goto ReadData;
                }
                if (!IsWhiteSpace(_chars[_curPos + 1])) {
                    if (IgnoreEntityReferences()) {
                        _curPos++;
                    } else {
                        HandleEntityReference(true, false, false);
                    }
                    continue;
                }
                goto Default1;
            default:
            Default1:
                if (needWhiteSpace && !_whitespaceSeen &&
                    _scanningFunction != ScanningFunction::ParamEntitySpace) {
                    Throw(_curPos, "Xml_ExpectingWhiteSpace", ParseUnexpectedToken(_curPos));
                }
                _tokenStartPos = _curPos;
            SwitchAgain:
                switch (_scanningFunction) {
                case ScanningFunction::Name:
                    return ScanNameExpected();
                case ScanningFunction::QName:
                    return ScanQNameExpected();
                case ScanningFunction::Nmtoken:
                    return ScanNmtokenExpected();
                case ScanningFunction::SubsetContent:
                    return ScanSubsetContent();
                case ScanningFunction::Doctype1:
                    return ScanDoctype1();
                case ScanningFunction::Doctype2:
                    return ScanDoctype2();
                case ScanningFunction::Element1:
                    return ScanElement1();
                case ScanningFunction::Element2:
                    return ScanElement2();
                case ScanningFunction::Element3:
                    return ScanElement3();
                case ScanningFunction::Element4:
                    return ScanElement4();
                case ScanningFunction::Element5:
                    return ScanElement5();
                case ScanningFunction::Element6:
                    return ScanElement6();
                case ScanningFunction::Element7:
                    return ScanElement7();
                case ScanningFunction::Attlist1:
                    return ScanAttlist1();
                case ScanningFunction::Attlist2:
                    return ScanAttlist2();
                case ScanningFunction::Attlist3:
                    return ScanAttlist3();
                case ScanningFunction::Attlist4:
                    return ScanAttlist4();
                case ScanningFunction::Attlist5:
                    return ScanAttlist5();
                case ScanningFunction::Attlist6:
                    return ScanAttlist6();
                case ScanningFunction::Attlist7:
                    return ScanAttlist7();
                case ScanningFunction::Notation1:
                    return ScanNotation1();
                case ScanningFunction::SystemId:
                    return ScanSystemId();
                case ScanningFunction::PublicId1:
                    return ScanPublicId1();
                case ScanningFunction::PublicId2:
                    return ScanPublicId2();
                case ScanningFunction::Entity1:
                    return ScanEntity1();
                case ScanningFunction::Entity2:
                    return ScanEntity2();
                case ScanningFunction::Entity3:
                    return ScanEntity3();
                case ScanningFunction::CondSection1:
                    return ScanCondSection1();
                case ScanningFunction::CondSection2:
                    return ScanCondSection2();
                case ScanningFunction::CondSection3:
                    return ScanCondSection3();
                case ScanningFunction::ClosingTag:
                    return ScanClosingTag();
                case ScanningFunction::ParamEntitySpace:
                    _whitespaceSeen = true;
                    _scanningFunction = _savedScanningFunction;
                    goto SwitchAgain;
                default:

                    return Token::None;
                }
            }
        ReadData:
            if (_readerAdapter.is_eof() || ReadData() == 0) {
                if (HandleEntityEnd(false)) {
                    continue;
                }
                if (_scanningFunction == ScanningFunction::SubsetContent) {
                    return Token::Eof;
                } else {
                    Throw(_curPos, "Xml_IncompleteDtdContent");
                }
            }
        }
    }

    Token ScanSubsetContent() {
        while (true) {
            switch (_chars[_curPos]) {
            case u'<':
                switch (_chars[_curPos + 1]) {
                case u'!':
                    switch (_chars[_curPos + 2]) {
                    case u'E':
                        if (_chars[_curPos + 3] == u'L') {
                            if (_charsUsed - _curPos < 9) {
                                goto ReadData;
                            }
                            if (_chars[_curPos + 4] != u'E' || _chars[_curPos + 5] != u'M' ||
                                _chars[_curPos + 6] != u'E' || _chars[_curPos + 7] != u'N' ||
                                _chars[_curPos + 8] != u'T') {
                                Throw(_curPos, "Xml_ExpectDtdMarkup");
                            }
                            _curPos += 9;
                            _scanningFunction = ScanningFunction::QName;
                            _nextScanningFunction = ScanningFunction::Element1;
                            return Token::ElementDecl;
                        } else if (_chars[_curPos + 3] == u'N') {
                            if (_charsUsed - _curPos < 8) {
                                goto ReadData;
                            }
                            if (_chars[_curPos + 4] != u'T' || _chars[_curPos + 5] != u'I' ||
                                _chars[_curPos + 6] != u'T' || _chars[_curPos + 7] != u'Y') {
                                Throw(_curPos, "Xml_ExpectDtdMarkup");
                            }
                            _curPos += 8;
                            _scanningFunction = ScanningFunction::Entity1;
                            return Token::EntityDecl;
                        } else {
                            if (_charsUsed - _curPos < 4) {
                                goto ReadData;
                            }
                            Throw(_curPos, "Xml_ExpectDtdMarkup");
                            return Token::None;
                        }

                    case u'A':
                        if (_charsUsed - _curPos < 9) {
                            goto ReadData;
                        }
                        if (_chars[_curPos + 3] != u'T' || _chars[_curPos + 4] != u'T' ||
                            _chars[_curPos + 5] != u'L' || _chars[_curPos + 6] != u'I' ||
                            _chars[_curPos + 7] != u'S' || _chars[_curPos + 8] != u'T') {
                            Throw(_curPos, "Xml_ExpectDtdMarkup");
                        }
                        _curPos += 9;
                        _scanningFunction = ScanningFunction::QName;
                        _nextScanningFunction = ScanningFunction::Attlist1;
                        return Token::AttlistDecl;

                    case u'N':
                        if (_charsUsed - _curPos < 10) {
                            goto ReadData;
                        }
                        if (_chars[_curPos + 3] != u'O' || _chars[_curPos + 4] != u'T' ||
                            _chars[_curPos + 5] != u'A' || _chars[_curPos + 6] != u'T' ||
                            _chars[_curPos + 7] != u'I' || _chars[_curPos + 8] != u'O' ||
                            _chars[_curPos + 9] != u'N') {
                            Throw(_curPos, "Xml_ExpectDtdMarkup");
                        }
                        _curPos += 10;
                        _scanningFunction = ScanningFunction::Name;
                        _nextScanningFunction = ScanningFunction::Notation1;
                        return Token::NotationDecl;

                    case u'[':
                        _curPos += 3;
                        _scanningFunction = ScanningFunction::CondSection1;
                        return Token::CondSectionStart;
                    case u'-':
                        if (_chars[_curPos + 3] == u'-') {
                            _curPos += 4;
                            return Token::Comment;
                        } else {
                            if (_charsUsed - _curPos < 4) {
                                goto ReadData;
                            }
                            Throw(_curPos, "Xml_ExpectDtdMarkup");
                            break;
                        }
                    default:
                        if (_charsUsed - _curPos < 3) {
                            goto ReadData;
                        }
                        Throw(_curPos + 2, "Xml_ExpectDtdMarkup");
                        break;
                    }
                    break;
                case u'?':
                    _curPos += 2;
                    return Token::PI;
                default:
                    if (_charsUsed - _curPos < 2) {
                        goto ReadData;
                    }
                    Throw(_curPos, "Xml_ExpectDtdMarkup");
                    return Token::None;
                }
                break;
            case u']':
                if (_charsUsed - _curPos < 2 && !_readerAdapter.is_eof()) {
                    goto ReadData;
                }
                if (_chars[_curPos + 1] != u']') {
                    _curPos++;
                    _scanningFunction = ScanningFunction::ClosingTag;
                    return Token::RightBracket;
                }
                if (_charsUsed - _curPos < 3 && !_readerAdapter.is_eof()) {
                    goto ReadData;
                }
                if (_chars[_curPos + 1] == u']' && _chars[_curPos + 2] == u'>') {
                    _curPos += 3;
                    return Token::CondSectionEnd;
                }
                goto Default3;
            default:
            Default3:
                if (_charsUsed - _curPos == 0) {
                    goto ReadData;
                }
                Throw(_curPos, "Xml_ExpectDtdMarkup");
                break;
            }
        ReadData:
            if (ReadData() == 0) {
                Throw(_charsUsed, "Xml_IncompleteDtdContent");
            }
        }
    }

    Token ScanNameExpected() {
        ScanName();
        _scanningFunction = _nextScanningFunction;
        return Token::Name;
    }

    Token ScanQNameExpected() {
        ScanQName();
        _scanningFunction = _nextScanningFunction;
        return Token::QName;
    }

    Token ScanNmtokenExpected() {
        ScanNmtoken();
        _scanningFunction = _nextScanningFunction;
        return Token::Nmtoken;
    }

    Token ScanDoctype1() {
        switch (_chars[_curPos]) {
        case u'P':
            if (!EatPublicKeyword()) {
                Throw(_curPos, "Xml_ExpectExternalOrClose");
            }
            _nextScanningFunction = ScanningFunction::Doctype2;
            _scanningFunction = ScanningFunction::PublicId1;
            return Token::PUBLIC;
        case u'S':
            if (!EatSystemKeyword()) {
                Throw(_curPos, "Xml_ExpectExternalOrClose");
            }
            _nextScanningFunction = ScanningFunction::Doctype2;
            _scanningFunction = ScanningFunction::SystemId;
            return Token::SYSTEM;
        case u'[':
            _curPos++;
            _scanningFunction = ScanningFunction::SubsetContent;
            return Token::LeftBracket;
        case u'>':
            _curPos++;
            _scanningFunction = ScanningFunction::SubsetContent;
            return Token::GreaterThan;
        default:
            Throw(_curPos, "Xml_ExpectExternalOrClose");
            return Token::None;
        }
    }

    Token ScanDoctype2() {
        switch (_chars[_curPos]) {
        case u'[':
            _curPos++;
            _scanningFunction = ScanningFunction::SubsetContent;
            return Token::LeftBracket;
        case u'>':
            _curPos++;
            _scanningFunction = ScanningFunction::SubsetContent;
            return Token::GreaterThan;
        default:
            Throw(_curPos, "Xml_ExpectSubOrClose");
            return Token::None;
        }
    }

    Token ScanClosingTag() {
        if (_chars[_curPos] != u'>') {
            ThrowUnexpectedToken(_curPos, u">");
        }
        _curPos++;
        _scanningFunction = ScanningFunction::SubsetContent;
        return Token::GreaterThan;
    }

    Token ScanElement1() {
        while (true) {
            switch (_chars[_curPos]) {
            case u'(':
                _scanningFunction = ScanningFunction::Element2;
                _curPos++;
                return Token::LeftParen;
            case u'E':
                if (_charsUsed - _curPos < 5) {
                    goto ReadData;
                }
                if (_chars[_curPos + 1] == u'M' && _chars[_curPos + 2] == u'P' &&
                    _chars[_curPos + 3] == u'T' && _chars[_curPos + 4] == u'Y') {
                    _curPos += 5;
                    _scanningFunction = ScanningFunction::ClosingTag;
                    return Token::EMPTY;
                }
                goto Default8;
            case u'A':
                if (_charsUsed - _curPos < 3) {
                    goto ReadData;
                }
                if (_chars[_curPos + 1] == u'N' && _chars[_curPos + 2] == u'Y') {
                    _curPos += 3;
                    _scanningFunction = ScanningFunction::ClosingTag;
                    return Token::ANY;
                }
                goto Default8;
            default:
            Default8:
                Throw(_curPos, "Xml_InvalidContentModel");
                break;
            }
        ReadData:
            if (ReadData() == 0) {
                Throw(_curPos, "Xml_IncompleteDtdContent");
            }
        }
    }

    Token ScanElement2() {
        if (_chars[_curPos] == u'#') {
            while (_charsUsed - _curPos < 7) {
                if (ReadData() == 0) {
                    Throw(_curPos, "Xml_IncompleteDtdContent");
                }
            }

            if (std::u16string_view(_chars.data() + _curPos + 1, _chars.size() - (_curPos + 1))
                    .starts_with(u"PCDATA")) {
                _curPos += 7;
                _scanningFunction = ScanningFunction::Element6;
                return Token::PCDATA;
            } else {
                Throw(_curPos + 1, "Xml_ExpectPcData");
            }
        }

        _scanningFunction = ScanningFunction::Element3;
        return Token::None;
    }

    Token ScanElement3() {
        switch (_chars[_curPos]) {
        case u'(':
            _curPos++;
            return Token::LeftParen;
        case u'>':
            _curPos++;
            _scanningFunction = ScanningFunction::SubsetContent;
            return Token::GreaterThan;
        default:
            ScanQName();
            _scanningFunction = ScanningFunction::Element4;
            return Token::QName;
        }
    }

    Token ScanElement4() {
        _scanningFunction = ScanningFunction::Element5;

        Token t;
        switch (_chars[_curPos]) {
        case u'*':
            t = Token::Star;
            break;
        case u'?':
            t = Token::QMark;
            break;
        case u'+':
            t = Token::Plus;
            break;
        default:
            return Token::None;
        }
        if (_whitespaceSeen) {
            Throw(_curPos, "Xml_ExpectNoWhitespace");
        }
        _curPos++;
        return t;
    }

    Token ScanElement5() {
        switch (_chars[_curPos]) {
        case u',':
            _curPos++;
            _scanningFunction = ScanningFunction::Element3;
            return Token::Comma;
        case u'|':
            _curPos++;
            _scanningFunction = ScanningFunction::Element3;
            return Token::Or;
        case u')':
            _curPos++;
            _scanningFunction = ScanningFunction::Element4;
            return Token::RightParen;
        case u'>':
            _curPos++;
            _scanningFunction = ScanningFunction::SubsetContent;
            return Token::GreaterThan;
        default:
            Throw(_curPos, "Xml_ExpectOp");
            return Token::None;
        }
    }

    Token ScanElement6() {
        switch (_chars[_curPos]) {
        case u')':
            _curPos++;
            _scanningFunction = ScanningFunction::Element7;
            return Token::RightParen;
        case u'|':
            _curPos++;
            _nextScanningFunction = ScanningFunction::Element6;
            _scanningFunction = ScanningFunction::QName;
            return Token::Or;
        default:
            ThrowUnexpectedToken(_curPos, u")", u"|");
            return Token::None;
        }
    }

    Token ScanElement7() {
        _scanningFunction = ScanningFunction::ClosingTag;
        if (_chars[_curPos] == u'*' && !_whitespaceSeen) {
            _curPos++;
            return Token::Star;
        }
        return Token::None;
    }

    Token ScanAttlist1() {
        switch (_chars[_curPos]) {
        case u'>':
            _curPos++;
            _scanningFunction = ScanningFunction::SubsetContent;
            return Token::GreaterThan;
        default:
            if (!_whitespaceSeen) {
                Throw(_curPos, "Xml_ExpectingWhiteSpace", ParseUnexpectedToken(_curPos));
            }
            ScanQName();
            _scanningFunction = ScanningFunction::Attlist2;
            return Token::QName;
        }
    }

    Token ScanAttlist2() {
        while (true) {
            switch (_chars[_curPos]) {
            case u'(':
                _curPos++;
                _scanningFunction = ScanningFunction::Nmtoken;
                _nextScanningFunction = ScanningFunction::Attlist5;
                return Token::LeftParen;
            case u'C':
                if (_charsUsed - _curPos < 5)
                    goto ReadData;
                if (_chars[_curPos + 1] != u'D' || _chars[_curPos + 2] != u'A' ||
                    _chars[_curPos + 3] != u'T' || _chars[_curPos + 4] != u'A') {
                    Throw(_curPos, "Xml_InvalidAttributeType1");
                }
                _curPos += 5;
                _scanningFunction = ScanningFunction::Attlist6;
                return Token::CDATA;
            case u'E':
                if (_charsUsed - _curPos < 9)
                    goto ReadData;
                _scanningFunction = ScanningFunction::Attlist6;
                if (_chars[_curPos + 1] != u'N' || _chars[_curPos + 2] != u'T' ||
                    _chars[_curPos + 3] != u'I' || _chars[_curPos + 4] != u'T') {
                    Throw(_curPos, "Xml_InvalidAttributeType");
                }
                switch (_chars[_curPos + 5]) {
                case u'I':
                    if (_chars[_curPos + 6] != u'E' || _chars[_curPos + 7] != u'S') {
                        Throw(_curPos, "Xml_InvalidAttributeType");
                    }
                    _curPos += 8;
                    return Token::ENTITIES;
                case u'Y':
                    _curPos += 6;
                    return Token::ENTITY;
                default:
                    Throw(_curPos, "Xml_InvalidAttributeType");
                    break;
                }
                break;
            case u'I':
                if (_charsUsed - _curPos < 6)
                    goto ReadData;
                _scanningFunction = ScanningFunction::Attlist6;
                if (_chars[_curPos + 1] != u'D') {
                    Throw(_curPos, "Xml_InvalidAttributeType");
                }

                if (_chars[_curPos + 2] != u'R') {
                    _curPos += 2;
                    return Token::ID;
                }

                if (_chars[_curPos + 3] != u'E' || _chars[_curPos + 4] != u'F') {
                    Throw(_curPos, "Xml_InvalidAttributeType");
                }

                if (_chars[_curPos + 5] != u'S') {
                    _curPos += 5;
                    return Token::IDREF;
                } else {
                    _curPos += 6;
                    return Token::IDREFS;
                }
            case u'N':
                if (_charsUsed - _curPos < 8 && !_readerAdapter.is_eof()) {
                    goto ReadData;
                }
                switch (_chars[_curPos + 1]) {
                case u'O':
                    if (_chars[_curPos + 2] != u'T' || _chars[_curPos + 3] != u'A' ||
                        _chars[_curPos + 4] != u'T' || _chars[_curPos + 5] != u'I' ||
                        _chars[_curPos + 6] != u'O' || _chars[_curPos + 7] != u'N') {
                        Throw(_curPos, "Xml_InvalidAttributeType");
                    }
                    _curPos += 8;
                    _scanningFunction = ScanningFunction::Attlist3;
                    return Token::NOTATION;
                case u'M':
                    if (_chars[_curPos + 2] != u'T' || _chars[_curPos + 3] != u'O' ||
                        _chars[_curPos + 4] != u'K' || _chars[_curPos + 5] != u'E' ||
                        _chars[_curPos + 6] != u'N') {
                        Throw(_curPos, "Xml_InvalidAttributeType");
                    }
                    _scanningFunction = ScanningFunction::Attlist6;

                    if (_chars[_curPos + 7] == u'S') {
                        _curPos += 8;
                        return Token::NMTOKENS;
                    } else {
                        _curPos += 7;
                        return Token::NMTOKEN;
                    }
                default:
                    Throw(_curPos, "Xml_InvalidAttributeType");
                    break;
                }
                break;
            default:
                Throw(_curPos, "Xml_InvalidAttributeType");
                break;
            }

        ReadData:
            if (ReadData() == 0) {
                Throw(_curPos, "Xml_IncompleteDtdContent");
            }
        }
    }

    Token ScanAttlist3() {
        if (_chars[_curPos] == u'(') {
            _curPos++;
            _scanningFunction = ScanningFunction::Name;
            _nextScanningFunction = ScanningFunction::Attlist4;
            return Token::LeftParen;
        } else {
            ThrowUnexpectedToken(_curPos, u"(");
            return Token::None;
        }
    }

    Token ScanAttlist4() {
        switch (_chars[_curPos]) {
        case u')':
            _curPos++;
            _scanningFunction = ScanningFunction::Attlist6;
            return Token::RightParen;
        case u'|':
            _curPos++;
            _scanningFunction = ScanningFunction::Name;
            _nextScanningFunction = ScanningFunction::Attlist4;
            return Token::Or;
        default:
            ThrowUnexpectedToken(_curPos, u")", u"|");
            return Token::None;
        }
    }

    Token ScanAttlist5() {
        switch (_chars[_curPos]) {
        case u')':
            _curPos++;
            _scanningFunction = ScanningFunction::Attlist6;
            return Token::RightParen;
        case u'|':
            _curPos++;
            _scanningFunction = ScanningFunction::Nmtoken;
            _nextScanningFunction = ScanningFunction::Attlist5;
            return Token::Or;
        default:
            ThrowUnexpectedToken(_curPos, u")", u"|");
            return Token::None;
        }
    }

    Token ScanAttlist6() {
        while (true) {
            switch (_chars[_curPos]) {
            case u'"':
            case u'\'':
                ScanLiteral(LiteralType::AttributeValue);
                _scanningFunction = ScanningFunction::Attlist1;
                return Token::Literal;
            case u'#':
                if (_charsUsed - _curPos < 6)
                    goto ReadData;
                switch (_chars[_curPos + 1]) {
                case u'R':
                    if (_charsUsed - _curPos < 9)
                        goto ReadData;
                    if (_chars[_curPos + 2] != u'E' || _chars[_curPos + 3] != u'Q' ||
                        _chars[_curPos + 4] != u'U' || _chars[_curPos + 5] != u'I' ||
                        _chars[_curPos + 6] != u'R' || _chars[_curPos + 7] != u'E' ||
                        _chars[_curPos + 8] != u'D') {
                        Throw(_curPos, "Xml_ExpectAttType");
                    }
                    _curPos += 9;
                    _scanningFunction = ScanningFunction::Attlist1;
                    return Token::REQUIRED;
                case u'I':
                    if (_charsUsed - _curPos < 8)
                        goto ReadData;
                    if (_chars[_curPos + 2] != u'M' || _chars[_curPos + 3] != u'P' ||
                        _chars[_curPos + 4] != u'L' || _chars[_curPos + 5] != u'I' ||
                        _chars[_curPos + 6] != u'E' || _chars[_curPos + 7] != u'D') {
                        Throw(_curPos, "Xml_ExpectAttType");
                    }
                    _curPos += 8;
                    _scanningFunction = ScanningFunction::Attlist1;
                    return Token::IMPLIED;
                case u'F':
                    if (_chars[_curPos + 2] != u'I' || _chars[_curPos + 3] != u'X' ||
                        _chars[_curPos + 4] != u'E' || _chars[_curPos + 5] != u'D') {
                        Throw(_curPos, "Xml_ExpectAttType");
                    }
                    _curPos += 6;
                    _scanningFunction = ScanningFunction::Attlist7;
                    return Token::FIXED;
                default:
                    Throw(_curPos, "Xml_ExpectAttType");
                    break;
                }
                break;
            default:
                Throw(_curPos, "Xml_ExpectAttType");
                break;
            }
        ReadData:
            if (ReadData() == 0) {
                Throw(_curPos, "Xml_IncompleteDtdContent");
            }
        }
    }

    Token ScanAttlist7() {
        switch (_chars[_curPos]) {
        case u'"':
        case u'\'':
            ScanLiteral(LiteralType::AttributeValue);
            _scanningFunction = ScanningFunction::Attlist1;
            return Token::Literal;
        default:
            ThrowUnexpectedToken(_curPos, u"\"", u"'");
            return Token::None;
        }
    }

    Token ScanLiteral(LiteralType literalType) {

        char16_t quoteChar = _chars[_curPos];
        char16_t replaceChar = (literalType == LiteralType::AttributeValue) ? (char16_t)0x20 : (char16_t)0xA;
        int startQuoteEntityId = _currentEntityId;

        _literalLineInfo = {LineNo(), LinePos()};

        _curPos++;
        _tokenStartPos = _curPos;

        _stringBuilder.clear();

        while (true) {
            while (IsAttributeValueChar(_chars[_curPos]) && _chars[_curPos] != u'%') {
                _curPos++;
            }

            if (_chars[_curPos] == quoteChar && _currentEntityId == startQuoteEntityId) {
                if (_stringBuilder.size() > 0) {
                    _stringBuilder.append(_chars.data() + _tokenStartPos, _curPos - _tokenStartPos);
                }
                _curPos++;
                _literalQuoteChar = quoteChar;
                return Token::Literal;
            }

            int tmp1 = _curPos - _tokenStartPos;
            if (tmp1 > 0) {
                _stringBuilder.append(_chars.data() + _tokenStartPos, tmp1);
                _tokenStartPos = _curPos;
            }

            switch (_chars[_curPos]) {
            case u'"':
            case u'\'':
            case u'>':
                _curPos++;
                continue;

            case (char16_t)0xA:
                _curPos++;
                if (_normalize) {
                    Append(_stringBuilder, replaceChar);
                    _tokenStartPos = _curPos;
                }
                _readerAdapter.on_new_line(_curPos);
                continue;
            case (char16_t)0xD:
                if (_chars[_curPos + 1] == (char16_t)0xA) {
                    if (_normalize) {
                        if (literalType == LiteralType::AttributeValue) {
                            Append(_stringBuilder,
                                   _readerAdapter.entity_eol_normalized() ? u"\u0020\u0020" : u"\u0020");
                        } else {
                            Append(_stringBuilder,
                                   _readerAdapter.entity_eol_normalized() ? u"\u000D\u000A" : u"\u000A");
                        }
                        _tokenStartPos = _curPos + 2;

                        SaveParsingBuffer();
                        _readerAdapter.current_position(_readerAdapter.current_position() + 1);
                    }
                    _curPos += 2;
                } else if (_curPos + 1 == _charsUsed) {
                    goto ReadData;
                } else {
                    _curPos++;
                    if (_normalize) {
                        Append(_stringBuilder, replaceChar);
                        _tokenStartPos = _curPos;
                    }
                }
                _readerAdapter.on_new_line(_curPos);
                continue;

            case (char16_t)0x9:
                if (literalType == LiteralType::AttributeValue && _normalize) {
                    Append(_stringBuilder, (char16_t)0x20);
                    _tokenStartPos++;
                }
                _curPos++;
                continue;

            case u'<':
                if (literalType == LiteralType::AttributeValue) {
                    Throw(_curPos, "Xml_BadAttributeChar", CharArgs(u'<', u'\0'));
                }
                _curPos++;
                continue;

            case u'%':
                if (literalType != LiteralType::EntityReplText) {
                    _curPos++;
                    continue;
                }
                HandleEntityReference(true, true, literalType == LiteralType::AttributeValue);
                _tokenStartPos = _curPos;
                continue;

            case u'&':
                if (literalType == LiteralType::SystemOrPublicID) {
                    _curPos++;
                    continue;
                }
                if (_curPos + 1 == _charsUsed) {
                    goto ReadData;
                }

                if (_chars[_curPos + 1] == u'#') {
                    SaveParsingBuffer();
                    int endPos = _readerAdapter.parse_numeric_reference(
                        SaveInternalSubsetValue() ? &*_internalSubsetValueSb : nullptr);
                    LoadParsingBuffer();
                    _stringBuilder.append(_chars.data() + _curPos, endPos - _curPos);
                    _readerAdapter.current_position(endPos);
                    _tokenStartPos = endPos;
                    _curPos = endPos;
                    continue;
                } else {

                    SaveParsingBuffer();
                    if (literalType == LiteralType::AttributeValue) {
                        int endPos = _readerAdapter.parse_named_reference(
                            true, SaveInternalSubsetValue() ? &*_internalSubsetValueSb : nullptr);
                        LoadParsingBuffer();

                        if (endPos >= 0) {
                            _stringBuilder.append(_chars.data() + _curPos, endPos - _curPos);
                            _readerAdapter.current_position(endPos);
                            _tokenStartPos = endPos;
                            _curPos = endPos;
                            continue;
                        } else {
                            HandleEntityReference(false, true, true);
                            _tokenStartPos = _curPos;
                        }
                        continue;
                    } else {
                        int endPos = _readerAdapter.parse_named_reference(false, nullptr);
                        LoadParsingBuffer();

                        if (endPos >= 0) {
                            _tokenStartPos = _curPos;
                            _curPos = endPos;
                            continue;
                        } else {
                            Append(_stringBuilder, u'&');
                            _curPos++;
                            _tokenStartPos = _curPos;

                            MaybeName entityName = ScanEntityName();
                            VerifyEntityReference(entityName, false, false, false);
                        }
                        continue;
                    }
                }
            default:

                if (_curPos == _charsUsed) {
                    goto ReadData;
                }

                else {
                    char16_t ch = _chars[_curPos];
                    if (IsHighSurrogate(ch)) {
                        if (_curPos + 1 == _charsUsed) {
                            goto ReadData;
                        }
                        _curPos++;
                        if (IsLowSurrogate(_chars[_curPos])) {
                            _curPos++;
                            continue;
                        }
                    }
                    ThrowInvalidChar(_chars, _charsUsed, _curPos);
                    return Token::None;
                }
            }

        ReadData:

            if (_readerAdapter.is_eof() || ReadData() == 0) {
                if (literalType == LiteralType::SystemOrPublicID || !HandleEntityEnd(true)) {
                    Throw(_curPos, "Xml_UnclosedQuote");
                }
            }
            _tokenStartPos = _curPos;
        }
    }

    MaybeName ScanEntityName() {
        try {
            ScanName();
        } catch (const NiniXmlProviderError& e) {
            if (e.source_type != "System.Xml.XmlException")
                throw;
            Throw("Xml_ErrorParsingEntityName", std::u16string{}, e.line_number.value_or(0),
                  e.line_position.value_or(0));
        }

        if (_chars[_curPos] != u';') {
            ThrowUnexpectedToken(_curPos, u";");
        }
        MaybeName entityName = GetNameQualified(false);
        _curPos++;

        return entityName;
    }

    Token ScanNotation1() {
        switch (_chars[_curPos]) {
        case u'P':
            if (!EatPublicKeyword()) {
                Throw(_curPos, "Xml_ExpectExternalOrClose");
            }
            _nextScanningFunction = ScanningFunction::ClosingTag;
            _scanningFunction = ScanningFunction::PublicId1;
            return Token::PUBLIC;
        case u'S':
            if (!EatSystemKeyword()) {
                Throw(_curPos, "Xml_ExpectExternalOrClose");
            }
            _nextScanningFunction = ScanningFunction::ClosingTag;
            _scanningFunction = ScanningFunction::SystemId;
            return Token::SYSTEM;
        default:
            Throw(_curPos, "Xml_ExpectExternalOrPublicId");
            return Token::None;
        }
    }

    Token ScanSystemId() {
        if (_chars[_curPos] != u'"' && _chars[_curPos] != u'\'') {
            ThrowUnexpectedToken(_curPos, u"\"", u"'");
        }

        ScanLiteral(LiteralType::SystemOrPublicID);

        _scanningFunction = _nextScanningFunction;
        return Token::Literal;
    }

    Token ScanEntity1() {
        if (_chars[_curPos] == u'%') {
            _curPos++;
            _nextScanningFunction = ScanningFunction::Entity2;
            _scanningFunction = ScanningFunction::Name;
            return Token::Percent;
        } else {
            ScanName();
            _scanningFunction = ScanningFunction::Entity2;
            return Token::Name;
        }
    }

    Token ScanEntity2() {
        switch (_chars[_curPos]) {
        case u'P':
            if (!EatPublicKeyword()) {
                Throw(_curPos, "Xml_ExpectExternalOrClose");
            }
            _nextScanningFunction = ScanningFunction::Entity3;
            _scanningFunction = ScanningFunction::PublicId1;
            return Token::PUBLIC;
        case u'S':
            if (!EatSystemKeyword()) {
                Throw(_curPos, "Xml_ExpectExternalOrClose");
            }
            _nextScanningFunction = ScanningFunction::Entity3;
            _scanningFunction = ScanningFunction::SystemId;
            return Token::SYSTEM;

        case u'"':
        case u'\'':
            ScanLiteral(LiteralType::EntityReplText);
            _scanningFunction = ScanningFunction::ClosingTag;
            return Token::Literal;
        default:
            Throw(_curPos, "Xml_ExpectExternalIdOrEntityValue");
            return Token::None;
        }
    }

    Token ScanEntity3() {
        if (_chars[_curPos] == u'N') {
            while (_charsUsed - _curPos < 5) {
                if (ReadData() == 0) {
                    goto End;
                }
            }
            if (_chars[_curPos + 1] == u'D' && _chars[_curPos + 2] == u'A' && _chars[_curPos + 3] == u'T' &&
                _chars[_curPos + 4] == u'A') {
                _curPos += 5;
                _scanningFunction = ScanningFunction::Name;
                _nextScanningFunction = ScanningFunction::ClosingTag;
                return Token::NData;
            }
        }
    End:
        _scanningFunction = ScanningFunction::ClosingTag;
        return Token::None;
    }

    Token ScanPublicId1() {
        if (_chars[_curPos] != u'"' && _chars[_curPos] != u'\'') {
            ThrowUnexpectedToken(_curPos, u"\"", u"'");
        }

        ScanLiteral(LiteralType::SystemOrPublicID);

        _scanningFunction = ScanningFunction::PublicId2;
        return Token::Literal;
    }

    Token ScanPublicId2() {
        if (_chars[_curPos] != u'"' && _chars[_curPos] != u'\'') {
            _scanningFunction = _nextScanningFunction;
            return Token::None;
        }

        ScanLiteral(LiteralType::SystemOrPublicID);
        _scanningFunction = _nextScanningFunction;

        return Token::Literal;
    }

    Token ScanCondSection1() {
        if (_chars[_curPos] != u'I') {
            Throw(_curPos, "Xml_ExpectIgnoreOrInclude");
        }
        _curPos++;

        while (true) {
            if (_charsUsed - _curPos < 5) {
                goto ReadData;
            }
            switch (_chars[_curPos]) {
            case u'N':
                if (_charsUsed - _curPos < 6) {
                    goto ReadData;
                }
                if (_chars[_curPos + 1] != u'C' || _chars[_curPos + 2] != u'L' ||
                    _chars[_curPos + 3] != u'U' || _chars[_curPos + 4] != u'D' ||
                    _chars[_curPos + 5] != u'E' || IsNameSingleChar(_chars[_curPos + 6])) {
                    goto Default25;
                }
                _nextScanningFunction = ScanningFunction::SubsetContent;
                _scanningFunction = ScanningFunction::CondSection2;
                _curPos += 6;
                return Token::INCLUDE;
            case u'G':
                if (_chars[_curPos + 1] != u'N' || _chars[_curPos + 2] != u'O' ||
                    _chars[_curPos + 3] != u'R' || _chars[_curPos + 4] != u'E' ||
                    IsNameSingleChar(_chars[_curPos + 5])) {
                    goto Default25;
                }
                _nextScanningFunction = ScanningFunction::CondSection3;
                _scanningFunction = ScanningFunction::CondSection2;
                _curPos += 5;
                return Token::IGNORE;
            default:
            Default25:
                Throw(_curPos - 1, "Xml_ExpectIgnoreOrInclude");
                return Token::None;
            }
        ReadData:
            if (ReadData() == 0) {
                Throw(_curPos, "Xml_IncompleteDtdContent");
            }
        }
    }

    Token ScanCondSection2() {
        if (_chars[_curPos] != u'[') {
            ThrowUnexpectedToken(_curPos, u"[");
        }
        _curPos++;
        _scanningFunction = _nextScanningFunction;
        return Token::LeftBracket;
    }

    Token ScanCondSection3() {
        int ignoreSectionDepth = 0;

        while (true) {
            while (IsTextChar(_chars[_curPos]) && _chars[_curPos] != u']') {
                _curPos++;
            }

            switch (_chars[_curPos]) {
            case u'"':
            case u'\'':
            case (char16_t)0x9:
            case u'&':
                _curPos++;
                continue;

            case (char16_t)0xA:
                _curPos++;
                _readerAdapter.on_new_line(_curPos);
                continue;
            case (char16_t)0xD:
                if (_chars[_curPos + 1] == (char16_t)0xA) {
                    _curPos += 2;
                } else if (_curPos + 1 < _charsUsed || _readerAdapter.is_eof()) {
                    _curPos++;
                } else {
                    goto ReadData;
                }
                _readerAdapter.on_new_line(_curPos);
                continue;
            case u'<':
                if (_charsUsed - _curPos < 3) {
                    goto ReadData;
                }
                if (_chars[_curPos + 1] != u'!' || _chars[_curPos + 2] != u'[') {
                    _curPos++;
                    continue;
                }
                ignoreSectionDepth++;
                _curPos += 3;
                continue;
            case u']':
                if (_charsUsed - _curPos < 3) {
                    goto ReadData;
                }
                if (_chars[_curPos + 1] != u']' || _chars[_curPos + 2] != u'>') {
                    _curPos++;
                    continue;
                }
                if (ignoreSectionDepth > 0) {
                    ignoreSectionDepth--;
                    _curPos += 3;
                    continue;
                } else {
                    _curPos += 3;
                    _scanningFunction = ScanningFunction::SubsetContent;
                    return Token::CondSectionEnd;
                }
            default:

                if (_curPos == _charsUsed) {
                    goto ReadData;
                }

                else {
                    char16_t ch = _chars[_curPos];
                    if (IsHighSurrogate(ch)) {
                        if (_curPos + 1 == _charsUsed) {
                            goto ReadData;
                        }
                        _curPos++;
                        if (IsLowSurrogate(_chars[_curPos])) {
                            _curPos++;
                            continue;
                        }
                    }
                    ThrowInvalidChar(_chars, _charsUsed, _curPos);
                    return Token::None;
                }
            }

        ReadData:

            if (_readerAdapter.is_eof() || ReadData() == 0) {
                if (HandleEntityEnd(false)) {
                    continue;
                }
                Throw(_curPos, "Xml_UnclosedConditionalSection");
            }
            _tokenStartPos = _curPos;
        }
    }

    void ScanName() {
        ScanQName(false);
    }

    void ScanQName() {
        ScanQName(_supportNamespaces);
    }

    void ScanQName(bool isQName) {
        _tokenStartPos = _curPos;
        int colonOffset = -1;

        while (true) {
            if (IsStartNCNameSingleChar(_chars[_curPos]) || _chars[_curPos] == u':') {
                _curPos++;
            } else {
                if (_curPos + 1 >= _charsUsed) {
                    if (ReadDataInName()) {
                        continue;
                    }
                    Throw(_curPos, "Xml_UnexpectedEOF", u"Name");
                } else {
                    Throw(_curPos, "Xml_BadStartNameChar", CharArgs(_chars, _charsUsed, _curPos));
                }
            }

        ContinueName:

            while (true) {
                if (IsNCNameSingleChar(_chars[_curPos])) {
                    _curPos++;
                } else {
                    break;
                }
            }

            if (_chars[_curPos] == u':') {
                if (isQName) {
                    if (colonOffset != -1) {
                        Throw(_curPos, "Xml_BadNameChar", CharArgs(u':', u'\0'));
                    }
                    colonOffset = _curPos - _tokenStartPos;
                    _curPos++;
                    continue;
                } else {
                    _curPos++;
                    goto ContinueName;
                }
            }

            else if (_curPos == _charsUsed) {
                if (ReadDataInName()) {
                    goto ContinueName;
                }
                if (_tokenStartPos == _curPos) {
                    Throw(_curPos, "Xml_UnexpectedEOF", u"Name");
                }
            }

            _colonPos = (colonOffset == -1) ? -1 : _tokenStartPos + colonOffset;
            return;
        }
    }

    bool ReadDataInName() {
        int offset = _curPos - _tokenStartPos;
        _curPos = _tokenStartPos;
        bool newDataRead = (ReadData() != 0);
        _tokenStartPos = _curPos;
        _curPos += offset;
        return newDataRead;
    }

    void ScanNmtoken() {
        _tokenStartPos = _curPos;

        while (true) {
            while (true) {
                if (IsNCNameSingleChar(_chars[_curPos]) || _chars[_curPos] == u':') {
                    _curPos++;
                } else {
                    break;
                }
            }

            if (_curPos < _charsUsed) {
                if (_curPos - _tokenStartPos == 0) {
                    Throw(_curPos, "Xml_BadNameChar", CharArgs(_chars, _charsUsed, _curPos));
                }
                return;
            }

            int len = _curPos - _tokenStartPos;
            _curPos = _tokenStartPos;
            if (ReadData() == 0) {
                if (len > 0) {
                    _tokenStartPos = _curPos;
                    _curPos += len;
                    return;
                }
                Throw(_curPos, "Xml_UnexpectedEOF", u"NmToken");
            }
            _tokenStartPos = _curPos;
            _curPos += len;
        }
    }

    bool EatPublicKeyword() {

        while (_charsUsed - _curPos < 6) {
            if (ReadData() == 0) {
                return false;
            }
        }

        if (!std::u16string_view(_chars.data() + _curPos + 1, _chars.size() - _curPos - 1)
                 .starts_with(u"UBLIC")) {
            return false;
        }
        _curPos += 6;
        return true;
    }

    bool EatSystemKeyword() {

        while (_charsUsed - _curPos < 6) {
            if (ReadData() == 0) {
                return false;
            }
        }

        if (!std::u16string_view(_chars.data() + _curPos + 1, _chars.size() - _curPos - 1)
                 .starts_with(u"YSTEM")) {
            return false;
        }
        _curPos += 6;
        return true;
    }
};
static std::u16string Decimal(int n) {
    auto a = std::to_string(n);
    return {a.begin(), a.end()};
}
static std::u16string Resource(std::string_view key, const std::vector<std::u16string>& args) {
    auto it = std::find_if(std::begin(dtd_resources), std::end(dtd_resources),
                           [&](auto& v) { return v.first == key; });
    if (it == std::end(dtd_resources))
        throw std::logic_error("Missing pinned XML resource: " + std::string(key));
    std::u16string result;
    auto s = it->second;
    for (std::size_t p = 0; p < s.size();) {
        if (p + 2 < s.size() && s[p] == u'{' && s[p + 1] >= u'0' && s[p + 1] <= u'9' && s[p + 2] == u'}') {
            auto i = s[p + 1] - u'0';
            if (i >= args.size())
                throw std::logic_error("XML resource argument mismatch");
            result += args[i];
            p += 3;
        } else
            result += s[p++];
    }
    return result;
}
static NiniXmlDtdError XmlError(std::string_view key, const std::vector<std::u16string>& args, int line,
                                int column, std::optional<std::u16string> uri) {
    auto message = Resource(key, args);
    if (line != 0)
        message = Resource("Xml_MessageWithErrorPosition", {message, Decimal(line), Decimal(column)});
    NiniXmlDtdError error("System.Xml.XmlException", 0x80131940u, std::move(message));
    error.resource_name = key;
    error.line_number = line;
    error.line_position = column;
    error.source_uri = std::move(uri);
    return error;
}
std::vector<std::u16string> DtdParser::CharArgs(char16_t c, char16_t next) {
    std::u16string text;
    unsigned code = c;
    if (IsHighSurrogate(c) && next != 0) {
        text = {c, next};
        code = (std::uint32_t(next) - 0xdc00u) | (((std::uint32_t(c) - 0xd800u) << 10) + 0x10000u);
    } else
        text = c == 0 ? u"." : std::u16string(1, c);
    std::u16string hex;
    do {
        hex.insert(hex.begin(), u"0123456789ABCDEF"[code & 15]);
        code >>= 4;
    } while (code);
    if (hex.size() < 2)
        hex.insert(hex.begin(), u'0');
    return {text, u"0x" + hex};
}
std::vector<std::u16string> DtdParser::CharArgs(std::span<char16_t> chars, int len, int p) {
    return CharArgs(chars[p], p + 1 < len ? chars[p + 1] : u'\0');
}
void DtdParser::Throw(int p, std::string_view r, const std::vector<std::u16string>& args) {
    _curPos = p;
    auto uri = _readerAdapter.base_uri();
    int line = LineNo();
    int column = LinePos();
    _readerAdapter.throw_error(XmlError(r, args, line, column, uri ? std::optional(*uri) : std::nullopt));
}
void DtdParser::Throw(std::string_view r, std::u16string_view arg, int line, int col) {
    auto uri = _readerAdapter.base_uri();
    _readerAdapter.throw_error(
        XmlError(r, {std::u16string(arg)}, line, col, uri ? std::optional(*uri) : std::nullopt));
}
void DtdParser::OnUnexpectedError() {
    Throw(_curPos, "Xml_InternalError");
}
std::u16string DtdParser::ParseUnexpectedToken(int p) {
    int end = p;
    if (IsNCNameSingleChar(_chars[p]))
        while (IsNCNameSingleChar(_chars[end]))
            ++end;
    return {_chars.data() + p, static_cast<std::size_t>(std::max(1, end - p))};
}
void DtdParser::ThrowUnexpectedToken(int p, std::u16string_view a, std::optional<std::u16string_view> b) {
    auto token = ParseUnexpectedToken(p);
    if (b)
        Throw(_curPos, "Xml_UnexpectedTokens2",
              std::vector<std::u16string>{token, std::u16string(a), std::u16string(*b)});
    else
        Throw(_curPos, "Xml_UnexpectedTokenEx", std::vector<std::u16string>{token, std::u16string(a)});
}
MaybeName DtdParser::GetNameQualified(bool prefix) {
    if (_colonPos == -1) {
        auto name = _readerAdapter.intern(
            std::u16string_view(_chars.data() + _tokenStartPos, _curPos - _tokenStartPos));
        return Name{name ? *name : std::u16string{}, {}};
    }
    if (prefix) {
        auto local = _readerAdapter.intern(
            std::u16string_view(_chars.data() + _colonPos + 1, _curPos - _colonPos - 1));
        auto pre = _readerAdapter.intern(
            std::u16string_view(_chars.data() + _tokenStartPos, _colonPos - _tokenStartPos));
        return Name{local ? *local : std::u16string{}, pre ? *pre : std::u16string{}};
    }
    Throw(_tokenStartPos, "Xml_ColonInLocalName", GetNameString());
    return {};
}
std::u16string DtdParser::StripSpaces(std::u16string s) {
    if (s.empty())
        return {};
    auto start = s.find_first_not_of(u' ');
    if (start == s.npos)
        return u" ";
    auto end = s.find_last_not_of(u' ');
    std::u16string result;
    for (auto i = start; i <= end; ++i) {
        result += s[i];
        if (s[i] == u' ')
            while (i < end && s[i + 1] == u' ')
                ++i;
    }
    return result;
}
void DtdParser::SendValidationEvent(int p, int severity, std::string_view key,
                                    std::optional<std::u16string> arg) {
    auto uri = BaseUriStr();
    auto message = Resource(key, {arg.value_or(u"")});
    NiniXmlProviderError e("System.Xml.Schema.XmlSchemaException", 0x80131941u, std::move(message));
    e.source_uri = uri;
    e.line_number = LineNo();
    e.line_position = LinePos() + p - _curPos;
    if (_readerAdapter.has_validation_handler())
        _readerAdapter.validation_event(e, severity);
}
bool DtdParser::HandleEntityReference(bool param, bool literal, bool attribute) {
    ++_curPos;
    return HandleEntityReference(ScanEntityName(), param, literal, attribute);
}
bool DtdParser::HandleEntityReference(MaybeName maybe_name, bool param, bool literal, bool attribute) {
    Name name = maybe_name.value_or(Name{});
    SaveParsingBuffer();
    if (param && ParsingInternalSubset() && !ParsingTopLevelMarkup()) {
        if (!maybe_name)
            throw NiniXmlProviderError("System.NullReferenceException", 0x80004003u,
                                       u"Object reference not set to an instance of an object.");
        Throw(_curPos - static_cast<int>(name.local_name.size()) - 1, "Xml_InvalidParEntityRef");
    }
    auto entity = VerifyEntityReference(maybe_name, param, true, attribute);
    if (!entity)
        return false;
    if (entity->parsing_in_progress)
        Throw(_curPos - static_cast<int>(name.local_name.size()) - 1,
              param ? "Xml_RecursiveParEntity" : "Xml_RecursiveGenEntity", name.local_name);
    int id;
    if (entity->external) {
        if (!_readerAdapter.push_entity(entity, id))
            return false;
        ++_externalEntitiesDepth;
    } else {
        if (!entity->text)
            throw NiniXmlProviderError("System.NullReferenceException", 0x80004003u,
                                       u"Object reference not set to an instance of an object.");
        if (entity->text->empty())
            return false;
        if (!_readerAdapter.push_entity(entity, id))
            return false;
    }
    _currentEntityId = id;
    if (param && !literal && _scanningFunction != ScanningFunction::ParamEntitySpace) {
        _savedScanningFunction = _scanningFunction;
        _scanningFunction = ScanningFunction::ParamEntitySpace;
    }
    LoadParsingBuffer();
    return true;
}
bool DtdParser::HandleEntityEnd(bool literal) {
    SaveParsingBuffer();
    Entity old;
    if (!_readerAdapter.pop_entity(old, _currentEntityId))
        return false;
    LoadParsingBuffer();
    if (!old) {
        if (_scanningFunction == ScanningFunction::ParamEntitySpace)
            _scanningFunction = _savedScanningFunction;
        return false;
    }
    if (old->external)
        --_externalEntitiesDepth;
    if (!literal && _scanningFunction != ScanningFunction::ParamEntitySpace) {
        _savedScanningFunction = _scanningFunction;
        _scanningFunction = ScanningFunction::ParamEntitySpace;
    }
    return true;
}
Entity DtdParser::VerifyEntityReference(MaybeName maybe_name, bool param, bool must, bool attribute) {
    if (!maybe_name)
        throw NiniXmlProviderError("System.ArgumentNullException", 0x80004003u,
                                   u"Value cannot be null. (Parameter 'key')", u"key");
    const auto& name = *maybe_name;
    auto& list = param ? _schemaInfo->parameter_entities : _schemaInfo->general_entities;
    auto p = std::find_if(list.begin(), list.end(), [&](auto& e) { return e->name == name; });
    if (p == list.end()) {
        if (param) {
            if (_validate)
                SendValidationEvent(_curPos - static_cast<int>(name.local_name.size()) - 1, 0,
                                    "Xml_UndeclaredParEntity", name.local_name);
        } else if (must) {
            if (!ParsingInternalSubset()) {
                if (_validate)
                    SendValidationEvent(_curPos - static_cast<int>(name.local_name.size()) - 1, 0,
                                        "Xml_UndeclaredEntity", name.local_name);
            } else
                Throw(_curPos - static_cast<int>(name.local_name.size()) - 1, "Xml_UndeclaredEntity",
                      name.local_name);
        }
        return {};
    }
    auto e = *p;
    if (e->unparsed())
        Throw(_curPos - static_cast<int>(name.local_name.size()) - 1, "Xml_UnparsedEntityRef",
              name.local_name);
    if (attribute && e->external)
        Throw(_curPos - static_cast<int>(name.local_name.size()) - 1, "Xml_ExternalEntityInAttValue",
              name.local_name);
    return e;
}
void DtdParser::ParseInDocumentDtd(bool save) {
    LoadParsingBuffer();
    _scanningFunction = ScanningFunction::QName;
    _nextScanningFunction = ScanningFunction::Doctype1;
    if (GetToken(false) != Token::QName)
        OnUnexpectedError();
    _schemaInfo->name = GetNameQualified(true).value();
    auto token = GetToken(false);
    if (token == Token::SYSTEM || token == Token::PUBLIC) {
        ParseExternalId(token, Token::DOCTYPE, _publicId, _systemId);
        token = GetToken(false);
    }
    if (token == Token::LeftBracket) {
        if (save) {
            SaveParsingBuffer();
            _internalSubsetValueSb.emplace();
        }
        ParseSubset();
    } else if (token != Token::GreaterThan)
        OnUnexpectedError();
    SaveParsingBuffer();
    if (_systemId && !_systemId->empty())
        ParseExternalSubset();
}
void DtdParser::ParseExternalSubset() {
    if (!_readerAdapter.push_external_subset(_systemId, _publicId))
        return;
    auto uri = _readerAdapter.base_uri();
    if (uri)
        _externalDtdBaseUri = *uri;
    ++_externalEntitiesDepth;
    LoadParsingBuffer();
    ParseSubset();
}
void DtdParser::ParseSubset() {
    for (;;) {
        auto token = GetToken(false);
        int start = _currentEntityId;
        switch (token) {
        case Token::AttlistDecl:
            ParseAttlistDecl();
            break;
        case Token::ElementDecl:
            ParseElementDecl();
            break;
        case Token::EntityDecl:
            ParseEntityDecl();
            break;
        case Token::NotationDecl:
            ParseNotationDecl();
            break;
        case Token::Comment:
            ParseComment();
            break;
        case Token::PI:
            ParsePI();
            break;
        case Token::CondSectionStart:
            if (ParsingInternalSubset())
                Throw(_curPos - 3, "Xml_InvalidConditionalSection");
            ParseCondSection();
            start = _currentEntityId;
            break;
        case Token::CondSectionEnd:
            if (_condSectionDepth > 0) {
                --_condSectionDepth;
                if (_validate && _currentEntityId != _condSectionEntityIds[_condSectionDepth])
                    SendValidationEvent(_curPos, 0, "Sch_ParEntityRefNesting", u"");
            } else
                Throw(_curPos - 3, "Xml_UnexpectedCDataEnd");
            break;
        case Token::RightBracket:
            if (!ParsingInternalSubset())
                Throw(_curPos, "Xml_ExpectDtdMarkup");
            if (_condSectionDepth != 0)
                Throw(_curPos, "Xml_UnclosedConditionalSection");
            if (_internalSubsetValueSb) {
                SaveParsingBuffer(_curPos - 1);
                _schemaInfo->internal_subset = *_internalSubsetValueSb;
                _internalSubsetValueSb.reset();
            }
            if (GetToken(false) != Token::GreaterThan)
                ThrowUnexpectedToken(_curPos, u">");
            return;
        case Token::Eof:
            if (ParsingInternalSubset() && !_freeFloatingDtd)
                Throw(_curPos, "Xml_IncompleteDtdContent");
            if (_condSectionDepth != 0)
                Throw(_curPos, "Xml_UnclosedConditionalSection");
            return;
        default:
            break;
        }
        if (_currentEntityId != start) {
            if (_validate)
                SendValidationEvent(_curPos, 0, "Sch_ParEntityRefNesting", u"");
            else if (!_v1Compat)
                Throw(_curPos, "Sch_ParEntityRefNesting");
        }
    }
}
void DtdParser::ParseComment() {
    SaveParsingBuffer();
    try {
        if (SaveInternalSubsetValue()) {
            _readerAdapter.parse_comment(&*_internalSubsetValueSb);
            *_internalSubsetValueSb += u"-->";
        } else
            _readerAdapter.parse_comment(nullptr);
    } catch (const NiniXmlDtdError& e) {
        if (e.resource_name == "Xml_UnexpectedEOF" && _currentEntityId != 0)
            SendValidationEvent(_curPos, 0, "Sch_ParEntityRefNesting", {});
        else
            throw;
    }
    LoadParsingBuffer();
}
void DtdParser::ParsePI() {
    SaveParsingBuffer();
    if (SaveInternalSubsetValue()) {
        _readerAdapter.parse_processing_instruction(&*_internalSubsetValueSb);
        *_internalSubsetValueSb += u"?>";
    } else
        _readerAdapter.parse_processing_instruction(nullptr);
    LoadParsingBuffer();
}
void DtdParser::ParseCondSection() {
    int id = _currentEntityId;
    auto t = GetToken(false);
    if (t != Token::INCLUDE && t != Token::IGNORE) {
        OnUnexpectedError();
        return;
    }
    if (GetToken(false) != Token::LeftBracket) {
        OnUnexpectedError();
        return;
    }
    if (_validate && id != _currentEntityId)
        SendValidationEvent(_curPos, 0, "Sch_ParEntityRefNesting", u"");
    if (t == Token::INCLUDE) {
        if (_validate) {
            if (_condSectionEntityIds.empty())
                _condSectionEntityIds.resize(2);
            else if (_condSectionEntityIds.size() == _condSectionDepth)
                _condSectionEntityIds.resize(_condSectionEntityIds.size() * 2);
            _condSectionEntityIds[_condSectionDepth] = id;
        }
        ++_condSectionDepth;
    } else {
        if (GetToken(false) != Token::CondSectionEnd) {
            OnUnexpectedError();
            return;
        }
        if (_validate && id != _currentEntityId)
            SendValidationEvent(_curPos, 0, "Sch_ParEntityRefNesting", u"");
    }
}
void DtdParser::ParseExternalId(Token type, Token decl, NiniWriterText& pub, NiniWriterText& sys) {
    NiniXmlDtdLine keyword{LineNo(), LinePos() - 6};
    pub.reset();
    sys.reset();
    if (GetToken(true) != Token::Literal)
        ThrowUnexpectedToken(_curPos, u"\"", u"'");
    if (type == Token::SYSTEM) {
        sys = nini_writer_text(GetValue());
        auto p = sys->find(u'#');
        if (p != sys->npos)
            Throw(_curPos - static_cast<int>(sys->size()) - 1, "Xml_FragmentId",
                  std::vector<std::u16string>{sys->substr(p), *sys});
        if (decl == Token::DOCTYPE && !_freeFloatingDtd) {
            ++_literalLineInfo.position;
            _readerAdapter.on_system_id(sys, keyword, _literalLineInfo);
        }
    } else {
        pub = nini_writer_text(GetValue());
        auto i = nini_xml_invalid_public_id(*pub);
        if (i >= 0)
            Throw(_curPos - 1 - static_cast<int>(pub->size()) + static_cast<int>(i), "Xml_InvalidCharacter",
                  CharArgs((*pub)[i], i + 1 < pub->size() ? (*pub)[i + 1] : u'\0'));
        if (decl == Token::DOCTYPE && !_freeFloatingDtd) {
            ++_literalLineInfo.position;
            _readerAdapter.on_public_id(pub, keyword, _literalLineInfo);
            if (GetToken(false) == Token::Literal) {
                if (!_whitespaceSeen)
                    Throw("Xml_ExpectingWhiteSpace", std::u16string(1, _literalQuoteChar),
                          _literalLineInfo.number, _literalLineInfo.position);
                sys = nini_writer_text(GetValue());
                ++_literalLineInfo.position;
                _readerAdapter.on_system_id(sys, keyword, _literalLineInfo);
            } else
                ThrowUnexpectedToken(_curPos, u"\"", u"'");
        } else if (GetToken(false) == Token::Literal) {
            if (!_whitespaceSeen)
                Throw("Xml_ExpectingWhiteSpace", std::u16string(1, _literalQuoteChar),
                      _literalLineInfo.number, _literalLineInfo.position);
            sys = nini_writer_text(GetValue());
        } else if (decl != Token::NOTATION)
            ThrowUnexpectedToken(_curPos, u"\"", u"'");
    }
}
void DtdParser::AddUndeclaredNotation(std::u16string name) {
    NiniXmlDtdLine line{LineNo(), LinePos() - static_cast<int>(name.size())};
    auto p = std::find_if(_undeclaredNotations.begin(), _undeclaredNotations.end(),
                          [&](auto& n) { return n.active && n.name == name; });
    if (p != _undeclaredNotations.end()) {
        ++p->count;
        return;
    }
    UndeclaredNotation n{name, line, 1};
    if (_freeNotationSlots.empty())
        _undeclaredNotations.push_back(std::move(n));
    else {
        auto slot = _freeNotationSlots.back();
        _freeNotationSlots.pop_back();
        _undeclaredNotations[slot] = std::move(n);
    }
}
void DtdParser::ParseEntityDecl() {
    bool param = false;
    auto t = GetToken(true);
    if (t == Token::Percent) {
        param = true;
        t = GetToken(true);
    }
    if (t != Token::Name) {
        OnUnexpectedError();
        return;
    }
    auto maybe_name = GetNameQualified(false);
    auto name = maybe_name.value_or(Name{});
    auto entity = std::make_shared<NiniXmlDtdEntity>();
    entity->name = name;
    entity->parameter = param;
    entity->base_uri = nini_writer_text(BaseUriStr());
    entity->declared_uri =
        nini_writer_text(_externalDtdBaseUri.empty() ? _documentBaseUri : _externalDtdBaseUri);
    auto& list = param ? _schemaInfo->parameter_entities : _schemaInfo->general_entities;
    if (!maybe_name)
        throw NiniXmlProviderError("System.ArgumentNullException", 0x80004003u,
                                   u"Value cannot be null. (Parameter 'key')", u"key");
    if (std::none_of(list.begin(), list.end(), [&](auto& e) { return e->name == name; }))
        list.push_back(entity);
    entity->declared_in_external = !ParsingInternalSubset();
    entity->parsing_in_progress = true;
    t = GetToken(true);
    if (t == Token::PUBLIC || t == Token::SYSTEM) {
        ParseExternalId(t, Token::EntityDecl, entity->public_id, entity->system_id);
        entity->external = true;
        if (GetToken(false) == Token::NData) {
            if (param)
                ThrowUnexpectedToken(_curPos - 5, u">");
            if (!_whitespaceSeen)
                Throw(_curPos - 5, "Xml_ExpectingWhiteSpace", u"NDATA");
            if (GetToken(true) != Token::Name) {
                OnUnexpectedError();
                return;
            }
            auto ndata = GetNameQualified(false);
            entity->ndata = ndata.value_or(Name{});
            if (!ndata)
                throw NiniXmlProviderError("System.NullReferenceException", 0x80004003u,
                                           u"Object reference not set to an instance of an object.");
            auto nn = entity->ndata.local_name;
            if (std::none_of(_schemaInfo->notations.begin(), _schemaInfo->notations.end(),
                             [&](auto& n) { return n.name.local_name == nn; }))
                AddUndeclaredNotation(nn);
        }
    } else if (t == Token::Literal) {
        entity->text = nini_writer_text(GetValue());
        entity->line = _literalLineInfo;
    } else {
        OnUnexpectedError();
        return;
    }
    if (GetToken(false) != Token::GreaterThan) {
        OnUnexpectedError();
        return;
    }
    entity->parsing_in_progress = false;
}
void DtdParser::ParseNotationDecl() {
    if (GetToken(true) != Token::Name)
        OnUnexpectedError();
    auto maybe_name = GetNameQualified(false);
    if (!maybe_name)
        throw NiniXmlProviderError("System.NullReferenceException", 0x80004003u,
                                   u"Object reference not set to an instance of an object.");
    const auto& name = *maybe_name;
    auto& list = _schemaInfo->notations;
    auto p =
        std::find_if(list.begin(), list.end(), [&](auto& n) { return n.name.local_name == name.local_name; });
    int index = -1;
    if (p == list.end()) {
        for (std::size_t i = 0; i < _undeclaredNotations.size(); ++i)
            if (_undeclaredNotations[i].active && _undeclaredNotations[i].name == name.local_name) {
                _undeclaredNotations[i].active = false;
                _freeNotationSlots.push_back(i);
                break;
            }
        index = static_cast<int>(list.size());
        list.push_back({name, {}, {}});
    } else if (_validate)
        SendValidationEvent(_curPos - static_cast<int>(name.local_name.size()), 0, "Sch_DupNotation",
                            name.local_name);
    auto t = GetToken(true);
    if (t == Token::SYSTEM || t == Token::PUBLIC) {
        NiniWriterText pub, sys;
        ParseExternalId(t, Token::NOTATION, pub, sys);
        if (index >= 0) {
            list[index].public_id = pub;
            list[index].system_id = sys;
        }
    } else
        OnUnexpectedError();
    if (GetToken(false) != Token::GreaterThan)
        OnUnexpectedError();
}
static bool ManagedWhiteSpace(char16_t c) {
    return (c >= 9 && c <= 13) || c == 32 || c == 0x85 || c == 0xa0 || c == 0x1680 ||
           (c >= 0x2000 && c <= 0x200a) || c == 0x2028 || c == 0x2029 || c == 0x202f || c == 0x205f ||
           c == 0x3000;
}
static std::u16string Trim(std::u16string s) {
    std::size_t a = 0, b = s.size();
    while (a < b && ManagedWhiteSpace(s[a]))
        ++a;
    while (b > a && ManagedWhiteSpace(s[b - 1]))
        --b;
    return s.substr(a, b - a);
}
static std::u16string CollapseXmlSpace(std::u16string_view s) {
    std::u16string r;
    bool pending = false;
    for (auto c : s) {
        if (IsWhiteSpace(c)) {
            if (!r.empty())
                pending = true;
        } else {
            if (pending)
                r += u' ';
            r += c;
            pending = false;
        }
    }
    return r;
}
static Attribute FindAttribute(const Element& e, const Name& name) {
    auto p =
        std::find_if(e->attributes.begin(), e->attributes.end(), [&](auto& a) { return a->name == name; });
    return p == e->attributes.end() ? Attribute{} : *p;
}
Element DtdParser::FindElement(const Name& name, bool declare) {
    auto& declared = _schemaInfo->element_declarations;
    auto& undeclared = _schemaInfo->undeclared_element_declarations;
    auto p = std::find_if(declared.begin(), declared.end(), [&](auto& e) { return e->name == name; });
    if (p != declared.end())
        return *p;
    auto q = std::find_if(undeclared.begin(), undeclared.end(), [&](auto& e) { return e->name == name; });
    Element result;
    if (q != undeclared.end()) {
        result = *q;
        if (declare) {
            _freeElementSlots.push_back(_undeclaredElementSlots[result.get()]);
            _undeclaredElementSlots.erase(result.get());
            undeclared.erase(q);
        }
    } else {
        result = std::make_shared<NiniXmlDtdAttributeList>();
        result->name = name;
        if (!declare) {
            std::size_t slot;
            if (_freeElementSlots.empty())
                slot = _nextElementSlot++;
            else {
                slot = _freeElementSlots.back();
                _freeElementSlots.pop_back();
            }
            _undeclaredElementSlots[result.get()] = slot;
            auto before = std::find_if(undeclared.begin(), undeclared.end(),
                                       [&](auto& e) { return _undeclaredElementSlots[e.get()] > slot; });
            undeclared.insert(before, result);
        }
    }
    if (declare)
        declared.push_back(result);
    return result;
}
void DtdParser::CheckXmlSpace(const Attribute& a) {
    bool handler = _readerAdapter.has_validation_handler();
    bool valid = a->tokenized_type == NiniXmlDtdTokenizedType::Enumeration && !a->values.empty() &&
                 a->values.size() <= 2;
    if (valid) {
        if (a->values.size() == 1)
            valid = a->values[0] == u"default" || a->values[0] == u"preserve";
        else
            valid = (a->values[0] == u"default" || a->values[1] == u"default") &&
                    (a->values[0] == u"preserve" || a->values[1] == u"preserve");
    }
    if (!valid) {
        if (!handler)
            throw NiniXmlProviderError("System.NullReferenceException", 0x80004003u,
                                       u"Object reference not set to an instance of an object.");
        NiniXmlProviderError e("System.Xml.Schema.XmlSchemaException", 0x80131941u,
                               Resource("Sch_XmlSpace", {u""}));
        e.line_number = 0;
        e.line_position = 0;
        _readerAdapter.validation_event(e, 0);
    }
}
void DtdParser::ParseAttlistDecl() {
    if (GetToken(true) != Token::QName) {
        OnUnexpectedError();
        return;
    }
    auto element = FindElement(GetNameQualified(true).value(), false);
    Attribute attr;
    for (;;) {
        auto t = GetToken(false);
        if (t == Token::QName) {
            attr = std::make_shared<NiniXmlDtdAttribute>();
            attr->name = GetNameQualified(true).value();
            attr->declared_in_external = !ParsingInternalSubset();
            attr->line = {LineNo(), LinePos() - (_curPos - _tokenStartPos)};
        } else if (t == Token::GreaterThan) {
            if (_v1Compat && attr && attr->name.prefix == u"xml" && attr->name.local_name == u"space") {
                attr->is_xml_attribute = true;
                if (attr->tokenized_type != NiniXmlDtdTokenizedType::Enumeration)
                    Throw("Xml_EnumerationRequired", u"", attr->line.number, attr->line.position);
                if (_validate)
                    CheckXmlSpace(attr);
            }
            return;
        } else {
            OnUnexpectedError();
            return;
        }
        bool duplicate = static_cast<bool>(FindAttribute(element, attr->name));
        ParseAttlistType(attr, element, duplicate);
        ParseAttlistDefault(attr, duplicate);
        if (attr->name.prefix == u"xml") {
            if (attr->name.local_name == u"space") {
                if (_v1Compat) {
                    auto v = Trim(attr->default_value_expanded);
                    if (v == u"preserve" || v == u"default")
                        attr->is_xml_attribute = true;
                } else {
                    attr->is_xml_attribute = true;
                    if (attr->tokenized_type != NiniXmlDtdTokenizedType::Enumeration)
                        Throw("Xml_EnumerationRequired", u"", attr->line.number, attr->line.position);
                    if (_validate)
                        CheckXmlSpace(attr);
                }
            } else if (attr->name.local_name == u"lang")
                attr->is_xml_attribute = true;
        }
        if (!duplicate) {
            element->attributes.push_back(attr);
            if (attr->has_default())
                element->default_attributes.push_back(attr);
            if (!element->id_attribute && attr->tokenized_type == NiniXmlDtdTokenizedType::Id)
                element->id_attribute = attr;
        }
    }
}
void DtdParser::ParseAttlistType(const Attribute& attr, const Element& element, bool ignore) {
    auto t = GetToken(true);
    if (t != Token::CDATA)
        element->has_non_cdata_attributes = true;
    bool notation = false;
    auto& meta = _elementMetadata[element.get()];
    if (static_cast<int>(t) >= 0 && static_cast<int>(t) <= static_cast<int>(Token::NOTATION)) {
        attr->tokenized_type = static_cast<NiniXmlDtdTokenizedType>(t);
        _attributeDatatype[attr.get()] = attr->tokenized_type;
        if (t == Token::IDREFS)
            attr->tokenized_type = NiniXmlDtdTokenizedType::IdRef;
        else if (t == Token::ENTITIES)
            attr->tokenized_type = NiniXmlDtdTokenizedType::Entity;
        else if (t == Token::NMTOKENS)
            attr->tokenized_type = NiniXmlDtdTokenizedType::NmToken;
        if (t == Token::ID) {
            if (_validate && meta.id) {
                auto old = FindAttribute(element, attr->name);
                if ((!old || old->tokenized_type != NiniXmlDtdTokenizedType::Id) && !ignore)
                    SendValidationEvent(_curPos, 0, "Sch_IdAttrDeclared", element->name.qualified());
            }
            meta.id = true;
            return;
        }
        if (t != Token::NOTATION)
            return;
        notation = true;
        if (_validate) {
            if (meta.notation && !ignore)
                SendValidationEvent(_curPos - 8, 0, "Sch_DupNotationAttribute", element->name.qualified());
            else {
                if (meta.empty && !ignore)
                    SendValidationEvent(_curPos - 8, 0, "Sch_NotationAttributeOnEmptyElement",
                                        element->name.qualified());
                meta.notation = true;
            }
        }
        if (GetToken(true) != Token::LeftParen) {
            OnUnexpectedError();
            return;
        }
        if (GetToken(false) != Token::Name) {
            OnUnexpectedError();
            return;
        }
    } else if (t == Token::LeftParen) {
        attr->tokenized_type = NiniXmlDtdTokenizedType::Enumeration;
        _attributeDatatype[attr.get()] = attr->tokenized_type;
        if (GetToken(false) != Token::Nmtoken) {
            OnUnexpectedError();
            return;
        }
    } else {
        OnUnexpectedError();
        return;
    }
    for (;;) {
        auto name = GetNameString();
        if (notation && std::none_of(_schemaInfo->notations.begin(), _schemaInfo->notations.end(),
                                     [&](auto& n) { return n.name.local_name == name; }))
            AddUndeclaredNotation(name);
        if (_validate && !_v1Compat && !ignore &&
            std::find(attr->values.begin(), attr->values.end(), name) != attr->values.end())
            SendValidationEvent(_curPos, 0,
                                notation ? "Xml_AttlistDuplNotationValue" : "Xml_AttlistDuplEnumValue", name);
        attr->values.push_back(name);
        auto next = GetToken(false);
        if (next == Token::RightParen)
            return;
        if (next != Token::Or) {
            OnUnexpectedError();
            return;
        }
        if (GetToken(false) != (notation ? Token::Name : Token::Nmtoken)) {
            OnUnexpectedError();
            return;
        }
    }
}
void DtdParser::ParseAttlistDefault(const Attribute& a, bool ignore) {
    auto t = GetToken(true);
    if (t == Token::REQUIRED) {
        a->presence = NiniXmlDtdUse::Required;
        return;
    }
    if (t == Token::IMPLIED) {
        a->presence = NiniXmlDtdUse::Implied;
        return;
    }
    if (t == Token::FIXED) {
        a->presence = NiniXmlDtdUse::Fixed;
        if (GetToken(true) != Token::Literal) {
            OnUnexpectedError();
            return;
        }
    } else if (t != Token::Literal) {
        OnUnexpectedError();
        return;
    }
    if (_validate && a->tokenized_type == NiniXmlDtdTokenizedType::Id && !ignore)
        SendValidationEvent(_curPos, 0, "Sch_AttListPresence", u"");
    a->default_value_expanded = a->non_cdata() ? GetValueWithStrippedSpaces() : GetValue();
    a->value_line = {_literalLineInfo.number, _literalLineInfo.position + 1};
    SetDefaultTypedValue(a);
}
void DtdParser::SetDefaultTypedValue(const Attribute& a) {
    // DtdValidator.SetDefaultTypedValue catches all datatype/provider failures,
    // then sends its source no-position validation event (which may itself throw).
    try {
        auto type = _attributeDatatype.contains(a.get()) ? _attributeDatatype[a.get()] : a->tokenized_type;
        auto text = a->default_value_expanded;
        if (type != NiniXmlDtdTokenizedType::CData)
            text = Trim(text);
        if (type == NiniXmlDtdTokenizedType::CData) {
            a->default_value_typed = text;
            return;
        }
        text = CollapseXmlSpace(text);
        auto verify = [&](std::u16string_view v, bool nc) {
            auto n = nc ? nini_xml_parse_ncname(v) : nini_xml_parse_nmtoken(v, 0, false);
            if (v.empty() || n != v.size())
                throw std::invalid_argument("XML datatype lexical failure");
        };
        if (type == NiniXmlDtdTokenizedType::Notation) {
            std::size_t colon;
            auto n = nini_xml_parse_qname(text, 0, colon);
            if (text.empty() || n != text.size())
                throw std::invalid_argument("XML QName lexical failure");
            std::u16string prefix = colon ? text.substr(0, colon) : u"",
                           local = colon ? text.substr(colon + 1) : text;
            auto ns = _readerAdapter.lookup_namespace(prefix);
            if (!ns && !prefix.empty())
                throw std::invalid_argument("XML QName namespace failure");
            a->default_value_typed = Name{local, ns ? *ns : u""};
            return;
        }
        bool list = type == NiniXmlDtdTokenizedType::IdRefs || type == NiniXmlDtdTokenizedType::Entities ||
                    type == NiniXmlDtdTokenizedType::NmTokens;
        bool nc = type == NiniXmlDtdTokenizedType::Id || type == NiniXmlDtdTokenizedType::IdRef ||
                  type == NiniXmlDtdTokenizedType::IdRefs || type == NiniXmlDtdTokenizedType::Entity ||
                  type == NiniXmlDtdTokenizedType::Entities;
        if (list) {
            std::vector<std::u16string> values;
            std::size_t p = 0;
            while (p < text.size()) {
                auto q = text.find(u' ', p);
                if (q == text.npos)
                    q = text.size();
                auto value = text.substr(p, q - p);
                verify(value, nc);
                if (nc)
                    _readerAdapter.intern(value);
                values.push_back(std::move(value));
                p = q + 1;
            }
            if (values.empty())
                throw std::invalid_argument("XML datatype empty list");
            a->default_value_typed = std::move(values);
        } else {
            verify(text, nc);
            if (nc)
                _readerAdapter.intern(text);
            a->default_value_typed = text;
        }
    } catch (...) {
        if (_readerAdapter.has_validation_handler()) {
            NiniXmlProviderError e("System.Xml.Schema.XmlSchemaException", 0x80131941u,
                                   Resource("Sch_AttributeDefaultDataType", {a->name.qualified()}));
            e.line_number = 0;
            e.line_position = 0;
            _readerAdapter.validation_event(e, 0);
        }
    }
}
void DtdParser::ParseHowMany(DtdParticle& pcv) {
    switch (GetToken(false)) {
    case Token::Star:
        pcv.closure(0);
        return;
    case Token::QMark:
        pcv.closure(1);
        return;
    case Token::Plus:
        pcv.closure(2);
        return;
    default:
        return;
    }
}
void DtdParser::ParseElementOnlyContent(DtdParticle& pcv, int id) {
    struct Frame {
        int entity;
        Token connector = Token::None;
    };
    std::vector<Frame> frames{{id}};
Loop:
    switch (GetToken(false)) {
    case Token::QName:
        pcv.name(GetNameQualified(true).value());
        ParseHowMany(pcv);
        break;
    case Token::LeftParen:
        pcv.open();
        frames.push_back({_currentEntityId});
        goto Loop;
    case Token::GreaterThan:
        Throw(_curPos, "Xml_InvalidContentModel");
        goto Return;
    default:
        goto UnexpectedError;
    }
ReturnFromRecursiveCall:
    switch (GetToken(false)) {
    case Token::Comma:
        if (frames.back().connector == Token::Or)
            Throw(_curPos, "Xml_InvalidContentModel");
        pcv.connector(false);
        frames.back().connector = Token::Comma;
        break;
    case Token::Or:
        if (frames.back().connector == Token::Comma)
            Throw(_curPos, "Xml_InvalidContentModel");
        pcv.connector(true);
        frames.back().connector = Token::Or;
        break;
    case Token::RightParen:
        pcv.close();
        if (_validate && _currentEntityId != frames.back().entity)
            SendValidationEvent(_curPos, 0, "Sch_ParEntityRefNesting", u"");
        ParseHowMany(pcv);
        goto Return;
    case Token::GreaterThan:
        Throw(_curPos, "Xml_InvalidContentModel");
        goto Return;
    default:
        goto UnexpectedError;
    }
    goto Loop;
UnexpectedError:
    OnUnexpectedError();
Return:
    frames.pop_back();
    if (!frames.empty())
        goto ReturnFromRecursiveCall;
}
void DtdParser::ParseElementMixedContent(DtdParticle& pcv, int start) {
    bool has_names = false;
    int connector = -1, content = _currentEntityId;
    for (;;) {
        switch (GetToken(false)) {
        case Token::RightParen:
            pcv.close();
            if (_validate && _currentEntityId != start)
                SendValidationEvent(_curPos, 0, "Sch_ParEntityRefNesting", u"");
            if (GetToken(false) == Token::Star && has_names)
                pcv.closure(0);
            else if (has_names)
                ThrowUnexpectedToken(_curPos, u"*");
            return;
        case Token::Or: {
            if (!has_names)
                has_names = true;
            else
                pcv.connector(true);
            if (_validate) {
                connector = _currentEntityId;
                if (content < connector)
                    SendValidationEvent(_curPos, 0, "Sch_ParEntityRefNesting", u"");
            }
            if (GetToken(false) != Token::QName)
                goto UnexpectedError;
            auto name = GetNameQualified(true).value();
            if (pcv.exists(name) && _validate)
                SendValidationEvent(_curPos, 0, "Sch_DupElement", name.qualified());
            pcv.name(name);
            if (_validate) {
                content = _currentEntityId;
                if (content < connector)
                    SendValidationEvent(_curPos, 0, "Sch_ParEntityRefNesting", u"");
            }
            continue;
        }
        default:
            goto UnexpectedError;
        }
    UnexpectedError:
        OnUnexpectedError();
    }
}
void DtdParser::ParseElementDecl() {
    if (GetToken(true) != Token::QName) {
        OnUnexpectedError();
        return;
    }
    auto name = GetNameQualified(true).value();
    auto& declared = _schemaInfo->element_declarations;
    bool duplicate = std::any_of(declared.begin(), declared.end(), [&](auto& e) { return e->name == name; });
    if (duplicate && _validate)
        SendValidationEvent(_curPos - static_cast<int>(name.local_name.size()), 0, "Sch_DupElementDecl",
                            GetNameString());
    auto element = FindElement(name, true);
    auto t = GetToken(true);
    auto& meta = _elementMetadata[element.get()];
    if (t == Token::EMPTY)
        meta.empty = true;
    else if (t == Token::ANY)
        meta.empty = false;
    else if (t == Token::LeftParen) {
        int start = _currentEntityId;
        t = GetToken(false);
        if (t == Token::PCDATA) {
            DtdParticle pcv(true);
            pcv.open();
            ParseElementMixedContent(pcv, start);
            meta.empty = pcv.finish();
        } else if (t == Token::None) {
            DtdParticle pcv(false);
            pcv.open();
            ParseElementOnlyContent(pcv, start);
            meta.empty = pcv.finish();
        } else {
            OnUnexpectedError();
            return;
        }
    } else {
        OnUnexpectedError();
        return;
    }
    if (GetToken(false) != Token::GreaterThan)
        ThrowUnexpectedToken(_curPos, u">");
}
std::shared_ptr<NiniXmlDtdInfo> DtdParser::Parse(bool save) {
    if (_freeFloatingDtd) {
        if (_hasFreeFloatingInternalSubset) {
            LoadParsingBuffer();
            ParseSubset();
            SaveParsingBuffer();
        }
        if (_systemId && !_systemId->empty())
            ParseExternalSubset();
    } else
        ParseInDocumentDtd(save);
    for (auto* list : {&_schemaInfo->element_declarations, &_schemaInfo->undeclared_element_declarations})
        for (auto& e : *list) {
            if (e->has_non_cdata_attributes)
                _schemaInfo->has_non_cdata_attributes = true;
            if (!e->default_attributes.empty())
                _schemaInfo->has_default_attributes = true;
        }
    if (_validate)
        for (auto& n : _undeclaredNotations)
            if (n.active)
                for (int i = 0; i < n.count; ++i) {
                    NiniXmlProviderError e("System.Xml.Schema.XmlSchemaException", 0x80131941u,
                                           Resource("Sch_UndeclaredNotation", {n.name}));
                    e.source_uri = BaseUriStr();
                    e.line_number = n.line.number;
                    e.line_position = n.line.position;
                    if (_readerAdapter.has_validation_handler())
                        _readerAdapter.validation_event(e, 0);
                }
    _schemaInfo->system_id = _systemId;
    _schemaInfo->public_id = _publicId;
    return _schemaInfo;
}
void DtdParser::InitializeFreeFloating(NiniWriterText base, NiniWriterText name, NiniWriterText pub,
                                       NiniWriterText sys, NiniWriterText subset) {
    if (!name)
        throw NiniXmlProviderError("System.ArgumentNullException", 0x80004003u,
                                   u"Value cannot be null. (Parameter 'docTypeName')", u"docTypeName");
    if (name->empty())
        throw NiniXmlProviderError("System.ArgumentException", 0x80070057u,
                                   u"The value cannot be an empty string. (Parameter 'docTypeName')",
                                   u"docTypeName");
    auto n = nini_xml_parse_name(*name);
    if (n != name->size()) {
        auto args = CharArgs((*name)[n], n + 1 < name->size() ? (*name)[n + 1] : u'\0');
        auto error = XmlError(n == 0 ? "Xml_BadStartNameChar" : "Xml_BadNameChar", args, 0,
                              static_cast<int>(n + 1), {});
        throw error;
    }
    auto colon = name->find(u':');
    if (colon == name->npos) {
        auto interned = _readerAdapter.intern(*name);
        _schemaInfo->name = {interned ? *interned : std::u16string{}, {}};
    } else {
        auto first = _readerAdapter.intern(name->substr(0, colon));
        auto second = _readerAdapter.intern(name->substr(colon + 1));
        _schemaInfo->name = {first ? *first : std::u16string{}, second ? *second : std::u16string{}};
    }
    if (sys && !sys->empty()) {
        auto i = nini_xml_invalid_character_data(*sys);
        if (i >= 0)
            Throw(_curPos, "Xml_InvalidCharacter",
                  CharArgs((*sys)[i], i + 1 < sys->size() ? (*sys)[i + 1] : u'\0'));
        _systemId = sys;
    }
    if (pub && !pub->empty()) {
        auto i = nini_xml_invalid_public_id(*pub);
        if (i >= 0)
            Throw(_curPos, "Xml_InvalidCharacter",
                  CharArgs((*pub)[i], i + 1 < pub->size() ? (*pub)[i + 1] : u'\0'));
        _publicId = pub;
    }
    if (subset && !subset->empty()) {
        _readerAdapter.push_internal_dtd(base, subset);
        _hasFreeFloatingInternalSubset = true;
    }
    auto uri = _readerAdapter.base_uri();
    if (uri)
        _documentBaseUri = *uri;
    _freeFloatingDtd = true;
}

}
std::shared_ptr<NiniXmlDtdInfo> native_nini_xml_parse_dtd(NiniXmlDtdAdapter& a, bool save) {
    return DtdParser(a).Parse(save);
}
std::shared_ptr<NiniXmlDtdInfo>
native_nini_xml_parse_free_floating_dtd(NiniXmlDtdAdapter& a, NiniWriterText base, NiniWriterText name,
                                        NiniWriterText pub, NiniWriterText sys, NiniWriterText subset) {
    DtdParser p(a);
    p.InitializeFreeFloating(base, name, pub, sys, subset);
    return p.Parse(false);
}
}
