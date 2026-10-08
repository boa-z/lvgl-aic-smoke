#include "update/meter_package.h"
#include <stdio.h>
#include <string.h>
enum
{
    HEADER,
    NAME,
    NAME_PAD,
    BODY,
    BODY_PAD,
    TAIL
};
static meter_package_result_t reject(meter_package_guard_t *g, meter_package_result_t result)
{
    g->result = result;
    return result;
}
static bool identifier(const char *text, size_t capacity)
{
    if (!text || !text[0])
        return false;
    size_t i;
    for (i = 0; i < capacity && text[i]; ++i)
    {
        char c = text[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' ||
              c == '-' || c == '_' || c == '+'))
            return false;
    }
    return i < capacity;
}
bool meter_package_init(meter_package_guard_t *g, const meter_package_policy_t *p)
{
    if (!g)
        return false;
    memset(g, 0, sizeof(*g));
    g->result = METER_PACKAGE_ARGUMENT;
    if (!p || !p->package_size || p->package_size % 512u || !p->candidate_capacity ||
        !identifier(p->os_file, sizeof(g->os_file)) || !identifier(p->version, sizeof(g->version)))
        return false;
    size_t n = strlen(p->os_file);
    if (n < 5u || strcmp(p->os_file + n - 4u, ".itb"))
        return false;
    /* identifier 已验证容量内存在终止符；复制长度包含终止符。 */
    memcpy(g->os_file, p->os_file, n + 1u);
    memcpy(g->version, p->version, strlen(p->version) + 1u);
    g->package_size = p->package_size;
    g->capacity = p->candidate_capacity;
    g->result = METER_PACKAGE_OK;
    return true;
}
static bool hexadecimal(const uint8_t *p, uint32_t *out)
{
    uint32_t value = 0;
    for (size_t i = 0; i < 8u; ++i)
    {
        uint8_t c = p[i];
        uint32_t digit;
        if (c >= '0' && c <= '9')
            digit = c - '0';
        else if (c >= 'a' && c <= 'f')
            digit = c - 'a' + 10u;
        else if (c >= 'A' && c <= 'F')
            digit = c - 'A' + 10u;
        else
            return false;
        value = (value << 4) | digit;
    }
    *out = value;
    return true;
}
static bool header(meter_package_guard_t *g)
{
    uint32_t field[13];
    if (memcmp(g->header, "070702", 6))
        return false;
    for (unsigned i = 0; i < 13u; ++i)
        if (!hexadecimal(g->header + 6u + i * 8u, &field[i]))
            return false;
    g->file_size = field[6];
    g->name_size = field[11];
    g->expected_checksum = field[12];
    g->checksum = 0;
    if (!g->name_size || g->name_size > sizeof(g->name))
        return false;
    if (g->member < 2u)
    {
        if ((field[1] & 0170000u) != 0100000u || field[4] != 1u)
            return false;
    }
    else if (g->member != 2u || field[1] != 0 || g->file_size || g->expected_checksum)
        return false;
    g->filled = 0;
    g->phase = NAME;
    return true;
}
static bool member(meter_package_guard_t *g)
{
    const char *expected = g->member == 0u ? "ota_info.bin" : (g->member == 1u ? g->os_file : "TRAILER!!!");
    size_t n = strlen(expected) + 1u;
    if (g->name_size != n || memcmp(g->name, expected, n))
        return false;
    if (g->member == 0u && g->file_size != sizeof(g->metadata))
        return false;
    if (g->member == 1u)
    {
        if (!g->file_size || g->file_size > g->capacity || g->file_size > g->capacity - (g->capacity % 4096u))
        {
            reject(g, METER_PACKAGE_CAPACITY);
            return false;
        }
        g->os_size = g->file_size;
    }
    g->padding = (4u - ((110u + g->name_size) % 4u)) % 4u;
    g->remaining = g->file_size;
    g->filled = 0;
    g->phase = NAME_PAD;
    return true;
}
static bool token(const uint8_t *data, size_t *offset, const char *expected)
{
    size_t n = strlen(expected) + 1u;
    if (n > 512u - *offset || memcmp(data + *offset, expected, n))
        return false;
    *offset += n;
    return true;
}
static bool metadata(meter_package_guard_t *g)
{
    /* SDK 将前四字节视为 ENV 前缀；此处由 CPIO checksum 与外层 SHA256 保护其内容。 */
    size_t offset = 4;
    char text[80];
    if (!token(g->metadata, &offset, "[image]"))
        return false;
    /* SDK 打包器允许十进制长度后的空格；限制到 uint32 范围，拒绝 atoi 的歧义。 */
    if (offset + 8u >= 512u || memcmp(g->metadata + offset, "size = \"", 8))
        return false;
    offset += 8u;
    uint32_t length = 0;
    unsigned digits = 0;
    while (offset < 512u && g->metadata[offset] >= '0' && g->metadata[offset] <= '9')
    {
        uint32_t digit = g->metadata[offset++] - '0';
        if (++digits > 10u || length > (UINT32_MAX - digit) / 10u)
            return false;
        length = length * 10u + digit;
    }
    while (offset < 512u && g->metadata[offset] == ' ')
        ++offset;
    if (!digits || length != g->package_size || !token(g->metadata, &offset, "\";"))
        return false;
    int n = snprintf(text, sizeof(text), "version = \"%s\";", g->version);
    if (n <= 0 || (size_t)n >= sizeof(text) || !token(g->metadata, &offset, text) ||
        !token(g->metadata, &offset, "[file]") || !token(g->metadata, &offset, "ota_info.bin:file;"))
        return false;
    n = snprintf(text, sizeof(text), "%s:os;", g->os_file);
    if (n <= 0 || (size_t)n >= sizeof(text) || !token(g->metadata, &offset, text) || offset >= 512u ||
        g->metadata[offset++] != 0)
        return false;
    while (offset < 512u)
    {
        uint8_t c = g->metadata[offset++];
        if (c != 0 && c != 0xffu)
            return false;
    }
    return true;
}
static bool advance(meter_package_guard_t *g)
{
    for (;;)
    {
        if (g->phase == NAME_PAD && !g->padding)
        {
            if (g->member == 2u)
            {
                g->trailer_end = g->received;
                if (g->package_size - g->received >= 512u)
                    return false;
                g->phase = TAIL;
            }
            else
            {
                if (g->member == 1u)
                    g->os_offset = g->received;
                g->phase = BODY;
            }
        }
        else if (g->phase == BODY && !g->remaining)
        {
            if (g->checksum != g->expected_checksum)
            {
                reject(g, METER_PACKAGE_CHECKSUM);
                return false;
            }
            if (!g->member && !metadata(g))
            {
                reject(g, METER_PACKAGE_METADATA);
                return false;
            }
            g->padding = (4u - (g->file_size % 4u)) % 4u;
            g->phase = BODY_PAD;
        }
        else if (g->phase == BODY_PAD && !g->padding)
        {
            ++g->member;
            g->filled = 0;
            g->phase = HEADER;
        }
        else
            return true;
    }
}
meter_package_result_t meter_package_feed(meter_package_guard_t *g, const uint8_t *data, size_t size)
{
    if (!g)
        return METER_PACKAGE_ARGUMENT;
    if (g->result != METER_PACKAGE_OK)
        return g->result;
    if ((!data && size) || size > g->package_size - g->received)
        return reject(g, METER_PACKAGE_ARGUMENT);
    for (size_t i = 0; i < size; ++i)
    {
        uint8_t c = data[i];
        ++g->received;
        switch (g->phase)
        {
        case HEADER:
            g->header[g->filled++] = c;
            if (g->filled == sizeof(g->header) && !header(g))
                return reject(g, METER_PACKAGE_HEADER);
            break;
        case NAME:
            g->name[g->filled++] = c;
            if (g->filled == g->name_size && !member(g))
                return reject(g, g->result == METER_PACKAGE_OK ? METER_PACKAGE_MEMBER : g->result);
            break;
        case NAME_PAD:
        case BODY_PAD:
            if (c)
                return reject(g, METER_PACKAGE_PADDING);
            --g->padding;
            break;
        case BODY:
            if (!g->member)
                g->metadata[g->filled++] = c;
            /* CRC CPIO 使用 uint32 加和，标准定义的模 2^32 回绕。 */
            g->checksum += c;
            --g->remaining;
            break;
        case TAIL:
            if (c)
                return reject(g, METER_PACKAGE_PADDING);
            break;
        default:
            return reject(g, METER_PACKAGE_HEADER);
        }
        if (!advance(g))
            return reject(g, g->result == METER_PACKAGE_OK ? METER_PACKAGE_PADDING : g->result);
    }
    return g->result;
}
meter_package_result_t meter_package_finish(meter_package_guard_t *g)
{
    if (!g)
        return METER_PACKAGE_ARGUMENT;
    if (g->result != METER_PACKAGE_OK)
        return g->result;
    if (g->received != g->package_size || g->phase != TAIL || !g->trailer_end)
        return reject(g, METER_PACKAGE_TRUNCATED);
    return METER_PACKAGE_OK;
}
const char *meter_package_result_name(meter_package_result_t result)
{
    static const char *const names[] = {"OK",       "ARGUMENT", "HEADER",  "MEMBER",   "METADATA",
                                        "CAPACITY", "CHECKSUM", "PADDING", "TRUNCATED"};
    return (unsigned)result < sizeof(names) / sizeof(names[0]) ? names[result] : "UNKNOWN";
}
