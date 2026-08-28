#include "../include/parser.h"
#include "../include/output.h"
#include <stdlib.h>

int main(int argc, char** argv) {
    if ( argc < 2 ) {
        printf("Usage: %s <input_file>\n",argv[0]);
        return 1;
    }
    FILE* src = fopen(argv[1],"r");
    if ( !src ) {
        printf("Error opening source file \n");
        return 1;
    }

    // finding file size
    fseek(src,0,SEEK_END);
    long length = ftell(src);
    fseek(src,0,SEEK_SET);

    if (length < 0) {
        printf("Error determining file size\n");
        fclose(src);
        return 1;
    }

    // putting entire file into buffer
    char* buffer = malloc((size_t)length);
    if (!buffer) {
        printf("Error allocating memory for source buffer\n");
        fclose(src);
        return 1;
    }
    size_t read_bytes = fread(buffer,1,(size_t)length,src);
    fclose(src);

    FILE* dest = fopen("output.html","w");
    if ( !dest ) {
        printf("Error creating output file\n");
        free(buffer);
        return 1;
    }
    set_output_file(dest);
    fprintf(dest,
            "<html>\n"
            "<head>\n"
            "<style>\n"
            "body { background-color: #282a36; color: #f8f8f2; margin: 0; padding: 20px; }\n"
            "pre  { font-family: 'Consolas', 'Courier New', monospace; font-size: 14px; line-height: 1.5; }\n"
            ".comment      { color: #6272a4; font-style: italic; }\n"
            ".preprocessor { color: #ff79c6; }\n"
            ".header       { color: #8be9fd; }\n"
            ".string       { color: #f1fa8c; }\n"
            ".char         { color: #f1fa8c; }\n"
            ".keyword_data    { color: #8be9fd; font-weight: bold; }\n"
            ".keyword_control { color: #ff79c6; font-weight: bold; }\n"
            ".number       { color: #bd93f9; }\n"
            "</style>\n"
            "</head>\n"
            "<body><pre>\n"
           );
    parse(buffer,(int)read_bytes,handle_token);
    fprintf(dest, "</pre>\n</body>\n</html>\n");
    fclose(dest);
    free(buffer);
    return 0;
}
