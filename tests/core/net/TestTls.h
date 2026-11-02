#pragma once

// Real certificate chains for TLS tests, and a relay that serves them. Shared
// by HttpClientTest (pin enforcement) and PairingControllerTest (the approval
// polls' pin). Every chain is a leaf for 127.0.0.1 under an issuer under a
// root: the pin anchors to the ISSUER (HttpClient::pinnedSpkiFromChain), so a
// chain has to be two deep or the pin cannot be computed at all. The roots
// are trusted by the tests, never served. Generated with openssl, valid to
// 2036; used nowhere else.

#include "FakeRelayServer.h"

#include <QList>
#include <QPair>
#include <QSslCertificate>
#include <QSslConfiguration>
#include <QSslKey>
#include <QSslSocket>
#include <QTcpServer>
#include <memory>

namespace TestTls {

// Chain A: "KyPost Test Issuer" under "KyPost Test Root".
inline const char* kServedLeafPem = R"PEM(
-----BEGIN CERTIFICATE-----
MIIDPDCCAiSgAwIBAgIBAzANBgkqhkiG9w0BAQsFADAdMRswGQYDVQQDDBJLeVBv
c3QgVGVzdCBJc3N1ZXIwHhcNMjYxMDI3MjEyNzE2WhcNMzYxMDI0MjEyNzE2WjAU
MRIwEAYDVQQDDAkxMjcuMC4wLjEwggEiMA0GCSqGSIb3DQEBAQUAA4IBDwAwggEK
AoIBAQDJA8qztywdbJqKEVJikP3cIkANUuHeAHXm4t8PaZZF4ROFfy0v5CjFoz+9
tAS7qtkNAwk+JfFxIyYUqsijpNu/rAxHbJf1XFavik1PjnEQvAWgwiINIuhroXdc
z9bSzGoAthNJqR1x24Q6N7VoQ8LX54q8H3R0QWL/55eHA0mnTsZOdneWx19BvPeZ
/M112ecFCOvZ8Xl6ADtJmTiI9SEAADUcNSmbky55bEbfRu5Eh9Te+oedz0rnbNWz
KuSXV77WuzC7pxPrF1EUXTcuGK8v31tOphtel9nQJfPS1L9rvknHDcFLk1tR8V4s
ogCO17fNlLAT02d9QWM9GPLqOdw1AgMBAAGjgY8wgYwwCQYDVR0TBAIwADAOBgNV
HQ8BAf8EBAMCBaAwEwYDVR0lBAwwCgYIKwYBBQUHAwEwGgYDVR0RBBMwEYcEfwAA
AYIJbG9jYWxob3N0MB0GA1UdDgQWBBQB/scun4urNGIpUcMJ8hnX3OAm1zAfBgNV
HSMEGDAWgBS2xJ1hEG1njmFQwlNqPqpNM982QDANBgkqhkiG9w0BAQsFAAOCAQEA
EvyExpiOvz3e7oXQcaZKaOhh8OwMuekAAooRxpyk/fc3YutQbrT6fD+ZDAttaM/k
qs3DkVGaRat70gSDP4Rs9G8n+tEF3sX8GW9WlPKb8uWuXQ1HJyihcjLM+snLyTvL
OwlJQU7fkKhIRFtsEb1X1B71Mj6n9SJzg2kjMJ+29EvWkIEZKu/CPIoi81mn7cvG
hK2/dVLq3CDd3zOu/xGXBuAYVMbwfBRJN3/UYZbpoHWBiDdxI8Im5IecYdRC5PFc
vr1Lo/5XHGopbU17NM11VC07Db+RbGJ1uOiiZcVPOYTbMi+ehTXVFl9Ey9hQL9hE
ZAjK1bfCBGJDW/L4EG111g==
-----END CERTIFICATE-----
)PEM";

inline const char* kServedIssuerPem = R"PEM(
-----BEGIN CERTIFICATE-----
MIIDFjCCAf6gAwIBAgIBAjANBgkqhkiG9w0BAQsFADAbMRkwFwYDVQQDDBBLeVBv
c3QgVGVzdCBSb290MB4XDTI2MTAyNzIxMjcxNloXDTM2MTAyNDIxMjcxNlowHTEb
MBkGA1UEAwwSS3lQb3N0IFRlc3QgSXNzdWVyMIIBIjANBgkqhkiG9w0BAQEFAAOC
AQ8AMIIBCgKCAQEA8dDUDn9ldHwKlFh4InFFprJ+IOMeVL7Uw9OhjNRaC2Xpc6TS
9LydEEFBz/Izcq0xEpwXAQyud62ritSyJtCJKju62wd9T21s6r9QslIDSN0gUPZW
nL+IqhHI43wYXtFLu6ymlWmJDo9Nv3a+B+aIb+AqflYyPDJN8J2UX+fzgo4mVvuX
mDfZjxDvHf6dEWGXVtZFRJLlcTEKZCH8wKJtJ0GpVU/E1ocM+/SgeihfflIuCT6v
UzOqOSQZavLscAv8AfPGSpp4qWPn9xeulCPhA35hd8jf9g44sRaIHi6iGu/iGcfW
vG/22A6bT9kQ0uZ7qfG54r5g7/nbK8JwGts8NQIDAQABo2MwYTAPBgNVHRMBAf8E
BTADAQH/MA4GA1UdDwEB/wQEAwIBBjAdBgNVHQ4EFgQUtsSdYRBtZ45hUMJTaj6q
TTPfNkAwHwYDVR0jBBgwFoAUXPX1DSLoti6Qcog4nnevx8iHz8QwDQYJKoZIhvcN
AQELBQADggEBAIAAsemDdokHEAwR9fDNJdUuDpu11wFZpKJDgRyIVfEZJdYZ8Aa3
ntVT6gGAsv01UpZgINTc3pWSrCe4vGSjzkH7mnN5adGWfyhX8IM04WAzZj+ZqpZT
lc4a9L2v2K8O0x6VRKJARvIAJb/aM3FzGlyY5QHTfPBttjCfEY0ywxX971oHR5ri
OM05fuUL4zKBVJdr3tlzRu7tUaKVIHIyICzYKGDU2Jl9+OXP4NI3oLlVHJYTXLGi
fOYjnIAHBRSNIWztf0EoccinG318ChmF7ofzudhSR4Io9HUo9Ge2xxDpF9XT25w1
wvkyzgESvSQ67BIU8XOrgUFtJTeCDL/fr14=
-----END CERTIFICATE-----
)PEM";

// The root the issuer chains to. Trusted by the pinned tests, never served.
inline const char* kTrustedRootPem = R"PEM(
-----BEGIN CERTIFICATE-----
MIIDJzCCAg+gAwIBAgIUY8yXOt++Bud0ylZsETEdVQXadsYwDQYJKoZIhvcNAQEL
BQAwGzEZMBcGA1UEAwwQS3lQb3N0IFRlc3QgUm9vdDAeFw0yNjEwMjcyMTI3MTZa
Fw0zNjEwMjQyMTI3MTZaMBsxGTAXBgNVBAMMEEt5UG9zdCBUZXN0IFJvb3QwggEi
MA0GCSqGSIb3DQEBAQUAA4IBDwAwggEKAoIBAQCXcTgPuqCrRTMFSgtkSwjfwvmr
njGMcrtBahwekR3ReF34JkFWrXy2LcTK+n2kdmqXTlD7cyDCVduIzkDIDpkW4gnm
5HuDavFARVgxSr2wIn5V7IzLd1ZPXKH6XP2+HGA0BRcrLK4xLB/27m4P2RM35euR
5XjvUiQUKGArnKKA7bj1GB9fn4/eMAc2Mi1caj2JOyFRL0FA//9CbS8cBcTpIROz
mI5faGI3jKXz7cjSlAhm5tpRl1RoszbuMMUtXccgNDcC6eyezpJ5mt3bKaEwKtzQ
INjBEb5J+egJtjsncvZNtGiiXa6UkIy7OJUHyL0Set1w/CuIRvpwhavW0+D5AgMB
AAGjYzBhMB0GA1UdDgQWBBRc9fUNIui2LpByiDied6/HyIfPxDAfBgNVHSMEGDAW
gBRc9fUNIui2LpByiDied6/HyIfPxDAPBgNVHRMBAf8EBTADAQH/MA4GA1UdDwEB
/wQEAwIBBjANBgkqhkiG9w0BAQsFAAOCAQEAPuwbb4KG5LGufiucYjljOo0blUZQ
ESfIF5hf9E3fPHXBxrvPSlC11I5nyKBl6VzW8WPcX6/7m3JTX+3dOAnwdIly8wpL
MQmwbdog09/hfkqLoPNGxZQzv29anbJq584n5NO1HqOPzMmuAnUEbJxdgGseQ6YV
MSYHL4BhpQef1YSHbk1cdkN1ydxDlWg+muNVBNm07rmAxpOfVqw9mD6f/EESIW94
WwMLevUTo0D1o6EEzX5zxYy8jmjt4w59YaxEjpd9S5JvAO3TAV2u96Udxv9fVQfo
F5F/vel+1NuGXBwHoOWILhrjlmmHPft9CP6dlY/QUwEkE0EevrPN447zbQ==
-----END CERTIFICATE-----
)PEM";

inline const char* kServedLeafKeyPem = R"PEM(
-----BEGIN PRIVATE KEY-----
MIIEvgIBADANBgkqhkiG9w0BAQEFAASCBKgwggSkAgEAAoIBAQDJA8qztywdbJqK
EVJikP3cIkANUuHeAHXm4t8PaZZF4ROFfy0v5CjFoz+9tAS7qtkNAwk+JfFxIyYU
qsijpNu/rAxHbJf1XFavik1PjnEQvAWgwiINIuhroXdcz9bSzGoAthNJqR1x24Q6
N7VoQ8LX54q8H3R0QWL/55eHA0mnTsZOdneWx19BvPeZ/M112ecFCOvZ8Xl6ADtJ
mTiI9SEAADUcNSmbky55bEbfRu5Eh9Te+oedz0rnbNWzKuSXV77WuzC7pxPrF1EU
XTcuGK8v31tOphtel9nQJfPS1L9rvknHDcFLk1tR8V4sogCO17fNlLAT02d9QWM9
GPLqOdw1AgMBAAECggEADi5yjUcZuWdeqBQh72VtHNacwA6OHqMCg7wTQmuE/XVB
iDxJRD+blxqV5gy5KG1HUVlFZxP3CpqRIUmi4eqrpy7ckehpzcFf4JQIXBm/mno4
B8KnyNdNiumyDwNJvlxLXoqkkIs6EieLbAO0ocBlaA/ZiZKgMmMk4yAkCv4oItcL
iwt+7O+AElwxoegLcokDPDGp2bIv1xD6IkbIhDZQoqlg7nXU7WzML45p4tcTfdKC
pI8KBTga85cZJJBhsKqzLTvJwymkbkovI/IizSCIvc6V+vDtpMloE2I58RrY6uZO
RWFVSumht3NaEE/dFPmyBAweigf/gjwAFgiLnXBXwQKBgQD1OUD2U7r96IczzziW
WuczjXzeDncmX5IzAy4aBKSxeSriKFG11a9lPrnyYXYb7YYFisE8rMyMo+ySrnp8
ASmePNgRWFwR0/LBVZPaVZZUm5GLfUDsHI30XE7H8h5TS8QT94qWFFqZMIaefDlO
3VrbF2LGsd2Sxv3nBcM8FiKcwQKBgQDR2TE0z7FNuTjwjm//uSpLWDuRqFqS0XIw
DWmAfcfliB1+/3SCdFfGyryDA7UllSAnVUZjOFFxxn69dzB9Mw3ON32Mtx8Mvksb
OFZGctM+tvu56Lg38YjzfHO3bKwaqhmHrRq8Dze6ExKjeEm4rule5ZeMms7ApFU/
+5goO544dQKBgQCrDAmNCDBpRRVxhPwynCuioPFVLn/bq1Qd8vzoNw7tn9fSsk70
Z52VIGCxeVvZ9R7DpNpOyTVQcFLmqX9UGbd+4W/RAXIPGWi9hETCSWq3j1/BDVgI
mY5GwVBOVUrjzPhy+PqY3o+nRNrRl1Kd+PaT1ccXUx97bwPRbgqVUddIwQKBgQCn
yyZIVyYrwsYfu5Ucgj+qXVPMZTR/HxBSEfdhMbIL++mx6auMwGCLaQKRM/69nV9H
dC1DGv4sno4rC0nvYkpQsRgn0yj1tJcznbmGvTvicsgXYFklCnH6V3HjofZ/n7Go
RicRZo2lsKh/o6g3D001fxnN6N8XNI+iCJgi24DpHQKBgE9PAWIWuM+eKtrz5c+q
3M0BLQZre3rtvWnNBTi7cvHvgJ7JDRJS+DG0TB94bScXJ/cTdGqMTkJIWnuN1F+f
4LrKG0KTxnojYKiicWAgyPWmxk+TSkqgajO97zL1Hid8XLz077yhYIShqRgAy2kk
Q1Je/KawVLddQD4wH7ok7cl3
-----END PRIVATE KEY-----
)PEM";

inline QSslCertificate certOf(const char* pem)
{
    return QSslCertificate(QByteArray(pem), QSsl::Pem);
}

// Chain B: "KyPost Test Issuer B" under "KyPost Test Root B". A different
// issuer that is just as trusted -- what a locally installed interception CA
// looks like to a client that checks only the system store.
inline const char* kServedLeafBPem = R"PEM(
-----BEGIN CERTIFICATE-----
MIIDPjCCAiagAwIBAgIBAzANBgkqhkiG9w0BAQsFADAfMR0wGwYDVQQDDBRLeVBv
c3QgVGVzdCBJc3N1ZXIgQjAeFw0yNjExMDIxMjU2MDZaFw0zNjEwMzAxMjU2MDZa
MBQxEjAQBgNVBAMMCTEyNy4wLjAuMTCCASIwDQYJKoZIhvcNAQEBBQADggEPADCC
AQoCggEBAKDRu4oai3zbaJcJEOqrIvUEdNC7hDceG+Q76vjoTbsTB5GliB2x5Gui
pU+4ksCzQejKtsAvmaghhoJjV1MTJQCjg7ArmPOG0BxfwtaU6su9b18OcOD4cQdo
2XV/kVKT61jH9JIbdAbMl4FkmSax27Gre17XptrwntLYKze2iV0iFzgUiRrOMnx2
KoxERudZvUV5j/yVRvTrP7ecn9QNBJkczeMBB5kebBFINBjslPU2ODJ8Bqs4H70c
Xr/aGUCcrA6BzIJV60ik2WdAYMNwKwodo4pbRq2wqdhF+Irw4xUKGHUSc1E2alCm
KWV6YcU3YuKflITxV5OFs0FUurHB6zMCAwEAAaOBjzCBjDAJBgNVHRMEAjAAMA4G
A1UdDwEB/wQEAwIFoDATBgNVHSUEDDAKBggrBgEFBQcDATAaBgNVHREEEzARhwR/
AAABgglsb2NhbGhvc3QwHQYDVR0OBBYEFFQkmEL20+9THWlea1im7HtCTngbMB8G
A1UdIwQYMBaAFFrD7G4hSKVQMi86N3ppGABU6hKnMA0GCSqGSIb3DQEBCwUAA4IB
AQA1YxY5f4VhH/kgjZTpozpNu4LIG6mlLIqbFlNW80fVXf+ausJWR3SE4tws1QgG
Ky7+BwXuYTacNfv0xjYZcI2hhfjclYM7yKqUBlOk9xnGpVREDSzr0oXyolF8J5jZ
BN3HSwQ/1R/MBm5At+ZaPR0nbb71UFoOUA21JrNaNz0CsRqnaXB3JxTI661JMeh5
8HGmeaZWfUe9Id+VwOvVT2pLguUzU47IzIJ/daV2gw/1T4zjRmql39NHEeyOk5ko
P3IvFpEXo4ngLIh850fyVBJWskr3HOG/Dz83xh7HNF4sIOHXgdoPIYDPZ764d+5C
z4T7Ked5xK7SGbbIGTMlAnSs
-----END CERTIFICATE-----
)PEM";

inline const char* kServedIssuerBPem = R"PEM(
-----BEGIN CERTIFICATE-----
MIIDGjCCAgKgAwIBAgIBAjANBgkqhkiG9w0BAQsFADAdMRswGQYDVQQDDBJLeVBv
c3QgVGVzdCBSb290IEIwHhcNMjYxMTAyMTI1NjA2WhcNMzYxMDMwMTI1NjA2WjAf
MR0wGwYDVQQDDBRLeVBvc3QgVGVzdCBJc3N1ZXIgQjCCASIwDQYJKoZIhvcNAQEB
BQADggEPADCCAQoCggEBAJo2URiwlO3Q3TPB6nmu7pU6Dj9ZzIE1lA03m5MBhNlj
UVO8RWIyGD3hVNMUgU9y5HXOGJm8NyuDen9VoSQliEoaD8t+9nfba1u6pY1IVYB+
9pFdRlvFlY/F+pMH0nVhg/QIbrnB891AtqpM/r7nQi5qpmL/Aqr54g5Vjm6IU961
T0v4bWFBrJaHRliJZ+mGY4QSp2LuAalI6HxYIx5giFcceuVIJ8Yv+F4BI5n466oQ
XdkugZaj255ShYWjZhYwZoQjDd2Y/qYVdteHtSPsMuEcGe67ouy5aqzhG8XG8TEt
1cZTcphVuj1iksx1hFX1A75/W3oVJC03Vw0YFt5SkG0CAwEAAaNjMGEwDwYDVR0T
AQH/BAUwAwEB/zAOBgNVHQ8BAf8EBAMCAQYwHQYDVR0OBBYEFFrD7G4hSKVQMi86
N3ppGABU6hKnMB8GA1UdIwQYMBaAFL5ZhY4G9oVlXyvw7ZS+b3aNMUMiMA0GCSqG
SIb3DQEBCwUAA4IBAQCF2dK/Fk3gcLX6VEQcvjFhGCl7MWs1OSPlhutfklE+U+lB
Zl93l+ear8AwnA4zsUh6hcQUAZxK1MFFHXHMGcCU14FxrX8Fe7vqE5RQg3exQgbt
NM4RHjyTsno58AfHHEMyu9V4ivpXlvzXZGYH/aiCQ8c72n0E71h9wLkkDYKJBOoD
QRqsxKiril5syZuCTvYx+dz8tOdgeh/s7QkSQZrI+LjxfZiPI8Czod/+LU+JHDJ7
pL3J3Ij2M34dmhPTDuS6QlYpkdA39EN39EezEHYOv7Gj54/yHsLdenlMFObs62lM
LUa9oTqUdRIAvYSThaCj4M9mS0SdNEkYDrX0Cvbz
-----END CERTIFICATE-----
)PEM";

inline const char* kTrustedRootBPem = R"PEM(
-----BEGIN CERTIFICATE-----
MIIDKzCCAhOgAwIBAgIUIJZw8TgotGTbnzw+iqCT0aoPvCYwDQYJKoZIhvcNAQEL
BQAwHTEbMBkGA1UEAwwSS3lQb3N0IFRlc3QgUm9vdCBCMB4XDTI2MTEwMjEyNTYw
NloXDTM2MTAzMDEyNTYwNlowHTEbMBkGA1UEAwwSS3lQb3N0IFRlc3QgUm9vdCBC
MIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEAsxtGom+al7+9Sb/oMBcE
NMl6LGq6erlsOnP/nMunPT1AFfg6xkOw22LttSRz/z7McyoIAUQsHhWilNOep/t+
p3XBbNiteqC7FQZDueCQ2ibxGhZn4ukR2V3jeGPYqsn3HnYPlmrOo+5Ci1BD5zFb
Al/c2uYbhXGDmSgDk2sZWOJdNlyETakeYlpfOlK4VNPq9Zq/MRnv1p0ZciMgdYDw
b+S4Y3WkOvxAPVKytx6thZOQ0ElewGpZiOSnpKR+xwW/Myevg2gab7XGeUL4dKvu
aggxmuZEOqev9lAbVBO+CAx0nmd7W6zv1gV7IhMfkiwQslhcgiSvKoZS+12V/zrB
UwIDAQABo2MwYTAdBgNVHQ4EFgQUvlmFjgb2hWVfK/DtlL5vdo0xQyIwHwYDVR0j
BBgwFoAUvlmFjgb2hWVfK/DtlL5vdo0xQyIwDwYDVR0TAQH/BAUwAwEB/zAOBgNV
HQ8BAf8EBAMCAQYwDQYJKoZIhvcNAQELBQADggEBAKHD4ouvQngvfyHXvM4SLii3
W0K68/e/1oqDsKS5fNWCXpzCsc9tJDGZ8OgJj63lhgisp/6A8MlpgTv2DLPm7dL4
Ag9gC7H+HhQhIndQwx4Dm2D2O3x8FK7gXuipSVr/bzZEXu6TEIsYiX7NcEdHiBob
RJ1FdHjxK0/v7b+UAVLsxZk6XLYGjsEJMtU9fkT2FC1ekzsoD8PZdwGad/IhCEnI
S+aPOx8ijjy4LNbv9LCd5rOEK9oy33oYTFK1MlXXVrxk3RecRPTkkOM4L7yVzlhn
rHThMgruEXHMrqfhYlxZOkA70mU1w4vmdlGQ6HJdIC4xIWom95cbni6wEJDWbOM=
-----END CERTIFICATE-----
)PEM";

inline const char* kServedLeafKeyBPem = R"PEM(
-----BEGIN PRIVATE KEY-----
MIIEvwIBADANBgkqhkiG9w0BAQEFAASCBKkwggSlAgEAAoIBAQCg0buKGot822iX
CRDqqyL1BHTQu4Q3HhvkO+r46E27EweRpYgdseRroqVPuJLAs0HoyrbAL5moIYaC
Y1dTEyUAo4OwK5jzhtAcX8LWlOrLvW9fDnDg+HEHaNl1f5FSk+tYx/SSG3QGzJeB
ZJkmsduxq3te16ba8J7S2Cs3toldIhc4FIkazjJ8diqMREbnWb1FeY/8lUb06z+3
nJ/UDQSZHM3jAQeZHmwRSDQY7JT1NjgyfAarOB+9HF6/2hlAnKwOgcyCVetIpNln
QGDDcCsKHaOKW0atsKnYRfiK8OMVChh1EnNRNmpQpillemHFN2Lin5SE8VeThbNB
VLqxweszAgMBAAECggEAGPUeNaszmoeHuRN0cfa9JhPa98nMwFZb3b+uvOd6/gf7
VLnBC1q1fZQa3j8MB1EfxBDcQQHMxtrtFQR1uN7FD077pjYd4S061CxzAE98C20K
q6XXaWfPorwS2LlDbvKlw3wZ6H16NClBb0M9a/6OKbSDIOThcCOHgmZ/I9ZK2w5V
k0+2jJH4CARRNm1N0NOzsynrVSuYqcYkmeSJxSeU91hcGlf2izGF6FljKqz0Ivbg
/q3eQ2XdaVfe6+iGqqWAjsz9lNK6P6yQqc7UA5I1Q/fM0xagnuR0PhgSmnb/DjCH
4m8wnBSm/TGq0UOEHOW/9ENER2h/q2MPwACu4wTgAQKBgQDXxJqbHbIIoPQTbVrN
WRTuOGgYYbVGg9pyuutPx7z8Brm+h8xuqoYaVXolPSO3m0+2bFRfHkoortDtUH8X
hcJymUwv1T4FiV04wJFK0ZriB4QalO57QKgKIadqiVjm8tlmzTSi52v7D2+EHHYn
g6xOjUk9NAlpYdyycH+dxZMWMwKBgQC+zjml3CP3zV7SHkc0ELbM/7Q06NRgjuW/
9vVqYlPwtCYpVMsx44SxMyn+HDfHVma+LWpzKgX5W56ZenHDXJOZd42hu1C0jddk
fAH30GJrfTyekOzEtYfw3aRxngNF+HOHKRkSlFjsMBS/ypos0kbUUPXf1xhZFlca
g10g207XAQKBgQCqw/MIqOvqYFhLt9SRiNtD3mikQ1BQyrHs83qg3Zf4buo6nQdy
3orb9KKDLmxCOC1SV1I/JZ3TrOe1qmurljVatXDSupYnk5uv+tgCuvk1wnUPBKfp
7DYSLqhtbN5rie/9qJLsCvQu8vxzNoNcqp2IXdGFGtYIDWe54cdeeqv6eQKBgQC0
5Gaa7B3dbIKcyb/PiJkFuNZr4uCVumPunn8EoLvb067qOE17uquuI0eIddAb17oI
Sp5FGG4Zfdbaws7f3ADC2ikYT7Gvu1mdFkP3YUQti8nQuqz9wOxkWnzwilGoIGfG
mBwBFUtIgDzrp3SLUb98wlSYJiAbNabWp/u+jqwsAQKBgQDXj52IR3UEARlyVFck
loiN4dbHehlGgEKyeLrSnN8MZq2mWH4E+x2zCsipQQ4khBwFwzHh2N8Sp3GzAx/n
l3PCrQTfrdufN2lb2fpza5r04yX/MYLlntVurU/CQvYbmUIg8PINDnx4qBiu7tNv
niVr/rrkNmDuOSDAPu9MbHysjw==
-----END PRIVATE KEY-----
)PEM";

// Restores the process-wide default QSslConfiguration, which the TLS tests
// point at the test roots: HttpClient always verifies the peer.
struct SslDefaultsGuard
{
    QSslConfiguration previous = QSslConfiguration::defaultConfiguration();
    ~SslDefaultsGuard() { QSslConfiguration::setDefaultConfiguration(previous); }
};

inline void trustRoots(const QList<QSslCertificate>& roots)
{
    QSslConfiguration trusted = QSslConfiguration::defaultConfiguration();
    trusted.setCaCertificates(roots);
    QSslConfiguration::setDefaultConfiguration(trusted);
}

// FakeRelayServer over TLS: answers each request with the response for its
// path, or the default, and closes the connection, so every request is a
// fresh handshake. The chain it serves can be swapped between connections,
// which is how a test puts a different, equally trusted server on the same
// origin. Requests are recorded only once they arrive, so one refused at the
// handshake leaves no trace.
class TlsRelayServer : public QTcpServer
{
public:
    explicit TlsRelayServer(QByteArray response)
        : m_response(std::move(response))
    {
        serveChainA();
        listen(QHostAddress::LocalHost);
    }

    void serveChainA() { serveChain(kServedLeafPem, kServedIssuerPem, kServedLeafKeyPem); }
    void serveChainB() { serveChain(kServedLeafBPem, kServedIssuerBPem, kServedLeafKeyBPem); }

    void setResponse(QByteArray response) { m_response = std::move(response); }
    // Replaces any earlier response for the same needle.
    void setResponseForPath(const QByteArray& needle, QByteArray response)
    {
        for (auto& entry : m_byPath) {
            if (entry.first == needle) {
                entry.second = std::move(response);
                return;
            }
        }
        m_byPath.append({ needle, std::move(response) });
    }

    const QList<QByteArray>& receivedRequests() const { return m_requests; }

protected:
    void incomingConnection(qintptr handle) override
    {
        auto* socket = new QSslSocket(this);
        if (!socket->setSocketDescriptor(handle)) {
            delete socket;
            return;
        }
        socket->setSslConfiguration(m_config);
        auto request = std::make_shared<QByteArray>();
        connect(socket, &QIODevice::readyRead, this, [this, socket, request]() {
            *request += socket->readAll();
            const int headerEnd = request->indexOf("\r\n\r\n");
            if (headerEnd < 0)
                return;
            qsizetype length = 0;
            for (const QByteArray& line : request->left(headerEnd).split('\n')) {
                if (line.toLower().startsWith("content-length:"))
                    length = line.mid(15).trimmed().toLongLong();
            }
            if (request->size() < headerEnd + 4 + length)
                return;
            m_requests.append(*request);
            socket->write(responseFor(*request));
            socket->flush();
            socket->disconnectFromHost();
        });
        connect(socket, &QSslSocket::disconnected, socket, &QObject::deleteLater);
        socket->startServerEncryption();
    }

private:
    void serveChain(const char* leaf, const char* issuer, const char* key)
    {
        m_config = QSslConfiguration::defaultConfiguration();
        m_config.setLocalCertificateChain({ certOf(leaf), certOf(issuer) });
        m_config.setPrivateKey(QSslKey(QByteArray(key), QSsl::Rsa, QSsl::Pem, QSsl::PrivateKey));
        m_config.setAllowedNextProtocols({ QSslConfiguration::NextProtocolHttp1_1 });
    }

    QByteArray responseFor(const QByteArray& request) const
    {
        const QByteArray requestLine = request.left(request.indexOf("\r\n"));
        for (const auto& entry : m_byPath) {
            if (requestLine.contains(entry.first))
                return entry.second;
        }
        return m_response;
    }

    QSslConfiguration m_config;
    QByteArray m_response;
    QList<QPair<QByteArray, QByteArray>> m_byPath;
    QList<QByteArray> m_requests;
};

} // namespace TestTls
