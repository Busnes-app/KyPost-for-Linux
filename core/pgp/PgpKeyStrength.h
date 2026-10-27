#pragma once

// core/pgp's OpenPGP strength floor, shared by new signing/encryption and by
// incoming signature verification. Include from .cpp files only: gpgme stays
// out of every header another directory sees.

#include <gpgme.h>

// RSA, DSA and ElGamal need 2048 actual bits; ECC needs 224. Anything gpgme
// cannot classify is refused, except GnuPG's ML-KEM composite (Kyber), which
// always pairs ML-KEM-768 or larger with an ECC key.
// ponytail: DSA's subgroup size is not exposed by gpgme; GnuPG pairs 2048-bit
// DSA with a 224/256-bit subgroup, but a hand-made 2048/160 key would pass.
inline bool pgpKeyStrengthAllowed(gpgme_subkey_t subkey)
{
    switch (static_cast<int>(subkey->pubkey_algo)) {
    case GPGME_PK_RSA:
    case GPGME_PK_RSA_E:
    case GPGME_PK_RSA_S:
    case GPGME_PK_DSA:
    case GPGME_PK_ELG:
    case GPGME_PK_ELG_E:
        return subkey->length >= 2048;
    case GPGME_PK_ECC:
    case GPGME_PK_ECDSA:
    case GPGME_PK_ECDH:
    case GPGME_PK_EDDSA:
        return subkey->length >= 224;
    case 8: // GPGME_PK_KYBER, absent from older gpgme headers
        return true;
    default:
        return false;
    }
}

// SHA-224 or stronger. SHA-1, RIPEMD-160 and MD5 signatures are refused.
inline bool pgpDigestAllowed(gpgme_hash_algo_t digest)
{
    return digest == GPGME_MD_SHA224 || digest == GPGME_MD_SHA256 || digest == GPGME_MD_SHA384
        || digest == GPGME_MD_SHA512;
}
