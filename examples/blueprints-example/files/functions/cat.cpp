extern "C" {
    #include <string.h>

    char* cat(char* s1, char* s2) {
        strcat(s1, " ");
        strcat(s1, s2);
        return s1;
    }
}