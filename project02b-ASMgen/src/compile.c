#include "compile.h"

#include <stdio.h>
#include <stdlib.h>

// Global variables as counters for if and while labels
uint8_t g_if_counter = 0;
uint8_t g_while_counter = 0;

/**
 * Helper method to get the relative stack loc based off register %rbp.
 *
 * @param var_letter the letter, aka name, of the stack variable
 * @return the number of bytes the stack var var_letter is away from %rbp
 */
int get_relative_stack_loc(char var_letter) {
    return ('A' - var_letter - 1) * INT_BYTES;
}

/**
 * Helper method that finds a integer's power of 2 efficiently, if it is one.
 * In other words, if n = 2^k, returns k or -1 if n doesn't follow 2^k form.
 *
 * @param n the int to check
 * @return the power k, -1 if k = 0 or n is not a power of 2
 */
int8_t power_of_two(int n) {
    if (n == 2) {
        return 1;
    }
    else if (n == 0 || n == 1) {
        return -1;
    }
    if (n & (n - 1)) {
        return -1;
    }

    int power = 1;
    while (n > 2) {
        n = n >> 1;
        power++;
    }
    return power;
}

// Flaggable Int_64 made for the evalute_arithmetic_expr helper method
typedef struct {
    bool error;
    value_t value;
} flag_int_t;

/**
 * Helper method that recursively folds a subtree of arithmetic operations
 * into a constant.
 *
 * @param node the node to
 * @return flaggable int,
 *      : {true, 0} if subtree isn't made of all constant arithmetic operations
 *      : {false, val} with val being the constant the subtree evaluates to
 */
flag_int_t evalute_arithmetic_expr(node_t *node) {
    flag_int_t ret = {false, 0};

    if (node->type == NUM) {
        ret.value = ((num_node_t *) node)->value;
        return ret;
    }

    if (node->type == BINARY_OP) {
        binary_node_t *bin = (binary_node_t *) node;
        if (bin->op != '+' && bin->op != '-' && bin->op != '*' && bin->op != '/') {
            ret.error = true;
            return ret;
        }
        flag_int_t left = evalute_arithmetic_expr(bin->left);
        flag_int_t right = evalute_arithmetic_expr(bin->right);
        if (left.error || right.error) {
            ret.error = true;
        }
        else {
            if (bin->op == '+') {
                ret.value = left.value + right.value;
            }
            else if (bin->op == '-') {
                ret.value = left.value - right.value;
            }
            else if (bin->op == '*') {
                ret.value = left.value * right.value;
            }
            else {
                ret.value = left.value / right.value;
            }
        }
        return ret;
    }

    ret.error = true;
    return ret;
}

bool compile_ast(node_t *node) {
    if (node->type == NUM) {
        num_node_t *num = (num_node_t *) node;
        printf("movq $%ld, %%rdi\n", num->value);
        return true;
    }
    else if (node->type == BINARY_OP) {
        // Arithmetic Expression Folding Optimization
        flag_int_t result = evalute_arithmetic_expr(node);
        if (!result.error) {
            printf("movq $%ld, %%rdi\n", result.value);
            return true;
        }

        binary_node_t *bin = (binary_node_t *) node;

        // Power of 2 Multiplication Optimization
        if (bin->op == '*' && bin->left->type == NUM && bin->right->type == NUM) {
            int64_t l_num = ((num_node_t *) bin->left)->value;
            int64_t r_num = ((num_node_t *) bin->right)->value;

            int8_t power = power_of_two(l_num);
            if (power != -1) {
                printf("movq $%ld, %%rdi\n", r_num << power);
                return true;
            }
            power = power_of_two(r_num);
            if (power != -1) {
                printf("movq $%ld, %%rdi\n", l_num << power);
                return true;
            }
        }

        // Reads left and right exprs into rdi and rsi (uses rdi as temp)
        compile_ast(bin->right); // rdi = right
        printf("pushq %%rdi\n"); // *(rsp) = rdi = right
        compile_ast(bin->left);  // rdi = left
        printf("popq %%rsi\n");  // rsi = *(rsp) = right
        // Current State: rdi = left, rsi = right

        switch (bin->op) {
            case ('+'):
                printf("addq %%rsi, %%rdi\n"); // rdi += rsi
                return true;
            case ('-'):
                printf("subq %%rsi, %%rdi\n"); // rdi -= rsi
                return true;
            case ('*'):
                printf("imulq %%rsi, %%rdi\n"); // rdi *= rsi
                return true;
            case ('/'): {
                printf("mov %%rdi, %%rax\n");  // rax = rdi
                printf("cqto\n");              // rdx:rax = (int128_t) rax
                printf("idivq %%rsi\n");       // rax = rdx:rax / rsi
                printf("movq %%rax, %%rdi\n"); // rdi = rax
                return true;
            }
        }

        // If it's not any of the math operators, assume compare operators
        printf("cmp %%rsi, %%rdi\n");
        return true;
    }
    else if (node->type == VAR) {
        var_node_t *var = (var_node_t *) node;
        printf("movq %d(%%rbp), %%rdi\n", get_relative_stack_loc(var->name));
        return true;
    }
    else if (node->type == SEQUENCE) {
        sequence_node_t *sequence = (sequence_node_t *) node;
        for (size_t i = 0; i < sequence->statement_count; i++) {
            compile_ast(sequence->statements[i]);
        }
        return true;
    }
    else if (node->type == PRINT) {
        print_node_t *print = (print_node_t *) node;
        compile_ast(print->expr);
        printf("callq print_int\n");
        return true;
    }
    else if (node->type == LET) {
        let_node_t *let = (let_node_t *) node;
        compile_ast(let->value);
        printf("movq %%rdi, %d(%%rbp)\n", get_relative_stack_loc(let->var));
        return true;
    }
    else if (node->type == IF) {
        int local_count = g_if_counter;
        g_if_counter++;
        if_node_t *conditional = (if_node_t *) node;
        char op = ((binary_node_t *) conditional->condition)->op;
        compile_ast((node_t *) conditional->condition);

        if (op == '>') {
            printf("jg ");
        }
        else if (op == '<') {
            printf("jl ");
        }
        else {
            printf("je ");
        }
        printf("IF_IN%d\n", local_count);

        if (conditional->else_branch != NULL) {
            printf("jmp IF_ELSE%d\n", local_count);
        }
        else {
            printf("jmp IF_OUT%d\n", local_count);
        }

        printf("IF_IN%d:\n", local_count);
        compile_ast(conditional->if_branch);
        printf("jmp IF_OUT%d\n", local_count);

        if (conditional->else_branch != NULL) {
            printf("IF_ELSE%d:\n", local_count);
            compile_ast(conditional->else_branch);
        }

        printf("IF_OUT%d:\n", local_count);
        return true;
    }
    else if (node->type == WHILE) {
        int local_count = g_while_counter;
        g_while_counter++;
        while_node_t *loop = (while_node_t *) node;
        char op = ((binary_node_t *) loop->condition)->op;

        printf("LOOP_CHECK%d:\n", local_count);

        compile_ast((node_t *) loop->condition);
        if (op == '>') {
            printf("jg ");
        }
        else if (op == '<') {
            printf("jl ");
        }
        else {
            printf("je ");
        }
        printf("LOOP_IN%d\n", local_count);

        printf("jmp LOOP_OUT%d\n", local_count);

        printf("LOOP_IN%d:\n", local_count);
        compile_ast(loop->body);
        printf("jmp LOOP_CHECK%d\n", local_count);

        printf("LOOP_OUT%d:\n", local_count);
        return true;
    }
    return false;
}
