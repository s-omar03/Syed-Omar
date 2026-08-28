#include <stdio.h>
#define MAX 100
#define ADD(a,b) ((a)+(b))
/* this is a
   multi-line block comment */
// this is a single line comment
int add(int a, int b) {
    return a + b;
}
void greet(char* name) {
    printf("Hello, %s!\n", name);
    printf("<span class=\"quoted\">escaped quote test</span>\n");
}
int main(int argc, char** argv) {
    int x = 10;
    int y = 20;
    int z = ADD(x, y);
    float pi = 3.14159f;
    int hex = 0xFF;
    long big = 100000L;
    char quote_char = '\'';
    if (z > MAX) {
        printf("Too large\n");
    } else {
        printf("Result: %d\n", z);
    }
    char* msg = "this is a string";
    unsigned long size = sizeof(int);
    while (x > 0) {
        x--;
    }
    return 0;
}
