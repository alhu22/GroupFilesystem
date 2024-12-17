#include <iostream>
#include <cstring>
#include "fs.h"

FS::FS()
{
    std::cout << "FS::FS()... Creating file system\n";
}

FS::~FS()
{

}

int
FS::format()
{
    std::cout << "FS::format()\n";
    for (int i = 0; i < BLOCK_SIZE / 2; ++i) {
        fat[i] = FAT_FREE;
    }
    fat[ROOT_BLOCK] = FAT_EOF;
    fat[FAT_BLOCK] = FAT_EOF;

    disk.write(FAT_BLOCK, reinterpret_cast<uint8_t*>(fat));

    dir_entry root_dir[BLOCK_SIZE / sizeof(dir_entry)] = {};
    disk.write(ROOT_BLOCK, reinterpret_cast<uint8_t*>(root_dir));

    std::cout << "Disk formatted successfully.\n";
    return 0;
}


// create <filepath> creates a new file on the disk, the data content is
// written on the following rows (ended with an empty row)
int
FS::create(std::string filename)
{
    std::cout << "FS::create(" << filename << ")\n";

    if (filename.size() > 55) {
        std::cerr << "Error: File name too long (max 55 characters).\n";
        return -1;
    }

    dir_entry root_dir[BLOCK_SIZE / sizeof(dir_entry)];
    disk.read(ROOT_BLOCK, reinterpret_cast<uint8_t*>(root_dir));

    for (const auto &entry : root_dir) {
        if (entry.file_name[0] != '\0' && filename == entry.file_name) {
            std::cerr << "Error: File already exists.\n";
            return -1;
        }
    }

    int free_entry_idx = -1;
    for (int i = 0; i < BLOCK_SIZE / sizeof(dir_entry); ++i) {
        if (root_dir[i].file_name[0] == '\0') {
            free_entry_idx = i;
            break;
        }
    }
    if (free_entry_idx == -1) {
        std::cerr << "Error: No space in root directory.\n";
        return -1;
    }

    int first_block = -1;
    for (int i = 2; i < BLOCK_SIZE / 2; ++i) {
        if (fat[i] == FAT_FREE) {
            first_block = i;
            break;
        }
    }
    if (first_block == -1) {
        std::cerr << "Error: No free blocks available.\n";
        return -1;
    }

    std::string data, line;
    std::cout << "Enter file content (end with empty line):\n";
    while (true) {
        std::getline(std::cin, line);
        if (line.empty()) break;
        data += line + "\n";
    }

    int current_block = first_block;
    size_t data_offset = 0;
    while (data_offset < data.size()) {
        uint8_t block_data[BLOCK_SIZE] = {};
        size_t chunk_size = std::min(static_cast<size_t>(BLOCK_SIZE), data.size() - data_offset);
        memcpy(block_data, data.c_str() + data_offset, chunk_size);
        disk.write(current_block, block_data);

        data_offset += chunk_size;
        if (data_offset < data.size()) {
            int next_block = -1;
            for (int i = 2; i < BLOCK_SIZE / 2; ++i) {
                if (fat[i] == FAT_FREE) {
                    next_block = i;
                    break;
                }
            }
            if (next_block == -1) {
                std::cerr << "Error: Insufficient disk space.\n";
                return -1;
            }
            fat[current_block] = next_block;
            current_block = next_block;
        } else {
            fat[current_block] = FAT_EOF;
        }
    }

    strncpy(root_dir[free_entry_idx].file_name, filename.c_str(), sizeof(root_dir[free_entry_idx].file_name) - 1);
    root_dir[free_entry_idx].size = data.size();
    root_dir[free_entry_idx].first_blk = first_block;
    root_dir[free_entry_idx].type = TYPE_FILE;
    root_dir[free_entry_idx].access_rights = READ | WRITE;

    disk.write(FAT_BLOCK, reinterpret_cast<uint8_t*>(fat));
    disk.write(ROOT_BLOCK, reinterpret_cast<uint8_t*>(root_dir));

    std::cout << "File created successfully: " << filename << "\n";
    return 0;
}

// cat <filepath> reads the content of a file and prints it on the screen
int
FS::cat(std::string filename)
{
    std::cout << "FS::cat(" << filename << ")\n";

    dir_entry root_dir[BLOCK_SIZE / sizeof(dir_entry)];
    disk.read(ROOT_BLOCK, reinterpret_cast<uint8_t*>(root_dir));

    int file_entry_idx = -1;
    for (int i = 0; i < BLOCK_SIZE / sizeof(dir_entry); ++i) {
        if (root_dir[i].file_name[0] != '\0' && filename == root_dir[i].file_name) {
            file_entry_idx = i;
            break;
        }
    }
    if (file_entry_idx == -1) {
        std::cerr << "Error: File not found.\n";
        return -1;
    }

    int current_block = root_dir[file_entry_idx].first_blk;
    if (current_block == FAT_FREE) {
        std::cerr << "Error: File is empty.\n";
        return -1;
    }

    std::cout << "File content:\n";
    while (current_block != FAT_EOF) {
        uint8_t block_data[BLOCK_SIZE] = {};
        disk.read(current_block, block_data);
        std::cout << reinterpret_cast<char*>(block_data);

        current_block = fat[current_block];
    }
    std::cout << "\n";

    return 0;
}


// ls lists the content in the currect directory (files and sub-directories)
int
FS::ls()
{
    std::cout << "FS::ls()\n";

    dir_entry root_dir[BLOCK_SIZE / sizeof(dir_entry)];
    disk.read(ROOT_BLOCK, reinterpret_cast<uint8_t*>(root_dir));

    std::cout << "name\tsize\n";

    for (int i = 0; i < BLOCK_SIZE / sizeof(dir_entry); ++i) {
        if (root_dir[i].file_name[0] != '\0') { // Valid entry
            std::cout << root_dir[i].file_name << "\t" 
                      << root_dir[i].size << "\n";
        }
    }

    return 0;
}


// cp <sourcepath> <destpath> makes an exact copy of the file
// <sourcepath> to a new file <destpath>
int
FS::cp(std::string sourcepath, std::string destpath)
{
    std::cout << "FS::cp(" << sourcepath << ", " << destpath << ")\n";

    dir_entry root_dir[BLOCK_SIZE / sizeof(dir_entry)];
    disk.read(ROOT_BLOCK, reinterpret_cast<uint8_t*>(root_dir));

    int source_idx = -1;
    for (int i = 0; i < BLOCK_SIZE / sizeof(dir_entry); ++i) {
        if (root_dir[i].file_name[0] != '\0' && root_dir[i].file_name == sourcepath) {
            source_idx = i;
            break;
        }
    }
    if (source_idx == -1) {
        std::cerr << "Error: Source file not found.\n";
        return -1;
    }

    for (const auto &entry : root_dir) {
        if (entry.file_name[0] != '\0' && destpath == entry.file_name) {
            std::cerr << "Error: Destination file already exists.\n";
            return -1;
        }
    }

    int free_entry_idx = -1;
    for (int i = 0; i < BLOCK_SIZE / sizeof(dir_entry); ++i) {
        if (root_dir[i].file_name[0] == '\0') {
            free_entry_idx = i;
            break;
        }
    }
    if (free_entry_idx == -1) {
        std::cerr << "Error: No space in root directory.\n";
        return -1;
    }

    int source_block = root_dir[source_idx].first_blk;
    int dest_first_block = -1, current_dest_block = -1;

    while (source_block != FAT_EOF) {
        int next_block = -1;
        for (int i = 2; i < BLOCK_SIZE / 2; ++i) {
            if (fat[i] == FAT_FREE) {
                next_block = i;
                break;
            }
        }
        if (next_block == -1) {
            std::cerr << "Error: No free blocks available.\n";
            return -1;
        }

        uint8_t block_data[BLOCK_SIZE] = {};
        disk.read(source_block, block_data);
        disk.write(next_block, block_data);
        fat[next_block] = FAT_EOF;

        if (dest_first_block == -1) {
            dest_first_block = next_block;
        } else {
            fat[current_dest_block] = next_block; 
        }
        current_dest_block = next_block;

        source_block = fat[source_block];
    }

    strncpy(root_dir[free_entry_idx].file_name, destpath.c_str(), sizeof(root_dir[free_entry_idx].file_name) - 1);
    root_dir[free_entry_idx].size = root_dir[source_idx].size;
    root_dir[free_entry_idx].first_blk = dest_first_block;
    root_dir[free_entry_idx].type = TYPE_FILE;
    root_dir[free_entry_idx].access_rights = READ | WRITE;

    disk.write(FAT_BLOCK, reinterpret_cast<uint8_t*>(fat));
    disk.write(ROOT_BLOCK, reinterpret_cast<uint8_t*>(root_dir));

    std::cout << "File copied successfully: " << sourcepath << " -> " << destpath << "\n";
    return 0;
}


// mv <sourcepath> <destpath> renames the file <sourcepath> to the name <destpath>,
// or moves the file <sourcepath> to the directory <destpath> (if dest is a directory)
int
FS::mv(std::string sourcepath, std::string destpath)
{
    std::cout << "FS::mv(" << sourcepath << ", " << destpath << ")\n";

    dir_entry root_dir[BLOCK_SIZE / sizeof(dir_entry)];
    disk.read(ROOT_BLOCK, reinterpret_cast<uint8_t*>(root_dir));

    int source_idx = -1;
    for (int i = 0; i < BLOCK_SIZE / sizeof(dir_entry); ++i) {
        if (root_dir[i].file_name[0] != '\0' && root_dir[i].file_name == sourcepath) {
            source_idx = i;
            break;
        }
    }
    if (source_idx == -1) {
        std::cerr << "Error: Source file not found.\n";
        return -1;
    }

    for (const auto &entry : root_dir) {
        if (entry.file_name[0] != '\0' && destpath == entry.file_name) {
            std::cerr << "Error: Destination file already exists.\n";
            return -1;
        }
    }

    strncpy(root_dir[source_idx].file_name, destpath.c_str(), sizeof(root_dir[source_idx].file_name) - 1);
    root_dir[source_idx].file_name[sizeof(root_dir[source_idx].file_name) - 1] = '\0';

    disk.write(ROOT_BLOCK, reinterpret_cast<uint8_t*>(root_dir));

    std::cout << "File moved/renamed successfully: " << sourcepath << " -> " << destpath << "\n";
    return 0;
}


// rm <filepath> removes / deletes the file <filepath>
int
FS::rm(std::string filepath)
{
    std::cout << "FS::rm(" << filepath << ")\n";

    dir_entry root_dir[BLOCK_SIZE / sizeof(dir_entry)];
    disk.read(ROOT_BLOCK, reinterpret_cast<uint8_t*>(root_dir));

    int file_idx = -1;
    for (int i = 0; i < BLOCK_SIZE / sizeof(dir_entry); ++i) {
        if (root_dir[i].file_name[0] != '\0' && filepath == root_dir[i].file_name) {
            file_idx = i;
            break;
        }
    }
    if (file_idx == -1) {
        std::cerr << "Error: File not found.\n";
        return -1; // File does not exist
    }

    int current_block = root_dir[file_idx].first_blk;
    while (current_block != FAT_EOF) {
        int next_block = fat[current_block];
        fat[current_block] = FAT_FREE;      
        current_block = next_block;         
    }


    memset(&root_dir[file_idx], 0, sizeof(dir_entry));

    disk.write(FAT_BLOCK, reinterpret_cast<uint8_t*>(fat));
    disk.write(ROOT_BLOCK, reinterpret_cast<uint8_t*>(root_dir));

    std::cout << "File deleted successfully: " << filepath << "\n";
    return 0;
}


// append <filepath1> <filepath2> appends the contents of file <filepath1> to
// the end of file <filepath2>. The file <filepath1> is unchanged.
int
FS::append(std::string sourcepath, std::string destpath)
{
    std::cout << "FS::append(" << sourcepath << ", " << destpath << ")\n";

    dir_entry root_dir[BLOCK_SIZE / sizeof(dir_entry)];
    disk.read(ROOT_BLOCK, reinterpret_cast<uint8_t*>(root_dir));

    int source_idx = -1, dest_idx = -1;
    for (int i = 0; i < BLOCK_SIZE / sizeof(dir_entry); ++i) {
        if (root_dir[i].file_name[0] != '\0') {
            if (root_dir[i].file_name == sourcepath) source_idx = i;
            if (root_dir[i].file_name == destpath) dest_idx = i;
        }
    }
    if (source_idx == -1 || dest_idx == -1) {
        std::cerr << "Error: Source or destination file not found.\n";
        return -1;
    }

    int dest_block = root_dir[dest_idx].first_blk;
    while (fat[dest_block] != FAT_EOF) {
        dest_block = fat[dest_block];
    }

    int source_block = root_dir[source_idx].first_blk;
    while (source_block != FAT_EOF) {

        int new_block = -1;
        for (int i = 2; i < BLOCK_SIZE / 2; ++i) {
            if (fat[i] == FAT_FREE) {
                new_block = i;
                break;
            }
        }
        if (new_block == -1) {
            std::cerr << "Error: No free blocks available.\n";
            return -1;
        }

        uint8_t block_data[BLOCK_SIZE] = {};
        disk.read(source_block, block_data);
        disk.write(new_block, block_data);

        fat[dest_block] = new_block;
        fat[new_block] = FAT_EOF;

        dest_block = new_block;
        source_block = fat[source_block];
    }

    root_dir[dest_idx].size += root_dir[source_idx].size;

    disk.write(FAT_BLOCK, reinterpret_cast<uint8_t*>(fat));
    disk.write(ROOT_BLOCK, reinterpret_cast<uint8_t*>(root_dir));

    std::cout << "File appended successfully: " << sourcepath << " -> " << destpath << "\n";
    return 0;
}


// mkdir <dirpath> creates a new sub-directory with the name <dirpath>
// in the current directory
int
FS::mkdir(std::string dirpath)
{
    std::cout << "FS::mkdir(" << dirpath << ")\n";
    return 0;
}

// cd <dirpath> changes the current (working) directory to the directory named <dirpath>
int
FS::cd(std::string dirpath)
{
    std::cout << "FS::cd(" << dirpath << ")\n";
    return 0;
}

// pwd prints the full path, i.e., from the root directory, to the current
// directory, including the currect directory name
int
FS::pwd()
{
    std::cout << "FS::pwd()\n";
    return 0;
}

// chmod <accessrights> <filepath> changes the access rights for the
// file <filepath> to <accessrights>.
int
FS::chmod(std::string accessrights, std::string filepath)
{
    std::cout << "FS::chmod(" << accessrights << "," << filepath << ")\n";
    return 0;
}
