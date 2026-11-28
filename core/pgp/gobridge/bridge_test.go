package main

import (
	"bytes"
	stdcrypto "crypto"
	"crypto/sha256"
	"encoding/base64"
	"encoding/binary"
	"encoding/json"
	"fmt"
	"io"
	"os"
	"strings"
	"testing"

	"github.com/ProtonMail/go-crypto/openpgp/armor"
	"github.com/ProtonMail/go-crypto/openpgp/packet"
	openpgp "github.com/ProtonMail/go-crypto/openpgp/v2"
	"github.com/ProtonMail/gopenpgp/v3/crypto"
)

func fixture(t *testing.T, name string) []byte {
	t.Helper()
	b, e := os.ReadFile("testdata/" + name)
	if e != nil {
		t.Fatal(e)
	}
	return b
}
func TestRFC9980(t *testing.T) {
	message := fixture(t, "openpgp-v6/message.txt")
	for _, name := range []string{"v4", "v4-pqc", "v6-pqc"} {
		t.Run(name, func(t *testing.T) {
			key, meta, status := dispatch(1, fixture(t, "openpgp-v6/"+name+".sec.asc"), nil, nil, maxPlaintext)
			if status != 0 {
				t.Fatal("inspect", status)
			}
			defer clear(key)
			var info keyInfo
			if json.Unmarshal(meta, &info) != nil || !info.Private || !info.Unlocked {
				t.Fatal("metadata")
			}
			plain, _, status := dispatch(2, fixture(t, "openpgp-v6/"+name+".msg.asc"), key, nil, maxPlaintext)
			if status != 0 || !bytes.Equal(plain, message) {
				t.Fatalf("decrypt %d", status)
			}
			_, _, status = dispatch(2, fixture(t, "openpgp-v6/"+name+".msg.asc"), key, nil, 2)
			if status == 0 {
				t.Fatal("plaintext limit ignored")
			}
			// Corrupt the actual authentication trailer, not merely the armor.
			// A streaming reader may have produced plaintext before detecting it.
			block, err := armor.Decode(bytes.NewReader(fixture(t, "openpgp-v6/"+name+".msg.asc")))
			if err != nil {
				t.Fatal(err)
			}
			binaryMessage, err := io.ReadAll(block.Body)
			if err != nil {
				t.Fatal(err)
			}
			binaryMessage[len(binaryMessage)-1] ^= 1
			partial, _, status := dispatch(2, binaryMessage, key, nil, maxPlaintext)
			if status == 0 || len(partial) != 0 {
				t.Fatal("corrupt authentication trailer exposed plaintext")
			}
			ciphertext := fixture(t, "openpgp-v6/"+name+".msg.asc")
			ciphertext[len(ciphertext)/2] ^= 1
			if _, _, status = dispatch(2, ciphertext, key, nil, maxPlaintext); status == 0 {
				t.Fatal("tampered ciphertext opened")
			}
		})
	}
	key, _, status := dispatch(1, fixture(t, "openpgp-v6/v6-pqc.pub.asc"), nil, nil, maxPlaintext)
	if status != 0 {
		t.Fatal("pub")
	}
	signature := fixture(t, "openpgp-v6/v6-pqc.sig.asc")
	_, meta, status := dispatch(3, message, key, signature, maxPlaintext)
	var verdict map[string]any
	_ = json.Unmarshal(meta, &verdict)
	if status != 0 || verdict["valid"] != true || verdict["weak"] != false {
		t.Fatalf("signature %d %s", status, meta)
	}
	block, err := armor.Decode(bytes.NewReader(signature))
	if err != nil {
		t.Fatal(err)
	}
	rawSignature, err := io.ReadAll(block.Body)
	if err != nil {
		t.Fatal(err)
	}
	for _, offset := range []int{1, 100} {
		bad := bytes.Clone(rawSignature)
		bad[len(bad)-offset] ^= 1
		_, meta, status := dispatch(3, message, key, bad, maxPlaintext)
		var invalid map[string]any
		_ = json.Unmarshal(meta, &invalid)
		if status == 0 && invalid["valid"] == true {
			t.Fatal("corrupt composite signature verified")
		}
	}
	message[0] ^= 1
	_, meta, status = dispatch(3, message, key, signature, maxPlaintext)
	_ = json.Unmarshal(meta, &verdict)
	if status == 0 && verdict["valid"] == true {
		t.Fatal("bad signature accepted")
	}
}
func TestInspectRefusesKeySmuggling(t *testing.T) {
	key := fixture(t, "openpgp-v6/v6-pqc.sec.asc")
	for _, bad := range [][]byte{append(bytes.Clone(key), key...), append(bytes.Clone(key), []byte("junk")...), {0xc8, 1, 0}} {
		if _, _, status := dispatch(1, bad, nil, nil, maxPlaintext); status == 0 {
			t.Fatal("bad key accepted")
		}
	}
}
func TestXWingServerAndBrowserVectors(t *testing.T) {
	for _, name := range []string{"device-envelope-xwing.json", "device-envelope-xwing-browser.json"} {
		t.Run(name, func(t *testing.T) {
			raw := fixture(t, "device-envelope-xwing.json")
			var f struct {
				Seed, PublicKey, DeviceID, Fingerprint, Plaintext string
				Envelope                                          json.RawMessage
			}
			if err := json.Unmarshal(raw, &f); err != nil {
				t.Fatal(err)
			}
			if name == "device-envelope-xwing-browser.json" {
				f.Envelope = fixture(t, name)
			}
			seed, _ := base64.StdEncoding.DecodeString(f.Seed)
			pub, _, status := dispatch(5, nil, seed, nil, maxPlaintext)
			if status != 0 || base64.StdEncoding.EncodeToString(pub) != f.PublicKey {
				t.Fatal("public key")
			}
			info := []byte("kypost-device-envelope/v6")
			for _, s := range []string{f.DeviceID, f.Fingerprint} {
				info = binary.BigEndian.AppendUint16(info, uint16(len(s)))
				info = append(info, []byte(s)...)
			}
			plain, _, status := dispatch(8, f.Envelope, seed, info, maxPlaintext)
			if status != 0 || string(plain) != f.Plaintext {
				t.Fatalf("vector open %d", status)
			}
			info[0] ^= 1
			if _, _, status = dispatch(8, f.Envelope, seed, info, maxPlaintext); status == 0 {
				t.Fatal("context not bound")
			}
			info[0] ^= 1
			framed, _, status := dispatch(7, []byte(f.Plaintext), pub, info, maxPlaintext)
			if status != 0 {
				t.Fatal("seal")
			}
			plain, _, status = dispatch(6, framed, seed, info, maxPlaintext)
			if status != 0 || string(plain) != f.Plaintext {
				t.Fatal("round trip")
			}
			framed[1] ^= 1
			if _, _, status = dispatch(6, framed, seed, info, maxPlaintext); status == 0 {
				t.Fatal("enc tamper")
			}
			for _, change := range []func(map[string]any){
				func(e map[string]any) { e["enc"] = base64.StdEncoding.EncodeToString(make([]byte, 1119)) },
				func(e map[string]any) { e["v"] = 5 }, func(e map[string]any) { e["other"] = true },
				func(e map[string]any) { e["alg"] = "HPKE-Base-P256-SHA256-A256GCM" },
			} {
				var env map[string]any
				_ = json.Unmarshal(f.Envelope, &env)
				change(env)
				bad, _ := json.Marshal(env)
				if _, _, status = dispatch(8, bad, seed, info, maxPlaintext); status == 0 {
					t.Fatal("framing accepted")
				}
			}
			duplicate := append([]byte(`{"v":6,`), bytes.TrimSpace(f.Envelope)[1:]...)
			if _, _, status = dispatch(8, duplicate, seed, info, maxPlaintext); status == 0 {
				t.Fatal("duplicate accepted")
			}
		})
	}
}

func TestFixtureChecksums(t *testing.T) {
	for _, dir := range []string{"", "openpgp-v6/"} {
		for _, line := range strings.Split(string(fixture(t, dir+"SHA256SUMS")), "\n") {
			fields := strings.Fields(line)
			if len(fields) == 0 {
				continue
			}
			if len(fields) != 2 {
				t.Fatal("checksum format")
			}
			got := fmt.Sprintf("%x", sha256.Sum256(fixture(t, dir+fields[1])))
			if got != fields[0] {
				t.Fatal("fixture changed", fields[1])
			}
		}
	}
}

func TestSignatureStrengthRequiresResolvedSigningKey(t *testing.T) {
	raw, err := keyBinary(fixture(t, "openpgp-v6/v6-pqc.pub.asc"))
	if err != nil {
		t.Fatal(err)
	}
	key, err := crypto.NewKey(raw)
	if err != nil {
		t.Fatal(err)
	}
	sig := &packet.Signature{Hash: stdcrypto.SHA256, IssuerFingerprint: make([]byte, 32)}
	result := &crypto.VerifyResult{Signatures: []*crypto.VerifiedSignature{{SignedBy: key, Signature: sig}}}
	var info map[string]any
	_ = json.Unmarshal(signatureInfo(result), &info)
	if info["weak"] != true {
		t.Fatal("unresolved signing key got strength credit")
	}
	sig.IssuerFingerprint = nil
	id := key.GetKeyID()
	sig.IssuerKeyId = &id
	_ = json.Unmarshal(signatureInfo(result), &info)
	if info["weak"] != false {
		t.Fatal("key-id signing key should resolve")
	}
}

// Spec A Q7 allows category 3 (ML-DSA-65) only; category 5 gets no strength credit.
func TestSignatureStrengthFollowsCategoryThreeAllowlist(t *testing.T) {
	for algo, weak := range map[packet.PublicKeyAlgorithm]bool{packet.PubKeyAlgoMldsa65Ed25519: false, packet.PubKeyAlgoMldsa87Ed448: true} {
		entity, err := openpgp.NewEntity("a", "", "a@example.test", &packet.Config{V6Keys: true, Algorithm: algo})
		if err != nil {
			t.Fatal(err)
		}
		key, err := crypto.NewKeyFromEntity(entity)
		if err != nil {
			t.Fatal(err)
		}
		sig := &packet.Signature{Hash: stdcrypto.SHA512, IssuerFingerprint: entity.PrimaryKey.Fingerprint}
		var info map[string]any
		_ = json.Unmarshal(signatureInfo(&crypto.VerifyResult{Signatures: []*crypto.VerifiedSignature{{SignedBy: key, Signature: sig}}}), &info)
		if info["weak"] != weak {
			t.Fatalf("algorithm %d: weak = %v, want %v", algo, info["weak"], weak)
		}
	}
}

func TestDecryptionRequiresEncryption(t *testing.T) {
	key, _, status := dispatch(1, fixture(t, "openpgp-v6/v6-pqc.sec.asc"), nil, nil, maxPlaintext)
	if status != 0 {
		t.Fatal("fixture key")
	}
	defer clear(key)
	// A valid unencrypted literal-data packet containing "public".
	literal := []byte{0xcb, 12, 'b', 0, 0, 0, 0, 0, 'p', 'u', 'b', 'l', 'i', 'c'}
	plain, _, status := dispatch(2, literal, key, nil, maxPlaintext)
	if status == 0 || len(plain) != 0 {
		t.Fatal("unencrypted literal data reported as decrypted")
	}
}

func TestKnownRevocationSurvivesOlderKeyRevision(t *testing.T) {
	raw, _, status := dispatch(1, fixture(t, "openpgp-v6/v6-pqc.sec.asc"), nil, nil, maxPlaintext)
	if status != 0 {
		t.Fatal("fixture")
	}
	defer clear(raw)
	key, err := crypto.NewKey(raw)
	if err != nil {
		t.Fatal(err)
	}
	defer key.ClearPrivateParams()
	if err = key.GetEntity().Revoke(packet.KeyCompromised, "test revocation", &packet.Config{DefaultHash: stdcrypto.SHA512}); err != nil {
		t.Fatal(err)
	}
	revoked, err := key.GetPublicKey()
	if err != nil {
		t.Fatal(err)
	}
	for _, versions := range [][]byte{append(bytes.Clone(raw), revoked...), append(bytes.Clone(revoked), raw...)} {
		plain, _, status := dispatch(2, fixture(t, "openpgp-v6/v6-pqc.msg.asc"), versions, nil, maxPlaintext)
		if status != 0 || !bytes.Equal(plain, fixture(t, "openpgp-v6/message.txt")) {
			t.Fatal("known revocation made historical mail unreadable")
		}
		_, meta, status := dispatch(3, fixture(t, "openpgp-v6/message.txt"), versions, fixture(t, "openpgp-v6/v6-pqc.sig.asc"), maxPlaintext)
		var verdict map[string]any
		_ = json.Unmarshal(meta, &verdict)
		if status == 0 && verdict["valid"] == true {
			t.Fatal("older revision hid a known revocation")
		}
	}
}
