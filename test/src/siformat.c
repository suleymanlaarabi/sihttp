#include <test.h>

#include <stdlib.h>

void siformat_basic(void) {
    char *str = siformat("page(%d)-%s", 12, "ok");
    test_not_null(str);
    test_str(str, "page(12)-ok");
    free(str);
}

void siformat_empty(void) {
    char *str = siformat("%s", "");
    test_not_null(str);
    test_str(str, "");
    free(str);
}
