#ifndef UNIFROG_HOST_TEST_H
#define UNIFROG_HOST_TEST_H

#include <stdio.h>
#include <string.h>

static unsigned test_failures;

#define TEST_CHECK(condition) \
   do { \
      if (!(condition)) { \
         fprintf(stderr, "%s:%d: check failed: %s\n", \
            __FILE__, __LINE__, #condition); \
         test_failures++; \
      } \
   } while (0)

#define TEST_EQ_INT(expected, actual) \
   do { \
      long test_expected = (long)(expected); \
      long test_actual = (long)(actual); \
      if (test_expected != test_actual) { \
         fprintf(stderr, "%s:%d: expected %ld, got %ld\n", \
            __FILE__, __LINE__, test_expected, test_actual); \
         test_failures++; \
      } \
   } while (0)

#define TEST_EQ_STR(expected, actual) \
   do { \
      const char *test_expected = (expected); \
      const char *test_actual = (actual); \
      if (!test_expected || !test_actual || \
          strcmp(test_expected, test_actual) != 0) { \
         fprintf(stderr, "%s:%d: expected \"%s\", got \"%s\"\n", \
            __FILE__, __LINE__, test_expected ? test_expected : "(null)", \
            test_actual ? test_actual : "(null)"); \
         test_failures++; \
      } \
   } while (0)

static int test_finish(const char *suite)
{
   if (test_failures) {
      fprintf(stderr, "FAIL %s (%u failures)\n", suite, test_failures);
      return 1;
   }
   printf("OK %s\n", suite);
   return 0;
}

#endif
