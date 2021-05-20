#ifndef ARMOR_552ec660df954a0ea7fc6e2fa9bf4bac_H
#define ARMOR_552ec660df954a0ea7fc6e2fa9bf4bac_H

#ifdef __cplusplus
extern "C" {
#endif

int armor(char *dst, const char *dst_end,
    const char *src, const char *end);
int armor_line_break(char *dst, const char *dst_end,
    const char *src, const char *end, int line_width);
int unarmor(char *dst, const char *dst_end,
    const char *src, const char *end);
#ifdef __cplusplus
}
#endif

#endif // ARMOR_552ec660df954a0ea7fc6e2fa9bf4bac_H
