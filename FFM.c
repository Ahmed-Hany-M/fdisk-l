#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <string.h>
#include <fcntl.h>
#include <sys/types.h>
#include <unistd.h>
#include <inttypes.h>
#include <stdbool.h>

typedef struct {
	uint8_t status;
	uint8_t first_chs[3];
	uint8_t partition_type;
	uint8_t last_chs[3];
	uint32_t lba;
	uint32_t sector_count;
}PartitionEntry;

typedef struct {									//Same as PartitionEntry.
	uint8_t status;
	uint8_t first_chs[3];
	uint8_t partition_type;
	uint8_t last_chs[3];
	uint32_t lba;
	uint32_t sector_count;
}EBR_entry;

typedef struct EBR_table{
	uint32_t lba;
	uint32_t sector_count;
	EBR_entry partitions[4];						
	struct EBR_table* next;
}EBR_table;

void add_EBR_table(EBR_table** head, EBR_table* new_ebr) {
	if(*head == NULL)
		*head = new_ebr;
	else {
		EBR_table* current = *head;
		while(current->next != NULL)
			current = current->next;
		
		current->next = new_ebr;
	}
}
	//I will use this for debugging only (for now).
void print_EBR_list(EBR_table* head) {
    EBR_table* current = head;
    while (current != NULL) {
        printf("EBR Start LBA: %u, Sector Count: %u\n", current->lba, current->sector_count);
        for (int i = 0; i < 4; ++i) {
            EBR_entry* entry = &current->partitions[i];
            if (entry->sector_count > 0) {
                printf("  Logical Partition %d - Start LBA: %u, Sector Count: %u\n",
                       i + 1, entry->lba, entry->sector_count);
            }
        }
        current = current->next;
    }
}

int main(int argc, char** argv) {
	char buffer[512];								//Will be used to read MBR.
	
	if(argc < 2) {									
		char* err_msg = "Too few arguments.\n";
		write(2, err_msg, strlen(err_msg));
		exit(EXIT_FAILURE);
	}
	
	int fd = open(argv[1], O_RDONLY);
	if(fd == -1) {									//If file failed to be opened.									
		char* err_msg = "Unable to open the file.\n";
		write(2, err_msg, strlen(err_msg));
		exit(EXIT_FAILURE);
	}
	
	ssize_t read_bytes = read(fd, buffer, sizeof(buffer));
	if(read_bytes == -1) {								//If reading from the file failed.
		char* err_msg = "Unable to read.\n";
		write(2, err_msg, strlen(err_msg));
		exit(EXIT_FAILURE);
	}

	PartitionEntry* table_entry_ptr = (PartitionEntry*)&buffer[446];		//The first 446 bytes are for IPL {Iniital Program Loader}.
	EBR_table* EBR_list_head = NULL;
	
	printf("%-13s %-10s %-10s %-10s %-10s %-10s %-10s %-10s\n", "Device", "Boot", "Start", "End", "Sector", "Size", "Id", "Type");
	for(int i = 0 ; i < 4 ; i++) {
		if(table_entry_ptr[i].sector_count == 0)
			continue;
			
		printf("%s%-5d %-10c %-10u %-10u %-10u %.1fG       %-10X\n", argv[1], i+1, table_entry_ptr[i].status == 0x80 ? '*' : ' ',
			table_entry_ptr[i].lba, table_entry_ptr[i].lba + table_entry_ptr[i].sector_count - 1, table_entry_ptr[i].sector_count,
			(double)table_entry_ptr[i].sector_count*512/(1024*1024*1024), table_entry_ptr[i].partition_type);
		if(table_entry_ptr[i].partition_type == 0x5 ) {
			uint32_t ebr_lba = table_entry_ptr[i].lba;
			while(ebr_lba != 0) {
				EBR_table* new_ebr = (EBR_table*)malloc(sizeof(EBR_table));
				if(new_ebr == NULL) {
					char* err_msg = "Memory allocation failed.\n";
					write(2, err_msg, strlen(err_msg));
					close(fd);
					exit(EXIT_FAILURE);
				}
				
				off_t check_lseek = lseek(fd, ebr_lba*512, SEEK_SET);
				if(check_lseek == -1) {
					char* err_msg = "lseek failed.\n";
					write(2, err_msg, strlen(err_msg));
					close(fd);
					exit(EXIT_FAILURE);
				}
				
				char bufferEBR[512];
				ssize_t read_ebr_bytes = read(fd, bufferEBR, sizeof(bufferEBR));
				if(read_ebr_bytes == -1) {
					char* err_msg = "reading failed.\n";
					write(2, err_msg, strlen(err_msg));
					close(fd);
					exit(EXIT_FAILURE);
				}
				
				memcpy(new_ebr, bufferEBR, sizeof(EBR_table));
				new_ebr->lba = ebr_lba;
				new_ebr->next = NULL;
				
				add_EBR_table(&EBR_list_head, new_ebr);
				/* Moving to the next EBR, which should be present in the first partition of the current EBR.*/
				ebr_lba = new_ebr->partitions[0].lba;
				if(ebr_lba == 0)
					break;
				
			}
		}
	}
	print_EBR_list(EBR_list_head);			//For debugging (I will replace it later).
	
	
	
close(fd);
return 0;
}

