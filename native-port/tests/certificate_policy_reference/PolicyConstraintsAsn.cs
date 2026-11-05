using System.Formats.Asn1;

namespace System.Security.Cryptography.X509Certificates.Asn1;

internal struct PolicyConstraintsAsn
{
	internal int? RequireExplicitPolicyDepth;

	internal int? InhibitMappingDepth;

	internal static PolicyConstraintsAsn Decode(ReadOnlyMemory<byte> encoded, AsnEncodingRules ruleSet)
	{
		return Decode(Asn1Tag.Sequence, encoded, ruleSet);
	}

	internal static PolicyConstraintsAsn Decode(Asn1Tag expectedTag, ReadOnlyMemory<byte> encoded, AsnEncodingRules ruleSet)
	{
		try
		{
			AsnValueReader reader = new AsnValueReader(encoded.Span, ruleSet);
			DecodeCore(ref reader, expectedTag, out var decoded);
			reader.ThrowIfNotEmpty();
			return decoded;
		}
		catch (AsnContentException inner)
		{
			throw new CryptographicException(SR.Cryptography_Der_Invalid_Encoding, inner);
		}
	}

	private static void DecodeCore(ref AsnValueReader reader, Asn1Tag expectedTag, out PolicyConstraintsAsn decoded)
	{
		decoded = default;
		AsnValueReader asnValueReader = reader.ReadSequence(expectedTag);
		if (asnValueReader.HasData && asnValueReader.PeekTag().HasSameClassAndValue(new Asn1Tag(TagClass.ContextSpecific, 0)))
		{
			if (asnValueReader.TryReadInt32(out var value, new Asn1Tag(TagClass.ContextSpecific, 0)))
			{
				decoded.RequireExplicitPolicyDepth = value;
			}
			else
			{
				asnValueReader.ThrowIfNotEmpty();
			}
		}
		if (asnValueReader.HasData && asnValueReader.PeekTag().HasSameClassAndValue(new Asn1Tag(TagClass.ContextSpecific, 1)))
		{
			if (asnValueReader.TryReadInt32(out var value2, new Asn1Tag(TagClass.ContextSpecific, 1)))
			{
				decoded.InhibitMappingDepth = value2;
			}
			else
			{
				asnValueReader.ThrowIfNotEmpty();
			}
		}
		asnValueReader.ThrowIfNotEmpty();
	}
}
