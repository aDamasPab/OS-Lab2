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
    // 1. Clear Root Directory (Block 0)
    uint8_t zeroBlock[BLOCK_SIZE] = {0};
    // Initialize FAT (assuming this part is done and fat array is loaded)
    for (int i = 0; i < BLOCK_SIZE/2; ++i) {
        fat[i] = FAT_FREE;
    }
    fat[ROOT_BLOCK] = FAT_EOF;
    fat[FAT_BLOCK] = FAT_EOF;
    save_fat();

    // 2. Initialize Root Directory Entries (., ..)
    dir_entry* rootEntries = (dir_entry*)zeroBlock;
    
    // . (self)
    strncpy(rootEntries[0].file_name, ".", 56);
    rootEntries[0].size = 0;
    rootEntries[0].first_blk = ROOT_BLOCK; 
    rootEntries[0].type = TYPE_DIR;
    rootEntries[0].access_rights = READ | WRITE | EXECUTE;

    // .. (parent) - In the root directory, '..' points to '.' (itself)
    strncpy(rootEntries[1].file_name, "..", 56);
    rootEntries[1].size = 0;
    rootEntries[1].first_blk = ROOT_BLOCK; 
    rootEntries[1].type = TYPE_DIR;
    rootEntries[1].access_rights = READ | WRITE | EXECUTE;

    // 3. Save Root and FAT
    if (disk.write(ROOT_BLOCK, (uint8_t*)rootEntries) < 0) return -1;

    // 4. Reset CWD state
    current_dir_block = ROOT_BLOCK;
    current_path = "/";

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
            std::cout << entries[i].file_name << "\t";
            
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

    // 1. Cargar Root
    uint8_t blockBuffer[BLOCK_SIZE];
    disk.read(ROOT_BLOCK, blockBuffer);
    dir_entry* root = (dir_entry*)blockBuffer;

    // 2. Buscar origen
    int srcIdx = -1;
    for (int i = 0; i < BLOCK_SIZE / sizeof(dir_entry); ++i) {
        if (strcmp(root[i].file_name, sourcepath.c_str()) == 0) {
            srcIdx = i;
            break;
        }
    }

    if (srcIdx == -1) {
        std::cout << "Error: File not found.\n";
        return -1;
    }

    // 3. Verificar destino (Noclobber)
    for (int i = 0; i < BLOCK_SIZE / sizeof(dir_entry); ++i) {
        if (strcmp(root[i].file_name, destpath.c_str()) == 0) {
            std::cout << "Error: Destination file already exists.\n";
            return -1;
        }
    }

    // 4. Renombrar
    // Como es un sistema de un solo nivel (por ahora), mv es solo renombrar.
    memset(root[srcIdx].file_name, 0, 56);
    strcpy(root[srcIdx].file_name, destpath.c_str());

    // 5. Guardar
    disk.write(ROOT_BLOCK, (uint8_t*)root);

    return 0;

    std::cout << "FS::mv(" << sourcepath << "," << destpath << ")\n";
}

// rm <filepath> removes / deletes the file <filepath>
int FS::rm(std::string filepath)
{
    // 1. Cargar
    uint8_t blockBuffer[BLOCK_SIZE];
    disk.read(ROOT_BLOCK, blockBuffer);
    dir_entry* root = (dir_entry*)blockBuffer;
    
    disk.read(FAT_BLOCK, (uint8_t*)fat);

    // 2. Buscar archivo
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

    // 3. Liberar bloques en la FAT
    int curr = root[idx].first_blk;
    // Asumimos que first_blk es válido si size > 0. 
    // Si size es 0, first_blk podría ser FAT_EOF o 0, verificamos.
    if (root[idx].size > 0 && curr != FAT_EOF && curr != 0) {
        while (curr != FAT_EOF && curr != FAT_FREE) {
            int next = fat[curr];
            fat[curr] = FAT_FREE; // Marcar libre
            curr = next;
        }
    }

    // 4. Liberar entrada de directorio
    // Limpiamos todo el struct o solo ponemos el primer char del nombre a 0
    memset(&root[idx], 0, sizeof(dir_entry)); 

    // 5. Guardar
    disk.write(FAT_BLOCK, (uint8_t*)fat);
    disk.write(ROOT_BLOCK, (uint8_t*)root);

    return 0;

    std::cout << "FS::rm(" << filepath << ")\n";
}

// append <filepath1> <filepath2> appends the contents of file <filepath1> to
// the end of file <filepath2>. The file <filepath1> is unchanged.
int FS::append(std::string filepath1, std::string filepath2)
{
    if (filepath1 == filepath2) {
        std::cout << "Error: Cannot append file to itself.\n";
        return -1;
    }

    // 1. Cargar estructuras
    uint8_t rootBuf[BLOCK_SIZE];
    disk.read(ROOT_BLOCK, rootBuf);
    dir_entry* root = (dir_entry*)rootBuf;
    disk.read(FAT_BLOCK, (uint8_t*)fat);

    // 2. Encontrar archivos
    int idx1 = -1, idx2 = -1;
    for (int i = 0; i < BLOCK_SIZE / sizeof(dir_entry); ++i) {
        if (strcmp(root[i].file_name, filepath1.c_str()) == 0) idx1 = i;
        if (strcmp(root[i].file_name, filepath2.c_str()) == 0) idx2 = i;
    }

    if (idx1 == -1 || idx2 == -1) {
        std::cout << "Error: One or both files not found.\n";
        return -1;
    }

    // Si origen está vacío, nada que hacer
    if (root[idx1].size == 0) return 0;

    // 3. Preparar lectura de File 1
    int currSrc = root[idx1].first_blk;
    int srcSize = root[idx1].size;
    int srcRead = 0;

    // 4. Preparar escritura en File 2
    // Necesitamos encontrar el último bloque de File 2
    int currDest = root[idx2].first_blk;
    int lastDest = -1;

    // Si File 2 está vacío, hay que asignar el primer bloque
    if (root[idx2].size == 0 || currDest == FAT_EOF || currDest == 0) {
        // Buscar bloque libre
        int newBlk = -1;
        for (int k = 2; k < disk.get_no_blocks(); ++k) {
            if (fat[k] == FAT_FREE) { newBlk = k; break; }
        }
        if (newBlk == -1) { std::cout << "Disk full.\n"; return -1; }
        
        fat[newBlk] = FAT_EOF;
        root[idx2].first_blk = newBlk;
        currDest = newBlk;
        lastDest = newBlk;
    } else {
        // Recorrer FAT hasta el final
        while (fat[currDest] != FAT_EOF) {
            currDest = fat[currDest];
        }
        lastDest = currDest;
    }

    // Calcular offset en el último bloque de destino
    int offset = root[idx2].size % BLOCK_SIZE;
    // Si el archivo es multiplo exacto de BLOCK_SIZE y no es 0, offset será 0.
    // Pero en ese caso el bloque está lleno, necesitamos uno nuevo.
    // Lógica simplificada: Leemos el último bloque al buffer.
    
    uint8_t destBuf[BLOCK_SIZE];
    disk.read(lastDest, destBuf);

    // Bucle para leer todo el Source y escribir en Dest
    uint8_t srcBuf[BLOCK_SIZE];
    
    while (srcRead < srcSize) {
        // Leer bloque actual de source
        disk.read(currSrc, srcBuf);
        
        // Cuánto queda por leer de este bloque source
        int bytesInSrcBlock = std::min((int)BLOCK_SIZE, srcSize - srcRead);
        int srcBlockOffset = 0; // Posición dentro del buffer de lectura

        while (bytesInSrcBlock > 0) {
            // Cuánto cabe en el bloque actual de destino
            int spaceInDest = BLOCK_SIZE - offset;

            // Si el bloque destino está lleno, asignar uno nuevo
            if (spaceInDest == 0) {
                disk.write(lastDest, destBuf); // Guardar el lleno

                int newBlk = -1;
                for (int k = 2; k < disk.get_no_blocks(); ++k) {
                    if (fat[k] == FAT_FREE) { newBlk = k; break; }
                }
                if (newBlk == -1) {
                    std::cout << "Disk full during append.\n";
                    // Actualizar tamaño hasta donde llegamos y guardar
                    root[idx2].size += (srcRead - bytesInSrcBlock); // Ajuste aproximado
                    disk.write(FAT_BLOCK, (uint8_t*)fat);
                    disk.write(ROOT_BLOCK, (uint8_t*)root);
                    return -1;
                }

                fat[lastDest] = newBlk; // Enlazar
                fat[newBlk] = FAT_EOF;
                lastDest = newBlk;
                
                // Resetear buffer destino y offset
                memset(destBuf, 0, BLOCK_SIZE);
                offset = 0;
                spaceInDest = BLOCK_SIZE;
            }

            // Copiar lo que quepa
            int toCopy = std::min(bytesInSrcBlock, spaceInDest);
            memcpy(destBuf + offset, srcBuf + srcBlockOffset, toCopy);

            offset += toCopy;
            srcBlockOffset += toCopy;
            bytesInSrcBlock -= toCopy;
            srcRead += toCopy;
            root[idx2].size += toCopy;
        }

        // Avanzar al siguiente bloque de source
        currSrc = fat[currSrc];
    }

    // Escribir el último bloque de destino (que puede estar parcialmente lleno)
    disk.write(lastDest, destBuf);

    // 5. Guardar metadatos
    disk.write(FAT_BLOCK, (uint8_t*)fat);
    disk.write(ROOT_BLOCK, (uint8_t*)root);

    return 0;
    
    std::cout << "FS::append(" << filepath1 << "," << filepath2 << ")\n";
}

// mkdir <dirpath> creates a new sub-directory with the name <dirpath>
// in the current directory
int FS::mkdir(std::string dirpath)
{
    // 1. Load CWD and FAT
    uint8_t cwdBuf[BLOCK_SIZE];
    disk.read(current_dir_block, cwdBuf);
    dir_entry* cwdEntries = (dir_entry*)cwdBuf;
    
    disk.read(FAT_BLOCK, (uint8_t*)fat);

    int numEntries = BLOCK_SIZE / sizeof(dir_entry);

    // 2. Check if name already exists and find a free entry slot
    int freeEntryIdx = -1;
    for (int i = 0; i < numEntries; ++i) {
        if (cwdEntries[i].file_name[0] != '\0' && strcmp(cwdEntries[i].file_name, dirpath.c_str()) == 0) {
            std::cout << "Error: Entry '" << dirpath << "' already exists.\n";
            return -1;
        }
        if (cwdEntries[i].file_name[0] == '\0' && freeEntryIdx == -1) {
            freeEntryIdx = i;
        }
    }

    if (freeEntryIdx == -1) {
        std::cout << "Error: Current directory is full.\n";
        return -1;
    }

    // 3. Find a free data block for the new directory
    int newBlk = -1;
    int fat_size = disk.get_no_blocks();
    for (int k = 2; k < fat_size; ++k) {
        if (fat[k] == FAT_FREE) {
            newBlk = k;
            break;
        }
    }
    if (newBlk == -1) {
        std::cout << "Error: Disk is full.\n";
        return -1;
    }

    // 4. Create Directory Entries (., ..) in the new block
    uint8_t newDirBuf[BLOCK_SIZE];
    memset(newDirBuf, 0, BLOCK_SIZE);
    dir_entry* newDirEntries = (dir_entry*)newDirBuf;
    
    // . (self)
    strncpy(newDirEntries[0].file_name, ".", 56);
    newDirEntries[0].size = 0;
    newDirEntries[0].first_blk = newBlk; // Points to itself
    newDirEntries[0].type = TYPE_DIR;
    newDirEntries[0].access_rights = READ | WRITE | EXECUTE;

    // .. (parent)
    strncpy(newDirEntries[1].file_name, "..", 56);
    newDirEntries[1].size = 0;
    newDirEntries[1].first_blk = current_dir_block; // Points to parent
    newDirEntries[1].type = TYPE_DIR;
    newDirEntries[1].access_rights = READ | WRITE | EXECUTE;

    // 5. Update FAT and CWD entry
    fat[newBlk] = FAT_EOF; // Mark new directory block as occupied
    
    // Update CWD entry (the new directory link)
    strncpy(cwdEntries[freeEntryIdx].file_name, dirpath.c_str(), 56);
    cwdEntries[freeEntryIdx].size = 0; 
    cwdEntries[freeEntryIdx].first_blk = newBlk;
    cwdEntries[freeEntryIdx].type = TYPE_DIR;
    cwdEntries[freeEntryIdx].access_rights = READ | WRITE | EXECUTE;

    // 6. Write back to disk
    disk.write(newBlk, newDirBuf);
    disk.write(current_dir_block, cwdBuf);
    disk.write(FAT_BLOCK, (uint8_t*)fat);

    return 0;
}

// cd <dirpath> changes the current (working) directory to the directory named <dirpath>
int FS::cd(std::string dirpath)
{
    // 1. Load CWD
    uint8_t cwdBuf[BLOCK_SIZE];
    disk.read(current_dir_block, cwdBuf);
    dir_entry* cwdEntries = (dir_entry*)cwdBuf;

    int numEntries = BLOCK_SIZE / sizeof(dir_entry);

    // 2. Find the target directory entry
    int targetIdx = -1;
    for (int i = 0; i < numEntries; ++i) {
        if (cwdEntries[i].file_name[0] != '\0' && strcmp(cwdEntries[i].file_name, dirpath.c_str()) == 0) {
            targetIdx = i;
            break;
        }
    }

    if (targetIdx == -1) {
        std::cout << "Error: Directory '" << dirpath << "' not found.\n";
        return -1;
    }

    // 3. Check if it's actually a directory
    if (cwdEntries[targetIdx].type != TYPE_DIR) {
        std::cout << "Error: '" << dirpath << "' is not a directory.\n";
        return -1;
    }

    // 4. Update CWD block
    uint16_t new_dir_block = cwdEntries[targetIdx].first_blk;
    current_dir_block = new_dir_block;

    // 5. Update current_path (pwd)
    if (dirpath == "/") {
        current_path = "/";
    } else if (dirpath == "..") {
        if (current_path != "/") {
            // Find the last '/' and remove the last directory component
            size_t last_slash = current_path.rfind('/');
            if (last_slash == 0) { 
                current_path = "/"; // Went from /dir to /
            } else { 
                current_path = current_path.substr(0, last_slash); // Went from /d1/d2 to /d1
            }
        }
    } else if (dirpath != ".") {
        if (current_path == "/") {
            current_path += dirpath;
        } else {
            current_path += "/" + dirpath;
        }
    }
    
    return 0;
}

// pwd prints the full path, i.e., from the root directory, to the current
// directory, including the currect directory name
int FS::pwd()
{
    std::cout << current_path << std::endl;
    return 0;
}

// chmod <accessrights> <filepath> changes the access rights for the
// file <filepath> to <accessrights>.
int FS::chmod(std::string accessrights, std::string filepath)
{
    std::cout << "FS::chmod(" << accessrights << "," << filepath << ")\n";
    return 0;
}
