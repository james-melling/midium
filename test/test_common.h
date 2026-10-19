/* Shared check macros for the host unit tests. */
#ifndef MIDIUM_TEST_COMMON_H
#define MIDIUM_TEST_COMMON_H

#include <stdio.h>
#include <string.h>

extern int g_checks, g_fail;

#define CHECK(cond) do { g_checks++; if (!(cond)) { g_fail++; \
    printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_STR(a, b) do { g_checks++; if (strcmp((a), (b)) != 0) { g_fail++; \
    printf("  FAIL %s:%d: \"%s\" != \"%s\"\n", __FILE__, __LINE__, (a), (b)); } } while (0)

void test_msc(void);
void test_m2(void);
void test_power(void);

#endif
