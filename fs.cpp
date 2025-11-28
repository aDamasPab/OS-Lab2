#include <iostream>
#include <vector>
#include <cstring>
#include <string>
#include <algorithm>
#include "fs.h"

// ============ PRIVATE AUXILIARY METHODS ============

void FS::save_fat()
{
    // FAT is always stored in block 1
    disk.write(FAT_BLOCK, (uint8_t *)fat);
}

int FS::find_free_block()
{
    // find form second block (block 0 is root, block 1 is FAT)
    for (int i = 2; i < disk.get_no_blocks(); ++i)
    {
        if (fat[i] == FAT_FREE)
        {
            return i;
        }
    }
    // no free block found
    return -1;
}

int FS::find_file_in_current_dir(const std::string &filename, dir_entry *entries)
{
    int max_entries = BLOCK_SIZE / sizeof(dir_entry);
    for (int i = 0; i < max_entries; ++i)
    {
        // check if entry is used
        if (entries[i].file_name[0] != '\0')
        {
            if (filename == std::string(entries[i].file_name))
            {
                return i;
            }
        }
    }
    // file not found
    return -1;
}

int FS::find_free_entry_in_current_dir(dir_entry *entries)
{
    int max_entries = BLOCK_SIZE / sizeof(dir_entry);
    for (int i = 0; i < max_entries; ++i)
    {
        // check if entry is free
        if (entries[i].file_name[0] == '\0')
        {
            return i;
        }
    }
    // no free entry found
    return -1;
}

// ================= PUBLIC METHODS ==================

FS::FS()
{
    std::cout << "FS::FS()... Creating file system\n";
    disk.read(FAT_BLOCK, (uint8_t *)fat); // read FAT form disk
    current_dir_block = ROOT_BLOCK;       // set current directory to root
}

FS::~FS()
{
}

// formats the disk, i.e., creates an empty file system
int FS::format()
{
    // initialize FAT
    for (unsigned i = 0; i < disk.get_no_blocks(); ++i)
    {
        fat[i] = FAT_FREE;
    }
    // reserve block 0 (root) and block 1 (FAT)
    fat[ROOT_BLOCK] = FAT_EOF;
    fat[FAT_BLOCK] = FAT_EOF;
    save_fat();

    // clean root directory (block 0)
    uint8_t zero_block[BLOCK_SIZE];
    memset(zero_block, 0, BLOCK_SIZE);
    disk.write(ROOT_BLOCK, zero_block);

    // restart current position
    current_dir_block = ROOT_BLOCK;

    return 0;
}

// create <filepath> creates a new file on the disk, the data content is
// written on the following rows (ended with an empty row)
int FS::create(std::string filepath)
{
    // check filename length
    if (filepath.length() > 55)
    {
        std::cout << "FS::create(): Error - filename too long\n";
        return -1;
    }

    // read current directory
    uint8_t dir_buffer[BLOCK_SIZE];
    disk.read(current_dir_block, dir_buffer);
    dir_entry *entries = (dir_entry *)dir_buffer;

    // check if file already exists
    if (find_file_in_current_dir(filepath, entries) != -1)
    {
        std::cout << "FS::create(): Error - file already exists\n";
        return -1;
    }

    // find free entry in current directory
    int entry_idx = find_free_entry_in_current_dir(entries);
    if (entry_idx == -1)
    {
        std::cout << "FS::create(): Error - no free directory entry\n";
        return -1;
    }

    // read user input
    std::string content = "";
    std::string line;

    while (std::getline(std::cin, line) && !line.empty())
    {
        content += line + "\n";
    }

    // calculate number of blocks needed
    uint32_t file_size = content.size();
    int blocks_needed = (file_size + BLOCK_SIZE - 1) / BLOCK_SIZE;
    if (file_size == 0)
        blocks_needed = 0; // empty file

    int first_block = FAT_EOF;
    int previous_block = -1;

    if (blocks_needed > 0)
    {
        int bytes_written = 0;

        for (unsigned i = 0; i < blocks_needed; ++i)
        {
            int new_block = find_free_block();
            if (new_block == -1)
            {
                std::cout << "FS::create(): Error - not enough disk space\n";
                return -2; // no free block available
            }

            fat[new_block] = FAT_EOF; // mark new block as end of file for now

            if (first_block == FAT_EOF)
                first_block = new_block;
            if (previous_block != -1)
                fat[previous_block] = new_block; // link previous block to new block

            // prepare data to the block
            uint8_t data_buffer[BLOCK_SIZE];
            memset(data_buffer, 0, BLOCK_SIZE);

            // write data to block
            int chunk_size = std::min((int)BLOCK_SIZE, (int)(file_size - bytes_written));
            memcpy(data_buffer, content.c_str() + bytes_written, chunk_size);

            disk.write(new_block, data_buffer);

            previous_block = new_block;
            bytes_written += chunk_size;
        }
    }
    else
    {
        // empty file
        first_block = FAT_EOF;
    }

    // save updated FAT
    save_fat();

    // update directory entry
    strncpy(entries[entry_idx].file_name, filepath.c_str(), 55);
    entries[entry_idx].file_name[55] = '\0'; //
    entries[entry_idx].size = file_size;
    entries[entry_idx].first_blk = (first_block == FAT_BLOCK) ? 0 : first_block; // 0 or FAT_EOF representation
    entries[entry_idx].type = TYPE_FILE;
    entries[entry_idx].access_rights = READ | WRITE; // rw-

    // write updated direcory into the disk
    disk.write(current_dir_block, dir_buffer);

    return 0;
}

// cat <filepath> reads the content of a file and prints it on the screen
int FS::cat(std::string filepath)
{
    // read current directory
    uint8_t dir_buffer[BLOCK_SIZE];
    disk.read(current_dir_block, dir_buffer);
    dir_entry *entries = (dir_entry *)dir_buffer;

    // find file in current directory
    int idx = find_file_in_current_dir(filepath, entries);
    if (idx == -1)
    {
        std::cout << "FS::cat(): Error - file not found\n";
        return -1;
    }

    // verify that it is a file
    if (entries[idx].type != TYPE_FILE)
    {
        std::cout << "FS::cat(): Error - not a file\n";
        return -1;
    }

    // read blocks following the FAT chain
    uint16_t current_block = entries[idx].first_blk;
    uint32_t remaining_size = entries[idx].size;

    if (remaining_size == 0 || current_block == 0 || current_block == FAT_EOF)
        return 0; // empty file

    uint8_t data_buffer[BLOCK_SIZE];

    while (current_block != FAT_EOF && remaining_size > 0)
    {
        disk.read(current_block, data_buffer);
        int to_print = std::min((uint32_t)BLOCK_SIZE, remaining_size);
        std::cout.write((char *)data_buffer, to_print);
        remaining_size -= to_print;
        current_block = fat[current_block]; // next block in FAT chain
    }

    std::cout << std::endl;

    return 0;
}

// ls lists the content in the currect directory (files and sub-directories)
int FS::ls()
{
    // read current directory
    uint8_t dir_buffer[BLOCK_SIZE];
    if (disk.read(current_dir_block, dir_buffer) != 0)
    {
        std::cout << "FS::ls(): Error - could not read current directory\n";
        return -1;
    }

    dir_entry *entries = (dir_entry *)dir_buffer;
    int max_entries = BLOCK_SIZE / sizeof(dir_entry);

    std::cout << "name\tsize\n";
    for (unsigned i = 0; i < max_entries; ++i)
    {
        if (entries[i].file_name[0] != '\0')
            std::cout << entries[i].file_name << "\t" << entries[i].size << "\n";
    }

    return 0;
}

// cp <sourcepath> <destpath> makes an exact copy of the file
// <sourcepath> to a new file <destpath>
int FS::cp(std::string sourcepath, std::string destpath)
{
    // read current directory
    uint8_t dir_buffer[BLOCK_SIZE];
    disk.read(current_dir_block, dir_buffer);
    dir_entry* entries = (dir_entry*)dir_buffer;

    // find original file
    int src_idx = find_file_in_current_dir(sourcepath, entries);
    if (src_idx == -1) {
        std::cout << "FS::cp(): Error - source file not found\n";
        return -1;
    }

    // verify destination file does not already exist
    if (find_file_in_current_dir(destpath, entries) != -1) {
        std::cout << "FS::cp(): Error - destination file already exists\n";
        return -1;
    }

    // find free entry for destination file
    int dest_idx = find_free_entry_in_current_dir(entries);
    if (dest_idx == -1) {
        std::cout << "FS::cp(): Error - no free directory entry for destination file\n";
        return -1;
    }

    // copy data block by block
    uint16_t src_block = entries[src_idx].first_blk;
    int destination_first_block = FAT_EOF;
    int previous_block = -1;
    uint32_t file_size = entries[src_idx].size;

    // only proceed if file is not empty
    if (file_size > 0 && src_block != FAT_EOF && src_block != 0) {
        uint8_t buffer[BLOCK_SIZE];
        
        while (src_block != FAT_EOF) {
            // read source block
            disk.read(src_block, buffer);

            // find free block for destination
            int new_block = find_free_block();
            if (new_block == -1) {
                std::cout << "FS::cp(): Error - not enough disk space for destination file\n";  
                return -2;
            }

            fat[new_block] = FAT_EOF; // mark new block as end of file for now

            disk.write(new_block, buffer);

            // link FAT
            if (destination_first_block == FAT_EOF)
                destination_first_block = new_block;
            if (previous_block != -1)
                fat[previous_block] = new_block;
        }
    }

    // save FAT
    save_fat();

    // create directory entry for destination file
    strncpy(entries[dest_idx].file_name, destpath.c_str(), 55);
    entries[dest_idx].file_name[55] = '\0';
    entries[dest_idx].size = entries[src_idx].size;
    entries[dest_idx].type = entries[src_idx].type;
    entries[dest_idx].access_rights = entries[src_idx].access_rights;
    entries[dest_idx].first_blk = (destination_first_block == FAT_EOF) ? 0 : destination_first_block;

    // write updated directory back to disk
    disk.write(current_dir_block, dir_buffer);

    return 0;
}

// mv <sourcepath> <destpath> renames the file <sourcepath> to the name <destpath>,
// or moves the file <sourcepath> to the directory <destpath> (if dest is a directory)
int FS::mv(std::string sourcepath, std::string destpath)
{
    std::cout << "FS::mv(" << sourcepath << "," << destpath << ")\n";
    return 0;
}

// rm <filepath> removes / deletes the file <filepath>
int FS::rm(std::string filepath)
{
    std::cout << "FS::rm(" << filepath << ")\n";
    return 0;
}

// append <filepath1> <filepath2> appends the contents of file <filepath1> to
// the end of file <filepath2>. The file <filepath1> is unchanged.
int FS::append(std::string filepath1, std::string filepath2)
{
    std::cout << "FS::append(" << filepath1 << "," << filepath2 << ")\n";
    return 0;
}

// mkdir <dirpath> creates a new sub-directory with the name <dirpath>
// in the current directory
int FS::mkdir(std::string dirpath)
{
    std::cout << "FS::mkdir(" << dirpath << ")\n";
    return 0;
}

// cd <dirpath> changes the current (working) directory to the directory named <dirpath>
int FS::cd(std::string dirpath)
{
    std::cout << "FS::cd(" << dirpath << ")\n";
    return 0;
}

// pwd prints the full path, i.e., from the root directory, to the current
// directory, including the currect directory name
int FS::pwd()
{
    std::cout << "FS::pwd()\n";
    return 0;
}

// chmod <accessrights> <filepath> changes the access rights for the
// file <filepath> to <accessrights>.
int FS::chmod(std::string accessrights, std::string filepath)
{
    std::cout << "FS::chmod(" << accessrights << "," << filepath << ")\n";
    return 0;
}
