#ifndef METER_SHA_CONFIG_H
#define METER_SHA_CONFIG_H
/* Only the SDK mbedtls SHA256 is used; no key handling or TLS. */
#define MBEDTLS_CONFIG_FILE "update/meter_sha_config.h"
#define MBEDTLS_SHA256_C
#endif
