# File System

## Introduction

This project implements a FAT-based file system which is composed of four consecutive logical parts, including the Superblock, the File Allocation Table, the Root directory and the Data blocks.

## Building the Project

- This project is to run in linux environment only.
- To build the project simply run “make” command in the apps directory.

## Testing the Project

- To create a new virtual disk:
    
    ```bash
    ./fs_make.x disk.fs 4096
    ```
    
- To get information from the virtual disk:
    
    ```bash
    ./test_fs.x info disk.fs
    ```
    
- To list all the files in the virtual disk:
    
    ```bash
    ./test_fs.x ls disk.fs
    ```
    
- To add a file to the virtual disk:
    
    ```bash
    echo "This is a test message" > 1.txt
    echo "This a second test message" > 2.txt
    ./test_fs.x add disk.fs 1.txt
    ./test_fs.x add disk.fs 2.txt
    ```
    
- To remove a file:
    
    ```bash
    ./test_fs.x rm disk.fs 1.txt
    ```
    
- To read the content of a file:
    
    ```bash
    ./test_fs.x cat disk.fs 2.txt
    ```
    
- To return the size of the file corresponding to the specified file descriptor:
    
    ```bash
    ./test_fs.x stat disk.fs 2.txt
    ```
