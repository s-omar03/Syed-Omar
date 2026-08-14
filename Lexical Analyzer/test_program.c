#include <stdio.h>

/* This is a test program
   for the lexical analyser */
int main() {
    int count = 10;
    float pi = 3.14;
    char name[20] = "hello world";
    // single line comment
    if (count >= 5 && pi != 0) {
        count++;
        count += 2;
    } else {
        count--;
    }
    for (int i = 0; i < count; i = i + 1) {
        printf("i = %d\n", i);
    }
    return 0;
}
