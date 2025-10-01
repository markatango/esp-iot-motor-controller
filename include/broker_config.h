#ifndef BROKER_CONFIG_H
#define BROKER_CONFIG_H

#include <Arduino.h>     // For Serial
#include <string.h>      // For strcmp

// Arduino-friendly broker configuration
// No STL required - works with ESP32/Arduino

// Structure to hold broker configuration
struct BrokerConfig {
    const char* name;
    const char* url;
    const char* server_cert;
    const char* username;
    const char* password;
    int port_plain;
    int port_ssl;
};

// Certificate for data-dancer.com (your local broker)
const char* dataDancerCert = R"EOF(-----BEGIN CERTIFICATE-----
MIID2zCCAsOgAwIBAgIUeIGVaxph730/cmJzpfuGvLAeD4EwDQYJKoZIhvcNAQEL
BQAwezELMAkGA1UEBhMCVVMxCzAJBgNVBAgMAkNBMRYwFAYDVQQHDA1TYW4gRnJh
bmNpc2NvMRQwEgYDVQQKDAtNUVRUIEJyb2tlcjEXMBUGA1UECwwOSW9UIERlcGFy
dG1lbnQxGDAWBgNVBAMMD2RhdGEtZGFuY2VyLmNvbTAeFw0yNTA5MjcwMzU3MjFa
Fw0yNjA5MjcwMzU3MjFaMHsxCzAJBgNVBAYTAlVTMQswCQYDVQQIDAJDQTEWMBQG
A1UEBwwNU2FuIEZyYW5jaXNjbzEUMBIGA1UECgwLTVFUVCBCcm9rZXIxFzAVBgNV
BAsMDklvVCBEZXBhcnRtZW50MRgwFgYDVQQDDA9kYXRhLWRhbmNlci5jb20wggEi
MA0GCSqGSIb3DQEBAQUAA4IBDwAwggEKAoIBAQDDcWyHLqARs3EhylAH8U3rHXW/
VIbbff1Nz3XxIjhBo/g5llq6xF1YGCpafbhZn/J2K6sYkE+2OyQgCxt9/GKHwjBg
nFVJHCDtcsnRJ8Z5z1iFVn3Ky0Ct22KZBjNJ//RGugUNMRiFc8zTmwSgUYB6RSGy
IRzcbUUdsIhTWxfVx2GCuRFEOBxU76kk0s1c5wgMIpnl35bl/Y4POslIXFlLERuF
1zfplOEdimuu4XMVwNBeWdOpIZOmPQ+e5dJjLp8w0vsBlhRQTlaSWIYj17nXmwjV
7xS836GdN/eAPPaZYfXEfQ2OngBUGkgQY6RUJlv0U6gDUKe0yI+zJi9NYtPfAgMB
AAGjVzBVMCsGA1UdEQQkMCKCD2RhdGEtZGFuY2VyLmNvbYIJbG9jYWxob3N0hwR/
AAABMA4GA1UdDwEB/wQEAwIFoDAWBgNVHSUBAf8EDDAKBggrBgEFBQcDATANBgkq
hkiG9w0BAQsFAAOCAQEAvxOeQfXcFmNC5BsmmuDA7R/NsEelPomsNFsylSYTKBPN
0X3B/Z/s9a7PNXAjbEkKQz+d8fjdg/GK4X3qYb15LoMcSGDRe1QGgs7XBzvhGe3y
qFUcUkr60uUMGnBt1+i6tc17G7VocYdbmEt2duJOyJXrG9zMTTs7sFfbYPa4iszn
Viww3TL8dc0+CehCcAlCq+gaUuBLzvtDrHDINMmAybTzglVYrJ22Rvtcv/hxcKTe
ZuRIIBEeF7Cn31Vp//MXYnbQN208e03gsWnZmsfzB97j6txBhds9kTwCi5+ZtwD+
LhCS9XMyIm3siiIes8xJL8tdarLuSN4HZSekQ41lSg==
-----END CERTIFICATE-----
)EOF";

// Certificate for broker.emqx.io
const char* emqxCert = R"EOF(-----BEGIN CERTIFICATE-----
MIIDrzCCApegAwIBAgIQCDvgVpBCRrGhdWrJWZHHSjANBgkqhkiG9w0BAQUFADBh
MQswCQYDVQQGEwJVUzEVMBMGA1UEChMMRGlnaUNlcnQgSW5jMRkwFwYDVQQLExB3
d3cuZGlnaWNlcnQuY29tMSAwHgYDVQQDExdEaWdpQ2VydCBHbG9iYWwgUm9vdCBD
QTAeFw0wNjExMTAwMDAwMDBaFw0zMTExMTAwMDAwMDBaMGExCzAJBgNVBAYTAlVT
MRUwEwYDVQQKEwxEaWdpQ2VydCBJbmMxGTAXBgNVBAsTEHd3dy5kaWdpY2VydC5j
b20xIDAeBgNVBAMTF0RpZ2lDZXJ0IEdsb2JhbCBSb290IENBMIIBIjANBgkqhkiG
9w0BAQEFAAOCAQ8AMIIBCgKCAQEA4jvhEXLeqKTTo1eqUKKPC3eQyaKl7hLOllsB
CSDMAZOnTjC3U/dDxGkAV53ijSLdhwZAAIEJzs4bg7/fzTtxRuLWZscFs3YnFo97
nh6Vfe63SKMI2tavegw5BmV/Sl0fvBf4q77uKNd0f3p4mVmFaG5cIzJLv07A6Fpt
43C/dxC//AH2hdmoRBBYMql1GNXRor5H4idq9Joz+EkIYIvUX7Q6hL+hqkpMfT7P
T19sdl6gSzeRntwi5m3OFBqOasv+zbMUZBfHWymeMr/y7vrTC0LUq7dBMtoM1O/4
gdW7jVg/tRvoSSiicNoxBN33shbyTApOB6jtSj1etX+jkMOvJwIDAQABo2MwYTAO
BgNVHQ8BAf8EBAMCAYYwDwYDVR0TAQH/BAUwAwEB/zAdBgNVHQ4EFgQUA95QNVbR
TLtm8KPiGxvDl7I90VUwHwYDVR0jBBgwFoAUA95QNVbRTLtm8KPiGxvDl7I90VUw
DQYJKoZIhvcNAQEFBQADggEBAMucN6pIExIK+t1EnE9SsPTfrgT1eXkIoyQY/Esr
hMAtudXH/vTBH1jLuG2cenTnmCmrEbXjcKChzUyImZOMkXDiqw8cvpOp/2PV5Adg
06O/nVsJ8dWO41P0jmP6P6fbtGbfYmbW0W5BjfIttep3Sp+dWOIrWcBAI+0tKIJF
PnlUkiaY4IBIqDfv8NZ5YBberOgOzW6sRBc4L0na4UU+Krk2U886UAb3LujEV0ls
YSEY1QSteDwsOoBrp+uvFRTp2InBuThs4pFsiv9kuXclVzDAGySj4dzp30d8tbQk
CAUw7C29C79Fv1C5qfPrmAESrciIxpg0X40KPMbp1ZWVbd4=
-----END CERTIFICATE-----
)EOF";


const char* localCert = R"EOF(-----BEGIN CERTIFICATE-----
MIID2DCCAsCgAwIBAgIUXfzn5h2tz3iEnng6BYmGxDsVq5swDQYJKoZIhvcNAQEL
BQAweDELMAkGA1UEBhMCVVMxCzAJBgNVBAgMAkNBMRYwFAYDVQQHDA1TYW4gRnJh
bmNpc2NvMRQwEgYDVQQKDAtNUVRUIEJyb2tlcjEXMBUGA1UECwwOSW9UIERlcGFy
dG1lbnQxFTATBgNVBAMMDDE5Mi4xNjguMS40MTAeFw0yNTA5MjYyMzE1NThaFw0y
NjA5MjYyMzE1NThaMHgxCzAJBgNVBAYTAlVTMQswCQYDVQQIDAJDQTEWMBQGA1UE
BwwNU2FuIEZyYW5jaXNjbzEUMBIGA1UECgwLTVFUVCBCcm9rZXIxFzAVBgNVBAsM
DklvVCBEZXBhcnRtZW50MRUwEwYDVQQDDAwxOTIuMTY4LjEuNDEwggEiMA0GCSqG
SIb3DQEBAQUAA4IBDwAwggEKAoIBAQCjmwQOWEMiQd1uNIgjSITQK11VCWPsi/Vn
j8MO29pZVFBIgZzTNOoA0f7dHI2RduyiKSCE3Jn5aWm1E2j+63hdjojrVJXiWAR8
mxhTdYxxZam5F4omBoiBv0CLDCbKqtTk8Ezis6zZxGLMz7gziAlqFr0A6ysWbnmW
a+uABmbfEQOCo4iNWO+icjdDl8bJSZ2GApM4FY3EDYtqkm0gHGJMrEGSP3TcjNJw
b+KwzfxR/LlFYmt5uB552TihpmXOFWrchcEB04tk0N0ysySQp79muwi6s8zrhvfE
MgoL3EohdYR4C+t+61LC4qzkSRrj4Vvrskks8jGWm0i/H9SnvRGhAgMBAAGjWjBY
MC4GA1UdEQQnMCWCDDE5Mi4xNjguMS40MYIJbG9jYWxob3N0hwR/AAABhwTAqAEp
MA4GA1UdDwEB/wQEAwIFoDAWBgNVHSUBAf8EDDAKBggrBgEFBQcDATANBgkqhkiG
9w0BAQsFAAOCAQEAVCCBJBR0uGQN5uM1hKdaw+ehHG4zzeN5cKWalx5nET++jIEY
tpSK9Bv9cKEXWLZry4HiX/dcxd5RVxAWuMa/cgx20T3Tp/ibpuX3aA0sbwhq5NUU
0M/uCI8nFy0Wgi+7+Rgy1l7kC2UO6CBTJMC2OkdqRonA/Qc0etqRYbmktgE9X2ug
sQR2Ei+qQa4XjNMJnG7OIPOEBHLyNRw2exxn3MHxNuWkw2NCe8yKapH8Kkti779S
3yanBrXQb67lOv6D1Mi2CIcHsU0FV8gEXXiEr40hAYcTd/H+7tRTupkVq3bXD57t
iQOsBYdCWzbZqVs93SsYpRvjtEz2PRV+YbxmAw==
-----END CERTIFICATE-----
)EOF";


// Array of broker configurations (dictionary)
const BrokerConfig BROKER_CONFIGS[] = {
    {
        "data-dancer.com",     // name
        "data-dancer.com",        // url (IP address)
        dataDancerCert,        // certificate
        "",                    // username (empty = no auth)
        "",                    // password
        1883,                  // plain MQTT port
        8883                   // SSL MQTT port
    },
    {
        "broker.emqx.io",      // name
        "broker.emqx.io",      // url
        emqxCert,              // certificate
        "emqx",                // username
        "public",              // password
        1883,                  // plain MQTT port
        8883                   // SSL MQTT port
    },
    {
        "192.168.1.41",      // name
        "192.168.1.41",      // url
        localCert,              // certificate
        "",                // username
        "",              // password
        1883,                  // plain MQTT port
        8883                   // SSL MQTT port
    }
    

    // Add more brokers here as needed
};

// Number of brokers in the array
const int NUM_BROKERS = sizeof(BROKER_CONFIGS) / sizeof(BROKER_CONFIGS[0]);

// Function to get broker config by name
const BrokerConfig* getBrokerConfig(const char* brokerName) {
    for (int i = 0; i < NUM_BROKERS; i++) {
        if (strcmp(BROKER_CONFIGS[i].name, brokerName) == 0) {
            return &BROKER_CONFIGS[i];
        }
    }
    return nullptr;  // Not found
}

// Function to list all available brokers
void listBrokers() {
    Serial.println("\n📋 Available Brokers:");
    Serial.println("=====================");
    
    for (int i = 0; i < NUM_BROKERS; i++) {
        Serial.printf("\n🔹 %s\n", BROKER_CONFIGS[i].name);
        Serial.printf("   URL: %s\n", BROKER_CONFIGS[i].url);
        Serial.printf("   Ports: %d (plain), %d (ssl)\n", 
                     BROKER_CONFIGS[i].port_plain, 
                     BROKER_CONFIGS[i].port_ssl);
        
        if (strlen(BROKER_CONFIGS[i].username) > 0) {
            Serial.printf("   Username: %s\n", BROKER_CONFIGS[i].username);
        } else {
            Serial.println("   Auth: None");
        }
    }
}

#endif // BROKER_CONFIG_H