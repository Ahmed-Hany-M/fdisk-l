#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <inttypes.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdbool.h>

typedef struct {
    uint8_t status;
    uint8_t first_chs[3];
    uint8_t partition_type;
    uint8_t last_chs[3];
    uint32_t lba;
    uint32_t sector_count;
} PartitionEntry;

						/*Single linked list for partitions*/
typedef struct PartitionNode {
    char device_name[20];
    uint64_t start;
    uint64_t end;
    uint32_t sector_count;
    double size_gb;
    uint8_t partition_type;
    char type_str[20];
    struct PartitionNode* next;
} PartitionNode;


PartitionNode* create_node(const char* device_name, uint64_t start, uint64_t end, uint32_t sector_count, double size_gb, uint8_t partition_type, const char* type_str) {
    PartitionNode* new_node = (PartitionNode*)malloc(sizeof(PartitionNode));
    if (!new_node) {
        perror("Failed to allocate memory");
        exit(EXIT_FAILURE);
    }
    strncpy(new_node->device_name, device_name, sizeof(new_node->device_name));
    new_node->start = start;
    new_node->end = end;
    new_node->sector_count = sector_count;
    new_node->size_gb = size_gb;
    new_node->partition_type = partition_type;
    strncpy(new_node->type_str, type_str, sizeof(new_node->type_str));
    new_node->next = NULL;
    return new_node;
}

					/*I will use this to add a node to the singly linked list*/
void add_node(PartitionNode** head, PartitionNode* new_node) {
    if (*head == NULL) {
        *head = new_node;
    } else {
        PartitionNode* temp = *head;
        while (temp->next != NULL) {
            temp = temp->next;
        }
        temp->next = new_node;
    }
}

			/* Prints the linked list*/
void print_list(PartitionNode* head) {
    printf("%-13s %-10s %-10s %-10s %-10s %-10s %-10s %-10s\n",
           "Device", "Boot", "Start", "End", "Sector", "Size (G)", "Id", "Type");

    PartitionNode* temp = head;
    while (temp != NULL) {
        printf("%-13s %-10c %-10lu %-10lu %-10u %-10.1f %-10X %-10s\n",
               temp->device_name,
               (temp->partition_type == 0x80) ? '*' : ' ', 
               temp->start,
               temp->end,
               temp->sector_count,
               temp->size_gb,
               temp->partition_type,
               temp->type_str);
        temp = temp->next;
    }
}

			/*Frees the singly linked list at the very end of the program*/
void free_list(PartitionNode* head) {
    PartitionNode* temp;
    while (head != NULL) {
        temp = head;
        head = head->next;
        free(temp);
    }
}


int main(int argc, char** argv) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <device>\n", argv[0]);
        return EXIT_FAILURE;
    }

    char buffer[512];
    int fd = open(argv[1], O_RDONLY);
    if (fd == -1) {
        perror("Couldn't open the file.\n");
        return EXIT_FAILURE;
    }

    ssize_t read_bytes = read(fd, buffer, 512);
    if (read_bytes == -1) {
        perror("Unable to read MBR");
        close(fd);
        return EXIT_FAILURE;
    }

    PartitionEntry* table_entry_ptr = (PartitionEntry*)&buffer[446];
    PartitionNode* head = NULL;

    off_t initial_pos = lseek(fd, 0, SEEK_CUR);					//To save the initial position, instead of multiple calls to lseek.
    if (initial_pos == -1) {
        perror("lseek failed");
        close(fd);
        return EXIT_FAILURE;
    }

    for (int i = 0; i < 4; i++) {
        if (table_entry_ptr[i].sector_count == 0)
            continue;

        char device_name[20];
        snprintf(device_name, sizeof(device_name), "%s%d", argv[1], i + 1);		//I am using snprintf mainly for formatting here.

        uint64_t start = table_entry_ptr[i].lba;
        uint64_t end = start + table_entry_ptr[i].sector_count - 1;
        double size_gb = (double)table_entry_ptr[i].sector_count * 512 / (1024.0 * 1024 * 1024);

        const char* type_str = (table_entry_ptr[i].partition_type == 0x83) ? "Linux" :
                               (table_entry_ptr[i].partition_type == 0x05) ? "Extended" :
                               "Unknown";									//For now I put 'Unknown'. 

        add_node(&head, create_node(device_name, start, end, table_entry_ptr[i].sector_count, size_gb, table_entry_ptr[i].partition_type, type_str));
        
       
		/*0x05 is for extended partition*/
        if (table_entry_ptr[i].partition_type == 0x05) { 
            char buffer_ext[512];
            off_t offset = table_entry_ptr[i].lba * 512;
            if (lseek(fd, offset, SEEK_SET) == -1) {
                perror("lseek failed");
                free_list(head);
                close(fd);
                return EXIT_FAILURE;
            }
            ssize_t read_bytes_ext = read(fd, buffer_ext, 512);
            if (read_bytes_ext != 512) {
                perror("Unable to read EBR");
                free_list(head);
                close(fd);
                return EXIT_FAILURE;
            }

            //Restoring the original position.
            if (lseek(fd, initial_pos, SEEK_SET) == -1) {
                perror("lseek failed");
                free_list(head);
                close(fd);
                return EXIT_FAILURE;
            }

            PartitionEntry* EBR_ptr = (PartitionEntry*) &buffer_ext[446];
            for (int j = 0; j < 4; j++) {
                if (EBR_ptr[j].sector_count == 0)
                    continue;

                char logical_device_name[20];
                snprintf(logical_device_name, sizeof(logical_device_name), "%s%d", argv[1], i + j + 2);			//Formatting mainly.

                uint64_t logical_start = EBR_ptr[j].lba + table_entry_ptr[i].lba;
                uint64_t logical_end = logical_start + EBR_ptr[j].sector_count - 1;
                double logical_size_gb = (double)EBR_ptr[j].sector_count * 512 / (1024.0 * 1024 * 1024);

                const char* logical_type_str = (EBR_ptr[j].partition_type == 0x83) ? "Linux" :
                                                (EBR_ptr[j].partition_type == 0x05) ? "Extended" :
                                                "Unknown";

                add_node(&head, create_node(logical_device_name, logical_start, logical_end, EBR_ptr[j].sector_count, 
                	logical_size_gb, EBR_ptr[j].partition_type, logical_type_str));
            }
        }
    }

    print_list(head);
    free_list(head);
    close(fd);
    return EXIT_SUCCESS;
}

