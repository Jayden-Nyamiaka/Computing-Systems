#include "directory_tree.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

const unsigned DIR_MODE = 0777;

void init_node(node_t *node, char *name, node_type_t type) {
    if (name == NULL) {
        name = strdup("ROOT");
        assert(name != NULL);
    }
    node->name = name;
    node->type = type;
}

file_node_t *init_file_node(char *name, size_t size, uint8_t *contents) {
    file_node_t *node = malloc(sizeof(file_node_t));
    assert(node != NULL);
    init_node((node_t *) node, name, FILE_TYPE);
    node->size = size;
    node->contents = contents;
    return node;
}

directory_node_t *init_directory_node(char *name) {
    directory_node_t *node = malloc(sizeof(directory_node_t));
    assert(node != NULL);
    init_node((node_t *) node, name, DIRECTORY_TYPE);
    node->num_children = 0;
    node->children = NULL;
    return node;
}

void add_child_directory_tree(directory_node_t *dnode, node_t *child) {
    /** Handles case of no children */
    if (dnode->children == NULL) {
        dnode->children = malloc(sizeof(node_t *));
        assert(dnode->children != NULL);
        dnode->children[0] = child;
        dnode->num_children = 1;
        return;
    }

    /** Returns if the new child's name is a duplicate */
    for (size_t i = 0; i < dnode->num_children; i++) {
        if (strcmp(child->name, dnode->children[i]->name) == 0) {
            return;
        }
    }

    /** Makes space for one more node_t * at the end of dnode->children */
    dnode->num_children++;
    dnode->children = realloc(dnode->children, dnode->num_children * sizeof(node_t *));
    assert(dnode->children != NULL);

    /** Moves nodes back until the child node is inserted in the alphabetic position */
    for (size_t i = dnode->num_children - 1; i > 0; i--) {
        int comp = strcmp(child->name, dnode->children[i - 1]->name);
        assert(comp != 0);
        if (comp > 0) {
            dnode->children[i] = child;
            return;
        }
        dnode->children[i] = dnode->children[i - 1];
        dnode->children[i - 1] = NULL;
    }
    dnode->children[0] = child;
}

/**
 * Helper method for print_directory_tree that takes the node and level up to the
 * current file/directory to print the files and directories.
 *
 */
void print_directory_tree_level(node_t *node, size_t level) {
    for (size_t i = 0; i < level; i++) {
        printf("    ");
    }
    printf("%s\n", node->name);

    if (node->type == FILE_TYPE) {
        return;
    }

    assert(node->type == DIRECTORY_TYPE);
    directory_node_t *dnode = (directory_node_t *) node;
    for (size_t i = 0; i < dnode->num_children; i++) {
        print_directory_tree_level(dnode->children[i], level + 1);
    }
}

void print_directory_tree(node_t *node) {
    print_directory_tree_level(node, 0);
}

/**
 * Helper method for create_directory_tree that takes the node and path up to the
 * current file/directory to create the files and directories.
 *
 */
void create_directory_tree_path(node_t *node, char *path) {
    size_t length = strlen(path) + strlen(node->name) + 2;
    char path_new[length];
    strcpy(path_new, path);
    strcat(path_new, "/");
    strcat(path_new, node->name);

    if (node->type == FILE_TYPE) {
        FILE *nfile;
        nfile = fopen(path_new, "w");
        assert(nfile != NULL);
        file_node_t *fnode = (file_node_t *) node;
        size_t write_result =
            fwrite(fnode->contents, sizeof(uint8_t), fnode->size, nfile);
        assert(write_result == fnode->size);
        int close_result = fclose(nfile);
        assert(close_result == 0);
        return;
    }

    assert(node->type == DIRECTORY_TYPE);
    int create_result = mkdir(path_new, DIR_MODE);
    assert(create_result == 0);
    directory_node_t *dnode = (directory_node_t *) node;
    for (size_t i = 0; i < dnode->num_children; i++) {
        create_directory_tree_path(dnode->children[i], path_new);
    }
}

void create_directory_tree(node_t *node) {
    create_directory_tree_path(node, ".");
}

void free_directory_tree(node_t *node) {
    if (node->type == FILE_TYPE) {
        file_node_t *fnode = (file_node_t *) node;
        free(fnode->contents);
    }
    else {
        assert(node->type == DIRECTORY_TYPE);
        directory_node_t *dnode = (directory_node_t *) node;
        for (size_t i = 0; i < dnode->num_children; i++) {
            free_directory_tree(dnode->children[i]);
        }
        free(dnode->children);
    }
    free(node->name);
    free(node);
}
