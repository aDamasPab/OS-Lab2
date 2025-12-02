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
            
            previous_block = new_block;
            src_block = fat[src_block];
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

// mv <sourcepath> <destpath> renames the file <sourcepath>
int FS::mv(std::string sourcepath, std::string destpath)
{
    // 1. Load Root (Using ROOT_BLOCK based on provided working version)
    uint8_t block_buffer[BLOCK_SIZE];
    disk.read(ROOT_BLOCK, block_buffer);
    dir_entry* root = (dir_entry*)block_buffer;

    // 2. Search source
    int src_idx = -1;
    for (int i = 0; i < BLOCK_SIZE / sizeof(dir_entry); ++i) {
        if (strcmp(root[i].file_name, sourcepath.c_str()) == 0) {
            src_idx = i;
            break;
        }
    }

    if (src_idx == -1) {
        std::cout << "Error: File not found.\n";
        return -1;
    }

    // 3. Verify destination (Noclobber)
    for (int i = 0; i < BLOCK_SIZE / sizeof(dir_entry); ++i) {
        if (strcmp(root[i].file_name, destpath.c_str()) == 0) {
            std::cout << "Error: Destination file already exists.\n";
            return -1;
        }
    }

    // 4. Rename
    memset(root[src_idx].file_name, 0, 56);
    strcpy(root[src_idx].file_name, destpath.c_str());

    // 5. Save
    disk.write(ROOT_BLOCK, (uint8_t*)root);

    return 0;
}

// rm <filepath> removes / deletes the file <filepath>
int FS::rm(std::string filepath)
{
    // 1. Load CWD
    uint8_t block_buffer[BLOCK_SIZE];
    disk.read(current_dir_block, block_buffer);
    dir_entry * root = (dir_entry*)block_buffer;
    
    disk.read(FAT_BLOCK, (uint8_t*)fat);

    // 2. Search for the file or directory entry 
    int idx = -1;
    for (int i = 0; i < BLOCK_SIZE / sizeof(dir_entry); ++i) {
        if (strcmp(root[i].file_name, filepath.c_str()) == 0) {
            idx = i;
            break;
        }
    }

    if (idx == -1) {
        std::cout << "Error: File not found.\n";
        return -1;
    }

    if (root[idx].type == TYPE_DIR) {
        // a) Prevent removal of special directories '.' or '..'
        if (strcmp(filepath.c_str(), ".") == 0 || strcmp(filepath.c_str(), "..") == 0) {
             std::cout << "Error: Cannot remove special directories '.' or '..'.\n";
             return -1;
        }

        uint16_t dir_blk = root[idx].first_blk;
        uint8_t dir_buf[BLOCK_SIZE];
        
        // Load the content of the directory to be removed
        if (disk.read(dir_blk, dir_buf) < 0) return -1;
        dir_entry * dir_entries = (dir_entry*)dir_buf;
        
        bool is_empty = true;
        // Check if the directory is empty (ignore first 2 entries: . and ..)
        for (int i = 2; i < BLOCK_SIZE / sizeof(dir_entry); ++i) {
            if (dir_entries[i].file_name[0] != '\0') {
                is_empty = false;
                break;
            }
        }

        if (!is_empty) {
            std::cout << "Error: Directory '" << filepath << "' is not empty.\n";
            return -1;
        }
        
        // If empty, free the single data block used by the directory itself
        fat[dir_blk] = FAT_FREE;
        
    } else {
        // 3. Free blocks in the FAT
        int curr = root[idx].first_blk;

        // Only free blocks if size > 0
        if (root[idx].size > 0) {
            while (curr != FAT_EOF && curr != FAT_FREE && curr != 0) {
                int next = fat[curr];
                fat[curr] = FAT_FREE; // Mark free
                curr = next;
            }
        }
    }

    // 4. Free directory entry in the CWD
    memset(&root[idx], 0, sizeof(dir_entry)); 

    // 5. Save FAT and CWD
    disk.write(FAT_BLOCK, (uint8_t*)fat);
    disk.write(current_dir_block, (uint8_t*)root);

    return 0;
}

// append <filepath1> <filepath2> appends the contents of file <filepath1> to
// the end of file <filepath2>
int FS::append(std::string filepath1, std::string filepath2)
{
    if (filepath1 == filepath2) {
        std::cout << "Error: Cannot append file to itself.\n";
        return -1;
    }

    // 1. Load structures (Using ROOT based on provided working version)
    uint8_t root_buf[BLOCK_SIZE];
    disk.read(ROOT_BLOCK, root_buf);
    dir_entry* root = (dir_entry*)root_buf;
    disk.read(FAT_BLOCK, (uint8_t*)fat);

    // 2. Find files
    int idx1 = -1, idx2 = -1;
    for (int i = 0; i < BLOCK_SIZE / sizeof(dir_entry); ++i) {
        if (strcmp(root[i].file_name, filepath1.c_str()) == 0) idx1 = i;
        if (strcmp(root[i].file_name, filepath2.c_str()) == 0) idx2 = i;
    }

    if (idx1 == -1 || idx2 == -1) {
        std::cout << "Error: One or both files not found.\n";
        return -1;
    }

    // If source is empty, nothing to do
    if (root[idx1].size == 0) return 0;

    // 3. Prepare File 1 reading
    int curr_src = root[idx1].first_blk;
    int src_size = root[idx1].size;
    int src_read = 0;

    // 4. Prepare File 2 writing
    int curr_dest = root[idx2].first_blk;
    int last_dest = -1;

    // If File 2 is empty, allocate first block
    if (root[idx2].size == 0 || curr_dest == FAT_EOF || curr_dest == 0) {
        int new_blk = -1;
        for (int k = 2; k < disk.get_no_blocks(); ++k) {
            if (fat[k] == FAT_FREE) { new_blk = k; break; }
        }
        if (new_blk == -1) { std::cout << "Disk full.\n"; return -1; }
        
        fat[new_blk] = FAT_EOF;
        root[idx2].first_blk = new_blk;
        curr_dest = new_blk;
        last_dest = new_blk;
    } else {
        // Traverse FAT to end
        while (fat[curr_dest] != FAT_EOF) {
            curr_dest = fat[curr_dest];
        }
        last_dest = curr_dest;
    }

    // Calculate offset in last destination block
    int offset = root[idx2].size % BLOCK_SIZE;
    
    uint8_t dest_buf[BLOCK_SIZE];
    disk.read(last_dest, dest_buf);

    uint8_t src_buf[BLOCK_SIZE];
    
    while (src_read < src_size) {
        // Read current source block
        disk.read(curr_src, src_buf);
        
        int bytes_in_src_block = std::min((int)BLOCK_SIZE, src_size - src_read);
        int src_block_offset = 0; 

        while (bytes_in_src_block > 0) {
            int space_in_dest = BLOCK_SIZE - offset;

            // If destination block is full, allocate new one
            if (space_in_dest == 0) {
                disk.write(last_dest, dest_buf); 

                int new_blk = -1;
                for (int k = 2; k < disk.get_no_blocks(); ++k) {
                    if (fat[k] == FAT_FREE) { new_blk = k; break; }
                }
                if (new_blk == -1) {
                    std::cout << "Disk full during append.\n";
                    root[idx2].size += (src_read - bytes_in_src_block); 
                    disk.write(FAT_BLOCK, (uint8_t*)fat);
                    disk.write(ROOT_BLOCK, (uint8_t*)root);
                    return -1;
                }

                fat[last_dest] = new_blk; // Link
                fat[new_blk] = FAT_EOF;
                last_dest = new_blk;
                
                memset(dest_buf, 0, BLOCK_SIZE);
                offset = 0;
                space_in_dest = BLOCK_SIZE;
            }

            // Copy what fits
            int to_copy = std::min(bytes_in_src_block, space_in_dest);
            memcpy(dest_buf + offset, src_buf + src_block_offset, to_copy);

            offset += to_copy;
            src_block_offset += to_copy;
            bytes_in_src_block -= to_copy;
            src_read += to_copy;
            root[idx2].size += to_copy;
        }

        // Advance to next source block
        curr_src = fat[curr_src];
    }

    // Write last block
    disk.write(last_dest, dest_buf);

    // 5. Save metadata
    disk.write(FAT_BLOCK, (uint8_t*)fat);
    disk.write(ROOT_BLOCK, (uint8_t*)root);

    return 0;
}

// mkdir <dirpath> creates a new sub-directory
int FS::mkdir(std::string dirpath)
{
    // 1. Load CWD and FAT
    uint8_t cwd_buf[BLOCK_SIZE];
    disk.read(current_dir_block, cwd_buf);
    dir_entry* cwd_entries = (dir_entry*)cwd_buf;
    
    disk.read(FAT_BLOCK, (uint8_t*)fat);

    int num_entries = BLOCK_SIZE / sizeof(dir_entry);

    // 2. Check if name already exists and find a free entry slot
    int free_entry_idx = -1;
    for (int i = 0; i < num_entries; ++i) {
        if (cwd_entries[i].file_name[0] != '\0' && strcmp(cwd_entries[i].file_name, dirpath.c_str()) == 0) {
            std::cout << "Error: Entry '" << dirpath << "' already exists.\n";
            return -1;
        }
        if (cwd_entries[i].file_name[0] == '\0' && free_entry_idx == -1) {
            free_entry_idx = i;
        }
    }

    if (free_entry_idx == -1) {
        std::cout << "Error: Current directory is full.\n";
        return -1;
    }

    // 3. Find a free data block for the new directory
    int new_blk = -1;
    int fat_size = disk.get_no_blocks();
    for (int k = 2; k < fat_size; ++k) {
        if (fat[k] == FAT_FREE) {
            new_blk = k;
            break;
        }
    }
    if (new_blk == -1) {
        std::cout << "Error: Disk is full.\n";
        return -1;
    }

    // 4. Create Directory Entries (., ..) in the new block
    uint8_t new_dir_buf[BLOCK_SIZE];
    memset(new_dir_buf, 0, BLOCK_SIZE);
    dir_entry* new_dir_entries = (dir_entry*)new_dir_buf;
    
    // . (self)
    strncpy(new_dir_entries[0].file_name, ".", 56);
    new_dir_entries[0].size = 0;
    new_dir_entries[0].first_blk = new_blk; // Points to itself
    new_dir_entries[0].type = TYPE_DIR;
    new_dir_entries[0].access_rights = READ | WRITE | EXECUTE;

    // .. (parent)
    strncpy(new_dir_entries[1].file_name, "..", 56);
    new_dir_entries[1].size = 0;
    new_dir_entries[1].first_blk = current_dir_block; // Points to parent
    new_dir_entries[1].type = TYPE_DIR;
    new_dir_entries[1].access_rights = READ | WRITE | EXECUTE;

    // 5. Update FAT and CWD entry
    fat[new_blk] = FAT_EOF; // Mark new directory block as occupied
    
    // Update CWD entry (the new directory link)
    strncpy(cwd_entries[free_entry_idx].file_name, dirpath.c_str(), 56);
    cwd_entries[free_entry_idx].size = 0; 
    cwd_entries[free_entry_idx].first_blk = new_blk;
    cwd_entries[free_entry_idx].type = TYPE_DIR;
    cwd_entries[free_entry_idx].access_rights = READ | WRITE | EXECUTE;

    // 6. Write back to disk
    disk.write(new_blk, new_dir_buf);
    disk.write(current_dir_block, cwd_buf);
    disk.write(FAT_BLOCK, (uint8_t*)fat);

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