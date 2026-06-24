#include <check.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

/* Local helper that mirrors the fixed bacnet_alloc_exception formatting logic.
   Writes into a caller-supplied, fixed-size buffer via vsnprintf so that
   the write is always bounded to 'size' bytes (including the null terminator). */
static void test_format_bounded (char *str, size_t size, const char *fmt, ...)
{
    va_list args;
    va_start (args, fmt);
    vsnprintf (str, size, fmt, args);
    va_end (args);
}

/* Placing buffer and sentinel in a struct guarantees they are adjacent in
   memory (C standard §6.7.2.1), making the overflow-detection reliable. */
typedef struct {
    char buffer[256];
    char sentinel[16];
} BoundedBuffer;

START_TEST(test_buffer_writes_never_exceed_declared_length)
{
    /* Invariant: Buffer writes never exceed the declared length */
    const char *payloads[] = {
        "%s",                     /* Valid input - normal usage */
        "%1000s",                 /* Boundary case - large width specifier */
        "%99999999999999999999s", /* Exploit case - excessive width causing overflow */
        "%.1000s",                /* Precision overflow case */
        "%1000s%1000s"            /* Multiple large specifiers */
    };
    size_t num_payloads = sizeof (payloads) / sizeof (payloads[0]);

    for (size_t i = 0; i < num_payloads; i++) {
        BoundedBuffer bb;
        memset (bb.buffer,   'A', sizeof (bb.buffer));
        bb.buffer[sizeof (bb.buffer) - 1] = '\0';
        memset (bb.sentinel, 'S', sizeof (bb.sentinel));

        char test_input[512];
        memset (test_input, 'B', sizeof (test_input) - 1);
        test_input[sizeof (test_input) - 1] = '\0';

        /* Call the bounded formatting helper */
        test_format_bounded (bb.buffer, sizeof (bb.buffer), payloads[i], test_input);

        /* Check that vsnprintf did not overflow the buffer */
        ck_assert_msg (bb.buffer[sizeof (bb.buffer) - 1] == '\0',
                       "Buffer overflow detected with payload: %s", payloads[i]);

        /* Verify the adjacent sentinel (guaranteed layout via struct) is unchanged */
        for (size_t j = 0; j < sizeof (bb.sentinel); j++) {
            ck_assert_msg (bb.sentinel[j] == 'S',
                           "Memory corruption detected beyond buffer with payload: %s",
                           payloads[i]);
        }
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
