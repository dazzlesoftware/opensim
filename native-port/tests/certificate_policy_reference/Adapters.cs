// Reference-only certificate storage and public AsnReader bridge. The pinned
// policy processor and runtime-generated ASN.1 decoder bodies are unchanged.
using System;
using System.Formats.Asn1;
namespace System.Security.Cryptography.X509Certificates
{
    internal struct PolicyData
    {
        internal byte[] ApplicationCertPolicies, CertPolicies, CertPolicyMappings, CertPolicyConstraints,
            EnhancedKeyUsage, InhibitAnyPolicyExtension;
    }
    internal sealed class X509Certificate2
    {
        internal PolicyPal Pal;
        internal X509Certificate2(PolicyData data)
        {
            Pal = new PolicyPal(data);
        }
    }
    internal sealed class PolicyPal
    {
        readonly PolicyData data;
        internal PolicyPal(PolicyData value)
        {
            data = value;
        }
        internal PolicyData GetPolicyData() => data;
    }
    internal static class Oids
    {
        internal const string AnyCertPolicy = "2.5.29.32.0";
    }
}
namespace System.Security.Cryptography
{
    internal static class SR
    {
        internal const string Cryptography_Der_Invalid_Encoding = "ASN1 corrupted data.";
    }
}
namespace System.Formats.Asn1
{
    internal struct AsnValueReader
    {
        AsnReader reader;
        internal AsnValueReader(ReadOnlySpan<byte> data, AsnEncodingRules rules)
        {
            reader = new AsnReader(data.ToArray(), rules);
        }
        AsnValueReader(AsnReader value)
        {
            reader = value;
        }
        internal bool HasData => reader.HasData;
        internal Asn1Tag PeekTag() => reader.PeekTag();
        internal AsnValueReader ReadSequence() => new(reader.ReadSequence());
        internal AsnValueReader ReadSequence(Asn1Tag tag) => new(reader.ReadSequence(tag));
        internal string ReadObjectIdentifier() => reader.ReadObjectIdentifier();
        internal ReadOnlySpan<byte> ReadEncodedValue() => reader.ReadEncodedValue().Span;
        internal bool TryReadInt32(out int value) => reader.TryReadInt32(out value);
        internal bool TryReadInt32(out int value, Asn1Tag tag) => reader.TryReadInt32(out value, tag);
        internal void ThrowIfNotEmpty() => reader.ThrowIfNotEmpty();
    }
}
