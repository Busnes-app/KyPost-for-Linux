// The bridge is stateless: no Go handles or private keys survive a call.
package main

/*
#include <stdlib.h>
#include <stddef.h>
typedef struct { void *data; size_t size; void *metadata; size_t metadata_size; int status; } KyPgpResult;
*/
import "C"

import (
	"bytes"
	stdcrypto "crypto"
	"encoding/base64"
	"encoding/hex"
	"encoding/json"
	"errors"
	"io"
	"strings"
	"time"
	"unsafe"

	"github.com/Busnes-app/ky-primitives/xwing"
	"github.com/ProtonMail/go-crypto/openpgp/armor"
	pgperrors "github.com/ProtonMail/go-crypto/openpgp/errors"
	"github.com/ProtonMail/go-crypto/openpgp/packet"
	"github.com/ProtonMail/gopenpgp/v3/crypto"
)

const maxKey = 512 << 10
const maxMessage = 48 << 20
const maxPlaintext = 32 << 20

// Input slices borrow C memory for this call only. Never retain or mutate them.
func borrow(p unsafe.Pointer, n C.size_t, maximum int) ([]byte, bool) {
	if uint64(n) > uint64(maximum) || (n != 0 && p == nil) {
		return nil, false
	}
	return unsafe.Slice((*byte)(p), int(n)), true
}

//export KyPgpCall
func KyPgpCall(op C.int, data unsafe.Pointer, dataSize C.size_t, keys unsafe.Pointer, keysSize C.size_t, extra unsafe.Pointer, extraSize C.size_t, limit C.longlong) (out C.KyPgpResult) {
	out.status = 1
	// Fail closed on a library panic; no Go exception crosses the C ABI.
	defer func() {
		if recover() != nil {
			out.status = 1
		}
	}()
	d, ok := borrow(data, dataSize, maxMessage)
	if !ok {
		return
	}
	k, ok := borrow(keys, keysSize, 16*maxKey)
	if !ok {
		return
	}
	e, ok := borrow(extra, extraSize, maxMessage)
	if !ok {
		return
	}
	result, metadata, status := dispatch(int(op), d, k, e, int64(limit))
	defer clear(result)
	defer clear(metadata)
	out.status = C.int(status)
	if status != 0 {
		return
	}
	if len(result) != 0 {
		out.data = C.CBytes(result)
		out.size = C.size_t(len(result))
	}
	if len(metadata) != 0 {
		out.metadata = C.CBytes(metadata)
		out.metadata_size = C.size_t(len(metadata))
	}
	return
}

//export KyPgpFree
func KyPgpFree(p unsafe.Pointer, n C.size_t) {
	if p != nil {
		clear(unsafe.Slice((*byte)(p), int(n)))
		C.free(p)
	}
}

func metadata(v any) []byte { b, _ := json.Marshal(v); return b }

// Reject compressed/streaming key packets before a key parser can expand them.
// One complete armor block, one primary, no silently ignored trailing certificate.
func keyBinary(data []byte) ([]byte, error) {
	if len(data) == 0 || len(data) > maxKey {
		return nil, errors.New("key size")
	}
	raw := data
	if bytes.HasPrefix(bytes.TrimSpace(data), []byte("-----BEGIN PGP ")) {
		if bytes.Count(data, []byte("-----BEGIN PGP ")) != 1 {
			return nil, errors.New("multiple armor")
		}
		end := bytes.Index(data, []byte("-----END PGP "))
		if end < 0 {
			return nil, errors.New("armor end")
		}
		tail := data[end:]
		newline := bytes.IndexByte(tail, '\n')
		if newline >= 0 && len(bytes.TrimSpace(tail[newline:])) != 0 {
			return nil, errors.New("trailing armor")
		}
		block, err := armor.Decode(bytes.NewReader(data))
		if err != nil {
			return nil, err
		}
		if block.Type != "PGP PRIVATE KEY BLOCK" && block.Type != "PGP PUBLIC KEY BLOCK" {
			return nil, errors.New("armor type")
		}
		if !bytes.HasSuffix(bytes.TrimSpace(data), []byte("-----END "+block.Type+"-----")) {
			return nil, errors.New("armor trailing")
		}
		raw, err = io.ReadAll(io.LimitReader(block.Body, maxKey+1))
		if err != nil {
			return nil, err
		}
		if len(raw) > maxKey {
			clear(raw)
			return nil, errors.New("key size")
		}
	} else {
		raw = bytes.Clone(data)
	}
	primary := 0
	valid := false
	defer func() {
		if !valid {
			clear(raw)
		}
	}()
	for pos := 0; pos < len(raw); {
		h := raw[pos]
		pos++
		if h&0x80 == 0 {
			return nil, errors.New("header")
		}
		tag := (h >> 2) & 15
		if h&64 != 0 {
			tag = h & 63
		}
		switch tag {
		case 5, 6:
			primary++
		case 7, 14, 2, 13, 17:
		default:
			return nil, errors.New("key packet")
		}
		var length uint64
		if h&64 != 0 {
			if pos == len(raw) {
				return nil, io.ErrUnexpectedEOF
			}
			first := raw[pos]
			pos++
			switch {
			case first < 192:
				length = uint64(first)
			case first < 224:
				if pos == len(raw) {
					return nil, io.ErrUnexpectedEOF
				}
				length = uint64(first-192)*256 + uint64(raw[pos]) + 192
				pos++
			case first == 255:
				if len(raw)-pos < 4 {
					return nil, io.ErrUnexpectedEOF
				}
				for i := 0; i < 4; i++ {
					length = length<<8 | uint64(raw[pos])
					pos++
				}
			default:
				return nil, errors.New("partial key")
			}
		} else {
			count := 1 << (h & 3)
			if count == 8 || len(raw)-pos < count {
				return nil, io.ErrUnexpectedEOF
			}
			for i := 0; i < count; i++ {
				length = length<<8 | uint64(raw[pos])
				pos++
			}
		}
		if length == 0 || length > uint64(len(raw)-pos) {
			return nil, io.ErrUnexpectedEOF
		}
		pos += int(length)
	}
	if primary != 1 {
		return nil, errors.New("primary count")
	}
	valid = true
	return raw, nil
}

type keyInfo struct {
	Emails       []string `json:"emails"`
	Expired      bool     `json:"expired"`
	Revoked      bool     `json:"revoked"`
	Fingerprint  string   `json:"fingerprint"`
	Fingerprints []string `json:"fingerprints"`
	PublicKey    string   `json:"publicKey"`
	Private      bool     `json:"private"`
	Unlocked     bool     `json:"unlocked"`
	Modern       bool     `json:"modern"`
	Version      int      `json:"version"`
}

func inspect(data []byte) ([]byte, []byte, int) {
	raw, err := keyBinary(data)
	if err != nil {
		return nil, nil, 1
	}
	defer clear(raw)
	key, err := crypto.NewKey(raw)
	if err != nil {
		return nil, nil, 1
	}
	defer key.ClearPrivateParams()
	entity := key.GetEntity()
	unlocked, _ := key.IsUnlocked()
	pub, err := key.GetArmoredPublicKey()
	if err != nil {
		return nil, nil, 1
	}
	info := keyInfo{Fingerprint: strings.ToUpper(key.GetFingerprint()), PublicKey: pub, Private: key.IsPrivate(), Unlocked: unlocked, Version: key.GetVersion()}
	info.Expired = key.IsExpired(time.Now().Unix())
	info.Revoked = key.IsRevoked(time.Now().Unix())
	for _, id := range entity.Identities {
		if id.UserId != nil {
			info.Emails = append(info.Emails, strings.ToLower(id.UserId.Email))
		}
	}
	info.Modern = info.Version == 6
	info.Fingerprints = append(info.Fingerprints, info.Fingerprint)
	for _, sub := range entity.Subkeys {
		info.Fingerprints = append(info.Fingerprints, strings.ToUpper(hex.EncodeToString(sub.PublicKey.Fingerprint)))
		if int(sub.PublicKey.PubKeyAlgo) == 35 {
			info.Modern = true
		}
	}
	if key.IsPrivate() {
		if entity.PrivateKey == nil || entity.PrivateKey.Dummy() {
			info.Unlocked = false
		}
		for _, sub := range entity.Subkeys {
			if sub.PrivateKey == nil || sub.PrivateKey.Dummy() || sub.PrivateKey.Encrypted {
				info.Unlocked = false
			}
		}
	}
	// Retain exact packet order: reserializing map-backed identities would make
	// content-addressed persistence non-idempotent for multi-UID certificates.
	return bytes.Clone(raw), metadata(info), 0
}

// Keep the existing first-signature rule, rather than gopenpgp's best-signature selection.
func signatureInfo(result *crypto.VerifyResult) []byte {
	out := map[string]any{"present": false}
	if result != nil && len(result.Signatures) > 0 {
		sig := result.Signatures[0]
		out["present"] = true
		out["valid"] = sig.SignatureError == nil
		out["unavailable"] = sig.SignedBy == nil
		if sig.Signature != nil {
			out["fingerprint"] = strings.ToUpper(hex.EncodeToString(sig.Signature.IssuerFingerprint))
			strong := sig.Signature.Hash == stdcrypto.SHA224 || sig.Signature.Hash == stdcrypto.SHA256 || sig.Signature.Hash == stdcrypto.SHA384 || sig.Signature.Hash == stdcrypto.SHA512
			if sig.SignedBy != nil {
				entity := sig.SignedBy.GetEntity()
				out["primary"] = strings.ToUpper(sig.SignedBy.GetFingerprint())

				check := func(p *packet.PublicKey) bool {
					bits, err := p.BitLength()
					if err != nil {
						return false
					}
					switch p.PubKeyAlgo {
					case packet.PubKeyAlgoRSA, packet.PubKeyAlgoRSASignOnly, packet.PubKeyAlgoDSA, packet.PubKeyAlgoElGamal:
						return bits >= 2048
					case packet.PubKeyAlgoECDSA, packet.PubKeyAlgoEdDSA, packet.PubKeyAlgoEd25519, packet.PubKeyAlgoEd448:
						return bits >= 224
					case packet.PubKeyAlgoMldsa65Ed25519, packet.PubKeyAlgoMldsa87Ed448:
						return true
					default:
						return false
					}
				}
				matches := func(p *packet.PublicKey) bool {
					if len(sig.Signature.IssuerFingerprint) != 0 {
						return bytes.Equal(p.Fingerprint, sig.Signature.IssuerFingerprint)
					}
					return sig.Signature.IssuerKeyId != nil && p.KeyId == *sig.Signature.IssuerKeyId
				}
				found := false
				strong = strong && check(entity.PrimaryKey)
				checkSigner := func(public *packet.PublicKey) {
					if matches(public) {
						found = true
						strong = strong && check(public)
						out["fingerprint"] = strings.ToUpper(hex.EncodeToString(public.Fingerprint))
					}
				}
				checkSigner(entity.PrimaryKey)
				for _, sub := range entity.Subkeys {
					checkSigner(sub.PublicKey)
				}
				strong = strong && found
			}
			out["weak"] = !strong
		}
	}
	return metadata(out)
}

func dispatch(op int, data, keys, extra []byte, limit int64) ([]byte, []byte, int) {
	switch op {
	case 1:
		return inspect(data)
	case 2, 3:
		if limit < 1 || limit > maxPlaintext {
			return nil, nil, 1
		}
		if len(keys) == 0 {
			return nil, nil, 4
		}
		ring, err := crypto.NewKeyRingFromBinary(keys)
		if err != nil {
			return nil, nil, 1
		}
		defer ring.ClearPrivateParams()
		pgp := crypto.PGP()
		if op == 3 {
			if len(data) > int(limit) || len(extra) > maxKey {
				return nil, nil, 3
			}
			verifier, err := pgp.Verify().VerificationKeys(ring).New()
			if err != nil {
				return nil, nil, 1
			}
			verified, err := verifier.VerifyDetached(data, extra, crypto.Auto)
			if err != nil {
				return nil, nil, 1
			}
			return nil, signatureInfo(verified), 0
		}
		handle, err := pgp.Decryption().DecryptionKeys(ring).VerificationKeys(ring).MaxDecompressedMessageSize(limit).DisableAutomaticTextSanitize().RetrieveSessionKey().New()
		if err != nil {
			return nil, nil, 1
		}
		reader, err := handle.DecryptingReader(bytes.NewReader(data), crypto.Auto)
		if err != nil {
			if errors.Is(err, pgperrors.ErrKeyIncorrect) {
				return nil, nil, 2
			}
			if errors.Is(err, pgperrors.ErrMessageTooLarge) {
				return nil, nil, 3
			}
			return nil, nil, 1
		}
		// ReadMessage also accepts unencrypted literal packets. A decryption
		// result requires an actual session key; never label those as encrypted.
		session := reader.SessionKey()
		if session == nil {
			return nil, nil, 1
		}
		defer session.Clear()
		plaintext, err := io.ReadAll(io.LimitReader(reader, limit+1))
		if int64(len(plaintext)) > limit {
			clear(plaintext)
			return nil, nil, 3
		}
		if err != nil {
			clear(plaintext)
			if errors.Is(err, pgperrors.ErrMessageTooLarge) {
				return nil, nil, 3
			}
			return nil, nil, 1
		}
		verified, err := reader.VerifySignature()
		if err != nil {
			clear(plaintext)
			if errors.Is(err, pgperrors.ErrMessageTooLarge) {
				return nil, nil, 3
			}
			return nil, nil, 1
		}
		return plaintext, signatureInfo(verified), 0
	case 4:
		key, err := xwing.Generate()
		if err != nil {
			return nil, nil, 1
		}
		return key.Seed(), key.Public().Bytes(), 0
	case 5:
		key, err := xwing.FromSeed(keys)
		if err != nil {
			return nil, nil, 1
		}
		return key.Public().Bytes(), nil, 0
	case 6:
		if len(data) > maxKey || len(extra) > 131200 {
			return nil, nil, 1
		}
		key, err := xwing.FromSeed(keys)
		if err != nil {
			return nil, nil, 1
		}
		plain, err := xwing.Open(key, extra, nil, data)
		if err != nil {
			return nil, nil, 1
		}
		return plain, nil, 0
	case 8:
		if len(data) > maxKey || len(extra) > 131200 {
			return nil, nil, 1
		}
		decoder := json.NewDecoder(bytes.NewReader(data))
		token, err := decoder.Token()
		if err != nil || token != json.Delim('{') {
			return nil, nil, 1
		}
		fields := map[string]json.RawMessage{}
		for decoder.More() {
			token, err = decoder.Token()
			if err != nil {
				return nil, nil, 1
			}
			name, ok := token.(string)
			if !ok {
				return nil, nil, 1
			}
			if _, exists := fields[name]; exists {
				return nil, nil, 1
			}
			var raw json.RawMessage
			if decoder.Decode(&raw) != nil {
				return nil, nil, 1
			}
			fields[name] = raw
		}
		if _, err = decoder.Token(); err != nil || len(fields) != 4 {
			return nil, nil, 1
		}
		if decoder.Decode(new(any)) != io.EOF {
			return nil, nil, 1
		}
		if string(fields["v"]) != "6" {
			return nil, nil, 1
		}
		var alg, enc, ct string
		if json.Unmarshal(fields["alg"], &alg) != nil || alg != "HPKE-Base-XWing-SHA256-A256GCM" || json.Unmarshal(fields["enc"], &enc) != nil || json.Unmarshal(fields["ct"], &ct) != nil {
			return nil, nil, 1
		}
		decode := func(s string) ([]byte, error) {
			raw, err := base64.StdEncoding.Strict().DecodeString(s)
			if err != nil || base64.StdEncoding.EncodeToString(raw) != s {
				return nil, errors.New("base64")
			}
			return raw, nil
		}
		encapsulated, err := decode(enc)
		if err != nil || len(encapsulated) != 1120 {
			return nil, nil, 1
		}
		ciphertext, err := decode(ct)
		if err != nil || len(ciphertext) < 16 {
			return nil, nil, 1
		}
		framed := append([]byte{1}, encapsulated...)
		framed = append(framed, ciphertext...)
		return dispatch(6, framed, keys, extra, limit)

	case 7:
		if len(data) > maxKey || len(extra) > 131200 {
			return nil, nil, 1
		}
		key, err := xwing.ParsePublicKey(keys)
		if err != nil {
			return nil, nil, 1
		}
		sealed, err := xwing.Seal(key, extra, nil, data)
		if err != nil {
			return nil, nil, 1
		}
		return sealed, nil, 0
	}
	return nil, nil, 1
}
func main() {}
