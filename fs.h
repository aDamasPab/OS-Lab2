#include <iostream>
#include <vector>
#include <cstring>
#include <cstdint>
#include "disk.h"

#ifndef __FS_H__
#define __FS_H__

#define ROOT_BLOCK 0
#define FAT_BLOCK 1
#define FAT_FREE 0
#define FAT_EOF -1

#define TYPE_FILE 0
#define TYPE_DIR 1
#define READ 0x04
#define WRITE 0x02
#define EXECUTE 0x01

struct dir_entry {
    char file_name[56]; // name of the file / sub-directory
    uint32_t size; // size of the file in bytes
    uint16_t first_blk; // index in the FAT for the first block of the file
    uint8_t type; // directory (1) or file (0)
    uint8_t access_rights; // read (0x04), write (0x02), execute (0x01)
};

class FS {
private:
    Disk disk;
    int16_t fat[BLOCK_SIZE/2]; // size of a FAT entry is 2 bytes
    uint16_t current_dir_block = ROOT_BLOCK; // store the current directory block (default is block 0)
    std ::string current_path = "/"; // full path for the pwd command (default is root)

    
    void save_fat(); // saves the FAT from memory to the disk
    int find_free_block(); // finds a free block in the FAT
    int find_file_in_current_dir(const std::string& filename, dir_entry* entries); // finds a file by name in the current directory. Returns index or -1 if not found
    int find_free_entry_in_current_dir(dir_entry* entries); // finds a free entry int the current directory. Returns index or -1 if full
    int parse_path(std::string path, std::string& filename);
    int find_file_in_dir_block(int block, const std::string& filename);

public:
    FS();
    ~FS();
    int format(); // formats the disk, i.e., creates an empty file system
    int create(std::string filepath); // create <filepath> creates a new file on the disk, the data content is written on the following rows (ended with an empty row)
    int cat(std::string filepath); // cat <filepath> reads the content of a file and prints it on the screen
    int ls(); // ls lists the content in the current directory (files and sub-directories)

    int cp(std::string sourcepath, std::string destpath); // cp <sourcepath> <destpath> makes an exact copy of the file <sourcepath> to a new file <destpath>
    int mv(std::string sourcepath, std::string destpath); // mv <sourcepath> <destpath> renames the file <sourcepath> to the name <destpath>, or moves the file <sourcepath> to the directory <destpath> (if dest is a directory)
    int rm(std::string filepath); // rm <filepath> removes / deletes the file <filepath>
    int append(std::string filepath1, std::string filepath2); // append <filepath1> <filepath2> appends the contents of file <filepath1> to the end of file <filepath2>. The file <filepath1> is unchanged.

    int mkdir(std::string dirpath); // mkdir <dirpath> creates a new sub-directory with the name <dirpath> in the current directory
    int cd(std::string dirpath); // cd <dirpath> changes the current (working) directory to the directory named <dirpath>
    int pwd(); // pwd prints the full path, i.e., from the root directory, to the current directory, including the current directory name

    int chmod(std::string accessrights, std::string filepath); // chmod <accessrights> <filepath> changes the access rights for the file <filepath> to <accessrights>.
};

#endif // __FS_H__
