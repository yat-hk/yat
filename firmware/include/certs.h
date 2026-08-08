#pragma once
// TLS trust anchors — multi-root CA bundle for arbitrary packs.
//
// GENERATED — do not hand-edit. v0.1 shipped only the Hongkong Post root
// (data.weather.gov.hk); v0.2 packs load from LittleFS and can point at any
// https source (data.gov.hk, hn.algolia.com, open-meteo.com, open.er-api.com,
// ...), so the bundle now covers the common public roots those chain
// through. Concatenated multi-PEM in one buffer: mbedTLS's x509 parser (used
// by WiFiClientSecure::setCACert) trusts every certificate in the buffer, not
// just the first — no esp_crt_bundle binary-blob build step needed. Verify
// any addition with `openssl x509 -noout -subject -issuer -dates -in <file>`
// before pasting it in; each entry below has the commands used to fetch it.
//
// 1. Hongkong Post Root CA 3 — data.weather.gov.hk (HKO), data.gov.hk.
//    Regenerate: echo | openssl s_client -connect data.weather.gov.hk:443 -showcerts
//    Two anchors, both subject "C=HK, O=Hongkong Post, CN=Hongkong Post Root CA 3":
//      a. cross-signed copy the server actually presents as the chain tail
//         (issued by GlobalSign Root CA - R3, expires 2029-03-18)
//      b. self-signed root (expires 2042-06-03)
//    mbedTLS matches a trust anchor by issuer name + signature, so either one
//    terminates the chain; (b) is what keeps this working past 2029.
// 2. GlobalSign Root CA - R3 — self-signed, expires 2029-03-18. Covers any
//    pack source chaining through GlobalSign R3 generally (not just the HKO
//    cross-sign above). Fetch: https://secure.globalsign.com/cacert/root-r3.crt
//    (DER; `openssl x509 -inform der -in root-r3.crt -out root-r3.pem`).
// 3. ISRG Root X1 (Let's Encrypt) — self-signed, expires 2035-06-04. Covers
//    the large fraction of small/free API hosts on Let's Encrypt.
//    Fetch: https://letsencrypt.org/certs/isrgrootx1.pem
// 4. DigiCert Global Root G2 — self-signed, expires 2038-01-15. One of the
//    most widely cross-signed commercial roots.
//    Fetch: https://cacerts.digicert.com/DigiCertGlobalRootG2.crt.pem
// 5. Google Trust Services GTS Root R1 — self-signed, expires 2036-06-22.
//    Fetch: https://pki.goog/repo/certs/gtsr1.pem
//    Covers e.g. hn.algolia.com (chain: leaf -> GTS WR3 -> GTS Root R1).
// 6. Google Trust Services GTS Root R4 — self-signed (ECDSA P-384), expires
//    2036-06-22. Fetch: https://pki.goog/repo/certs/gtsr4.pem
//    GTS runs R1-R4 as *separate* trust anchors (R1/R3 RSA, R2/R4 ECDSA) —
//    R1 alone is not enough. Confirmed by chain inspection
//    (openssl s_client -connect <host>:443 -showcerts) that open-meteo.com
//    and open.er-api.com both terminate at R4, not R1; R1 only covers
//    hn.algolia.com. Both are included so "any Google Trust Services chain"
//    actually holds, not just the one host that happened to be checked first.
//    Also covers openrouter.ai (the voice router's second provider), whose
//    chain is leaf -> GTS WE1 -> GTS Root R4. WE1 is not in this bundle and
//    does not need to be: the server presents it, and mbedTLS terminates the
//    presented chain on the anchor below. Verified 2026-08-03 with this anchor
//    ALONE as the CAfile — `openssl s_client -connect openrouter.ai:443
//    -servername openrouter.ai -CAfile <this cert> -verify_return_error` ->
//    "Verify return code: 0 (ok)" — so adding the second LLM provider added no
//    certificate to this bundle at all.
// 7. Google Trust Services WR3 — intermediate cert (RSA 2048), signed by GTS
//    Root R1, expires 2029-02-20. Used by api.elevenlabs.io (ElevenLabs).
//    Fetch: echo | openssl s_client -connect api.elevenlabs.io:443 -showcerts
//    Needed for cert chain verification: leaf -> WR3 -> GTS Root R1.
// 8. Amazon Root CA 1 — self-signed, expires 2038-01-17. Used by
//    api.deepseek.com (the voice router's LLM tier), whose chain is
//    leaf *.deepseek.com -> Amazon RSA 2048 M01 -> Amazon Root CA 1.
//    Fetch: https://www.amazontrust.com/repository/AmazonRootCA1.pem
//    Verified 2026-08-02: `openssl x509 -noout -subject -enddate` gives
//    "C=US, O=Amazon, CN=Amazon Root CA 1" / "Jan 17 00:00:00 2038 GMT".
//    As with anchor 1, the server's own chain tail is the CROSS-SIGNED copy of
//    this root (issued by Starfield Services Root CA - G2), not this
//    self-signed one — and as there, that does not matter: mbedTLS looks for
//    each certificate's issuer in the trust store before it looks in the
//    presented chain, so "Amazon RSA 2048 M01" terminates on the anchor below
//    and the cross-signed copy is never needed. Confirmed end to end with
//    `openssl s_client -connect api.deepseek.com:443 -CAfile AmazonRootCA1.pem
//    -verify_return_error` -> "Verify return code: 0 (ok)", i.e. this one
//    certificate alone is enough; no Starfield anchor is added.
//    Also covers integrate.api.nvidia.com (the voice router's third provider),
//    whose chain is byte-for-byte the same shape: leaf -> Amazon RSA 2048 M01
//    -> Amazon Root CA 1. Verified live 2026-08-03 against this bundle ->
//    "Verify return code: 0 (ok)", so the NVIDIA provider added no certificate
//    here either.
// 9. Sectigo Public Server Authentication Root E46 — self-signed (ECDSA
//    P-384), expires 2046-03-21. Used by github.com, which is where the
//    firmware update path (yat_ota.cpp) asks for a release asset.
//    Fetch: http://crt.sectigo.com/SectigoPublicServerAuthenticationRootE46.crt
//    (DER over plain http — Sectigo's crt host serves no https. That is not a
//    trust problem here and was not taken on faith: the public key of the file
//    below is byte-identical to the one in the cross-signed copy github.com
//    itself presents as its chain tail, checked with
//    `openssl x509 -noout -pubkey | openssl sha256` on both ->
//    b84bb222a895ff25e969cac3d8404983bd433f46b590dabffed341ea1e23b340.
//    sha256 fingerprint of the certificate below:
//    C9:0F:26:F0:FB:1B:40:18:B2:22:27:51:9B:5C:A2:B5:3E:2C:A5:B3:BE:5C:F1:8E:FE:1B:EF:47:38:0C:53:83)
//    As with anchors 1 and 8, the chain tail github.com actually sends is the
//    CROSS-SIGNED copy of this root (issued by USERTrust ECC Certification
//    Authority) rather than this self-signed one, and as there that does not
//    matter: mbedTLS looks for each certificate's issuer in the trust store
//    before the presented chain, so "Sectigo Public Server Authentication CA
//    DV E36" terminates on the anchor below. Verified live 2026-08-04 with
//    this certificate ALONE as the CAfile — `openssl s_client -connect
//    github.com:443 -servername github.com -CAfile <this cert>
//    -verify_return_error` -> "Verify return code: 0 (ok)".
//
//    The OTHER host the update path talks to — the release-asset CDN that
//    github.com 302-redirects to, release-assets.githubusercontent.com — added
//    NOTHING to this bundle, which is worth writing down because it is not
//    obvious from the chain it presents. That chain is
//    leaf(*.githubusercontent.com) -> Let's Encrypt YR2 -> ISRG Root YR, and
//    "Root YR" is a 2026 root that is not in this file. It verifies anyway:
//    the copy of Root YR the server sends is cross-signed by ISRG Root X1,
//    which IS here as anchor 3, so the presented chain terminates one link
//    further along than it looks like it should. Verified live 2026-08-04
//    against this bundle -> "Verify return code: 0 (ok)". If Let's Encrypt
//    ever stops sending the cross-signed copy, the self-signed ISRG Root YR
//    has to be added here or firmware updates stop downloading.
//
// No setInsecure() anywhere in this firmware.

inline const char YAT_CA_BUNDLE[] PROGMEM =
    // --- 1a. Hongkong Post Root CA 3, cross-signed by GlobalSign R3 ---
    "-----BEGIN CERTIFICATE-----\n"
    "MIIFqjCCBJKgAwIBAgIQfYd70RQkwiYMcCxd6zOrFzANBgkqhkiG9w0BAQsFADBM\n"
    "MSAwHgYDVQQLExdHbG9iYWxTaWduIFJvb3QgQ0EgLSBSMzETMBEGA1UEChMKR2xv\n"
    "YmFsU2lnbjETMBEGA1UEAxMKR2xvYmFsU2lnbjAeFw0yMjExMTYwMzM1MDhaFw0y\n"
    "OTAzMTgwMDAwMDBaMG8xCzAJBgNVBAYTAkhLMRIwEAYDVQQIEwlIb25nIEtvbmcx\n"
    "EjAQBgNVBAcTCUhvbmcgS29uZzEWMBQGA1UEChMNSG9uZ2tvbmcgUG9zdDEgMB4G\n"
    "A1UEAxMXSG9uZ2tvbmcgUG9zdCBSb290IENBIDMwggIiMA0GCSqGSIb3DQEBAQUA\n"
    "A4ICDwAwggIKAoICAQCziNfqzg8gTr7m1gNt7ln8wlffKWihgw4+aMdoWJwcYEuJ\n"
    "Qwy51BWy7sFOdem1p+/l6TWZ5Mwc50tfjTMwIDNT2aa71T4Tjukfh0mtUC1Qyhi+\n"
    "AViiE3CWu4mIVoBc+L0sPOFMV4i707mV78vH9toxdCim5lSJ9UExyuUmGs2C4HDa\n"
    "Oym71QP1mbpV9WTRYA6ziUm4ii8F0oRFKHyPaFASePwLtVPLwpgchKOesL4jpNrc\n"
    "yCse2m5FHomY2vkALgbpDDtw1VAliJnLzXNg99X/NWfFobxeq81KuEXryGgeDQ0U\n"
    "RhLj0mRiikKYvLTGCAj4/ahMZJx2Ab0vqWwzD9g/KLg8aQFChn5pwckGyuV6RmXp\n"
    "wtZQQS4/t+TtbNe/JgERohYpSms0BpDsE9K2+2p20jzt8NYt3eEV7KObLyzJPivk\n"
    "aTv/ciWxNoZbx39ri1UbSsUgYT2uy1DhCDq+sI9jQVMwCFk8mB13umOResoQUGC/\n"
    "8Ne8lYePl8X+l2oBlKN8W4UdKjk60FSh0Tlxnf0h+bV78OLgAo9uliQlLKAeLKjE\n"
    "iafv7ZkGL7YKTE/bosw3Gq9HhS2KX8Q0NEwA/RiTZxPRN+ZItIsGxVd7GYYKecsA\n"
    "yVKvQv83j+GjHno9UKtjBucVtT+2RTeUN7F+8kjDf8V1/peNRY8apxpyKBpADwID\n"
    "AQABo4IBYzCCAV8wDgYDVR0PAQH/BAQDAgGGMB0GA1UdJQQWMBQGCCsGAQUFBwMB\n"
    "BggrBgEFBQcDAjAPBgNVHRMBAf8EBTADAQH/MB0GA1UdDgQWBBQXnc0ei9Y5K3DT\n"
    "XNSguB+wAPzFYTAfBgNVHSMEGDAWgBSP8Et/qC5FJK5NUPpjmove4t0bvDB6Bggr\n"
    "BgEFBQcBAQRuMGwwLQYIKwYBBQUHMAGGIWh0dHA6Ly9vY3NwLmdsb2JhbHNpZ24u\n"
    "Y29tL3Jvb3RyMzA7BggrBgEFBQcwAoYvaHR0cDovL3NlY3VyZS5nbG9iYWxzaWdu\n"
    "LmNvbS9jYWNlcnQvcm9vdC1yMy5jcnQwNgYDVR0fBC8wLTAroCmgJ4YlaHR0cDov\n"
    "L2NybC5nbG9iYWxzaWduLmNvbS9yb290LXIzLmNybDApBgNVHSAEIjAgMAcGBWeB\n"
    "DAEBMAgGBmeBDAECAjALBgkrBgEEAaAyAQEwDQYJKoZIhvcNAQELBQADggEBAEA0\n"
    "F9QNS0HJRzP9ui8n0ZSxxzLvddbFpQmbXAiPoOiGdy9DQVf+ZneXUKl7SN55eOUX\n"
    "lvHkE9FvlfBgrGAuDlfvlfQCXVQc/2VWYqEivAH0a3/wEUWxP0+vQb2cUexWcTDy\n"
    "dDxPiyaGKevJPXN68Qm8HiBW+I+jZn4NIh0+oAEwfatwa4PdKKixEETHZWL7/Ij+\n"
    "X9rMiMGMSb61BoszkVh3kd5j9O69E/qQRZFJ7XbJoCz6tLEtA35tMX4gv/CUFs20\n"
    "fffSf6k0eSNeMADAvclGMoPb/MH2ULptZBER/Q6bh3UE26yva2MwXUCXV8kUnSDX\n"
    "xrNpi9utIhHtiIiGgO4=\n"
    "-----END CERTIFICATE-----\n"
    // --- 1b. Hongkong Post Root CA 3, self-signed (long-lived anchor) ---
    "-----BEGIN CERTIFICATE-----\n"
    "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\n"
    "mpv0\n"
    "-----END CERTIFICATE-----\n"
    // --- 2. GlobalSign Root CA - R3, self-signed ---
    "-----BEGIN CERTIFICATE-----\n"
    "MIIDXzCCAkegAwIBAgILBAAAAAABIVhTCKIwDQYJKoZIhvcNAQELBQAwTDEgMB4G\n"
    "A1UECxMXR2xvYmFsU2lnbiBSb290IENBIC0gUjMxEzARBgNVBAoTCkdsb2JhbFNp\n"
    "Z24xEzARBgNVBAMTCkdsb2JhbFNpZ24wHhcNMDkwMzE4MTAwMDAwWhcNMjkwMzE4\n"
    "MTAwMDAwWjBMMSAwHgYDVQQLExdHbG9iYWxTaWduIFJvb3QgQ0EgLSBSMzETMBEG\n"
    "A1UEChMKR2xvYmFsU2lnbjETMBEGA1UEAxMKR2xvYmFsU2lnbjCCASIwDQYJKoZI\n"
    "hvcNAQEBBQADggEPADCCAQoCggEBAMwldpB5BngiFvXAg7aEyiie/QV2EcWtiHL8\n"
    "RgJDx7KKnQRfJMsuS+FggkbhUqsMgUdwbN1k0ev1LKMPgj0MK66X17YUhhB5uzsT\n"
    "gHeMCOFJ0mpiLx9e+pZo34knlTifBtc+ycsmWQ1z3rDI6SYOgxXG71uL0gRgykmm\n"
    "KPZpO/bLyCiR5Z2KYVc3rHQU3HTgOu5yLy6c+9C7v/U9AOEGM+iCK65TpjoWc4zd\n"
    "QQ4gOsC0p6Hpsk+QLjJg6VfLuQSSaGjlOCZgdbKfd/+RFO+uIEn8rUAVSNECMWEZ\n"
    "XriX7613t2Saer9fwRPvm2L7DWzgVGkWqQPabumDk3F2xmmFghcCAwEAAaNCMEAw\n"
    "DgYDVR0PAQH/BAQDAgEGMA8GA1UdEwEB/wQFMAMBAf8wHQYDVR0OBBYEFI/wS3+o\n"
    "LkUkrk1Q+mOai97i3Ru8MA0GCSqGSIb3DQEBCwUAA4IBAQBLQNvAUKr+yAzv95ZU\n"
    "RUm7lgAJQayzE4aGKAczymvmdLm6AC2upArT9fHxD4q/c2dKg8dEe3jgr25sbwMp\n"
    "jjM5RcOO5LlXbKr8EpbsU8Yt5CRsuZRj+9xTaGdWPoO4zzUhw8lo/s7awlOqzJCK\n"
    "6fBdRoyV3XpYKBovHd7NADdBj+1EbddTKJd+82cEHhXXipa0095MJ6RMG3NzdvQX\n"
    "mcIfeg7jLQitChws/zyrVQ4PkX4268NXSb7hLi18YIvDQVETI53O9zJrlAGomecs\n"
    "Mx86OyXShkDOOyyGeMlhLxS67ttVb9+E7gUJTb0o2HLO02JQZR7rkpeDMdmztcpH\n"
    "WD9f\n"
    "-----END CERTIFICATE-----\n"
    // --- 3. ISRG Root X1 (Let's Encrypt), self-signed ---
    "-----BEGIN CERTIFICATE-----\n"
    "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\n"
    "-----END CERTIFICATE-----\n"
    // --- 4. DigiCert Global Root G2, self-signed ---
    "-----BEGIN CERTIFICATE-----\n"
    "MIIDjjCCAnagAwIBAgIQAzrx5qcRqaC7KGSxHQn65TANBgkqhkiG9w0BAQsFADBh\n"
    "MQswCQYDVQQGEwJVUzEVMBMGA1UEChMMRGlnaUNlcnQgSW5jMRkwFwYDVQQLExB3\n"
    "d3cuZGlnaWNlcnQuY29tMSAwHgYDVQQDExdEaWdpQ2VydCBHbG9iYWwgUm9vdCBH\n"
    "MjAeFw0xMzA4MDExMjAwMDBaFw0zODAxMTUxMjAwMDBaMGExCzAJBgNVBAYTAlVT\n"
    "MRUwEwYDVQQKEwxEaWdpQ2VydCBJbmMxGTAXBgNVBAsTEHd3dy5kaWdpY2VydC5j\n"
    "b20xIDAeBgNVBAMTF0RpZ2lDZXJ0IEdsb2JhbCBSb290IEcyMIIBIjANBgkqhkiG\n"
    "9w0BAQEFAAOCAQ8AMIIBCgKCAQEAuzfNNNx7a8myaJCtSnX/RrohCgiN9RlUyfuI\n"
    "2/Ou8jqJkTx65qsGGmvPrC3oXgkkRLpimn7Wo6h+4FR1IAWsULecYxpsMNzaHxmx\n"
    "1x7e/dfgy5SDN67sH0NO3Xss0r0upS/kqbitOtSZpLYl6ZtrAGCSYP9PIUkY92eQ\n"
    "q2EGnI/yuum06ZIya7XzV+hdG82MHauVBJVJ8zUtluNJbd134/tJS7SsVQepj5Wz\n"
    "tCO7TG1F8PapspUwtP1MVYwnSlcUfIKdzXOS0xZKBgyMUNGPHgm+F6HmIcr9g+UQ\n"
    "vIOlCsRnKPZzFBQ9RnbDhxSJITRNrw9FDKZJobq7nMWxM4MphQIDAQABo0IwQDAP\n"
    "BgNVHRMBAf8EBTADAQH/MA4GA1UdDwEB/wQEAwIBhjAdBgNVHQ4EFgQUTiJUIBiV\n"
    "5uNu5g/6+rkS7QYXjzkwDQYJKoZIhvcNAQELBQADggEBAGBnKJRvDkhj6zHd6mcY\n"
    "1Yl9PMWLSn/pvtsrF9+wX3N3KjITOYFnQoQj8kVnNeyIv/iPsGEMNKSuIEyExtv4\n"
    "NeF22d+mQrvHRAiGfzZ0JFrabA0UWTW98kndth/Jsw1HKj2ZL7tcu7XUIOGZX1NG\n"
    "Fdtom/DzMNU+MeKNhJ7jitralj41E6Vf8PlwUHBHQRFXGU7Aj64GxJUTFy8bJZ91\n"
    "8rGOmaFvE7FBcf6IKshPECBV1/MUReXgRPTqh5Uykw7+U0b6LJ3/iyK5S9kJRaTe\n"
    "pLiaWN0bfVKfjllDiIGknibVb63dDcY3fe0Dkhvld1927jyNxF1WW6LZZm6zNTfl\n"
    "MrY=\n"
    "-----END CERTIFICATE-----\n"
    // --- 5. Google Trust Services GTS Root R1, self-signed ---
    "-----BEGIN CERTIFICATE-----\n"
    "MIIFVzCCAz+gAwIBAgINAgPlk28xsBNJiGuiFzANBgkqhkiG9w0BAQwFADBHMQsw\n"
    "CQYDVQQGEwJVUzEiMCAGA1UEChMZR29vZ2xlIFRydXN0IFNlcnZpY2VzIExMQzEU\n"
    "MBIGA1UEAxMLR1RTIFJvb3QgUjEwHhcNMTYwNjIyMDAwMDAwWhcNMzYwNjIyMDAw\n"
    "MDAwWjBHMQswCQYDVQQGEwJVUzEiMCAGA1UEChMZR29vZ2xlIFRydXN0IFNlcnZp\n"
    "Y2VzIExMQzEUMBIGA1UEAxMLR1RTIFJvb3QgUjEwggIiMA0GCSqGSIb3DQEBAQUA\n"
    "A4ICDwAwggIKAoICAQC2EQKLHuOhd5s73L+UPreVp0A8of2C+X0yBoJx9vaMf/vo\n"
    "27xqLpeXo4xL+Sv2sfnOhB2x+cWX3u+58qPpvBKJXqeqUqv4IyfLpLGcY9vXmX7w\n"
    "Cl7raKb0xlpHDU0QM+NOsROjyBhsS+z8CZDfnWQpJSMHobTSPS5g4M/SCYe7zUjw\n"
    "TcLCeoiKu7rPWRnWr4+wB7CeMfGCwcDfLqZtbBkOtdh+JhpFAz2weaSUKK0Pfybl\n"
    "qAj+lug8aJRT7oM6iCsVlgmy4HqMLnXWnOunVmSPlk9orj2XwoSPwLxAwAtcvfaH\n"
    "szVsrBhQf4TgTM2S0yDpM7xSma8ytSmzJSq0SPly4cpk9+aCEI3oncKKiPo4Zor8\n"
    "Y/kB+Xj9e1x3+naH+uzfsQ55lVe0vSbv1gHR6xYKu44LtcXFilWr06zqkUspzBmk\n"
    "MiVOKvFlRNACzqrOSbTqn3yDsEB750Orp2yjj32JgfpMpf/VjsPOS+C12LOORc92\n"
    "wO1AK/1TD7Cn1TsNsYqiA94xrcx36m97PtbfkSIS5r762DL8EGMUUXLeXdYWk70p\n"
    "aDPvOmbsB4om3xPXV2V4J95eSRQAogB/mqghtqmxlbCluQ0WEdrHbEg8QOB+DVrN\n"
    "VjzRlwW5y0vtOUucxD/SVRNuJLDWcfr0wbrM7Rv1/oFB2ACYPTrIrnqYNxgFlQID\n"
    "AQABo0IwQDAOBgNVHQ8BAf8EBAMCAYYwDwYDVR0TAQH/BAUwAwEB/zAdBgNVHQ4E\n"
    "FgQU5K8rJnEaK0gnhS9SZizv8IkTcT4wDQYJKoZIhvcNAQEMBQADggIBAJ+qQibb\n"
    "C5u+/x6Wki4+omVKapi6Ist9wTrYggoGxval3sBOh2Z5ofmmWJyq+bXmYOfg6LEe\n"
    "QkEzCzc9zolwFcq1JKjPa7XSQCGYzyI0zzvFIoTgxQ6KfF2I5DUkzps+GlQebtuy\n"
    "h6f88/qBVRRiClmpIgUxPoLW7ttXNLwzldMXG+gnoot7TiYaelpkttGsN/H9oPM4\n"
    "7HLwEXWdyzRSjeZ2axfG34arJ45JK3VmgRAhpuo+9K4l/3wV3s6MJT/KYnAK9y8J\n"
    "ZgfIPxz88NtFMN9iiMG1D53Dn0reWVlHxYciNuaCp+0KueIHoI17eko8cdLiA6Ef\n"
    "MgfdG+RCzgwARWGAtQsgWSl4vflVy2PFPEz0tv/bal8xa5meLMFrUKTX5hgUvYU/\n"
    "Z6tGn6D/Qqc6f1zLXbBwHSs09dR2CQzreExZBfMzQsNhFRAbd03OIozUhfJFfbdT\n"
    "6u9AWpQKXCBfTkBdYiJ23//OYb2MI3jSNwLgjt7RETeJ9r/tSQdirpLsQBqvFAnZ\n"
    "0E6yove+7u7Y/9waLd64NnHi/Hm3lCXRSHNboTXns5lndcEZOitHTtNCjv0xyBZm\n"
    "2tIMPNuzjsmhDYAPexZ3FL//2wmUspO8IFgV6dtxQ/PeEMMA3KgqlbbC1j+Qa3bb\n"
    "bP6MvPJwNQzcmRk13NfIRmPVNnGuV/u3gm3c\n"
    "-----END CERTIFICATE-----\n"
    // --- 6. Google Trust Services GTS Root R4, self-signed (ECDSA P-384) ---
    "-----BEGIN CERTIFICATE-----\n"
    "MIICCTCCAY6gAwIBAgINAgPlwGjvYxqccpBQUjAKBggqhkjOPQQDAzBHMQswCQYD\n"
    "VQQGEwJVUzEiMCAGA1UEChMZR29vZ2xlIFRydXN0IFNlcnZpY2VzIExMQzEUMBIG\n"
    "A1UEAxMLR1RTIFJvb3QgUjQwHhcNMTYwNjIyMDAwMDAwWhcNMzYwNjIyMDAwMDAw\n"
    "WjBHMQswCQYDVQQGEwJVUzEiMCAGA1UEChMZR29vZ2xlIFRydXN0IFNlcnZpY2Vz\n"
    "IExMQzEUMBIGA1UEAxMLR1RTIFJvb3QgUjQwdjAQBgcqhkjOPQIBBgUrgQQAIgNi\n"
    "AATzdHOnaItgrkO4NcWBMHtLSZ37wWHO5t5GvWvVYRg1rkDdc/eJkTBa6zzuhXyi\n"
    "QHY7qca4R9gq55KRanPpsXI5nymfopjTX15YhmUPoYRlBtHci8nHc8iMai/lxKvR\n"
    "HYqjQjBAMA4GA1UdDwEB/wQEAwIBhjAPBgNVHRMBAf8EBTADAQH/MB0GA1UdDgQW\n"
    "BBSATNbrdP9JNqPV2Py1PsVq8JQdjDAKBggqhkjOPQQDAwNpADBmAjEA6ED/g94D\n"
    "9J+uHXqnLrmvT/aDHQ4thQEd0dlq7A/Cr8deVl5c1RxYIigL9zC2L7F8AjEA8GE8\n"
    "p/SgguMh1YQdc4acLa/KNJvxn7kjNuK8YAOdgLOaVsjh4rsUecrNIdSUtUlD\n"
    "-----END CERTIFICATE-----\n"
    // --- 7. Google Trust Services WR3, signed by GTS Root R1 ---
    "-----BEGIN CERTIFICATE-----\n"
    "MIIFCzCCAvOgAwIBAgIQf/AFqRVo1jq8IoYWhKpLWjANBgkqhkiG9w0BAQsFADBH\n"
    "MQswCQYDVQQGEwJVUzEiMCAGA1UEChMZR29vZ2xlIFRydXN0IFNlcnZpY2VzIExM\n"
    "QzEUMBIGA1UEAxMLR1RTIFJvb3QgUjEwHhcNMjMxMjEzMDkwMDAwWhcNMjkwMjIw\n"
    "MTQwMDAwWjA7MQswCQYDVQQGEwJVUzEeMBwGA1UEChMVR29vZ2xlIFRydXN0IFNl\n"
    "cnZpY2VzMQwwCgYDVQQDEwNXUjMwggEiMA0GCSqGSIb3DQEBAQUAA4IBDwAwggEK\n"
    "AoIBAQCPNHWHr4RyFI0HEJFvA6zx1Ag1mhnymxiJNGyYj3rU3eoF6N4bfIxUErp5\n"
    "ivsYDQ18nPO9OOSoXsYzy0aJb0ag6TdjjdzM1ZdOMq17HSMFufV7SUOY0LxXx1N4\n"
    "GLHtp1SyfIa+8FRFvIe6mVkd9LjbAPuBT0YrYl6xOqUqFyOsor7FjuVe/XEefaS0\n"
    "I30EUrI00t+ZrIfGTFlf+OZPjnWSwrIwRpLQtg3H5Iln/z9UlCdl4wHISiyEL2Vf\n"
    "za1c/aatQVvcTD8XlpF9qdg8Uyoc0ObUd+ZDSsK3+Eiiza1jtSVrlnIdgUVvhmnE\n"
    "5OZ4TDHmoX+nAXMKh++HiXLM08WNAgMBAAGjgf4wgfswDgYDVR0PAQH/BAQDAgGG\n"
    "MB0GA1UdJQQWMBQGCCsGAQUFBwMBBggrBgEFBQcDAjASBgNVHRMBAf8ECDAGAQH/\n"
    "AgEAMB0GA1UdDgQWBBTHgfX9jojZADxNY6JQMSSgziP+IzAfBgNVHSMEGDAWgBTk\n"
    "rysmcRorSCeFL1JmLO/wiRNxPjA0BggrBgEFBQcBAQQoMCYwJAYIKwYBBQUHMAKG\n"
    "GGh0dHA6Ly9pLnBraS5nb29nL3IxLmNydDArBgNVHR8EJDAiMCCgHqAchhpodHRw\n"
    "Oi8vYy5wa2kuZ29vZy9yL3IxLmNybDATBgNVHSAEDDAKMAgGBmeBDAECATANBgkq\n"
    "hkiG9w0BAQsFAAOCAgEAnI1DlJQzSKcWbyXXrJSsgKMo6KG74TMqhsuTg67a0FX0\n"
    "2752+eiJb5YsOJc8DVOHalwpOvbuPdl5BuAEgIK4Va7l9j3J9M1/EjeWjGTM3Ros\n"
    "zmBJGu82oz6EWi5q75xeF+onJmh2Hm98a/yJAI/mODXq5LofYcQ9AffKP9ZMZu+Y\n"
    "wW+/qHqyX2JhaOntvl7i8S+l1Y8CcKEqM1NFK4s4EBYPRFjZDawOHX7fSchbSMVP\n"
    "n5Nu04lrU6xufuZqRosEQw2o0UAyzDoyA52NXzJTWr1G2FVg/0A9hdrQ/6fe9G31\n"
    "67zKxNqXErs6MpHttEouGbpm2ftzrmcvruYxTfxc4G2GwBi3LFLozNpy042gDfXB\n"
    "zDyn1staWsy7+QnzMlR59Fz6jBOk5R4LT+ma0+KjnfRhMh5T2ucm69HkvNQtDZlV\n"
    "a1tLUlzs0zLEdQSehTCjZ6SYsGt2bMVK6dvtxzcyCP0QDUFnNXCwgw12+mGSkAuj\n"
    "4ORi8kMRpnL8UEjkNbdw9KL1eYbEC3D0GPue2Yk2AGhxkmcdm1BoOp05kYw/Nnqg\n"
    "h7QV8DKyBTUHbjH0pXlLiOsSOY+CLh1eTM+Do6rSjqGnDQeUXylZmPCmuveaw38I\n"
    "VnBaa6Eiz6pngZ1u6OeO/1UzfhmyTm0n0G+9JZ3KS2Mq08isNgXHLnhlHJaphpE=\n"
    "-----END CERTIFICATE-----\n"
    // --- 8. Amazon Root CA 1, self-signed ---
    "-----BEGIN CERTIFICATE-----\n"
    "MIIDQTCCAimgAwIBAgITBmyfz5m/jAo54vB4ikPmljZbyjANBgkqhkiG9w0BAQsF\n"
    "ADA5MQswCQYDVQQGEwJVUzEPMA0GA1UEChMGQW1hem9uMRkwFwYDVQQDExBBbWF6\n"
    "b24gUm9vdCBDQSAxMB4XDTE1MDUyNjAwMDAwMFoXDTM4MDExNzAwMDAwMFowOTEL\n"
    "MAkGA1UEBhMCVVMxDzANBgNVBAoTBkFtYXpvbjEZMBcGA1UEAxMQQW1hem9uIFJv\n"
    "b3QgQ0EgMTCCASIwDQYJKoZIhvcNAQEBBQADggEPADCCAQoCggEBALJ4gHHKeNXj\n"
    "ca9HgFB0fW7Y14h29Jlo91ghYPl0hAEvrAIthtOgQ3pOsqTQNroBvo3bSMgHFzZM\n"
    "9O6II8c+6zf1tRn4SWiw3te5djgdYZ6k/oI2peVKVuRF4fn9tBb6dNqcmzU5L/qw\n"
    "IFAGbHrQgLKm+a/sRxmPUDgH3KKHOVj4utWp+UhnMJbulHheb4mjUcAwhmahRWa6\n"
    "VOujw5H5SNz/0egwLX0tdHA114gk957EWW67c4cX8jJGKLhD+rcdqsq08p8kDi1L\n"
    "93FcXmn/6pUCyziKrlA4b9v7LWIbxcceVOF34GfID5yHI9Y/QCB/IIDEgEw+OyQm\n"
    "jgSubJrIqg0CAwEAAaNCMEAwDwYDVR0TAQH/BAUwAwEB/zAOBgNVHQ8BAf8EBAMC\n"
    "AYYwHQYDVR0OBBYEFIQYzIU07LwMlJQuCFmcx7IQTgoIMA0GCSqGSIb3DQEBCwUA\n"
    "A4IBAQCY8jdaQZChGsV2USggNiMOruYou6r4lK5IpDB/G/wkjUu0yKGX9rbxenDI\n"
    "U5PMCCjjmCXPI6T53iHTfIUJrU6adTrCC2qJeHZERxhlbI1Bjjt/msv0tadQ1wUs\n"
    "N+gDS63pYaACbvXy8MWy7Vu33PqUXHeeE6V/Uq2V8viTO96LXFvKWlJbYK8U90vv\n"
    "o/ufQJVtMVT8QtPHRh8jrdkPSHCa2XV4cdFyQzR1bldZwgJcJmApzyMZFo6IQ6XU\n"
    "5MsI+yMRQ+hDKXJioaldXgjUkK642M4UwtBV8ob2xJNDd2ZhwLnoQdeXeGADbkpy\n"
    "rqXRfboQnoZsG4q5WTP468SQvvG5\n"
    "-----END CERTIFICATE-----\n"
    // --- 9. Sectigo Public Server Authentication Root E46, self-signed (ECDSA P-384) ---
    "-----BEGIN CERTIFICATE-----\n"
    "MIICOjCCAcGgAwIBAgIQQvLM2htpN0RfFf51KBC49DAKBggqhkjOPQQDAzBfMQsw\n"
    "CQYDVQQGEwJHQjEYMBYGA1UEChMPU2VjdGlnbyBMaW1pdGVkMTYwNAYDVQQDEy1T\n"
    "ZWN0aWdvIFB1YmxpYyBTZXJ2ZXIgQXV0aGVudGljYXRpb24gUm9vdCBFNDYwHhcN\n"
    "MjEwMzIyMDAwMDAwWhcNNDYwMzIxMjM1OTU5WjBfMQswCQYDVQQGEwJHQjEYMBYG\n"
    "A1UEChMPU2VjdGlnbyBMaW1pdGVkMTYwNAYDVQQDEy1TZWN0aWdvIFB1YmxpYyBT\n"
    "ZXJ2ZXIgQXV0aGVudGljYXRpb24gUm9vdCBFNDYwdjAQBgcqhkjOPQIBBgUrgQQA\n"
    "IgNiAAR2+pmpbiDt+dd34wc7qNs9Xzjoq1WmVk/WSOrsfy2qw7LFeeyZYX8QeccC\n"
    "WvkEN/U0NSt3zn8gj1KjAIns1aeibVvjS5KToID1AZTc8GgHHs3u/iVStSBDHBv+\n"
    "6xnOQ6OjQjBAMB0GA1UdDgQWBBTRItpMWfFLXyY4qp3W7usNw/upYTAOBgNVHQ8B\n"
    "Af8EBAMCAYYwDwYDVR0TAQH/BAUwAwEB/zAKBggqhkjOPQQDAwNnADBkAjAn7qRa\n"
    "qCG76UeXlImldCBteU/IvZNeWBj7LRoAasm4PdCkT0RHlAFWovgzJQxC36oCMB3q\n"
    "4S6ILuH5px0CMk7yn2xVdOOurvulGu7t0vzCAxHrRVxgED1cf5kDW21USAGKcw==\n"
    "-----END CERTIFICATE-----\n"
    ;
