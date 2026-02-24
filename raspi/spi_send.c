#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/spi/spidev.h>
#include <byteswap.h> // For the __bswap_16 intrinsic

// --- USER-DEFINABLE CONSTANTS ---
// NOTE: You must define these constants based on your system setup.
#define SPI_DEVICE      "/dev/spidev0.0" 
#define SPI_MODE        0
#define SPI_SPEED_HZ    1000000 // Example speed: 1 MHz
#define DAC_CONTROL_BITS 0x3000 // Example: Control bits set in the upper 4 bits (D15-D12)

// --- FILE CONSTANTS ---
#define SAMPLE_FILE "data.bin" 
#define BUFFER_SIZE_SAMPLES 4096 // Read 4096 samples (8KB) at a time for efficiency


// Helper function to swap bytes for endianness correction
uint16_t swap_endianness(uint16_t val) {
    // Uses the built-in GCC/Clang intrinsic for efficiency
    return __bswap_16(val); 
}

int main() {
    int fd;
    uint8_t tx[2];
    int ret;
    FILE *sample_file;
    
    // --- Data Buffering Variables ---
    // The data is expected to be 16-bit unsigned integers from numpy.tofile(dtype='uint16')
    uint16_t sample_buffer[BUFFER_SIZE_SAMPLES];
    size_t samples_read;
    int buffer_index = 0;
    
    // --- ENDIANNESS FLAG (CRITICAL) ---
    // Set this flag based on your system configuration:
    // 0: If the file generator and the SPI host are the same endianness (most common for single-board computers).
    // 1: If the file was generated with a different endianness (e.g., big-endian data on a little-endian host, or vice-versa).
    int needs_byte_swap = 0; 


    // 1. Open Sample File
    sample_file = fopen(SAMPLE_FILE, "rb");
    if (sample_file == NULL) {
        perror("Cannot open sample file " SAMPLE_FILE);
        return 1;
    }
    
    // 2. Open SPI device 
    fd = open(SPI_DEVICE, O_RDWR);
    if (fd < 0) {
        perror("Cannot open SPI device");
        fclose(sample_file);
        return 1;
    }

    // Set SPI mode 
    uint8_t mode = SPI_MODE;
    ret = ioctl(fd, SPI_IOC_WR_MODE, &mode);
    if (ret == -1) { perror("SPI_IOC_WR_MODE"); fclose(sample_file); return 1; }

    // Set SPI max speed 
    uint32_t speed = SPI_SPEED_HZ;
    ret = ioctl(fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed);
    if (ret == -1) { perror("SPI_IOC_WR_MAX_SPEED_HZ"); fclose(sample_file); return 1; }

    printf("Starting SPI transmission of file %s...\n", SAMPLE_FILE);

    // 3. Main Loop: Read from file and stream
    while (1) {
        // --- Read/Refill Buffer Logic ---
        if (buffer_index >= samples_read) {
            // Read a block of samples (each sample is 2 bytes)
            samples_read = fread(sample_buffer, sizeof(uint16_t), BUFFER_SIZE_SAMPLES, sample_file);
            buffer_index = 0; // Reset index for the new buffer chunk

            if (samples_read == 0) {
                // End of file (EOF) reached
                printf("End of file reached. Stopping transmission.\n");
                break;
            }
        }

        // --- Stream Data from Buffer ---
        if (buffer_index < samples_read) {
            uint16_t sample_raw = sample_buffer[buffer_index++]; 
            uint16_t sample;

            // Handle Endianness Correction
            if (needs_byte_swap) {
                sample = swap_endianness(sample_raw);
            } else {
                sample = sample_raw;
            }
            
            // Add DAC control bits (Existing logic, assumes 12-bit data in the lower 12 bits)
            sample = (sample & 0x0FFF) | DAC_CONTROL_BITS;

            // Split into MSB and LSB to form the 16-bit SPI word
            tx[0] = (sample >> 8) & 0xFF;  // MSB (Includes control bits)
            tx[1] = sample & 0xFF;         // LSB (Includes the lower 8 data bits)

            // Send 2-byte word (one CS pulse)
            ret = write(fd, tx, 2);
            if (ret != 2) {
                perror("SPI write failed");
                break;
            }
        }
    }

    // 4. Cleanup
    close(fd);
    fclose(sample_file);
    return 0;
}