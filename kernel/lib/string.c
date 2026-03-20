#include "string.h"

void *memset(void *dst, int c, size_t n){
    char *d = (char *)dst;
    for (size_t i = 0; i < n; i++){
        d[i] = (char)c;
    }
    return dst;
}
void *memcpy(void *dst, const void *src, size_t n){
    char *d = (char *)dst;
    const char *s = (const char *)src;
    for (size_t i = 0; i < n; i++){
        d[i] = s[i];
    }
    return dst;
}
int memcmp(const void *s1, const void *s2, size_t n){
    const unsigned char *s = (const unsigned char *)s1;
    const unsigned char *t = (const unsigned char *)s2;
    for (size_t i = 0; i < n; i++){
        if (s[i] != t[i]) return s[i] - t[i];
    }
    return 0;
}
int strcmp(const char *s1, const char *s2){
    for (size_t i = 0; ; i++) {
        if (s1[i] != s2[i]) return (unsigned char)s1[i] - (unsigned char)s2[i];
        if (s1[i] == '\0') return 0;
    }
}
int strncmp(const char *s1, const char *s2, size_t n){
    for (size_t i = 0; i < n; i++){
        if(s1[i] != s2[i]) return (unsigned char)s1[i] - (unsigned char)s2[i];
        if(s1[i] == '\0') return 0;
    }
    return 0;
}
char *strncpy(char *dst, const char *src, size_t n){
    size_t i = 0;
    for( ; i < n && src[i] != '\0'; i++){
        dst[i] = src[i];
    }
    for( ; i < n; i++) {
        dst[i] = '\0';
    }
    return dst;
}
size_t strlen(const char *s){
    size_t i = 0;
    for(; s[i] != '\0'; i++);
    return i;
}