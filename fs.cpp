#include <iostream>
#include <vector>
#include <cstring>
#include <string>
#include <algorithm>
#include <sstream>
#include "fs.h"

// ============ PRIVATE AUXILIARY METHODS ============

void FS::save_fat()
{
    // FAT is always stored in block 1
    disk.write(FAT_BLOCK, (uint8_t *)fat);
}

int FS::find_free_block()
{
    // find from second block (block 0 is root, block 1 is FAT)
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

uint16_t FS::resolve_path(const std::string& path, std::string& final_name, uint16_t& parent_block) {
    if (path.empty()) return (uint16_t)-1;

    // 1. Determine the starting point (Start Node)
    uint16_t current_block;
    std::string working_path = path;
    
    if (working_path.front() == '/') {
        current_block = ROOT_BLOCK;
        // If the path is just "/", return the root block.
        if (working_path.length() == 1) {
            final_name = "";
            parent_block = ROOT_BLOCK;
            return ROOT_BLOCK;
        }
        working_path = working_path.substr(1); // Skip the initial '/'
    } else {
        current_block = current_dir_block; // Relative path, start from CWD
    }

    // 2. Split the path into components
    // Use a C-style buffer for strtok_r
    char path_copy[1024];
    strncpy(path_copy, working_path.c_str(), sizeof(path_copy) - 1);
    path_copy[sizeof(path_copy) - 1] = '\0';

    char* component;
    char* saveptr;
    
    std::vector<std::string> components;
    component = strtok_r(path_copy, "/", &saveptr);
    while (component != NULL) {
        // Skip empty components resulting from multiple slashes (e.g., "a//b")
        if (strlen(component) > 0) {
            components.push_back(component);
        }
        component = strtok_r(NULL, "/", &saveptr);
    }
    
    if (components.empty()) {
        // This handles cases like: "", "/", "./" (if working_path was ".")
        final_name = "";
        parent_block = current_block;
        return current_block;
    }
    
    uint16_t target_block = current_block; 
    
    // 3. Traverse all components, except the last one (which is the target entry name)
    for (size_t i = 0; i < components.size() - 1; ++i) {
        std::string name = components[i];
        
        // Read the current directory block
        uint8_t dir_buffer[BLOCK_SIZE];
        if (disk.read(target_block, dir_buffer) < 0) return (uint16_t)-1;
        dir_entry* entries = (dir_entry*)dir_buffer;
        int max_entries = BLOCK_SIZE / sizeof(dir_entry);
        
        // Find the next component name in the current directory
        int found_idx = -1;
        for (int j = 0; j < max_entries; ++j) {
            if (entries[j].file_name[0] != '\0' && name == std::string(entries[j].file_name)) {
                found_idx = j;
                break;
            }
        }

        if (found_idx == -1) {
            // Path invalid: component 'name' not found during navigation.
            return (uint16_t)-1;
        }

        // Only directories can be navigated
        if (entries[found_idx].type != TYPE_DIR) {
            return (uint16_t)-1;
        }
        
        // Move to the next block (directory). This handles '.' and '..' correctly
        // because their first_blk points to the correct block.
        target_block = entries[found_idx].first_blk;
    }

    // 4. Handle the final component (the target entry itself)
    final_name = components.back();
    parent_block = target_block; // The last block we successfully navigated to is the parent

    // Read the parent directory block
    uint8_t dir_buffer[BLOCK_SIZE];
    if (disk.read(parent_block, dir_buffer) < 0) return (uint16_t)-1;
    dir_entry* entries = (dir_entry*)dir_buffer;
    int max_entries = BLOCK_SIZE / sizeof(dir_entry);
    
    // Search for the final name in the parent directory
    for (int j = 0; j < max_entries; ++j) {
        if (entries[j].file_name[0] != '\0' && final_name == std::string(entries[j].file_name)) {
            // Found: return the final entry's block index.
            return entries[j].first_blk;
        }
    }
    
    // The final component doesn't exist, but the parent directory does.
    return (uint16_t)-2;
    
// ================= PUBLIC METHODS ==================

FS::FS()
{
    std::cout << "FS::FS()... Creating file system\n";
    disk.read(FAT_BLOCK, (uint8_t *)fat); // read FAT form disk
    current_dir_block = ROOT_BLOCK;       // set current directory to root
    current_path = "/";
}

FS::~FS()
{
}

// formats the disk, i.e., creates an empty file system
int FS::format()
{
    // 1. Clear Root Directory (Block 0)
    uint8_t zero_block[BLOCK_SIZE] = {0};
    
    // Initialize FAT
    for (int i = 0; i < BLOCK_SIZE/2; ++i) {
        fat[i] = FAT_FREE;
    }
    fat[ROOT_BLOCK] = FAT_EOF;
    fat[FAT_BLOCK] = FAT_EOF;
    save_fat();

    // 2. Initialize Root Directory Entries (., ..)
    dir_entry* root_entries = (dir_entry*)zero_block;
    
    // . (self)
    strncpy(root_entries[0].file_name, ".", 56);
    root_entries[0].size = 0;
    root_entries[0].first_blk = ROOT_BLOCK; 
    root_entries[0].type = TYPE_DIR;
    root_entries[0].access_rights = READ | WRITE | EXECUTE;

    // .. (parent) - In the root directory, '..' points to '.' (itself)
    strncpy(root_entries[1].file_name, "..", 56);
    root_entries[1].size = 0;
    root_entries[1].first_blk = ROOT_BLOCK; 
    root_entries[1].type = TYPE_DIR;
    root_entries[1].access_rights = READ | WRITE | EXECUTE;

    // 3. Save Root
    if (disk.write(ROOT_BLOCK, (uint8_t*)root_entries) < 0) return -1;

    // 4. Reset CWD state
    current_dir_block = ROOT_BLOCK;
    current_path = "/";

    return 0;
}

// create <filepath> creates a new file on the disk
int FS::create(std::string filepath)
{
    std::string target_name;
    uint16_t parent_blk;

    // 1. Resolve the path. We expect the target file NOT to exist, 
    //    so resolve_path should return (uint16_t)-2.
    //    parent_blk will hold the block index of the parent directory.
    uint16_t target_blk = resolve_path(filepath, target_name, parent_blk);

    if (target_blk != (uint16_t)-2) {
        if (target_blk == (uint16_t)-1) {
            std::cout << "FS::create(): Error - invalid path to parent directory.\n";
            return -1;
        }
        // If target_blk is not -2, the entry already exists.
        std::cout << "FS::create(): Error - file '" << filepath << "' already exists.\n";
        return -1;
    }

    // 2. Basic checks
    if (target_name.empty() || target_name.length() >= 56) {
        std::cout << "FS::create(): Error - invalid or too long file name.\n";
        return -1;
    }

    // 3. Read the parent directory block
    uint8_t parent_buf[BLOCK_SIZE];
    if (disk.read(parent_blk, parent_buf) < 0) {
        std::cout << "FS::create(): Error reading parent directory.\n";
        return -1;
    }
    dir_entry* parent_entries = (dir_entry*)parent_buf;

    // 4. Find a free directory entry slot in the parent block
    int parent_free_idx = find_free_entry_in_current_dir(parent_entries);
    if (parent_free_idx == -1) {
        std::cout << "FS::create(): Error - parent directory is full.\n";
        return -1;
    }

    // 5. Find the first free data block for file content
    int first_data_block = find_free_block();
    if (first_data_block == -1) {
        std::cout << "FS::create(): Error - disk is full, no free data block.\n";
        return -1;
    }

    // 6. Initialize and write the new file's directory entry
    dir_entry new_entry;
    strncpy(new_entry.file_name, target_name.c_str(), 56);
    new_entry.file_name[55] = '\0'; // Ensure null termination
    new_entry.size = 0;
    new_entry.first_blk = first_data_block;
    new_entry.type = TYPE_FILE;
    new_entry.access_rights = READ | WRITE; // Default access rights

    // 7. Write file content from stdin to disk blocks
    uint16_t current_block = first_data_block;
    uint32_t total_size = 0;
    uint8_t data_buffer[BLOCK_SIZE];
    
    // Read and write content block by block
    while (std::cin.good()) {
        std::cin.read((char*)data_buffer, BLOCK_SIZE);
        int bytes_read = std::cin.gcount(); // Number of bytes actually read

        if (bytes_read > 0) {
            disk.write(current_block, data_buffer);
            total_size += bytes_read;
            
            // Check if we need more blocks
            if (bytes_read == BLOCK_SIZE) {
                int next_block = find_free_block();
                if (next_block == -1) {
                    // Out of disk space: mark current block as EOF and break
                    fat[current_block] = FAT_EOF;
                    break; 
                }
                
                // Link current block to the next block
                fat[current_block] = next_block;
                current_block = next_block;
            } else {
                // Last block written, mark as EOF
                fat[current_block] = FAT_EOF;
            }
        } else {
            // End of input (EOF or error)
            if (total_size == 0) {
                 // If no content was written, the block is still free. 
                 // Mark the block as free to avoid disk corruption.
                 fat[first_data_block] = FAT_FREE;
            } else {
                // Mark the last block used as EOF
                fat[current_block] = FAT_EOF;
            }
            break;
        }
    }
    
    // 8. Finalize: Update entry size and write directory/FAT
    new_entry.size = total_size;
    
    // Update the directory entry in the parent block
    parent_entries[parent_free_idx] = new_entry;
    disk.write(parent_blk, parent_buf);

    // Save the modified FAT to disk
    save_fat();

    return 0;
}

// cat <filepath> reads the content of a file and prints it on the screen
int FS::cat(std::string filepath)
{
    std::string target_name;
    uint16_t parent_blk;

    // 1. Resolve the path
    uint16_t target_blk = resolve_path(filepath, target_name, parent_blk);

    if (target_blk == (uint16_t)-1 || target_blk == (uint16_t)-2) {
        std::cout << "FS::cat(): Error - file not found or path is invalid.\n";
        return -1;
    }
    
    // 2. Read the parent directory entry to get metadata (type and size)
    uint8_t parent_buf[BLOCK_SIZE];
    disk.read(parent_blk, parent_buf);
    dir_entry* entries = (dir_entry*)parent_buf;
    int max_entries = BLOCK_SIZE / sizeof(dir_entry);
    
    int target_idx = -1;
    dir_entry target_entry;
    
    for(int i = 0; i < max_entries; ++i) {
        if (entries[i].file_name[0] != '\0' && target_name == std::string(entries[i].file_name)) {
            target_idx = i;
            target_entry = entries[i]; // Copy the entry data
            break;
        }
    }
    
    if (target_idx == -1) {
        std::cout << "FS::cat(): Error - Internal error finding entry.\n";
        return -1;
    }

    // 3. Verify that it is a file
    if (target_entry.type != TYPE_FILE)
    {
        std::cout << "FS::cat(): Error - not a file\n";
        return -1;
    }

    // 4. Continue with reading blocks
    uint16_t current_block = target_entry.first_blk;
    uint32_t remaining_size = target_entry.size;

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
    if (disk.read(current_dir_block, dir_buffer) < 0)
    {
        std::cout << "FS::ls(): Error - could not read current directory\n";
        return -1;
    }

    dir_entry * entries = (dir_entry *)dir_buffer;
    int max_entries = BLOCK_SIZE / sizeof(dir_entry);

    // Print header
    std::cout << "name\ttype\tsize" << std::endl;
    std::cout << "----------------------" << std::endl;

    for (int i = 0; i < max_entries; ++i) {
        if (entries[i].file_name[0] != '\0') {
            std::string name = entries[i].file_name;
            if (name == "." || name == "..") continue; // Hide special entries for clean output

            std::cout << name << "\t";
            
            if (entries[i].type == TYPE_DIR) {
                std::cout << "dir\t-\t" << std::endl;
            } else { // TYPE_FILE
                std::cout << "file\t" << entries[i].size << std::endl;
            }
        }
    }

    return 0;
}

// cp <sourcepath> <destpath> makes an exact copy of the file
int FS::cp(std::string sourcepath, std::string destpath)
{
    // --- 1. Resolve Source Path ---
    std::string src_name; uint16_t src_parent_blk;
    uint16_t src_blk = resolve_path(sourcepath, src_name, src_parent_blk);
    
    if (src_blk == (uint16_t)-1 || src_blk == (uint16_t)-2) { 
        std::cout << "FS::cp(): Error - source file '" << sourcepath << "' not found or path invalid.\n"; 
        return -1; 
    }
    
    // Get source entry metadata
    uint8_t src_parent_buf[BLOCK_SIZE];
    disk.read(src_parent_blk, src_parent_buf);
    dir_entry* src_parent_entries = (dir_entry*)src_parent_buf;
    int src_idx = get_entry_by_name(src_parent_entries, src_name);
    
    if (src_idx == -1 || src_parent_entries[src_idx].type != TYPE_FILE) {
        std::cout << "FS::cp(): Error - source '" << sourcepath << "' is not a file.\n";
        return -1;
    }
    dir_entry src_entry = src_parent_entries[src_idx];
    
    // --- 2. Resolve Destination Path ---
    std::string dest_name; uint16_t dest_parent_blk;
    uint16_t dest_blk = resolve_path(destpath, dest_name, dest_parent_blk);
    
    if (dest_parent_blk == (uint16_t)-1) {
        std::cout << "FS::cp(): Error - destination parent path invalid.\n"; 
        return -1;
    }

    // Determine the final name and parent block for the new entry
    uint16_t final_parent_blk = dest_parent_blk;
    std::string final_name = dest_name;
    int dest_free_idx = -1;
    bool dest_exists = (dest_blk != (uint16_t)-2);

    if (dest_exists) {
        // If destination exists, check if it's a directory
        uint8_t dest_buf[BLOCK_SIZE];
        disk.read(dest_parent_blk, dest_buf);
        dir_entry* dest_entries = (dir_entry*)dest_buf;
        int dest_idx = get_entry_by_name(dest_entries, dest_name);

        if (dest_entries[dest_idx].type == TYPE_DIR) {
            // Case A: destpath is an existing directory. Copy into it.
            final_parent_blk = dest_blk;
            final_name = src_name;
            
            // Re-read the new parent directory block
            disk.read(final_parent_blk, dest_buf);
            dest_entries = (dir_entry*)dest_buf;
            
            // Check if file already exists in the destination directory
            if (get_entry_by_name(dest_entries, final_name) != -1) {
                 std::cout << "FS::cp(): Error - file '" << final_name << "' already exists in destination directory.\n";
                 return -1;
            }
            dest_free_idx = find_free_entry_in_current_dir(dest_entries);
        } else {
            // Case B: destpath is an existing file. Overwrite/replace it.
            final_parent_blk = dest_parent_blk;
            dest_free_idx = dest_idx; // Use the existing index

            // First, free the blocks of the old destination file
            uint16_t block_to_free = dest_entries[dest_idx].first_blk;
            while (block_to_free != FAT_EOF && block_to_free >= 2) {
                uint16_t next = fat[block_to_free];
                fat[block_to_free] = FAT_FREE;
                block_to_free = next;
            }
        }
    } else {
        // Case C: destpath does not exist. Create new entry in dest_parent_blk.
        uint8_t dest_parent_buf_new[BLOCK_SIZE];
        disk.read(dest_parent_blk, dest_parent_buf_new);
        dir_entry* dest_entries = (dir_entry*)dest_parent_buf_new;
        dest_free_idx = find_free_entry_in_current_dir(dest_entries);
    }
    
    if (dest_free_idx == -1) {
        std::cout << "FS::cp(): Error - destination directory is full.\n";
        return -1;
    }
    
    // --- 3. Copy Data Blocks and Update FAT ---
    uint16_t current_src_block = src_entry.first_blk;
    uint16_t new_first_block = 0;
    uint16_t prev_dest_block = 0;
    uint32_t remaining_size = src_entry.size;
    
    uint8_t data_buffer[BLOCK_SIZE];
    
    while (current_src_block != FAT_EOF && remaining_size > 0) {
        // Read from source
        disk.read(current_src_block, data_buffer);
        
        // Find a new block for destination
        int new_block = find_free_block();
        if (new_block == -1) {
            std::cout << "FS::cp(): Error - disk full during copy. File partially copied.\n";
            // Clean up blocks allocated so far for the destination before returning -1
            // (Cleanup logic is omitted here for simplicity but is required for robustness)
            return -1; 
        }
        
        // Write to destination
        disk.write(new_block, data_buffer);
        
        // Link blocks in FAT
        if (prev_dest_block != 0) {
            fat[prev_dest_block] = new_block;
        } else {
            new_first_block = new_block; // Set the first block index
        }
        prev_dest_block = new_block;
        
        // Move to next source block
        remaining_size -= std::min((uint32_t)BLOCK_SIZE, remaining_size);
        current_src_block = fat[current_src_block];
    }
    
    // Mark the last destination block as EOF
    if (new_first_block != 0) {
        fat[prev_dest_block] = FAT_EOF;
    } else {
        // Source was an empty file, new_first_block remains 0. Mark block as free if it was reserved.
        // The first block check should handle this, but for robustness:
        if (src_entry.size > 0 && prev_dest_block != 0) {
            fat[prev_dest_block] = FAT_EOF;
        }
    }

    // --- 4. Create/Update Destination Directory Entry ---
    dir_entry new_entry = src_entry; // Copy most fields
    strncpy(new_entry.file_name, final_name.c_str(), 56);
    new_entry.file_name[55] = '\0';
    new_entry.first_blk = new_first_block;
    // Size remains the same (src_entry.size)
    
    uint8_t final_parent_buf[BLOCK_SIZE];
    disk.read(final_parent_blk, final_parent_buf);
    dir_entry* final_parent_entries = (dir_entry*)final_parent_buf;
    
    final_parent_entries[dest_free_idx] = new_entry;
    disk.write(final_parent_blk, final_parent_buf);
    
    save_fat();

    return 0;
}

// mv <sourcepath> <destpath> renames the file <sourcepath>
int FS::mv(std::string sourcepath, std::string destpath)
{
    // --- 1. Resolve Source Path ---
    std::string src_name; uint16_t src_parent_blk;
    uint16_t src_blk = resolve_path(sourcepath, src_name, src_parent_blk);

    if (src_blk == (uint16_t)-1 || src_blk == (uint16_t)-2) { 
        std::cout << "FS::mv(): Error - source '" << sourcepath << "' not found.\n"; return -1; 
    }
    if (src_name == "." || src_name == "..") {
        std::cout << "FS::mv(): Error - cannot move special directory entries.\n"; return -1;
    }
    
    // Get source entry metadata
    uint8_t src_parent_buf[BLOCK_SIZE];
    disk.read(src_parent_blk, src_parent_buf);
    dir_entry* src_parent_entries = (dir_entry*)src_parent_buf;
    int src_idx = get_entry_by_name(src_parent_entries, src_name);
    dir_entry src_entry = src_parent_entries[src_idx];

    // --- 2. Resolve Destination Path ---
    std::string dest_name; uint16_t dest_parent_blk;
    uint16_t dest_blk = resolve_path(destpath, dest_name, dest_parent_blk);
    
    if (dest_parent_blk == (uint16_t)-1) {
        std::cout << "FS::mv(): Error - destination path invalid.\n"; return -1;
    }
    
    // Check if the source and destination are the same
    if (src_parent_blk == dest_parent_blk && src_name == dest_name && dest_blk != (uint16_t)-2) {
        return 0; // Move to self: no operation needed
    }

    // --- 3. Determine Final Action ---
    uint16_t final_parent_blk = dest_parent_blk;
    std::string final_name = dest_name;
    int dest_target_idx = -1; // Index in the final parent block
    
    uint8_t final_parent_buf[BLOCK_SIZE];
    disk.read(dest_parent_blk, final_parent_buf);
    dir_entry* final_parent_entries = (dir_entry*)final_parent_buf;

    bool dest_exists = (dest_blk != (uint16_t)-2);

    if (dest_exists) {
        // Case A: Destination exists.
        if (final_parent_entries[get_entry_by_name(final_parent_entries, dest_name)].type == TYPE_DIR) {
            // A1: Destination is a directory. Move INTO the directory (rename/move).
            final_parent_blk = dest_blk;
            final_name = src_name; // Keep the original name
            
            // Read the new parent directory
            disk.read(final_parent_blk, final_parent_buf);
            final_parent_entries = (dir_entry*)final_parent_buf;

            // Check if file already exists in the destination directory
            if (get_entry_by_name(final_parent_entries, final_name) != -1) {
                 std::cout << "FS::mv(): Error - '" << final_name << "' already exists in destination directory.\n";
                 return -1;
            }
            dest_target_idx = find_free_entry_in_current_dir(final_parent_entries);
            
        } else {
            // A2: Destination is a file/non-directory. Overwrite/replace (delete dest, use its index).
            if (src_entry.type == TYPE_DIR) {
                std::cout << "FS::mv(): Error - cannot overwrite file with a directory.\n"; return -1;
            }
            // Free blocks of the old destination file
            int dest_old_idx = get_entry_by_name(final_parent_entries, dest_name);
            uint16_t block_to_free = final_parent_entries[dest_old_idx].first_blk;
            while (block_to_free != FAT_EOF && block_to_free >= 2) {
                uint16_t next = fat[block_to_free];
                fat[block_to_free] = FAT_FREE;
                block_to_free = next;
            }
            dest_target_idx = dest_old_idx; // Use the old index
        }
    } else {
        // Case B: Destination does not exist. Rename or move into a new entry slot.
        dest_target_idx = find_free_entry_in_current_dir(final_parent_entries);
    }
    
    if (dest_target_idx == -1) {
        std::cout << "FS::mv(): Error - destination directory is full.\n";
        return -1;
    }

    // --- 4. Move Entry and Update Directory ---
    dir_entry new_entry = src_entry;
    strncpy(new_entry.file_name, final_name.c_str(), 56);
    new_entry.file_name[55] = '\0';

    // Update the '..' pointer in the source directory if it's a directory being moved to a new parent
    if (src_entry.type == TYPE_DIR && src_parent_blk != final_parent_blk) {
        uint8_t src_dir_buf[BLOCK_SIZE];
        disk.read(src_blk, src_dir_buf); // src_blk is the block index of the source directory itself
        dir_entry* src_dir_entries = (dir_entry*)src_dir_buf;
        
        // Find ".." entry (should be at index 1)
        if (std::string(src_dir_entries[1].file_name) == "..") {
            src_dir_entries[1].first_blk = final_parent_blk; // Update '..' to point to the new parent
            disk.write(src_blk, src_dir_buf);
        }
    }
    
    // Write new entry into destination parent block
    final_parent_entries[dest_target_idx] = new_entry;
    disk.write(final_parent_blk, final_parent_buf);
    
    // Clear old entry from source parent block
    memset(&src_parent_entries[src_idx], 0, sizeof(dir_entry));
    disk.write(src_parent_blk, src_parent_buf);

    save_fat(); // FAT entries (file content chain) remain unchanged

    return 0;
}

// rm <filepath> removes / deletes the file <filepath>
int FS::rm(std::string filepath)
{
    std::string target_name;
    uint16_t parent_blk;

    // 1. Resolve the path. We expect the target entry to exist.
    uint16_t target_blk = resolve_path(filepath, target_name, parent_blk);

    if (target_blk == (uint16_t)-1 || target_blk == (uint16_t)-2) {
        std::cout << "FS::rm(): Error - file or directory '" << filepath << "' not found.\n";
        return -1;
    }

    // 2. Read the parent directory block
    uint8_t parent_buf[BLOCK_SIZE];
    if (disk.read(parent_blk, parent_buf) < 0) return -1;
    dir_entry* entries = (dir_entry*)parent_buf;
    int max_entries = BLOCK_SIZE / sizeof(dir_entry);
    
    // 3. Find the entry index in the parent block
    int target_idx = -1;
    for(int i = 0; i < max_entries; ++i) {
        if (entries[i].file_name[0] != '\0' && target_name == std::string(entries[i].file_name)) {
            target_idx = i;
            break;
        }
    }
    
    if (target_idx == -1) {
        std::cout << "FS::rm(): Error - internal error finding entry.\n";
        return -1;
    }
    
    dir_entry entry_to_remove = entries[target_idx];
    
    // 4. Check for special directory names or non-empty directory
    if (entry_to_remove.type == TYPE_DIR) {
        // Prevent removal of root, '.', or '..' entries
        if (entry_to_remove.first_blk == ROOT_BLOCK || target_name == "." || target_name == "..") {
             std::cout << "FS::rm(): Error - cannot remove root or special directory entries.\n";
             return -1;
        }
        
        // Check if the directory is empty (besides '.' and '..')
        uint8_t target_buf[BLOCK_SIZE];
        disk.read(entry_to_remove.first_blk, target_buf);
        dir_entry* target_entries = (dir_entry*)target_buf;
        
        bool is_empty = true;
        for (int i = 0; i < max_entries; ++i) {
            if (target_entries[i].file_name[0] != '\0' && 
                std::string(target_entries[i].file_name) != "." && 
                std::string(target_entries[i].file_name) != "..") {
                is_empty = false;
                break;
            }
        }
        
        if (!is_empty) {
            std::cout << "FS::rm(): Error - directory is not empty.\n";
            return -1;
        }
    }
    
    // 5. Free the disk blocks associated with the file/directory
    uint16_t current_block = entry_to_remove.first_blk;
    while (current_block != FAT_EOF && current_block >= 2) {
        uint16_t next_block = fat[current_block];
        fat[current_block] = FAT_FREE;
        current_block = next_block;
    }

    // 6. Clear the entry in the parent directory
    memset(&entries[target_idx], 0, sizeof(dir_entry));
    disk.write(parent_blk, parent_buf);
    
    // 7. Save updated FAT
    save_fat();

    return 0;
}

// append <filepath1> <filepath2> appends the contents of file <filepath1> to
// the end of file <filepath2>
int FS::append(std::string filepath1, std::string filepath2)
{
    // --- 1. Resolve Paths and Check Types ---
    std::string src_name; uint16_t src_parent_blk;
    uint16_t src_blk = resolve_path(filepath1, src_name, src_parent_blk);

    std::string dest_name; uint16_t dest_parent_blk;
    uint16_t dest_blk = resolve_path(filepath2, dest_name, dest_parent_blk);

    if (src_blk == (uint16_t)-1 || src_blk == (uint16_t)-2 || dest_blk == (uint16_t)-1 || dest_blk == (uint16_t)-2) {
        std::cout << "FS::append(): Error - one or both files not found.\n"; return -1;
    }

    // Get source entry metadata
    uint8_t src_parent_buf[BLOCK_SIZE]; disk.read(src_parent_blk, src_parent_buf);
    dir_entry* src_parent_entries = (dir_entry*)src_parent_buf;
    int src_idx = get_entry_by_name(src_parent_entries, src_name);
    dir_entry src_entry = src_parent_entries[src_idx];

    // Get destination entry metadata
    uint8_t dest_parent_buf[BLOCK_SIZE]; disk.read(dest_parent_blk, dest_parent_buf);
    dir_entry* dest_parent_entries = (dir_entry*)dest_parent_buf;
    int dest_idx = get_entry_by_name(dest_parent_entries, dest_name);
    dir_entry& dest_entry = dest_parent_entries[dest_idx]; // Get reference to modify entry in buffer

    if (src_entry.type != TYPE_FILE || dest_entry.type != TYPE_FILE) {
        std::cout << "FS::append(): Error - source or destination is not a file.\n"; return -1;
    }
    
    if (src_entry.size == 0) return 0; // Nothing to append

    // --- 2. Find the last block of the destination file ---
    uint16_t last_dest_block = 0;
    uint16_t current_dest_block = dest_entry.first_blk;
    
    if (dest_entry.size == 0) {
        // Destination is empty. We need to allocate a new block and start the chain.
        // For simplicity, we'll start with the first block of the source as the new first block.
        // This is a simpler logic that essentially copies the source blocks to destination.
        
        // First, check if destination had a first block allocated and free it if it was empty
        if (dest_entry.first_blk != 0 && fat[dest_entry.first_blk] != FAT_FREE) {
             fat[dest_entry.first_blk] = FAT_FREE;
        }
        dest_entry.first_blk = src_entry.first_blk;
        dest_entry.size = src_entry.size;
        disk.write(dest_parent_blk, dest_parent_buf);
        return 0; // Simple case: Destination replaces source content
    }

    // Find the last block in the destination FAT chain
    while (current_dest_block != FAT_EOF && current_dest_block >= 2) {
        last_dest_block = current_dest_block;
        current_dest_block = fat[current_dest_block];
    }
    
    if (last_dest_block == 0) {
        std::cout << "FS::append(): Error - destination file corrupted (no blocks found).\n"; return -1;
    }

    // --- 3. Handle data copy (filling the partial last block of destination) ---
    
    // Check space remaining in the last block of the destination file
    uint32_t last_dest_size_in_bytes = dest_entry.size % BLOCK_SIZE;
    uint32_t free_space_in_last_block = (last_dest_size_in_bytes == 0) ? 0 : BLOCK_SIZE - last_dest_size_in_bytes;
    uint32_t bytes_to_append = src_entry.size;
    uint16_t current_src_block = src_entry.first_blk;

    if (free_space_in_last_block > 0) {
        // We have space in the last destination block to fill
        uint8_t dest_data_buf[BLOCK_SIZE];
        disk.read(last_dest_block, dest_data_buf); // Read the partial block
        
        uint8_t src_data_buf[BLOCK_SIZE];
        disk.read(current_src_block, src_data_buf); // Read the first source block

        uint32_t copy_amount = std::min(free_space_in_last_block, (uint32_t)BLOCK_SIZE); // Copy up to a full block's worth

        // Copy data from source to fill the gap in destination
        memcpy(dest_data_buf + last_dest_size_in_bytes, src_data_buf, copy_amount);
        disk.write(last_dest_block, dest_data_buf); // Write back the filled block

        dest_entry.size += copy_amount;
        bytes_to_append -= copy_amount;

        // If the source was entirely consumed by filling the gap, we are done
        if (bytes_to_append == 0) {
            disk.write(dest_parent_blk, dest_parent_buf);
            save_fat();
            return 0;
        }

        // If the source block was consumed, move to the next source block
        current_src_block = fat[current_src_block];
    }

    // --- 4. Append remaining source blocks (or the whole source if no free_space) ---
    if (bytes_to_append > 0) {
        // Find the block where the source content starts copying from (already moved to next if first block was partially copied)
        uint16_t src_copy_start_block = current_src_block;
        
        // Find the end of the source file chain (since the source file is unchanged)
        uint16_t src_end_of_chain = src_copy_start_block;
        uint16_t next_block_in_src = src_copy_start_block;
        while (next_block_in_src != FAT_EOF && next_block_in_src >= 2) {
            src_end_of_chain = next_block_in_src;
            next_block_in_src = fat[next_block_in_src];
        }
        
        // Find a free block to copy the remaining source chain's blocks to
        uint16_t new_dest_block = 0;
        uint16_t prev_new_dest_block = last_dest_block;

        while (current_src_block != FAT_EOF && bytes_to_append > 0) {
            new_dest_block = find_free_block();
            if (new_dest_block == -1) {
                std::cout << "FS::append(): Error - disk full during append. File partially appended.\n";
                // Link the last block of destination to EOF and clean up allocated blocks
                if (prev_new_dest_block != last_dest_block) {
                    fat[prev_new_dest_block] = FAT_EOF;
                }
                dest_entry.size -= bytes_to_append; // Update size to reflect what was actually written
                disk.write(dest_parent_blk, dest_parent_buf);
                save_fat();
                return -1;
            }

            // Copy data and link in FAT
            uint8_t temp_buf[BLOCK_SIZE];
            disk.read(current_src_block, temp_buf);
            disk.write(new_dest_block, temp_buf);
            
            fat[prev_new_dest_block] = new_dest_block;
            prev_new_dest_block = new_dest_block;
            
            bytes_to_append -= BLOCK_SIZE;
            current_src_block = fat[current_src_block];
        }
        
        // Mark the new last block as EOF
        fat[prev_new_dest_block] = FAT_EOF;
    }
    
    // --- 5. Update Destination Metadata ---
    dest_entry.size += src_entry.size;
    disk.write(dest_parent_blk, dest_parent_buf);
    save_fat();

    return 0;
}

// mkdir <dirpath> creates a new sub-directory
int FS::mkdir(std::string dirpath)
{
    std::string target_name;
    uint16_t parent_blk;

    // 1. Resolve the path. We expect the target directory NOT to exist (-2)
    uint16_t target_blk = resolve_path(dirpath, target_name, parent_blk);

    if (target_blk != (uint16_t)-2) {
        if (target_blk == (uint16_t)-1) {
            std::cout << "FS::mkdir(): Error - invalid path to parent directory.\n";
            return -1;
        }
        // Entry already exists.
        std::cout << "FS::mkdir(): Error - directory '" << dirpath << "' already exists.\n";
        return -1;
    }
    
    // 2. Read the parent directory block and find a free entry slot
    uint8_t parent_buf[BLOCK_SIZE];
    if (disk.read(parent_blk, parent_buf) < 0) return -1;
    dir_entry* parent_entries = (dir_entry*)parent_buf;

    int parent_free_idx = find_free_entry_in_current_dir(parent_entries);
    if (parent_free_idx == -1) {
        std::cout << "FS::mkdir(): Error - parent directory is full.\n";
        return -1;
    }

    // 3. Find a free block for the new directory's entries
    int new_dir_block_idx = find_free_block();
    if (new_dir_block_idx == -1) {
        std::cout << "FS::mkdir(): Error - disk is full, no free block.\n";
        return -1;
    }

    // 4. Initialize the new directory block (crucial for '.' and '..')
    uint8_t new_dir_buf[BLOCK_SIZE];
    memset(new_dir_buf, 0, BLOCK_SIZE);
    dir_entry* new_dir_entries = (dir_entry*)new_dir_buf;

    // Entry for '.' (self-reference)
    strncpy(new_dir_entries[0].file_name, ".", 56);
    new_dir_entries[0].size = 0;
    new_dir_entries[0].first_blk = new_dir_block_idx;
    new_dir_entries[0].type = TYPE_DIR;
    new_dir_entries[0].access_rights = READ | WRITE | EXECUTE;

    // Entry for '..' (parent directory reference)
    strncpy(new_dir_entries[1].file_name, "..", 56);
    new_dir_entries[1].size = 0;
    new_dir_entries[1].first_blk = parent_blk; // Points back to the parent block
    new_dir_entries[1].type = TYPE_DIR;
    new_dir_entries[1].access_rights = READ | WRITE | EXECUTE;

    // Write the new directory block to disk
    disk.write(new_dir_block_idx, new_dir_buf);

    // 5. Create the entry in the parent directory
    dir_entry new_entry;
    strncpy(new_entry.file_name, target_name.c_str(), 56);
    new_entry.file_name[55] = '\0';
    new_entry.size = 0; 
    new_entry.first_blk = new_dir_block_idx;
    new_entry.type = TYPE_DIR;
    new_entry.access_rights = READ | WRITE | EXECUTE; 

    parent_entries[parent_free_idx] = new_entry;
    disk.write(parent_blk, parent_buf);

    // 6. Update FAT
    fat[new_dir_block_idx] = FAT_EOF; // Directory block is self-contained
    save_fat();

    return 0;
}

// cd <dirpath> changes the current (working) directory
int FS::cd(std::string dirpath)
{
    std::string target_name;
    uint16_t parent_blk;

    // Resolve the path to get the target block index
    uint16_t target_blk = resolve_path(dirpath, target_name, parent_blk);

    if (target_blk == (uint16_t)-1 || target_blk == (uint16_t)-2) {
        std::cout << "Error: Directory '" << dirpath << "' not found or path is invalid.\n";
        return -1;
    }

    // If target_name is not empty, check if the entry is a directory.
    // If target_name is empty (only possible for path="/"), we know it's a directory (ROOT_BLOCK).
    if (!target_name.empty()) {
        uint8_t parent_buf[BLOCK_SIZE];
        disk.read(parent_blk, parent_buf);
        dir_entry* entries = (dir_entry*)parent_buf;
        int max_entries = BLOCK_SIZE / sizeof(dir_entry);
        
        int target_idx = -1;
        for(int i = 0; i < max_entries; ++i) {
            if (entries[i].file_name[0] != '\0' && target_name == std::string(entries[i].file_name)) {
                target_idx = i;
                break;
            }
        }
        
        if (target_idx == -1 || entries[target_idx].type != TYPE_DIR) {
            std::cout << "Error: '" << dirpath << "' is not a directory or internal error.\n";
            return -1;
        }
    }
    
    // 3. Update CWD block
    current_dir_block = target_blk;

    // 4. Update current_path (pwd string) by normalizing the path components.
    
    // Start with the appropriate path (current_path for relative, "/" for absolute)
    std::string full_path;
    if (dirpath.front() == '/') {
        full_path = "/";
    } else {
        full_path = current_path;
    }
    
    // Normalize the path (remove '..' and '.')
    std::vector<std::string> path_components;
    
    // Lambda function to split and process path components from a given path string
    auto process_path = [&](const std::string& path_str) {
        char path_copy[1024];
        strncpy(path_copy, path_str.c_str(), sizeof(path_copy) - 1);
        path_copy[sizeof(path_copy) - 1] = '\0';
        
        char* component;
        char* saveptr;
        
        component = strtok_r(path_copy, "/", &saveptr);
        while (component != NULL) {
            std::string comp_str = component;
            if (comp_str == "..") {
                if (!path_components.empty()) {
                    path_components.pop_back(); // Go up one level
                }
            } else if (comp_str != "." && !comp_str.empty()) {
                path_components.push_back(comp_str); // Go down
            }
            component = strtok_r(NULL, "/", &saveptr);
        }
    };
    
    // Process current path components first if relative path is used
    if (dirpath.front() != '/') {
        process_path(full_path);
    }
    
    // Process new path components
    process_path(dirpath);
    
    // Rebuild the final path string
    current_path = "/";
    for (const std::string& comp : path_components) {
        current_path += comp + "/";
    }
    
    if (current_path.length() > 1 && current_path.back() == '/') {
        current_path.pop_back(); // Remove trailing slash unless it is the root
    }
    
    return 0;
}

// pwd prints the full path
int FS::pwd()
{
    std::cout << current_path << std::endl;
    return 0;
}

// chmod <accessrights> <filepath>
int FS::chmod(std::string accessrights, std::string filepath)
{
    std::cout << "FS::chmod(" << accessrights << "," << filepath << ")\n";
    return 0;
}