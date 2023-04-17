#include <stdio.h>
#include <stdlib.h>

#include "compile.h"
#include "parser.h"

void usage(char *program) {
    fprintf(stderr, "USAGE: %s <program file>\n", program);
    exit(1);
}

/**
 * Prints the start of the the x86-64 assembly output.
 * The assembly code implementing the TeenyBASIC statements
 * goes between the header and the footer.
 */
void header(void) {
    printf(
        "# The code section of the assembly file\n"
        ".text\n"
        ".globl basic_main\n"
        "basic_main:\n"
        "    # The main() function\n");

    // Pushes rbp to return later, moves rbp to rsp, then moves rsp down
    // to make space for 26 64-bit integers (A - Z)
    printf("pushq %%rbp\n");
    printf("movq %%rsp, %%rbp\n");
    printf("subq $%d, %%rsp\n", INT_BYTES * ALPHABET);
}

/**
 * Prints the end of the x86-64 assembly output.
 * The assembly code implementing the TeenyBASIC statements
 * goes between the header and the footer.
 */
void footer(void) {
    // Moves rsp back up the stack by the 26 64-bit integers (A - Z)
    // and restores rbp to its old value
    printf("addq $%d, %%rsp\n", INT_BYTES * ALPHABET);
    printf("popq %%rbp\n");
    printf("    ret\n");
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        usage(argv[0]);
    }

    FILE *program = fopen(argv[1], "r");
    if (program == NULL) {
        usage(argv[0]);
    }

    header();

    node_t *ast = parse(program);
    fclose(program);
    if (ast == NULL) {
        fprintf(stderr, "Parse error\n");
        return 2;
    }

    // Display the AST for debugging purposes
    print_ast(ast);

    // Compile the AST into assembly instructions
    if (!compile_ast(ast)) {
        free_ast(ast);
        fprintf(stderr, "Compilation error\n");
        return 3;
    }

    free_ast(ast);

    footer();
}
