#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <string.h>
#include <fcntl.h>
#include <sys/types.h>
#include <unistd.h>
#include <inttypes.h>
#include <stdbool.h>

struct PartitionEntry {
    uint8_t status;
    uint8_t first_chs[3];
    uint8_t partition_type;
    uint8_t last_chs[3];
    uint32_t lba;
    uint32_t sector_count;
};

int main(int argc, char** argv) {
    if(argc < 2) {                                  /* Ensuring that the users enters at least 2 arguments.*/
        char* err_msg = "Missing Argument(s).\n";
        write(2, err_msg, strlen(err_msg));
        exit(EXIT_FAILURE);
    }

    char primary_buffer[512];               

    int fd = open(argv[1], O_RDONLY);
    if(fd == -1) {
        char* err_msg = "Opening the given path failed.\n";
        write(2, err_msg, strlen(err_msg));
        exit(EXIT_FAILURE);
    }

    ssize_t read_primary_bytes = read(fd, primary_buffer, sizeof(primary_buffer));
    if(read_primary_bytes == -1) {
        char* err_msg = "Reading from the MBR failed.\n";
        write(2, err_msg, strlen(err_msg));
        exit(EXIT_FAILURE);
    }

    struct PartitionEntry* table_entry_ptr = (struct PartitionEntry*)&primary_buffer[446];

    /* These will be used to determine which partition is the extended one.*/
    bool is_extended = false;
    int count = 0;
    int extended_partition_index = -1;

    printf("%-13s %-10s %-10s %-10s %-10s %-10s %-10s %-10s\n", "Device", "Boot", "Start", "End", "Sector", "Size", "Id", "Type");

    for(int i = 0 ; i < 4 ; i++) {
        if(table_entry_ptr[i].sector_count == 0)
            continue;
        
        if(table_entry_ptr[i].partition_type == 0x05 || table_entry_ptr[i].partition_type == 0x0f) {
            is_extended = true;
            count++;
            extended_partition_index = i;
        }

        if(count > 1) {
            char* err_msg = "This has more than one extended partition, hence not MBR format.\n";
            write(2, err_msg, strlen(err_msg));
            exit(EXIT_FAILURE);
        }
        
        printf("%s%-5d %-10c %-10u %-10u %-10u %.1fG       %-10X\n", argv[1], i+1, table_entry_ptr[i].status == 0x80 ? '*' : ' ',
			table_entry_ptr[i].lba, table_entry_ptr[i].lba + table_entry_ptr[i].sector_count - 1, table_entry_ptr[i].sector_count,
			(double)table_entry_ptr[i].sector_count*512/(1024*1024*1024), table_entry_ptr[i].partition_type);
    
    }

    /* In the above, we printed the primary partitions; now we shall focus on the extended partition.*/
    close(fd);
    
    if(is_extended) {
        int ext_fd = open(argv[1], O_RDONLY);
        if(ext_fd == -1) {
            char* err_msg = "Opening the given path failed.\n";
            write(2, err_msg, strlen(err_msg));
            exit(EXIT_FAILURE);
        }
        off_t to_extended_lba_bytes = table_entry_ptr[extended_partition_index].lba*512;
        uint64_t to_extended_lba = table_entry_ptr[extended_partition_index].lba;

        off_t check_lseek = lseek(ext_fd, to_extended_lba_bytes, SEEK_SET);
        if(check_lseek == -1) {
            char* err_msg = "lseek failed.\n";
            write(2, err_msg, strlen(err_msg));
            exit(EXIT_FAILURE);
        }
        
        char ext_buff[512];
        ssize_t read_ext_bytes = read(ext_fd, ext_buff, 512);
        if(read_ext_bytes == -1) {
            char* err_msg = "Reading EBR failed.\n";
            write(2, err_msg, strlen(err_msg));
            exit(EXIT_FAILURE);
        }

        struct PartitionEntry* EBR_ptr = (struct PartitionEntry*)&ext_buff[446];

        /* First Entry of this EBR:*/
        uint64_t rel_lba_first_entry = EBR_ptr->lba;
        uint64_t no_sectors_first_entry = EBR_ptr->sector_count;

        //Start and end calculations
        uint64_t absolute_start_log = to_extended_lba + rel_lba_first_entry;
        uint64_t absolute_end_log = absolute_start_log + no_sectors_first_entry - 1;
        
        printf("%s%-5d            %-10lu %-10lu %-10lu %.1fG       \n", argv[1], extended_partition_index+2,
			absolute_start_log, absolute_start_log + no_sectors_first_entry - 1, no_sectors_first_entry,
			(double)no_sectors_first_entry*512/(1024*1024*1024));

        /*Now Let's get the next EBR for next logical partition info:*/
        EBR_ptr++;
        uint64_t rel_addr_next_EBR = EBR_ptr->lba;              //Relative address of next EBR. STARTS from EXTENDED PARTITION
        /* Let's go to the next EBR and read its data:*/
        off_t new_offset_absolute = (to_extended_lba + rel_addr_next_EBR)*512;
        off_t check_lseek2 = lseek(ext_fd, new_offset_absolute, SEEK_SET);
        if(check_lseek2 == -1) {
            char* err_msg = "lseek failed.\n";
            write(2, err_msg, strlen(err_msg));
            exit(EXIT_FAILURE);
        }
        
        struct PartitionEntry* old_ptr = EBR_ptr;               //NOW AT 2nd ENTRY.
        
        int CCount = 0;
        while(1) {
            uint64_t rel_addr_next_EBR = old_ptr->lba;          
            off_t new_offset_absolute = (to_extended_lba + rel_addr_next_EBR)*512;
            off_t check_lseek2 = lseek(ext_fd, new_offset_absolute, SEEK_SET);
            if(check_lseek2 == -1) {
                char* err_msg = "lseek failed.\n";
                write(2, err_msg, strlen(err_msg));
                exit(EXIT_FAILURE);
            }

            char ext2_buff[512];
            ssize_t check_read = read(ext_fd, ext2_buff, 512);
            if(check_read == -1) {
                char* err_msg = "Reading file descriptor.\n";
                write(2, err_msg, strlen(err_msg));
                exit(EXIT_FAILURE);
            }

            struct PartitionEntry* new_ptr = (struct PartitionEntry*)&ext2_buff[446];
            uint64_t rel_lba_first_entry_new = new_ptr->lba;
            uint64_t exact_sectors = old_ptr->sector_count - rel_lba_first_entry_new;
            uint64_t START = new_ptr->lba + to_extended_lba + rel_addr_next_EBR;
    
            printf("%s%-5d            %-10lu %-10lu %-10lu %.1fG       \n", argv[1], extended_partition_index+3+CCount,
			    START, START + exact_sectors - 1, exact_sectors, 
			    (double)exact_sectors*512/(1024*1024*1024));
            new_ptr++;
            old_ptr = new_ptr;
            CCount++;

            if(old_ptr->sector_count == 0) 
                exit(EXIT_SUCCESS);
            
        }

    }
    

    return 0;
}
    
