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
/*
struct EBR {
    uint8_t boot_region[446];
    struct PartitionEntry first_entry;              
    struct PartitionEntry second_entry;
    struct PartitionEntry third_entry;              //Typically Empty.
    struct PartitionEntry fourth_entry;             //Typically Empty.
    uint8_t magic_region[2];       
};
*/

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
        long unsigned int to_extended_lba = table_entry_ptr[extended_partition_index].lba;

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
        long unsigned int rel_lba_first_entry = EBR_ptr->lba;
        long unsigned int no_sectors_first_entry = EBR_ptr->sector_count;

        //Start and end calculations
        long unsigned int absolute_start_first_ebr_lba = rel_lba_first_entry + to_extended_lba;
        long unsigned int absolute_end_first_ebr_lba = absolute_start_first_ebr_lba + no_sectors_first_entry - 1;
        printf("\n FIRST LOGICAL PARTITION INFO:\n");
        printf("Start: %lu ; End: %lu ; Sectors: %lu", absolute_start_first_ebr_lba, absolute_end_first_ebr_lba, no_sectors_first_entry);

        /*Now Let's get the next EBR and logical partition info:*/
        EBR_ptr++;
        
        long unsigned int rel_addr_next_EBR = EBR_ptr->lba;
        /* Let's go to the next EBR and read its data:*/
        int fdd = open(argv[1], O_RDONLY);
        long unsigned int abs_lba_next_ebr = table_entry_ptr[extended_partition_index].lba + rel_addr_next_EBR;
        off_t snd_offset = abs_lba_next_ebr*512;
        lseek(fdd, snd_offset, SEEK_SET);
        char ext2_buff[512];
        read(fdd, ext2_buff, 512);
        struct PartitionEntry* ptr2 = (struct PartitionEntry*)&ext2_buff[446];
        long unsigned int rel_lba_first_entry2 = ptr2->lba;

        long unsigned int exact_sectors = EBR_ptr->sector_count - rel_lba_first_entry2;
        printf("\nSECTORS: %lu\n", exact_sectors);
        long unsigned int START_2nd_log = ptr2->lba + abs_lba_next_ebr;
        printf("START: %lu\n", START_2nd_log);
        printf("END: %lu\n", START_2nd_log + exact_sectors -1);
    }
    

    return 0;
}
    
