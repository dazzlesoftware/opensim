using System.Formats.Asn1;

namespace System.Security.Cryptography.X509Certificates.Asn1;

internal struct CertificatePolicyMappingAsn
{
	internal string IssuerDomainPolicy;

	internal string SubjectDomainPolicy;

	internal static void Decode(ref AsnValueReader reader, out CertificatePolicyMappingAsn decoded)
	{
		Decode(ref reader, Asn1Tag.Sequence, out decoded);
	}

	internal static void Decode(ref AsnValueReader reader, Asn1Tag expectedTag, out CertificatePolicyMappingAsn decoded)
	{
		try
		{
			DecodeCore(ref reader, expectedTag, out decoded);
		}
		catch (AsnContentException inner)
		{
			throw new CryptographicException(SR.Cryptography_Der_Invalid_Encoding, inner);
		}
	}

	private static void DecodeCore(ref AsnValueReader reader, Asn1Tag expectedTag, out CertificatePolicyMappingAsn decoded)
	{
		decoded = default;
		AsnValueReader asnValueReader = reader.ReadSequence(expectedTag);
		decoded.IssuerDomainPolicy = asnValueReader.ReadObjectIdentifier();
		decoded.SubjectDomainPolicy = asnValueReader.ReadObjectIdentifier();
		asnValueReader.ThrowIfNotEmpty();
	}
}
