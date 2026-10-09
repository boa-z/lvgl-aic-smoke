#include "update/meter_package.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif
static bool number(const char *text, uint32_t *out)
{
    char *end;
    if (!text || !text[0])
        return false;
    for (const char *p = text; *p; ++p)
        if (*p < '0' || *p > '9')
            return false;
    errno = 0;
    unsigned long value = strtoul(text, &end, 10);
    if (errno || *end || !value || value > UINT32_MAX)
        return false;
    *out = (uint32_t)value;
    return true;
}
int main(int argc, char **argv)
{
    meter_package_guard_t guard;
    meter_package_policy_t policy = {0};
    uint32_t chunk = 512;
    if ((argc != 5 && argc != 6) || !number(argv[1], &policy.package_size) ||
        !number(argv[2], &policy.candidate_capacity) ||
        (argc == 6 && (!number(argv[5], &chunk) || chunk > 512u)))
    {
        fputs("usage: meter-ota-inspect size capacity os-file version [chunk] < ota.cpio\n", stderr);
        return 2;
    }
    policy.os_file = argv[3];
    policy.version = argv[4];
#ifdef _WIN32
    if (_setmode(_fileno(stdin), _O_BINARY) == -1)
        return 2;
#endif
    if (!meter_package_init(&guard, &policy))
        return 2;
    uint8_t data[512];
    size_t count;
    while ((count = fread(data, 1, chunk, stdin)) != 0)
        if (meter_package_feed(&guard, data, count) != METER_PACKAGE_OK)
            break;
    if (ferror(stdin))
        return 2;
    meter_package_result_t result = meter_package_finish(&guard);
    printf("{\"result\":\"%s\",\"received\":%u,\"os_size\":%u,\"guard_bytes\":%u}\n",
           meter_package_result_name(result), (unsigned)guard.received, (unsigned)guard.os_size,
           (unsigned)sizeof(guard));
    return result == METER_PACKAGE_OK ? 0 : 1;
}
