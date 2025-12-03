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

int FS::get_entry_by_name(dir_entry *entries, const std::string &name)
{
    int max_entries = BLOCK_SIZE / sizeof(dir_entry);
    for (int i = 0; i < max_entries; ++i)
    {
        if (entries[i].file_name[0] != '\0')
        {
            if (name == std::string(entries[i].file_name))
                return i;
        }
    }
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
            final_name = ".";
            parent_block = ROOT_BLOCK;
            return ROOT_BLOCK;
        }
        working_path = working_path.substr(1); // Skip the initial '/'
    } else {
        current_block = current_dir_block; // Relative path, start from CWD
    }

    // 2. Split the path into components
    char path_copy[1024];
    strncpy(path_copy, working_path.c_str(), sizeof(path_copy) - 1);
    path_copy[sizeof(path_copy) - 1] = '\0';

    char* component;
    char* saveptr;
    
    std::vector<std::string> components;
    component = strtok_r(path_copy, "/", &saveptr);
    while (component != NULL) {
        if (strlen(component) > 0) {
            components.push_back(component);
        }
        component = strtok_r(NULL, "/", &saveptr);
    }
    
    if (components.empty()) {
        final_name = ".";
        parent_block = current_block;
        return current_block;
    }
    
    uint16_t target_block = current_block; 
    
    // 3. Traverse all components, except the last one
    for (size_t i = 0; i < components.size() - 1; ++i) {
        std::string name = components[i];
        
        uint8_t dir_buffer[BLOCK_SIZE];
        if (disk.read(target_block, dir_buffer) < 0) return (uint16_t)-1;
        dir_entry* entries = (dir_entry*)dir_buffer;
        int max_entries = BLOCK_SIZE / sizeof(dir_entry);
        
        int found_idx = -1;
        for (int j = 0; j < max_entries; ++j) {
            if (entries[j].file_name[0] != '\0' && name == std::string(entries[j].file_name)) {
                found_idx = j;
                break;
            }
        }

        if (found_idx == -1) return (uint16_t)-1;
        if (entries[found_idx].type != TYPE_DIR) return (uint16_t)-1;
        
        target_block = entries[found_idx].first_blk;
    }

    // 4. Handle the final component
    final_name = components.back();
    parent_block = target_block;

    uint8_t dir_buffer[BLOCK_SIZE];
    if (disk.read(parent_block, dir_buffer) < 0) return (uint16_t)-1;
    dir_entry* entries = (dir_entry*)dir_buffer;
    int max_entries = BLOCK_SIZE / sizeof(dir_entry);
    
    for (int j = 0; j < max_entries; ++j) {
        if (entries[j].file_name[0] != '\0' && final_name == std::string(entries[j].file_name)) {
            return entries[j].first_blk;
        }
    }
    
    return (uint16_t)-2;
}

// ================= PUBLIC METHODS ==================

FS::FS()
{
    std::cout << "FS::FS()... Creating file system\n";
    if (disk.read(FAT_BLOCK, (uint8_t *)fat) != 0) {
        // Handle error or init empty
    }
    current_dir_block = ROOT_BLOCK;       
    current_path = "/";
}

FS::~FS()
{
}

int FS::format()
{
    uint8_t zero_block[BLOCK_SIZE] = {0};
    for (int i = 0; i < BLOCK_SIZE/2; ++i) fat[i] = FAT_FREE;
    fat[ROOT_BLOCK] = FAT_EOF;
    fat[FAT_BLOCK] = FAT_EOF;
    save_fat();

    dir_entry* root_entries = (dir_entry*)zero_block;
    
    strncpy(root_entries[0].file_name, ".", 56);
    root_entries[0].first_blk = ROOT_BLOCK; 
    root_entries[0].type = TYPE_DIR;
    root_entries[0].access_rights = READ | WRITE | EXECUTE;

    strncpy(root_entries[1].file_name, "..", 56);
    root_entries[1].first_blk = ROOT_BLOCK; 
    root_entries[1].type = TYPE_DIR;
    root_entries[1].access_rights = READ | WRITE | EXECUTE;

    if (disk.write(ROOT_BLOCK, (uint8_t*)root_entries) < 0) return -1;

    current_dir_block = ROOT_BLOCK;
    current_path = "/";
    return 0;
}

int FS::create(std::string filepath)
{
    std::string target_name;
    uint16_t parent_blk;
    uint16_t target_blk = resolve_path(filepath, target_name, parent_blk);

    if (target_blk != (uint16_t)-2) {
        if (target_blk == (uint16_t)-1) {
            std::cout << "FS::create(): Error - invalid path.\n";
            return -1;
        }
        std::cout << "FS::create(): Error - file already exists.\n";
        return -1;
    }

    if (target_name.empty() || target_name.length() >= 56) return -1;

    uint8_t parent_buf[BLOCK_SIZE];
    disk.read(parent_blk, parent_buf);
    dir_entry* parent_entries = (dir_entry*)parent_buf;

    int parent_free_idx = find_free_entry_in_current_dir(parent_entries);
    if (parent_free_idx == -1) return -1;

    // Use getline to match "Empty line to end" spec and fix 16 vs 17 byte issue
    std::string content = "";
    std::string line;
    std::cout << "Enter data. Empty line to end.\n";
    while (std::getline(std::cin, line)) {
        if (line.empty()) break;
        content += line + "\n";
    }

    uint32_t file_size = content.size();
    int blocks_needed = (file_size + BLOCK_SIZE - 1) / BLOCK_SIZE;
    if (file_size == 0) blocks_needed = 0;

    int first_block = FAT_EOF;
    int previous_block = -1;

    if (blocks_needed > 0) {
        int bytes_written = 0;
        for (int i = 0; i < blocks_needed; ++i) {
            int new_block = find_free_block();
            if (new_block == -1) return -2; // Disk full

            fat[new_block] = FAT_EOF; // Mark as used immediately
            if (first_block == FAT_EOF) first_block = new_block;
            if (previous_block != -1) fat[previous_block] = new_block;

            uint8_t data_buffer[BLOCK_SIZE] = {0};
            int chunk_size = std::min((int)BLOCK_SIZE, (int)(file_size - bytes_written));
            memcpy(data_buffer, content.c_str() + bytes_written, chunk_size);
            disk.write(new_block, data_buffer);

            previous_block = new_block;
            bytes_written += chunk_size;
        }
    } else {
        // Create an empty file, still reserve one block? 
        // Or strictly 0 size means no blocks?
        // Let's allocate 1 block for safety and mark it EOF, consistent with previous behaviors.
        int new_block = find_free_block();
        if (new_block != -1) {
             fat[new_block] = FAT_EOF;
             first_block = new_block;
             // Write empty buffer to clear potential garbage
             uint8_t empty[BLOCK_SIZE] = {0};
             disk.write(new_block, empty);
        }
    }

    dir_entry new_entry;
    memset(&new_entry, 0, sizeof(dir_entry)); // Zero out to avoid garbage in padding
    strncpy(new_entry.file_name, target_name.c_str(), 56);
    new_entry.size = file_size;
    new_entry.first_blk = (first_block == FAT_EOF) ? 0 : first_block;
    new_entry.type = TYPE_FILE;
    new_entry.access_rights = READ | WRITE; 

    parent_entries[parent_free_idx] = new_entry;
    disk.write(parent_blk, parent_buf);
    save_fat();

    return 0;
}

int FS::cat(std::string filepath)
{
    std::string target_name;
    uint16_t parent_blk;
    uint16_t target_blk = resolve_path(filepath, target_name, parent_blk);

    if (target_blk == (uint16_t)-1 || target_blk == (uint16_t)-2) {
        std::cout << "FS::cat(): Error - file not found.\n";
        return -1;
    }
    
    uint8_t parent_buf[BLOCK_SIZE];
    disk.read(parent_blk, parent_buf);
    dir_entry* entries = (dir_entry*)parent_buf;
    
    int target_idx = get_entry_by_name(entries, target_name);
    if (target_idx == -1) return -1;

    // --- TASK 5 Check Read Permission ---
    if (!(entries[target_idx].access_rights & READ)) {
        std::cout << "FS::cat(): Error - permission denied.\n";
        return -1;
    }

    if (entries[target_idx].type != TYPE_FILE) {
        std::cout << "FS::cat(): Error - not a file\n";
        return -1;
    }

    uint16_t current_block = entries[target_idx].first_blk;
    uint32_t remaining_size = entries[target_idx].size;
    uint8_t data_buffer[BLOCK_SIZE];

    while (current_block != (uint16_t)FAT_EOF && remaining_size > 0 && current_block >= 2) {
        disk.read(current_block, data_buffer);
        int to_print = std::min((uint32_t)BLOCK_SIZE, remaining_size);
        std::cout.write((char *)data_buffer, to_print);
        remaining_size -= to_print;
        current_block = fat[current_block];
    }
    std::cout << std::endl;
    return 0;
}

int FS::ls()
{
    uint8_t dir_buffer[BLOCK_SIZE];
    if (disk.read(current_dir_block, dir_buffer) < 0) return -1;

    dir_entry * entries = (dir_entry *)dir_buffer;
    int max_entries = BLOCK_SIZE / sizeof(dir_entry);

    std::cout << "name\ttype\taccessrights\tsize" << std::endl;
    std::cout << "---------------------------------------------" << std::endl;

    for (int i = 0; i < max_entries; ++i) {
        if (entries[i].file_name[0] != '\0') {
            std::string name = entries[i].file_name;
            if (name == "." || name == "..") continue;

            std::cout << name << "\t";
            std::cout << ((entries[i].type == TYPE_DIR) ? "dir\t" : "file\t");
            
            // Print Access Rights
            uint8_t r = entries[i].access_rights;
            std::cout << ((r & READ) ? "r" : "-");
            std::cout << ((r & WRITE) ? "w" : "-");
            std::cout << ((r & EXECUTE) ? "x" : "-");
            
            std::cout << "\t" << entries[i].size << std::endl;
        }
    }
    return 0;
}

int FS::cp(std::string sourcepath, std::string destpath)
{
    std::string src_name; uint16_t src_parent_blk;
    uint16_t src_blk = resolve_path(sourcepath, src_name, src_parent_blk);
    if (src_blk == (uint16_t)-1 || src_blk == (uint16_t)-2) { 
        std::cout << "FS::cp(): Error - source not found.\n"; return -1; 
    }
    
    uint8_t src_parent_buf[BLOCK_SIZE];
    disk.read(src_parent_blk, src_parent_buf);
    dir_entry* src_parent_entries = (dir_entry*)src_parent_buf;
    int src_idx = get_entry_by_name(src_parent_entries, src_name);
    dir_entry src_entry = src_parent_entries[src_idx];
    
    // Check Source Read Permission
    if (!(src_entry.access_rights & READ)) {
        std::cout << "FS::cp(): Error - permission denied (read source).\n"; return -1;
    }

    std::string dest_name; uint16_t dest_parent_blk;
    uint16_t dest_blk = resolve_path(destpath, dest_name, dest_parent_blk);
    if (dest_parent_blk == (uint16_t)-1) return -1;

    uint16_t final_parent_blk = dest_parent_blk;
    std::string final_name = dest_name;
    int dest_free_idx = -1;

    uint8_t dest_buf[BLOCK_SIZE];
    
    if (dest_blk != (uint16_t)-2) {
        // Destination exists
        disk.read(dest_parent_blk, dest_buf);
        dir_entry* dest_entries = (dir_entry*)dest_buf;
        int dest_idx = get_entry_by_name(dest_entries, dest_name);

        if (dest_entries[dest_idx].type == TYPE_DIR) {
            // Copy into directory
            final_parent_blk = dest_blk;
            final_name = src_name;
            disk.read(final_parent_blk, dest_buf);
            dest_entries = (dir_entry*)dest_buf;
            if (get_entry_by_name(dest_entries, final_name) != -1) return -1; // exists
            dest_free_idx = find_free_entry_in_current_dir(dest_entries);
        } else {
            return -1; // No overwrite supported 
        }
    } else {
        disk.read(dest_parent_blk, dest_buf);
        dir_entry* dest_entries = (dir_entry*)dest_buf;
        dest_free_idx = find_free_entry_in_current_dir(dest_entries);
    }
    
    if (dest_free_idx == -1) return -1;
    
    // Copy Blocks
    uint16_t current_src_block = src_entry.first_blk;
    uint16_t new_first_block = 0;
    uint16_t prev_dest_block = 0;
    uint32_t remaining_size = src_entry.size;
    uint8_t data_buffer[BLOCK_SIZE];
    
    // Handle empty file case
    if (remaining_size == 0 || current_src_block == 0) {
        int new_block = find_free_block();
        if (new_block != -1) {
            new_first_block = new_block;
            fat[new_block] = FAT_EOF;
            // Write zeros to clean block
            memset(data_buffer, 0, BLOCK_SIZE);
            disk.write(new_block, data_buffer);
        }
    } else {
        while (current_src_block != (uint16_t)FAT_EOF && remaining_size > 0 && current_src_block >= 2) {
            disk.read(current_src_block, data_buffer);
            int new_block = find_free_block();
            if (new_block == -1) return -1;
            
            disk.write(new_block, data_buffer);
            
            if (prev_dest_block != 0) fat[prev_dest_block] = new_block;
            else new_first_block = new_block;
            
            prev_dest_block = new_block;
            fat[prev_dest_block] = FAT_EOF;

            remaining_size -= std::min((uint32_t)BLOCK_SIZE, remaining_size);
            current_src_block = fat[current_src_block];
        }
    }

    dir_entry new_entry = src_entry;
    memset(&new_entry, 0, sizeof(dir_entry)); // clean init
    strncpy(new_entry.file_name, final_name.c_str(), 56);
    new_entry.first_blk = new_first_block;
    new_entry.size = src_entry.size;
    new_entry.type = TYPE_FILE;
    new_entry.access_rights = READ | WRITE;

    ((dir_entry*)dest_buf)[dest_free_idx] = new_entry;
    disk.write(final_parent_blk, dest_buf);
    save_fat();

    return 0;
}

int FS::mv(std::string sourcepath, std::string destpath)
{
    std::string src_name; uint16_t src_parent_blk;
    uint16_t src_blk = resolve_path(sourcepath, src_name, src_parent_blk);
    if (src_blk == (uint16_t)-1 || src_blk == (uint16_t)-2) { 
        std::cout << "FS::mv(): Error - source not found.\n"; return -1; 
    }
    if (src_name == "." || src_name == "..") return -1;

    std::string dest_name; uint16_t dest_parent_blk;
    uint16_t dest_blk = resolve_path(destpath, dest_name, dest_parent_blk);
    if (dest_parent_blk == (uint16_t)-1) return -1;

    // Load Source Entry
    uint8_t src_parent_buf[BLOCK_SIZE];
    disk.read(src_parent_blk, src_parent_buf);
    dir_entry* src_parent_entries = (dir_entry*)src_parent_buf;
    int src_idx = get_entry_by_name(src_parent_entries, src_name);
    dir_entry src_entry = src_parent_entries[src_idx];

    // Determine Destination
    uint16_t final_parent_blk = dest_parent_blk;
    std::string final_name = dest_name;
    int dest_target_idx = -1;
    
    // Helper to read final block safely
    uint8_t final_parent_buf[BLOCK_SIZE];
    
    if (dest_blk != (uint16_t)-2) {
        // Dest exists
        disk.read(dest_parent_blk, final_parent_buf);
        dir_entry* entries = (dir_entry*)final_parent_buf;
        int idx = get_entry_by_name(entries, dest_name);
        
        if (entries[idx].type == TYPE_DIR) {
            // Move into directory
            final_parent_blk = dest_blk;
            final_name = src_name;
            disk.read(final_parent_blk, final_parent_buf);
            entries = (dir_entry*)final_parent_buf;
            if (get_entry_by_name(entries, final_name) != -1) return -1; // exists
            dest_target_idx = find_free_entry_in_current_dir(entries);
        } else {
             // Overwrite file not allowed 
             return -1; 
        }
    } else {
        // Rename / Move to new name
        disk.read(final_parent_blk, final_parent_buf);
        dest_target_idx = find_free_entry_in_current_dir((dir_entry*)final_parent_buf);
    }
    
    if (dest_target_idx == -1) return -1;

    // Write New Entry
    dir_entry new_entry = src_entry;
    strncpy(new_entry.file_name, final_name.c_str(), 56);
    
    // Update '..' if moving a directory
    if (src_entry.type == TYPE_DIR && src_parent_blk != final_parent_blk) {
        uint8_t my_dir_buf[BLOCK_SIZE];
        disk.read(src_entry.first_blk, my_dir_buf);
        ((dir_entry*)my_dir_buf)[1].first_blk = final_parent_blk;
        disk.write(src_entry.first_blk, my_dir_buf);
    }

    ((dir_entry*)final_parent_buf)[dest_target_idx] = new_entry;
    disk.write(final_parent_blk, final_parent_buf);
    
    // Remove Old Entry
    // CRITICAL FIX: Reload source parent buffer in case src_parent == final_parent
    disk.read(src_parent_blk, src_parent_buf);
    src_parent_entries = (dir_entry*)src_parent_buf; // re-point
    memset(&src_parent_entries[src_idx], 0, sizeof(dir_entry));
    disk.write(src_parent_blk, src_parent_buf);

    save_fat();
    return 0;
}

int FS::rm(std::string filepath)
{
    std::string target_name;
    uint16_t parent_blk;
    uint16_t target_blk = resolve_path(filepath, target_name, parent_blk);

    if (target_blk == (uint16_t)-1 || target_blk == (uint16_t)-2) {
        std::cout << "FS::rm(): Error - not found.\n"; return -1;
    }

    uint8_t parent_buf[BLOCK_SIZE];
    disk.read(parent_blk, parent_buf);
    dir_entry* entries = (dir_entry*)parent_buf;
    int idx = get_entry_by_name(entries, target_name);
    
    if (entries[idx].type == TYPE_DIR) {
        if (target_name == "." || target_name == "..") return -1;
        // Check empty
        uint8_t dir_buf[BLOCK_SIZE];
        disk.read(entries[idx].first_blk, dir_buf);
        dir_entry* sub = (dir_entry*)dir_buf;
        for(int i=0; i<BLOCK_SIZE/sizeof(dir_entry); ++i) {
             if (sub[i].file_name[0] != '\0' && 
                 std::string(sub[i].file_name) != "." && 
                 std::string(sub[i].file_name) != "..") return -1; // Not empty
        }
    }

    uint16_t current_block = entries[idx].first_blk;
    // CRITICAL SEGFAULT FIX: Bounds check and correct casting for EOF comparison
    while(current_block != (uint16_t)FAT_EOF && current_block >= 2 && current_block < 2048) {
        uint16_t next = fat[current_block];
        fat[current_block] = FAT_FREE;
        current_block = next;
    }
    
    memset(&entries[idx], 0, sizeof(dir_entry));
    disk.write(parent_blk, parent_buf);
    save_fat();
    return 0;
}

int FS::append(std::string filepath1, std::string filepath2)
{
    std::string src_name; uint16_t src_parent_blk;
    uint16_t src_blk = resolve_path(filepath1, src_name, src_parent_blk);

    std::string dest_name; uint16_t dest_parent_blk;
    uint16_t dest_blk = resolve_path(filepath2, dest_name, dest_parent_blk);

    if (src_blk == (uint16_t)-1 || src_blk == (uint16_t)-2 || dest_blk == (uint16_t)-1 || dest_blk == (uint16_t)-2) return -1;

    uint8_t src_parent_buf[BLOCK_SIZE]; disk.read(src_parent_blk, src_parent_buf);
    dir_entry* src_ent = (dir_entry*)src_parent_buf + get_entry_by_name((dir_entry*)src_parent_buf, src_name);

    uint8_t dest_parent_buf[BLOCK_SIZE]; disk.read(dest_parent_blk, dest_parent_buf);
    dir_entry* dest_ent = (dir_entry*)dest_parent_buf + get_entry_by_name((dir_entry*)dest_parent_buf, dest_name);

    if (src_ent->type != TYPE_FILE || dest_ent->type != TYPE_FILE) return -1;
    
    // Check Perms
    if (!(src_ent->access_rights & READ)) return -1;
    if (!(dest_ent->access_rights & WRITE)) return -1;

    if (src_ent->size == 0) return 0;

    uint16_t current_src = src_ent->first_blk;
    uint32_t bytes_to_copy = src_ent->size;
    uint32_t dest_offset = dest_ent->size % BLOCK_SIZE;
    
    // Find last block of dest
    uint16_t dest_curr = dest_ent->first_blk;
    uint16_t dest_prev = 0;
    
    // If dest is empty, allocate first block
    if (dest_ent->size == 0 || dest_curr == 0) {
        int nb = find_free_block();
        if (nb == -1) return -1;
        dest_ent->first_blk = nb;
        fat[nb] = FAT_EOF;
        dest_curr = nb;
        dest_offset = 0; // Fresh block
        // Zero out fresh block
        uint8_t zeros[BLOCK_SIZE] = {0};
        disk.write(nb, zeros);
    } else {
        while(dest_curr != (uint16_t)FAT_EOF && dest_curr >= 2) {
            dest_prev = dest_curr;
            dest_curr = fat[dest_curr];
        }
        dest_curr = dest_prev; // The actual last valid block
    }

    uint8_t dest_data[BLOCK_SIZE];
    disk.read(dest_curr, dest_data);
    
    uint8_t src_data[BLOCK_SIZE];
    
    while(current_src != (uint16_t)FAT_EOF && bytes_to_copy > 0) {
        disk.read(current_src, src_data);
        
        uint32_t space_in_dest = BLOCK_SIZE - dest_offset;
        uint32_t src_chunk = std::min(bytes_to_copy, (uint32_t)BLOCK_SIZE);
        
        if (src_chunk <= space_in_dest) {
             memcpy(dest_data + dest_offset, src_data, src_chunk);
             disk.write(dest_curr, dest_data);
             dest_offset += src_chunk;
             bytes_to_copy -= src_chunk;
             
             if (dest_offset == BLOCK_SIZE && bytes_to_copy > 0) {
                 int nb = find_free_block();
                 if (nb == -1) return -1;
                 fat[dest_curr] = nb;
                 fat[nb] = FAT_EOF;
                 dest_curr = nb;
                 dest_offset = 0;
                 memset(dest_data, 0, BLOCK_SIZE);
             } else if (dest_offset < BLOCK_SIZE) {
                 current_src = fat[current_src];
                 continue;
             }
             current_src = fat[current_src];
        } else {
            // Split write
            uint32_t part1 = space_in_dest;
            uint32_t part2 = src_chunk - part1;
            
            memcpy(dest_data + dest_offset, src_data, part1);
            disk.write(dest_curr, dest_data);
            
            int nb = find_free_block();
            if (nb == -1) return -1;
            fat[dest_curr] = nb;
            fat[nb] = FAT_EOF;
            dest_curr = nb;
            
            memset(dest_data, 0, BLOCK_SIZE);
            memcpy(dest_data, src_data + part1, part2);
            disk.write(dest_curr, dest_data);
            dest_offset = part2;
            
            bytes_to_copy -= src_chunk;
            current_src = fat[current_src];
        }
    }
    
    dest_ent->size += src_ent->size;
    disk.write(dest_parent_blk, dest_parent_buf);
    save_fat();
    return 0;
}

int FS::mkdir(std::string dirpath)
{
    std::string target_name;
    uint16_t parent_blk;
    uint16_t target_blk = resolve_path(dirpath, target_name, parent_blk);

    if (target_blk != (uint16_t)-2) return -1;

    uint8_t parent_buf[BLOCK_SIZE];
    disk.read(parent_blk, parent_buf);
    dir_entry* parent_entries = (dir_entry*)parent_buf;
    int free_idx = find_free_entry_in_current_dir(parent_entries);
    if (free_idx == -1) return -1;

    int new_blk = find_free_block();
    if (new_blk == -1) return -1;

    uint8_t new_dir_buf[BLOCK_SIZE] = {0};
    dir_entry* new_entries = (dir_entry*)new_dir_buf;
    
    strncpy(new_entries[0].file_name, ".", 56);
    new_entries[0].first_blk = new_blk;
    new_entries[0].type = TYPE_DIR;
    new_entries[0].access_rights = READ|WRITE|EXECUTE;

    strncpy(new_entries[1].file_name, "..", 56);
    new_entries[1].first_blk = parent_blk;
    new_entries[1].type = TYPE_DIR;
    new_entries[1].access_rights = READ|WRITE|EXECUTE;
    
    disk.write(new_blk, new_dir_buf);
    fat[new_blk] = FAT_EOF;

    memset(&parent_entries[free_idx], 0, sizeof(dir_entry));
    strncpy(parent_entries[free_idx].file_name, target_name.c_str(), 56);
    parent_entries[free_idx].first_blk = new_blk;
    parent_entries[free_idx].type = TYPE_DIR;
    parent_entries[free_idx].access_rights = READ|WRITE|EXECUTE;

    disk.write(parent_blk, parent_buf);
    save_fat();
    return 0;
}

int FS::cd(std::string dirpath)
{
    std::string target_name;
    uint16_t parent_blk;
    uint16_t target_blk = resolve_path(dirpath, target_name, parent_blk);

    if (target_blk == (uint16_t)-1 || target_blk == (uint16_t)-2) return -1;

    if (!target_name.empty()) {
        uint8_t pbuf[BLOCK_SIZE];
        disk.read(parent_blk, pbuf);
        dir_entry* entries = (dir_entry*)pbuf;
        int idx = get_entry_by_name(entries, target_name);
        if (entries[idx].type != TYPE_DIR) return -1;
    }

    current_dir_block = target_blk;
    
    if (dirpath == "/") current_path = "/";
    else if (dirpath == "..") {
        size_t pos = current_path.rfind('/');
        if (pos != std::string::npos && pos > 0) current_path = current_path.substr(0, pos);
        else current_path = "/";
    } else if (dirpath != ".") {
        if (current_path == "/") current_path += target_name.empty() ? "" : target_name;
        else current_path += "/" + target_name;
    }
    return 0;
}

int FS::pwd()
{
    std::cout << current_path << std::endl;
    return 0;
}

int FS::chmod(std::string accessrights, std::string filepath)
{
    std::string target_name;
    uint16_t parent_blk;
    uint16_t target_blk = resolve_path(filepath, target_name, parent_blk);

    if (target_blk == (uint16_t)-1 || target_blk == (uint16_t)-2) {
        std::cout << "FS::chmod(): Error - file not found.\n";
        return -1;
    }

    uint8_t parent_buf[BLOCK_SIZE];
    disk.read(parent_blk, parent_buf);
    dir_entry* entries = (dir_entry*)parent_buf;
    int idx = get_entry_by_name(entries, target_name);
    
    int rights = std::stoi(accessrights);
    entries[idx].access_rights = (uint8_t)rights;
    
    disk.write(parent_blk, parent_buf);
    return 0;
}