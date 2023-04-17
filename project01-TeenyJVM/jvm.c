#include "jvm.h"

#include <assert.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "heap.h"
#include "read_class.h"

/** The name of the method to invoke to run the class file */
const char MAIN_METHOD[] = "main";
/**
 * The "descriptor" string for main(). The descriptor encodes main()'s signature,
 * i.e. main() takes a String[] and returns void.
 * If you're interested, the descriptor string is explained at
 * https://docs.oracle.com/javase/specs/jvms/se12/html/jvms-4.html#jvms-4.3.2.
 */
const char MAIN_DESCRIPTOR[] = "([Ljava/lang/String;)V";

/**
 * Represents the return value of a Java method: either void or an int or a reference.
 * For simplification, we represent a reference as an index into a heap-allocated array.
 * (In a real JVM, methods could also return object references or other primitives.)
 */
typedef struct {
    /** Whether this returned value is an int */
    bool has_value;
    /** The returned value (only valid if `has_value` is true) */
    int32_t value;
} optional_value_t;

int16_t bit_move_helper(u1 b1, u1 b2) {
    return (int16_t)((b1 << 8) | b2);
}

u4 if_helper(bool comparison, u1 b1, u1 b2) {
    if (comparison) {
        return bit_move_helper(b1, b2);
    }
    return 3;
}

/**
 * Runs a method's instructions until the method returns.
 *
 * @param method the method to run
 * @param locals the array of local variables, including the method parameters.
 *   Except for parameters, the locals are uninitialized.
 * @param class the class file the method belongs to
 * @param heap an array of heap-allocated pointers, useful for references
 * @return an optional int containing the method's return value
 */
optional_value_t execute(method_t *method, int32_t *locals, class_file_t *class,
                         heap_t *heap) {
    int32_t operand_stack[method->code.max_stack];
    memset(operand_stack, 0, sizeof(operand_stack));
    u2 si = 0; // stack_index
    u4 pc = 0; // program_counter
    optional_value_t result = {.has_value = false};
    while (pc < method->code.code_length) {
        u1 instruction = method->code.code[pc];
        switch (instruction) {
            case i_bipush:
                operand_stack[si] = (int32_t)((int8_t) method->code.code[pc + 1]);
                si++;
                pc += 2;
                break;
            case i_getstatic:
                pc += 3;
                break;
            case i_invokevirtual:
                si--;
                printf("%d\n", operand_stack[si]);
                pc += 3;
                break;
            case i_iconst_m1 ... i_iconst_5:
                operand_stack[si] = (int32_t)(instruction - i_iconst_0);
                si++;
                pc++;
                break;
            case i_sipush:
                operand_stack[si] =
                    bit_move_helper(method->code.code[pc + 1], method->code.code[pc + 2]);
                si++;
                pc += 3;
                break;
            case i_iadd:
                operand_stack[si - 2] = operand_stack[si - 2] + operand_stack[si - 1];
                si--;
                pc++;
                break;
            case i_isub:
                operand_stack[si - 2] = operand_stack[si - 2] - operand_stack[si - 1];
                si--;
                pc++;
                break;
            case i_imul:
                operand_stack[si - 2] = operand_stack[si - 2] * operand_stack[si - 1];
                si--;
                pc++;
                break;
            case i_idiv:
                operand_stack[si - 2] = operand_stack[si - 2] / operand_stack[si - 1];
                si--;
                pc++;
                break;
            case i_irem:
                operand_stack[si - 2] = operand_stack[si - 2] % operand_stack[si - 1];
                si--;
                pc++;
                break;
            case i_ineg:
                operand_stack[si - 1] = -1 * operand_stack[si - 1];
                pc++;
                break;
            case i_ishl:
                operand_stack[si - 2] = operand_stack[si - 2] << operand_stack[si - 1];
                si--;
                pc++;
                break;
            case i_ishr:
                operand_stack[si - 2] = operand_stack[si - 2] >> operand_stack[si - 1];
                si--;
                pc++;
                break;
            case i_iushr:
                operand_stack[si - 2] =
                    (uint32_t) operand_stack[si - 2] >> operand_stack[si - 1];
                si--;
                pc++;
                break;
            case i_iand:
                operand_stack[si - 2] = operand_stack[si - 2] & operand_stack[si - 1];
                si--;
                pc++;
                break;
            case i_ior:
                operand_stack[si - 2] = operand_stack[si - 2] | operand_stack[si - 1];
                si--;
                pc++;
                break;
            case i_ixor:
                operand_stack[si - 2] = operand_stack[si - 2] ^ operand_stack[si - 1];
                si--;
                pc++;
                break;
            case i_aload:
            case i_iload:
                operand_stack[si] = locals[method->code.code[pc + 1]];
                si++;
                pc += 2;
                break;
            case i_astore:
            case i_istore:
                locals[method->code.code[pc + 1]] = operand_stack[si - 1];
                si--;
                pc += 2;
                break;
            case i_iload_0 ... i_iload_3:
                operand_stack[si] = locals[instruction - i_iload_0];
                si++;
                pc++;
                break;
            case i_aload_0 ... i_aload_3:
                operand_stack[si] = locals[instruction - i_aload_0];
                si++;
                pc++;
                break;
            case i_istore_0 ... i_istore_3:
                locals[instruction - i_istore_0] = operand_stack[si - 1];
                si--;
                pc++;
                break;
            case i_astore_0 ... i_astore_3:
                locals[instruction - i_astore_0] = operand_stack[si - 1];
                si--;
                pc++;
                break;
            case i_iinc:
                locals[method->code.code[pc + 1]] += (int8_t) method->code.code[pc + 2];
                pc += 3;
                break;
            case i_ldc: {
                void *info = (class->constant_pool[method->code.code[pc + 1] - 1]).info;
                operand_stack[si] = ((CONSTANT_Integer_info *) info)->bytes;
                si++;
                pc += 2;
                break;
            }
            case i_ifeq:
                pc += if_helper(operand_stack[si - 1] == 0, method->code.code[pc + 1],
                                method->code.code[pc + 2]);
                si--;
                break;
            case i_ifne:
                pc += if_helper(operand_stack[si - 1] != 0, method->code.code[pc + 1],
                                method->code.code[pc + 2]);
                si--;
                break;
            case i_iflt:
                pc += if_helper(operand_stack[si - 1] < 0, method->code.code[pc + 1],
                                method->code.code[pc + 2]);
                si--;
                break;
            case i_ifge:
                pc += if_helper(operand_stack[si - 1] >= 0, method->code.code[pc + 1],
                                method->code.code[pc + 2]);
                si--;
                break;
            case i_ifgt:
                pc += if_helper(operand_stack[si - 1] > 0, method->code.code[pc + 1],
                                method->code.code[pc + 2]);
                si--;
                break;
            case i_ifle:
                pc += if_helper(operand_stack[si - 1] <= 0, method->code.code[pc + 1],
                                method->code.code[pc + 2]);
                si--;
                break;
            case i_if_icmpeq:
                pc += if_helper(operand_stack[si - 2] == operand_stack[si - 1],
                                method->code.code[pc + 1], method->code.code[pc + 2]);
                si -= 2;
                break;
            case i_if_icmpne:
                pc += if_helper(operand_stack[si - 2] != operand_stack[si - 1],
                                method->code.code[pc + 1], method->code.code[pc + 2]);
                si -= 2;
                break;
            case i_if_icmplt:
                pc += if_helper(operand_stack[si - 2] < operand_stack[si - 1],
                                method->code.code[pc + 1], method->code.code[pc + 2]);
                si -= 2;
                break;
            case i_if_icmpge:
                pc += if_helper(operand_stack[si - 2] >= operand_stack[si - 1],
                                method->code.code[pc + 1], method->code.code[pc + 2]);
                si -= 2;
                break;
            case i_if_icmpgt:
                pc += if_helper(operand_stack[si - 2] > operand_stack[si - 1],
                                method->code.code[pc + 1], method->code.code[pc + 2]);
                si -= 2;
                break;
            case i_if_icmple:
                pc += if_helper(operand_stack[si - 2] <= operand_stack[si - 1],
                                method->code.code[pc + 1], method->code.code[pc + 2]);
                si -= 2;
                break;
            case i_goto:
                pc +=
                    bit_move_helper(method->code.code[pc + 1], method->code.code[pc + 2]);
                break;
            case i_invokestatic: {
                u2 index =
                    bit_move_helper(method->code.code[pc + 1], method->code.code[pc + 2]);
                method_t *mthd = find_method_from_index(index, class);
                uint16_t param_num = get_number_of_parameters(mthd);
                int32_t lcls[mthd->code.max_locals];
                memset(lcls, 0, sizeof(lcls));
                for (uint16_t i = 0; i < param_num; i++) {
                    lcls[param_num - 1 - i] = operand_stack[si - 1 - i];
                }
                si -= param_num;
                pc += 3;
                optional_value_t ret = execute(mthd, lcls, class, heap);
                if (ret.has_value) {
                    operand_stack[si] = ret.value;
                    si++;
                }
                break;
            }
            case i_nop:
                pc++;
                break;
            case i_dup:
                operand_stack[si] = operand_stack[si - 1];
                si++;
                pc++;
                break;
            // store length as the 0th element in the array
            case i_newarray: {
                int32_t length_w_count = operand_stack[si - 1] + 1;
                int32_t *array = malloc(sizeof(int32_t) * length_w_count);
                memset(array, 0, sizeof(int32_t) * length_w_count);
                array[0] = operand_stack[si - 1];
                operand_stack[si - 1] = heap_add(heap, array);
                pc += 2;
                break;
            }
            case i_arraylength:
                operand_stack[si - 1] = heap_get(heap, operand_stack[si - 1])[0];
                pc++;
                break;
            case i_iastore:
                heap_get(heap, operand_stack[si - 3])[operand_stack[si - 2] + 1] =
                    operand_stack[si - 1];
                pc++;
                si -= 3;
                break;
            case i_iaload:
                operand_stack[si - 2] =
                    heap_get(heap, operand_stack[si - 2])[operand_stack[si - 1] + 1];
                pc++;
                si--;
                break;
            case i_areturn:
            case i_ireturn:
                result.has_value = true;
                result.value = operand_stack[si - 1];
                return result;
            case i_return:
                return result;
        }
    }
    return result;
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "USAGE: %s <class file>\n", argv[0]);
        return 1;
    }

    // Open the class file for reading
    FILE *class_file = fopen(argv[1], "r");
    assert(class_file != NULL && "Failed to open file");

    // Parse the class file
    class_file_t *class = get_class(class_file);
    int error = fclose(class_file);
    assert(error == 0 && "Failed to close file");

    // The heap array is initially allocated to hold zero elements.
    heap_t *heap = heap_init();

    // Execute the main method
    method_t *main_method = find_method(MAIN_METHOD, MAIN_DESCRIPTOR, class);
    assert(main_method != NULL && "Missing main() method");
    /* In a real JVM, locals[0] would contain a reference to String[] args.
     * But since TeenyJVM doesn't support Objects, we leave it uninitialized. */
    int32_t locals[main_method->code.max_locals];
    // Initialize all local variables to 0
    memset(locals, 0, sizeof(locals));
    optional_value_t result = execute(main_method, locals, class, heap);
    assert(!result.has_value && "main() should return void");

    // Free the internal data structures
    free_class(class);

    // Free the heap
    heap_free(heap);
}
