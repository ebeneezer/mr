#include <stdio.h>
#include <stdlib.h>
#include "mrmac/mrmac.h"
int main(void) {
    const char *src =
        "$MACRO TEST;\n"
        "DEF_INT(W, C, X1, Y1, X2, Y2);\n"
        "DEF_STR(FN);\n"
        "FN := 'block.txt';\n"
        "W := WINDOW_COUNT;\n"
        "C := CUR_WINDOW;\n"
        "X1 := WIN_X1; Y1 := WIN_Y1; X2 := WIN_X2; Y2 := WIN_Y2;\n"
        "CREATE_WINDOW;\n"
        "SWITCH_WINDOW(1);\n"
        "SIZE_WINDOW(1, 1, 40, 10);\n"
        "MODIFY_WINDOW;\n"
        "SAVE_BLOCK(FN);\n"
        "ERASE_WINDOW;\n"
        "DELETE_WINDOW;\n"
        "END_MACRO;\n";
    size_t sz = 0;
    unsigned char *bc = compile_macro_code(src, &sz);
    if (!bc) {
        const char *err = get_last_compile_error();
        fprintf(stderr, "compile failed: %s\n", err ? err : "(null)");
        return 1;
    }
    free(bc);
    printf("ok %zu\n", sz);
    return 0;
}
