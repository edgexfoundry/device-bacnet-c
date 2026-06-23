#include <check.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

/* Forward declaration of the vulnerable function from main.c */
extern void vulnerable_format_function(char *str, const char *fmt, ...);

START_TEST(test_buffer_reads_never_exceed_declared_length)
{
    /* Invariant: Buffer reads never exceed the declared length */
    const char *payloads[] = {
        "%s",                     /* Valid input - normal usage */
        "%1000s",                 /* Boundary case - large width specifier */
        "%99999999999999999999s", /* Exploit case - excessive width causing overflow */
        "%.1000s",                /* Precision overflow case */
        "%1000s%1000s"            /* Multiple large specifiers */
    };
    int num_payloads = sizeof(payloads) / sizeof(payloads[0]);
    
    for (int i = 0; i < num_payloads; i++) {
        char buffer[256];
        memset(buffer, 'A', sizeof(buffer));
        buffer[sizeof(buffer)-1] = '\0';
        
        char test_input[512];
        memset(test_input, 'B', sizeof(test_input)-1);
        test_input[sizeof(test_input)-1] = '\0';
        
        /* Call the actual vulnerable function */
        vulnerable_format_function(buffer, payloads[i], test_input);
        
        /* Check that buffer wasn't overflowed by verifying null termination */
        ck_assert_msg(buffer[sizeof(buffer)-1] == '\0',
                     "Buffer overflow detected with payload: %s", payloads[i]);
        
        /* Additional safety check - ensure no writes beyond buffer */
        char sentinel[16];
        memset(sentinel, 'S', sizeof(sentinel));
        
        /* Re-initialize buffer with sentinel after */
        memset(buffer, 'A', sizeof(buffer));
        buffer[sizeof(buffer)-1] = '\0';
        
        vulnerable_format_function(buffer, payloads[i], test_input);
        
        /* Verify sentinel unchanged (crude but effective overflow detection) */
        for (int j = 0; j < sizeof(sentinel); j++) {
            ck_assert_msg(sentinel[j] == 'S',
                         "Memory corruption detected beyond buffer with payload: %s", 
                         payloads[i]);
        }
    }
}
END_TEST

Suite *security_suite(void)
{
    Suite *s;
    TCase *tc_core;

    s = suite_create("Security");
    tc_core = tcase_create("Core");

    tcase_add_test(tc_core, test_buffer_reads_never_exceed_declared_length);
    suite_add_tcase(s, tc_core);

    return s;
}

int main(void)
{
    int number_failed;
    Suite *s;
    SRunner *sr;

    s = security_suite();
    sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);

    return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}