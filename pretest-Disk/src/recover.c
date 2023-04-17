#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include "directory_tree.h"
#include "fat16.h"

const size_t MASTER_BOOT_RECORD_SIZE = 0x20B;

/**
 * Helper method for follow that creates directories, reads in files, and
 * adds nodes to the directory tree for all non-hidden directory entries.
 * @return if entry is a directory, the directory_node_t * to call follow on, else NULL
 */
directory_node_t *entry_handler(FILE *disk, directory_node_t *node,
                                directory_entry_t entry) {
    if (is_hidden(entry)) {
        return NULL;
    }

    if (is_directory(entry)) {
        directory_node_t *dnode = init_directory_node(get_file_name(entry));
        add_child_directory_tree(node, (node_t *) dnode);
        return dnode;
    }

    uint8_t *contents = malloc(sizeof(uint8_t) * entry.file_size);
    assert(contents != NULL);
    int read_result = fread(contents, entry.file_size, 1, disk);
    assert(read_result == 1);
    file_node_t *fnode = init_file_node(get_file_name(entry), entry.file_size, contents);
    add_child_directory_tree(node, (node_t *) fnode);
    return NULL;
}

void follow(FILE *disk, directory_node_t *node, bios_parameter_block_t bpb) {
    /* Reads directory_entry_t's until a null terminator starting name is reached */
    directory_entry_t entry;
    int read_result = fread(&entry, sizeof(directory_entry_t), 1, disk);
    assert(read_result == 1);
    while (entry.filename[0] != '\0') {
        /* Stores the current location and seeks to the offset location */
        long next_loc = ftell(disk);
        int seek_result =
            fseek(disk, get_offset_from_cluster(entry.first_cluster, bpb), SEEK_SET);
        assert(seek_result == 0);

        /* Handles directory entry through helper method */
        directory_node_t *dnode = entry_handler(disk, node, entry);
        if (dnode != NULL) {
            follow(disk, dnode, bpb);
        }

        /* Seeks back to the location of the next directory entry and reads it in */
        fseek(disk, next_loc, SEEK_SET);
        assert(seek_result == 0);
        read_result = fread(&entry, sizeof(directory_entry_t), 1, disk);
        assert(read_result == 1);
    }
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "USAGE: %s <image filename>\n", argv[0]);
        return 1;
    }

    FILE *disk = fopen(argv[1], "r");
    if (disk == NULL) {
        fprintf(stderr, "No such image file: %s\n", argv[1]);
        return 1;
    }

    bios_parameter_block_t bpb;

    /* Skips past the Master Boot Record */
    int seek_result = fseek(disk, MASTER_BOOT_RECORD_SIZE, SEEK_SET);
    assert(seek_result == 0);

    /* Reads the Bios Parameter Block into bpb */
    size_t read_result = fread(&bpb, sizeof(bios_parameter_block_t), 1, disk);
    assert(read_result == 1);

    /* Skips directly to the Root Directory Entries Block */
    seek_result = fseek(disk, get_root_directory_location(bpb), SEEK_SET);
    assert(seek_result == 0);

    directory_node_t *root = init_directory_node(NULL);
    follow(disk, root, bpb);
    print_directory_tree((node_t *) root);
    create_directory_tree((node_t *) root);
    free_directory_tree((node_t *) root);

    int result = fclose(disk);
    assert(result == 0);
}
