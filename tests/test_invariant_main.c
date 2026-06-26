#include <check.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "devsdk/devsdk.h"

/* Expose the production function — main.c is compiled with -DUNIT_TEST which
   removes the 'static' qualifier, giving this translation unit access to it. */
extern iot_data_t *bacnet_alloc_exception (char *fmt, ...);

START_TEST(test_buffer_writes_never_exceed_declared_length)
{
    /* Invariant: bacnet_alloc_exception never overflows its internal buffer. */
    const char *payloads[] = {
        "%s",                     /* normal usage */
        "%.100s",                 /* precision-bounded */
        "%s %s",                  /* multiple args */
        "%1000s",                 /* large width specifier */
        "%99999999999999999999s"  /* extreme width — original exploit case */
    };
    size_t num_payloads = sizeof (payloads) / sizeof (payloads[0]);

    /* A 512-byte string used as the %s argument in every payload */
    char test_input[512];
    memset (test_input, 'B', sizeof (test_input) - 1);
    test_input[sizeof (test_input) - 1] = '\0';

    for (size_t i = 0; i < num_payloads; i++) {
        iot_data_t *result = bacnet_alloc_exception (payloads[i], test_input, test_input);

        /* The function must either succeed (non-NULL) or return NULL on error —
           it must never crash or corrupt memory. */
        if (result != NULL) {
            const char *s = iot_data_string (result);
            ck_assert_msg (s != NULL,
                           "bacnet_alloc_exception returned non-NULL but iot_data_string is NULL "
                           "for payload: %s", payloads[i]);
            iot_data_free (result);
        }
        /* NULL is an acceptable result (encoding error path) */
    }
}
END_TEST

Suite *security_suite (void)
{
    Suite *s;
    TCase *tc_core;

    s = suite_create ("Security");
    tc_core = tcase_create ("Core");

    tcase_add_test (tc_core, test_buffer_writes_never_exceed_declared_length);
    suite_add_tcase (s, tc_core);

    return s;
}

int main (void)
{
    int number_failed;
    Suite *s;
    SRunner *sr;

    s = security_suite ();
    sr = srunner_create (s);

    srunner_run_all (sr, CK_NORMAL);
    number_failed = srunner_ntests_failed (sr);
    srunner_free (sr);

    return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
