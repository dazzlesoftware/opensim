using System.Formats.Asn1;

namespace System.Security.Cryptography.X509Certificates.Asn1;

internal struct PolicyInformationAsn
{
	internal string PolicyIdentifier;

	internal ReadOnlyMemory<byte>? PolicyQualifiers;

	internal static void Decode(ref AsnValueReader reader, ReadOnlyMemory<byte> rebind, out PolicyInformationAsn decoded)
	{
		Decode(ref reader, Asn1Tag.Sequence, rebind, out decoded);
	}

	internal static void Decode(ref AsnValueReader reader, Asn1Tag expectedTag, ReadOnlyMemory<byte> rebind, out PolicyInformationAsn decoded)
	{
		try
		{
			DecodeCore(ref reader, expectedTag, rebind, out decoded);
		}
		catch (AsnContentException inner)
		{
			throw new CryptographicException(SR.Cryptography_Der_Invalid_Encoding, inner);
		}
	}

	private static void DecodeCore(ref AsnValueReader reader, Asn1Tag expectedTag, ReadOnlyMemory<byte> rebind, out PolicyInformationAsn decoded)
	{
		decoded = default;
		AsnValueReader asnValueReader = reader.ReadSequence(expectedTag);
		ReadOnlySpan<byte> span = rebind.Span;
		decoded.PolicyIdentifier = asnValueReader.ReadObjectIdentifier();
		if (asnValueReader.HasData)
		{
			ReadOnlySpan<byte> other = asnValueReader.ReadEncodedValue();
			decoded.PolicyQualifiers = (span.Overlaps(other, out var elementOffset) ? rebind.Slice(elementOffset, other.Length) : ((ReadOnlyMemory<byte>)other.ToArray()));
		}
		asnValueReader.ThrowIfNotEmpty();
	}
}
