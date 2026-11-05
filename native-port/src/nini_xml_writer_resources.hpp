// Actual pinned .NET8.0.31 System.Xml English resource strings; MIT runtime license.
#pragma once
#include <string_view>
namespace opensim::framework::xml_writer_resources {
inline constexpr std::u16string_view Xml_CanNotBindToReservedNamespace=u"Cannot bind to the reserved namespace.";
inline constexpr std::u16string_view Xml_Closed=u"The Writer is closed.";
inline constexpr std::u16string_view Xml_ClosedOrError=u"The Writer is closed or in error state.";
inline constexpr std::u16string_view Xml_DupXmlDecl=u"Cannot write XML declaration. WriteStartDocument method has already written it.";
inline constexpr std::u16string_view Xml_InvalidCDataChars=u"Cannot have ']]>' inside an XML CDATA block.";
inline constexpr std::u16string_view Xml_InvalidCommentChars=u"An XML comment cannot contain '--', and '-' cannot be the last character.";
inline constexpr std::u16string_view Xml_InvalidNameChars=u"Invalid name character in '{0}'.";
inline constexpr std::u16string_view Xml_InvalidOperation=u"Operation is not valid due to the current state of the object.";
inline constexpr std::u16string_view Xml_InvalidPiChars=u"Cannot have '?>' inside an XML processing instruction.";
inline constexpr std::u16string_view Xml_InvalidPrefix=u"Prefixes beginning with \"xml\" (regardless of whether the characters are uppercase, lowercase, or some combination thereof) are reserved for use by XML.";
inline constexpr std::u16string_view Xml_InvalidQuote=u"Invalid XML attribute quote character. Valid attribute quote characters are ' and \".";
inline constexpr std::u16string_view Xml_InvalidXmlSpace=u"'{0}' is an invalid xml:space value.";
inline constexpr std::u16string_view Xml_NoNamespaces=u"Cannot set the namespace if Namespaces is 'false'.";
inline constexpr std::u16string_view Xml_NoRoot=u"Document does not have a root element.";
inline constexpr std::u16string_view Xml_NoStartTag=u"There was no XML start tag open.";
inline constexpr std::u16string_view Xml_NonWhitespace=u"Only whitespace characters should be used.";
inline constexpr std::u16string_view Xml_NotInWriteState=u"NotInWriteState.";
inline constexpr std::u16string_view Xml_NotTheFirst=u"WriteStartDocument needs to be the first call.";
inline constexpr std::u16string_view Xml_PrefixForEmptyNs=u"Cannot use a prefix with an empty namespace.";
inline constexpr std::u16string_view Xml_UndefNamespace=u"The '{0}' namespace is not defined.";
inline constexpr std::u16string_view Xml_UndefPrefix=u"An undefined prefix is in use.";
inline constexpr std::u16string_view Xml_WrongToken=u"Token {0} in state {1} would result in an invalid XML document.";
inline constexpr std::u16string_view Xml_XmlnsBelongsToReservedNs=u"The 'xmlns' attribute is bound to the reserved namespace 'http://www.w3.org/2000/xmlns/'.";
}
