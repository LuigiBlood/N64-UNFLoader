/***************************************************************
                      device_everdrive.cpp
                               
Handles EverDrive USB communication. A lot of the code here 
is courtesy of KRIKzz's USB tool:
http://krikzz.com/pub/support/everdrive-64/x-series/dev/
***************************************************************/

#include "main.h"
#include "helper.h"
#include "device_everdrive.h"


/*==============================
    device_test_everdrive
    Checks whether the device passed as an argument is EverDrive
    @param A pointer to the cart context
    @param The index of the cart
==============================*/

bool device_test_everdrive(ftdi_context_t* cart, int index)
{
    if (strcmp(cart->dev_info[index].Description, "FT245R USB FIFO") == 0 &&cart->dev_info[index].ID == 0x04036001)
    {
        char send_buff[16];
        char recv_buff[16];
        memset(send_buff, 0, 16);
        memset(recv_buff, 0, 16);  

        // Define the command to send
        send_buff[0] = 'c';
        send_buff[1] = 'm';
        send_buff[2] = 'd';
        send_buff[3] = 't'; 

        // Open the device
        cart->status = FT_Open(index, &cart->handle);
        if(cart->status != FT_OK || !cart->handle) 
        {
            free(cart->dev_info);
            terminate("Error: Could not open device.\n");
        }

        // Initialize the USB
        testcommand(FT_ResetDevice(cart->handle), "Error: Unable to reset flashcart.\n");
        testcommand(FT_SetTimeouts(cart->handle, 500, 500), "Error: Unable to set flashcart timeouts.\n");
        testcommand(FT_Purge(cart->handle, FT_PURGE_RX | FT_PURGE_TX), "Error: Unable to purge USB contents.\n");

        // Send the test command
        testcommand(FT_Write(cart->handle, send_buff, 16, &cart->bytes_written), "Error: Unable to write to flashcart.\n");
        testcommand(FT_Read(cart->handle, recv_buff, 16, &cart->bytes_read), "Error: Unable to read from flashcart.\n");
        testcommand(FT_Close(cart->handle), "Error: Unable to close flashcart.\n");

        // Check if the EverDrive responded correctly
        return recv_buff[3] == 'r';
    }
    return false;
}


/*==============================
    device_open_everdrive
    Opens the USB pipe
    @param A pointer to the cart context
==============================*/

void device_open_everdrive(ftdi_context_t* cart)
{
    // Open the cart
    cart->status = FT_Open(cart->device_index, &cart->handle);
    if (cart->status != FT_OK || !cart->handle)
        terminate("Error: Unable to open flashcart.\n");

    // Reset the cart
    testcommand(FT_ResetDevice(cart->handle), "Error: Unable to reset flashcart.\n");
    testcommand(FT_SetTimeouts(cart->handle, 500, 500), "Error: Unable to set flashcart timeouts.\n");
    testcommand(FT_Purge(cart->handle, FT_PURGE_RX | FT_PURGE_TX), "Error: Unable to purge USB contents.\n");
}

/*==============================
    device_sendcmd_everdrive
    Sends a command to the flashcart
    @param A pointer to the cart context
    @param A char with the command to send
    @param The address to send the data to
    @param The size of the data to send
    @param Any other args to send with the data
==============================*/

void device_sendcmd_everdrive(ftdi_context_t* cart, char command, int address, int size, int arg)
{
    char* cmd_buffer = (char*) malloc(sizeof(char) * 16);
    size /= 512;

    // Check we managed to malloc
    if (cmd_buffer == NULL)
        terminate("Error: Unable to allocate memory for buffer.\n");

    // Define the command and send it
    cmd_buffer[0] = 'c';
    cmd_buffer[1] = 'm';
    cmd_buffer[2] = 'd'; 
    cmd_buffer[3] = command;
    cmd_buffer[4] = (char) (address >> 24);
    cmd_buffer[5] = (char) (address >> 16);
    cmd_buffer[6] = (char) (address >> 8);
    cmd_buffer[7] = (char) (address);
    cmd_buffer[8] = (char) (size >> 24);
    cmd_buffer[9] = (char) (size >> 16);
    cmd_buffer[10]= (char) (size >> 8);
    cmd_buffer[11]= (char) (size);
    cmd_buffer[12]= (char) (arg >> 24);
    cmd_buffer[13]= (char) (arg >> 16);
    cmd_buffer[14]= (char) (arg >> 8);
    cmd_buffer[15]= (char) (arg);
    FT_Write(cart->handle, cmd_buffer, 16, &cart->bytes_written);

    // Free the allocated memory
    free(cmd_buffer);
}


/*==============================
    device_sendrom_everdrive
    Sends the ROM to the flashcart
    @param A pointer to the cart context
    @param A pointer to the ROM to send
    @param The size of the ROM
==============================*/

void device_sendrom_everdrive(ftdi_context_t* cart, FILE *file, u32 size)
{
	int	   bytes_done = 0;
    int	   bytes_left;
	int	   bytes_do;
    char*  rom_buffer = (char*) malloc(sizeof(int) * 32*1024);
    int    crc_area = 0x100000 + 4096;
    time_t upload_time = clock();

    // Check we managed to malloc
    if (rom_buffer == NULL)
        terminate("Error: Unable to allocate memory for buffer.\n");

    // Fill memory if the file is too small
    if ((int)size < crc_area)
    {
        char recv_buff[16];
        pdprint("Filling ROM.\n", CRDEF_PROGRAM);
        device_sendcmd_everdrive(cart, 'c', 0x10000000, crc_area, 0);
        device_sendcmd_everdrive(cart, 't', 0, 0, 0);
        FT_Read(cart->handle, recv_buff, 16, &cart->bytes_read);
    }

    // Get the correctly padded ROM size
    size = calc_padsize(size);
    bytes_left = size;

    // State that we're gonna send
    pdprint("\n", CRDEF_PROGRAM);
    progressbar_draw("Uploading ROM", 0);

    // Send a command saying we're about to write to the cart
    device_sendcmd_everdrive(cart, 'W', 0x10000000, size, 0);

    // Upload the ROM
    for ( ; ; )
    {
        int i;

        // Decide how many bytes to send
		if(bytes_left >= 0x8000) 
			bytes_do = 0x8000;
		else
			bytes_do = bytes_left;

        // End if we've got nothing else to send
		if (bytes_do <= 0) 
            break;

        // Try to send chunks
		for (i=0; i<2; i++)
        {
            int j;

            // If we failed the first time, clear the USB and try again
			if (i == 1) 
            {
				FT_ResetPort(cart->handle);
				FT_ResetDevice(cart->handle);
				FT_Purge(cart->handle, FT_PURGE_RX | FT_PURGE_TX);
			}

			// Send the chunk to RAM. If we reached EOF it doesn't matter what we send
            // TODO: Send 0's when EOF is reached
			fread(rom_buffer, bytes_do, 1, file);
            if (global_z64)
                for (j=0; j<bytes_do; j+=2)
                    SWAP(rom_buffer[j], rom_buffer[j+1]);
			FT_Write(cart->handle, rom_buffer, bytes_do, &cart->bytes_written);

            // If we managed to write, don't try again
			if(cart->bytes_written) 
                break;
		}

        // Check for a timeout
		if(cart->bytes_written == 0) 
        {
            free(rom_buffer);
            terminate("Error: Everdrive timed out");
        }

         // Keep track of how many bytes were uploaded
		bytes_left -= bytes_do;
		bytes_done += bytes_do;

		// Draw the progress bar
		progressbar_draw("Uploading ROM", (float)bytes_done/size);
    }

    // Send the PIFboot command
    #ifndef LINUX // Delay is needed or it won't boot properly
        Sleep(500);
    #else
        usleep(500);
    #endif
    pdprint_replace("Sending pifboot\n", CRDEF_PROGRAM);
    device_sendcmd_everdrive(cart, 's', 0, 0, 0);
    
    // Print that we've finished
    pdprint_replace("ROM successfully uploaded in %.2f seconds!\n", CRDEF_PROGRAM, ((double)(clock()-upload_time))/CLOCKS_PER_SEC);
    free(rom_buffer);
}


/*==============================
    device_senddata_everdrive
    Sends data to the flashcart
    @param A pointer to the cart context
    @param A pointer to the data to send
    @param The size of the data
==============================*/

void device_senddata_everdrive(ftdi_context_t* cart, int datatype, char* data, u32 size)
{
    char wrotecmp = 0;
    char cmp[] = {'C', 'M', 'P', 'H'};
    int read = 0;
    int left = size;
    int offset = 8;
    u32 header = (size & 0xFFFFFF) | (datatype << 24);
    char*  databuffer = (char*) malloc(sizeof(char) * 512);

    // Put in the DMA header along with length and type information in the global buffer
    databuffer[0] = 'D';
    databuffer[1] = 'M';
    databuffer[2] = 'A';
    databuffer[3] = '@';
    databuffer[4] = (header >> 24) & 0xFF;
    databuffer[5] = (header >> 16) & 0xFF;
    databuffer[6] = (header >> 8)  & 0xFF;
    databuffer[7] = header & 0xFF;

    // Write data to USB until we've finished
    while (left > 0)
    {
        int block = left;
        int blocksend;
        if (block+offset > 512)
            block = 512-offset;

        // Copy the data to the next available spots in the data buffer
        memcpy(databuffer+offset, (void*)((char*)data+read), block);

        // Restart the loop to write the CMP signal if we've finished
        if (!wrotecmp && read+block >= (int)size)
        {
            left = 4;
            offset = block+offset;
            data = cmp;
            wrotecmp = 1;
            read = 0;
            continue;
        }

        // Ensure the data is 16 byte aligned
        blocksend = (block+offset)+15 - ((block+offset)+15)%16;

        // Send data through USB
        FT_Write(cart->handle, databuffer, blocksend, &cart->bytes_written);

        // Keep track of what we've read so far
        left -= block;
        read += block;
        offset = 0;
    }

    // Free the data used by the buffer
    free(databuffer);
}


/*==============================
    device_close_everdrive
    Closes the USB pipe
    @param A pointer to the cart context
==============================*/

void device_close_everdrive(ftdi_context_t* cart)
{
    testcommand(FT_Close(cart->handle), "Error: Unable to close flashcart.\n");
}