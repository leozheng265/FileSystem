#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "disk.h"
#include "fs.h"

#define FAT_EOC 0xFFFF

// The SuperBlock, which stores disk information.
struct SuperBlock {
	uint64_t signature;
	uint16_t all_block_count;
	uint16_t root_start_index;
	uint16_t data_start_index;
	uint16_t data_block_count;
	uint8_t fat_block_count;
	uint8_t padding[4079];
}__attribute__((packed));

// File information entry within the root directory.
struct RootEntry {
	uint8_t filename[FS_FILENAME_LEN];
	uint32_t file_size;
	uint16_t data_index;
	uint8_t padding[10];
}__attribute__((packed));

// File Descriptor Information
struct FD {
	// index of the file in the root directory.
	// REF: Piazza @358.
	int index;
	// file offset.
	int offset;
};

// File descriptor table. Holds an array of FDs and its own size.
struct FDTable {
	struct FD *table;
	int open_files;
};

// The SuperBlock of the current disk.
struct SuperBlock *SBlock = NULL;
// The Root Directory that will contain 128 RootEntry for files.
struct RootEntry *RootDir = NULL;
// The FAT array of the current disk.
uint16_t *FAT = NULL;
// The File Descriptor table for keeping track of open files.
struct FDTable OpenTable;

// Sets up all the FD's in the table with initial values.
void InitTable()
{
	for (int i = 0; i < FS_OPEN_MAX_COUNT; i++)
	{
		// Use FAT_EOC to indicate that this FD slot is unused.
		OpenTable.table[i].index = FAT_EOC;
		OpenTable.table[i].offset = 0;
	}
	OpenTable.open_files = 0;
}

int fs_mount(const char *diskname)
{
	// Attempt to open the disk for reading and writing.
	// block_disk_open checks for most errors.
	if (block_disk_open(diskname)) return -1;

	// Put the superblock of the disk in readable form.
	SBlock = malloc(BLOCK_SIZE);
	if (SBlock == NULL) return -1;
	if (block_read(0, SBlock)) return -1;

	// Make sure superblock signature matches.
	if (memcmp(&SBlock->signature, "ECS150FS", 8)) return -1;

	// Make sure the correct number of blocks were read from the disk.
	if (SBlock->all_block_count != block_disk_count()) return -1;

	// Put FAT of disk into a readable form block by block.
	FAT = malloc(SBlock->fat_block_count * BLOCK_SIZE);
	if (FAT == NULL) return -1;
	for (int i = 0; i < SBlock->fat_block_count; i++)
	{
		if (block_read(1 + i, FAT + (BLOCK_SIZE / 2 * i)) == -1) return -1;
	}

	// Put the root directory's entries in readable form.
	RootDir = malloc(BLOCK_SIZE);
	if (RootDir == NULL) return -1;
	if (block_read(SBlock->root_start_index, RootDir) == -1) return -1;

	// Prepare space for the file table.
	OpenTable.table = malloc(sizeof(struct FD) * FS_OPEN_MAX_COUNT);
	if (OpenTable.table == NULL) return -1;
	InitTable();

	return 0;
}

int fs_umount(void)
{
	// No disk mounted.
	if (SBlock == NULL) return -1;

	// There are files open.
	if (OpenTable.open_files > 0) return -1;

	// Close currently read disk.
	if (block_disk_close()) return -1;
	
	// Cleanup readable storage for SuperBlock, Root, and FAT.
	free(SBlock);
	free(RootDir);
	free(FAT);
	free(OpenTable.table);
	SBlock = NULL;
	RootDir = NULL;
	FAT = NULL;
	OpenTable.table = NULL;
	return 0;
}

int fs_info(void)
{
	// No disk mounted.
	if (SBlock == NULL) return -1;

	// Counter for unused FAT and root entries.
	int free_fat_count = 0;
	int free_root_count = 0;

	// Get the number of unused FAT entries.
	for (int i = 0; i < SBlock->data_block_count; i++)
	{
		// If FAT Entry is 0, then its unused and add to the count.
		if (!FAT[i]) free_fat_count++;
	}

	// Get the number of unused root file entries.
	// Recall unused root files have null char as 1st char in filename.
	for (int k = 0; k < FS_FILE_MAX_COUNT; k++)
		if (RootDir[k].filename[0] == '\0') free_root_count++;
	
	// With everything known, print disk information.
	printf("FS Info:\n");
	printf("total_blk_count=%d\n", (int) SBlock->all_block_count);
	printf("fat_blk_count=%d\n", (int) SBlock->fat_block_count);
	printf("rdir_blk=%d\n", (int) SBlock->root_start_index);
	printf("data_blk=%d\n", (int) SBlock->data_start_index);
	printf("data_blk_count=%d\n", (int) SBlock->data_block_count);
	printf("fat_free_ratio=%d/%d\n", free_fat_count, SBlock->data_block_count);
	printf("rdir_free_ratio=%d/%d\n", free_root_count, FS_FILE_MAX_COUNT);

	return 0;
}

// Return the index of the first empty root entry.
// -1 if the root is full or filename already exists.
int RootEmptySearch(const char *filename)
{
	int index = -1;
	int first_flag = 0;
	for (int i = 0; i < FS_OPEN_MAX_COUNT; i++)
	{
		// Found the first empty root entry.
		if (RootDir[i].filename[0] == '\0' && !first_flag)
		{
			index = i;
			first_flag = 1;
		}

		// Filename already exists.
		if (!memcmp(RootDir[i].filename, filename, strlen(filename) + 1))
		{
			index = -1;
			break;
		}
	}
	return index;
}

// Searches the root directory for a for a file's entry by file name.
// Returns index in the root directory if found, -1 otherwise.
int RootNameSearch(const char *filename)
{
	for (int i = 0; i < FS_OPEN_MAX_COUNT; i++)
		if (!memcmp(RootDir[i].filename, filename, strlen(filename) + 1)) return i;
	return -1;
}

int fs_create(const char *filename)
{
	// No disk mounted.
	if (SBlock == NULL) return -1;

	// Invalid file name
	if (filename == NULL) return -1;

	// Length of the filename null character inclusive.
	int length = strlen(filename) + 1;

	// filename is too long.
	if (length > FS_FILENAME_LEN) return -1;

	// filename is not null terminated.
	if (filename[length - 1] != '\0') return -1;

	// Find the first empty root entry and get its index.
	int create_index = RootEmptySearch(filename);
	if (create_index == -1) return -1;

	// Create and setup a entry in the root directory for this new file.
	struct RootEntry new_file;
	for (int i = 0; i < length; i++)
	{
		new_file.filename[i] = filename[i];
	}
	new_file.file_size = 0;
	new_file.data_index = FAT_EOC;

	// Assign the new entry to the root directory and reflect that in the disk.
	RootDir[create_index] = new_file;
	block_write(SBlock->root_start_index, RootDir);
	return 0;
}

// Writes all FAT entries back to the disk.
void UpdateFAT()
{	
	for (int i = 0; i < SBlock->fat_block_count; i++)
	{
		block_write(1 + i, FAT + (BLOCK_SIZE / 2 * i));
	}
}

int fs_delete(const char *filename)
{
	// No disk mounted.
	if (SBlock == NULL) return -1;

	// Invalid file name.
	if (filename == NULL) return -1;

	// Length of the filename null character inclusive.
	int length = strlen(filename) + 1;

	// filename is too long.
	if (length > FS_FILENAME_LEN) return -1;

	// filename is not null terminated.
	if (filename[length - 1] != '\0') return -1;

	// Find the index in the root directory containing the file.
	int delete_index = RootNameSearch(filename);
	if (delete_index == -1) return -1;

	// Check that the file is not currently open.
	for (int i = 0; i < FS_OPEN_MAX_COUNT; i++)
	{
		if (OpenTable.table[i].index == delete_index) return -1;
	}

	// Empty block to write that "empties" a data block.
	uint8_t empty_block[BLOCK_SIZE];

	// Navigate the file's storage among the data blocks using the FAT.
	// Reset the data blocks and the FAT entries as we go.
	uint16_t cur_entry = RootDir[delete_index].data_index;
	uint16_t next_entry;
	while (cur_entry != FAT_EOC)
	{
		block_write(SBlock->data_start_index + cur_entry, empty_block);
		next_entry = FAT[cur_entry];
		FAT[cur_entry] = 0;
		cur_entry = next_entry;
	}

	// Reset the entry in the root directory.
	memset(RootDir[delete_index].filename, '\0', FS_FILENAME_LEN);
	RootDir[delete_index].file_size = 0;
	RootDir[delete_index].data_index = FAT_EOC;
	
	// Reflect changes to root and FAT back to the disk. 
	block_write(SBlock->root_start_index, RootDir);
	UpdateFAT();
	return 0;
}

int fs_ls(void)
{
	// No mounted disk.
	if (SBlock == NULL) return -1;

	printf("FS Ls:\n");
	for (int i = 0; i < FS_FILE_MAX_COUNT; i++)
	{
		// If the file space is used, print its information.
		if (RootDir[i].filename[0] != '\0')
			printf("file: %s, size: %d, data_blk: %d\n", RootDir[i].filename, RootDir[i].file_size, 
				RootDir[i].data_index);
	}
	return 0;
}

// Get the first empty File Descriptor in the file table.
int FirstEmptyFD()
{
	for (int i = 0; i < FS_OPEN_MAX_COUNT; i++)
	{
		if (OpenTable.table[i].index == FAT_EOC) 
		{
			return i;
		}
	}
	return -1;
}

int fs_open(const char *filename)
{
	// No disk mounted.
	if (SBlock == NULL) return -1;

	// Invalid filename or filename is too long.
	if (filename == NULL) return -1;
	if (strlen(filename) > FS_FILENAME_LEN - 1) return -1;

	// File table is maxed out on open files.
	if (OpenTable.open_files == FS_OPEN_MAX_COUNT) return -1;

	// Find the index of this file in the root directory.
	int root_index = RootNameSearch(filename);
	if (root_index == -1) return -1;

	// Get the index of the first empty FD to fill in information.
	int FD_index = FirstEmptyFD();
	OpenTable.table[FD_index].index = root_index;
	OpenTable.table[FD_index].offset = 0;

	OpenTable.open_files++;
	return FD_index;
}

int fs_close(int fd)
{
	// No disk mounted.
	if (SBlock == NULL) return -1;

	// fd out of bounds.
	if (fd < 0 || fd > FS_OPEN_MAX_COUNT - 1) return -1;

	// There are no files to close.
	if (OpenTable.open_files == 0) return -1;
	
	// Check if that fd contains an open file.
	if (OpenTable.table[fd].index == FAT_EOC) return -1;

	// Reset the information in the fd slot.
	OpenTable.table[fd].index = FAT_EOC;
	OpenTable.table[fd].offset = 0;
	OpenTable.open_files--;
	return 0;
}

int fs_stat(int fd)
{
	// No disk mounted.
	if (SBlock == NULL) return -1;

	// fd out of bounds.
	if (fd < 0 || fd > FS_OPEN_MAX_COUNT - 1) return -1;

	// Check if that fd contains an open file.
	if (OpenTable.table[fd].index == FAT_EOC) return -1;

	// Otherwise, navigate to the root directory using the fd's index value and return the file's size.
	return RootDir[OpenTable.table[fd].index].file_size;
}

int fs_lseek(int fd, size_t offset)
{
	// Get the size of the current file and make sure offset isn't too big.
	// fs_stat handles basic errors like no mounts or bad fd.
	int file_size = fs_stat(fd);
	if (file_size == -1 || (int) offset > file_size) return -1;

	OpenTable.table[fd].offset = offset;
	return 0;
}

// Uses a file's offset to locate its current data block number.
uint16_t GetCurrentPosition(int fd)
{
	int root_index = OpenTable.table[fd].index;
	int cur_offset = OpenTable.table[fd].offset;
	int block_number = RootDir[root_index].data_index;

	// For each multiple of BLOCK_SIZE, move forward one data block using the FAT.
	while (cur_offset > BLOCK_SIZE - 1 && block_number != FAT_EOC)
	{
		block_number = FAT[block_number];
		cur_offset -= BLOCK_SIZE;
	}
	return block_number;
}

// Returns the index of the first free FAT entry.
uint16_t AssignNewFAT()
{
	for (int i = 0; i < SBlock->data_block_count; i++)
	{
		if (FAT[i] == 0) return i;
	}	
	// Disk is full, no space available.
	return FAT_EOC;
}

int fs_write(int fd, void *buf, size_t count)
{
	// No disk mounted.
	if (SBlock == NULL) return -1;

	// fd out of bounds.
	if (fd < 0 || fd > FS_OPEN_MAX_COUNT - 1) return -1;

	// Null buffer
	if (buf == NULL) return -1;

	// Check that fd contains an open file.
	if (OpenTable.table[fd].index == FAT_EOC) return -1;

	// We actually want to write something...
	if (count <= 0) return 0;

	// Index of the current block that the file is in.
	uint16_t write_block = GetCurrentPosition(fd);

	// Writing to an empty file.
	if (write_block == FAT_EOC)
	{
		// Find the first free data block to start the file in.
		write_block = AssignNewFAT();

		// There are no data blocks available, so return "wrote nothing."
		if (write_block == FAT_EOC) return 0;

		// Set the data start index in the root directory and update the FAT.
		RootDir[OpenTable.table[fd].index].data_index = write_block;
		FAT[write_block] = FAT_EOC;
	}

	// Bounce buffer to write buf to then copy back to disk.
	uint8_t *write_line = malloc(BLOCK_SIZE);
	// Keep track of how many bytes left to write.
	int write_capacity = count;
	// Number of bytes actually written.
	int wrote_bytes = 0;
	// Current position in the data block based on offset.
	int cur_byte = OpenTable.table[fd].offset % BLOCK_SIZE;
	// Determines if we write the bounce buffer or skip and write an entire buffer block.
	uint8_t *to_write = NULL;

	// Write until written count bytes or run out of space.
	while (write_capacity > 0)
	{
		to_write = write_line;
		// If not enough writing bytes left, write as much as possible.
		if (BLOCK_SIZE - cur_byte > write_capacity)
		{
			block_read(SBlock->data_start_index + write_block, write_line);
			memcpy(write_line + cur_byte, buf + wrote_bytes, write_capacity);
			wrote_bytes += write_capacity;
			write_capacity = 0;
		}
		else {
		// Otherwise, write from the offset to the end of the block.
			// If at beginning of a block and can write a block's length,
			// then no need for a bounce buffer.
			if (cur_byte == 0) to_write = buf + wrote_bytes;
			else {
				block_read(SBlock->data_start_index + write_block, write_line);
				memcpy(write_line + cur_byte, buf + wrote_bytes, BLOCK_SIZE - cur_byte);
			}
			wrote_bytes += (BLOCK_SIZE - cur_byte);
			write_capacity -= (BLOCK_SIZE - cur_byte);
		}

		// Write the edited block back to disk.
		block_write(SBlock->data_start_index + write_block, to_write);

		// If there's still more to write, move to the next block.
		if (write_capacity > 0)
		{
			// Use the FAT to get the next block to write to.
			uint16_t next_block = FAT[write_block];

			// If that was the last block of the file, extend the file.
			if (next_block == FAT_EOC)
			{
				// Find the first available FAT Entry.
				next_block = AssignNewFAT();

				// If the disk is full, break and stop writing.
				if (next_block == FAT_EOC) break;

				// Otherwise, add a new FAT link write_block -> next_block -> FAT_EOC.
				FAT[write_block] = next_block;
				FAT[next_block] = FAT_EOC;
			}

			// Read the new block and start writing to it.
			write_block = next_block;
		}
		cur_byte = 0;
	}

	// Update the size of the file to account for written bytes.
	RootDir[OpenTable.table[fd].index].file_size += wrote_bytes;

	// Update the root and FAT on the disk.
	block_write(SBlock->root_start_index, RootDir);
	UpdateFAT();

	// Move the file's offset.
	fs_lseek(fd, OpenTable.table[fd].offset + wrote_bytes);

	free(write_line);
	return wrote_bytes;
}

int fs_read(int fd, void *buf, size_t count)
{
	// No disk mounted.
	if (SBlock == NULL) return -1;

	// fd out of bounds.
	if (fd < 0 || fd > FS_OPEN_MAX_COUNT - 1) return -1;

	// Null buffer
	if (buf == NULL) return -1;

	// Check that fd contains an open file.
	if (OpenTable.table[fd].index == FAT_EOC) return -1;

	// We actually want to read something...
	if (count <= 0) return 0;

	// A buffer that takes in an entire data block for reading.
	uint8_t *read_data = malloc(BLOCK_SIZE);
	// Index of the current block the file is in.
	uint16_t read_block = GetCurrentPosition(fd);
	// How much to actually read, if ordered to go past EOF, just go to EOF.
	int read_capacity = count;
	if (OpenTable.table[fd].offset + count > RootDir[OpenTable.table[fd].index].file_size)
		read_capacity = RootDir[OpenTable.table[fd].index].file_size - OpenTable.table[fd].offset;
	// How many bytes actually get read.
	int read_bytes = 0;
	// Current location within the block being read. Based on offset.
	int cur_byte = OpenTable.table[fd].offset % BLOCK_SIZE;

	// Start read operation. Read until out of allowed reading capacity.
	while (read_capacity > 0 && read_block != FAT_EOC)
	{
		// If not enough reading bytes left, read as much as possible.
		if (BLOCK_SIZE - cur_byte > read_capacity)
		{
			block_read(SBlock->data_start_index + read_block, read_data);
			memcpy(buf + read_bytes, read_data + cur_byte, read_capacity);
			read_bytes += read_capacity;
			read_capacity = 0;
		}
		else {
		// Otherwise, read from current offset to the end of the block.
			// If at beginning of a block and can read to end of block,
			// just read straight into buffer without need of a bounce.
			if (cur_byte == 0) block_read(SBlock->data_start_index + read_block, buf + read_bytes);
			else {
				block_read(SBlock->data_start_index + read_block, read_data);
				memcpy(buf + read_bytes, read_data + cur_byte, BLOCK_SIZE - cur_byte);
			}
			read_capacity -= (BLOCK_SIZE - cur_byte);
			read_bytes += (BLOCK_SIZE - cur_byte);
		}

		// Move to the next data block using the FAT.
		read_block = FAT[read_block];
		cur_byte = 0;
	}

	// Move the file's offset.
	fs_lseek(fd, OpenTable.table[fd].offset + read_bytes);

	free(read_data);
	return read_bytes;
}
