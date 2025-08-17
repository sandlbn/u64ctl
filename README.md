# Ultimate64 Control Library for Amiga OS 3.x

A comprehensive C library for controlling Ultimate64 and Ultimate-II devices from Amiga computers running AmigaOS 3.x. This library provides both programmatic access and command-line tools for remote control of your Ultimate64/Ultimate-II hardware.

## Inspiration and Credits

This project is inspired by and compatible with:
- The excellent [ultimate64 Rust crate](https://docs.rs/ultimate64/latest/ultimate64/index.html) which provides similar functionality for modern systems
- The amazing [Ultimate64 and Ultimate-II hardware](https://1541u-documentation.readthedocs.io/en/latest/api/api_calls.html) by Gideon's Logic Architectures

Special thanks to Gideon for creating these incredible devices that bring modern convenience to retro computing!

## Features

- **File Format Validation**: Built-in validation for PRG, CRT, SID, MOD, and disk image formats
- **Multiple Interfaces**: Both C library and command-line tool
- **Disk Management**: Mount/unmount disk images with full error reporting
- **Audio Playback**: Play SID and MOD files directly
- **Program Execution**: Load and run PRG and CRT files
- **Keyboard Emulation**: Type text directly into the C64
- **Configuration Management**: Persistent settings via ENV: variables

## Requirements

- Amiga OS 3.x (tested on 3.1+)
- bsdsocket.library for network functionality
- Ultimate64 or Ultimate-II device with network connectivity
- Cross-compilation toolchain (m68k-amigaos-gcc)

## Building

### Prerequisites

Install the Amiga cross-compilation toolchain:
```bash
# Install amiga-gcc toolchain
# Follow instructions at: https://github.com/bebbo/amiga-gcc
```

### Compilation

```bash
# Clone the repository
git clone https://github.com/sandlbn/u64ctl
cd ultimate64-amiga

# Build everything
make clean all

# Build specific targets
make library          # Build static library only
make cli             # Build command-line tool only
make complete        # Build everything

# Debug build
make DEBUG=1 all
```

### Build Options

- `DEBUG=1` - Build with debug symbols and verbose output
- `USE_BSDSOCKET=1` - Enable network support (default)

## Command Line Usage

The `u64ctl` command provides comprehensive control over your Ultimate64/Ultimate-II device.

### Initial Setup

```bash
# Set your Ultimate64's IP address
u64ctl sethost HOST 192.168.1.64

# Set password if required
u64ctl setpassword PASSWORD mysecret

# Set custom port (if not 80)
u64ctl setport TEXT 8080

# View current configuration
u64ctl config
```

### Device Information

```bash
# Get device information
u64ctl info

# Get firmware version
u64ctl 192.168.1.64 info    # Override host for one command
```

### Machine Control

```bash
# Reset the C64
u64ctl reset

# Reboot the Ultimate device
u64ctl reboot

# Power off (Ultimate64 only)
u64ctl poweroff

# Pause/resume C64 execution
u64ctl pause
u64ctl resume

# Press the menu button
u64ctl menu
```

### File Operations

```bash
# Load a PRG file
u64ctl load FILE games/elite.prg

# Run a PRG file directly
u64ctl run FILE games/elite.prg

# Run a cartridge file
u64ctl run FILE carts/simons.crt

# Load files with spaces in names
u64ctl load FILE "work/my game.prg"
```

### Memory Operations

```bash
# Read memory (peek)
u64ctl peek ADDRESS 0xd020        # Hex with 0x prefix
u64ctl peek ADDRESS 53280         # Decimal
u64ctl peek ADDRESS $d020         # Hex with $ prefix

# Write memory (poke)
u64ctl poke ADDRESS 0xd020 TEXT 1     # Set border to white
u64ctl poke ADDRESS 53280 TEXT 0      # Set border to black
u64ctl poke ADDRESS $d021 TEXT 6      # Set background to blue
```

### Disk Operations

```bash
# Mount disk images
u64ctl mount FILE disk1.d64 DRIVE a                    # Read-only (default)
u64ctl mount FILE disk1.d64 DRIVE a MODE rw           # Read/write
u64ctl mount FILE disk1.d71 DRIVE b MODE ro           # Read-only
u64ctl mount FILE disk1.d81 DRIVE c MODE ul           # Unlinked

# Unmount disks
u64ctl unmount DRIVE a
u64ctl unmount DRIVE b

# Check drive status
u64ctl drives
```

### Text Input

```bash
# Type simple text
u64ctl type TEXT "hello world"

# Type BASIC commands with escape sequences
u64ctl type TEXT "load\"*\",8,1\n"      # LOAD"*",8,1 + RETURN
u64ctl type TEXT "run\n"                # RUN + RETURN
u64ctl type TEXT "list\n"               # LIST + RETURN

# Type BASIC program lines
u64ctl type TEXT "10 print\"hello\"\n"
u64ctl type TEXT "20 goto 10\n"
u64ctl type TEXT "run\n"
```

### Audio Playback

```bash
# Play SID files
u64ctl playsid FILE music.sid                    # Play default song
u64ctl playsid FILE music.sid SONG 2            # Play specific song
u64ctl playsid FILE hvsc/rob_hubbard.sid SONG 1 VERBOSE

# Play MOD files
u64ctl playmod FILE music.mod
u64ctl playmod FILE "ambient/track1.mod" VERBOSE
```

### Verbose and Quiet Modes

```bash
# Enable verbose output for debugging
u64ctl info VERBOSE

# Suppress all output except errors
u64ctl reset QUIET

# Combine with other options
u64ctl load FILE test.prg VERBOSE
```

## Library Usage

### Basic Example

```c
#include "ultimate64_amiga.h"

int main()
{
    U64Connection *conn;
    STRPTR error_details = NULL;
    
    // Initialize library
    if (!U64_InitLibrary()) {
        printf("Failed to initialize library\n");
        return 1;
    }
    
    // Connect to device
    conn = U64_Connect("192.168.1.64", "password");
    if (!conn) {
        printf("Failed to connect\n");
        U64_CleanupLibrary();
        return 1;
    }
    
    // Get device information
    U64DeviceInfo info;
    if (U64_GetDeviceInfo(conn, &info) == U64_OK) {
        printf("Device: %s\n", info.product_name);
        printf("Firmware: %s\n", info.firmware_version);
        U64_FreeDeviceInfo(&info);
    }
    
    // Reset the C64
    if (U64_Reset(conn) != U64_OK) {
        printf("Reset failed: %s\n", U64_GetErrorString(U64_GetLastError(conn)));
    }
    
    // Load a PRG file with error details
    UBYTE prg_data[] = {0x01, 0x08, 0x0b, 0x08, 0x0a, 0x00, 0x99, 0x22, 0x48, 0x45, 0x4c, 0x4c, 0x4f, 0x22, 0x00, 0x00, 0x00};
    if (U64_LoadPRG(conn, prg_data, sizeof(prg_data), &error_details) != U64_OK) {
        if (error_details) {
            printf("Load failed: %s\n", error_details);
            FreeMem(error_details, strlen(error_details) + 1);
        }
    }
    
    // Cleanup
    U64_Disconnect(conn);
    U64_CleanupLibrary();
    return 0;
}
```

### Enhanced Functions with Error Details

All major functions now provide detailed error information:

```c
// Enhanced program loading with error details
LONG U64_LoadPRG(U64Connection *conn, CONST UBYTE *data, ULONG size, STRPTR *error_details);
LONG U64_RunPRG(U64Connection *conn, CONST UBYTE *data, ULONG size, STRPTR *error_details);
LONG U64_RunCRT(U64Connection *conn, CONST UBYTE *data, ULONG size, STRPTR *error_details);

// Enhanced audio playback with error details  
LONG U64_PlaySID(U64Connection *conn, CONST UBYTE *data, ULONG size, UBYTE song_num, STRPTR *error_details);
LONG U64_PlayMOD(U64Connection *conn, CONST UBYTE *data, ULONG size, STRPTR *error_details);

// Enhanced disk operations with error details
LONG U64_MountDiskWithErrors(U64Connection *conn, CONST_STRPTR filename, 
                            CONST_STRPTR drive_id, U64MountMode mode, 
                            BOOL run, STRPTR *error_details);
LONG U64_UnmountDiskWithErrors(U64Connection *conn, CONST_STRPTR drive_id, 
                              STRPTR *error_details);
```

### File Validation

```c
// Validate files before loading
STRPTR validation_info = NULL;

if (U64_ValidatePRGFile(data, size, &validation_info) == U64_OK) {
    printf("PRG validation: %s\n", validation_info);
    FreeMem(validation_info, strlen(validation_info) + 1);
} else {
    printf("Invalid PRG file\n");
}

// Similar functions for other formats
U64_ValidateSIDFile(data, size, &validation_info);
U64_ValidateMODFile(data, size, &validation_info);
U64_ValidateCRTFile(data, size, &validation_info);
U64_ValidateDiskImage(data, size, disk_type, &validation_info);
```

### Memory Operations

```c
// Write memory
UBYTE data[] = {0x01, 0x02, 0x03};
if (U64_WriteMem(conn, 0x0400, data, 3) != U64_OK) {
    printf("Write failed\n");
}

// Read memory
UBYTE buffer[256];
if (U64_ReadMem(conn, 0x0400, buffer, 256) == U64_OK) {
    printf("Read successful\n");
}

// Convenience functions
U64_Poke(conn, 0xd020, 1);      // Set border color
UBYTE border = U64_Peek(conn, 0xd020);  // Read border color
UWORD addr = U64_ReadWord(conn, 0x0002); // Read word (little-endian)
```

### Disk Operations

```c
// Mount a disk image
STRPTR mount_errors = NULL;
if (U64_MountDiskWithErrors(conn, "games/elite.d64", "a", U64_MOUNT_RO, FALSE, &mount_errors) != U64_OK) {
    if (mount_errors) {
        printf("Mount failed: %s\n", mount_errors);
        FreeMem(mount_errors, strlen(mount_errors) + 1);
    }
}

// Check drive status
BOOL mounted;
STRPTR image_name = NULL;
U64MountMode mode;
if (U64_GetDriveStatus(conn, "a", &mounted, &image_name, &mode) == U64_OK) {
    if (mounted) {
        printf("Drive A: %s (%s)\n", image_name, 
               mode == U64_MOUNT_RW ? "read/write" : "read-only");
        FreeMem(image_name, strlen(image_name) + 1);
    }
}
```

### Keyboard Emulation

```c
// Type text with PETSCII conversion
if (U64_TypeText(conn, "load\"*\",8,1\n") == U64_OK) {
    printf("Text typed successfully\n");
}

// Manual PETSCII conversion
ULONG petscii_len;
UBYTE *petscii = U64_StringToPETSCII("HELLO WORLD", &petscii_len);
if (petscii) {
    // Use petscii data...
    U64_FreePETSCII(petscii);
}
```

## Error Handling

The library provides comprehensive error reporting:

```c
// Check for errors
LONG result = U64_Reset(conn);
if (result != U64_OK) {
    printf("Error: %s\n", U64_GetErrorString(result));
    
    // Get last error from connection
    LONG last_error = U64_GetLastError(conn);
    printf("Last error: %s\n", U64_GetErrorString(last_error));
}

// Enhanced functions provide detailed error messages
STRPTR error_details = NULL;
result = U64_PlaySID(conn, sid_data, sid_size, 1, &error_details);
if (result != U64_OK) {
    if (error_details) {
        printf("SID playback failed: %s\n", error_details);
        FreeMem(error_details, strlen(error_details) + 1);
    } else {
        printf("SID playback failed: %s\n", U64_GetErrorString(result));
    }
}
```

## Supported File Formats

| Format | Extension | Description | Validation |
|--------|-----------|-------------|------------|
| PRG | .prg | C64 program files | Load address, size checks |
| CRT | .crt | C64 cartridge files | Header validation, type detection |
| SID | .sid | SID music files | PSID/RSID header, song count |
| MOD | .mod | Amiga module files | Format ID, channel count |
| D64 | .d64 | 1541 disk images | Size validation (174848/175531 bytes) |
| D71 | .d71 | 1571 disk images | Size validation (349696 bytes) |
| D81 | .d81 | 1581 disk images | Size validation (819200 bytes) |
| G64 | .g64 | GCR disk images | Variable size validation |
| G71 | .g71 | GCR disk images | Variable size validation |

## Network Configuration

### bsdsocket.library Setup

Make sure bsdsocket.library is properly configured:

```bash
# Add to your startup-sequence or user-startup
assign DEVS: SYS:Devs ADD
assign LIBS: SYS:Libs ADD

# Configure your TCP/IP stack (Miami, Genesis, etc.)
```

### Firewall Settings

Ensure your Ultimate64/Ultimate-II device allows HTTP connections on the configured port (usually 80).

## Troubleshooting

### Common Issues

1. **"Failed to connect"**
   - Verify IP address and port
   - Check network connectivity
   - Ensure Ultimate64 web interface is enabled

2. **"Load failed with errors"**
   - Use VERBOSE mode to see detailed error messages
   - Validate file format before loading
   - Check file size and format

### Debug Mode

Enable verbose output for detailed debugging:

```bash
# Command line
u64ctl info VERBOSE

# In code
U64_SetVerboseMode(TRUE);
```

### Network Testing

```bash
# Test basic connectivity
ping 192.168.1.64

# Test HTTP access (if you have curl/wget)
curl http://192.168.1.64/v1/info
```

## Contributing

Contributions are welcome! Please:

1. Fork the repository
2. Create a feature branch
3. Make your changes
4. Test thoroughly on real Amiga hardware
5. Submit a pull request

## Acknowledgments

- **Gideon Zweijtzer** - Creator of Ultimate64 and Ultimate-II hardware
- **Rust ultimate64 crate authors** - Inspiration for API design
- **Amiga community** - Testing and feedback
- **bebbo** - Amiga GCC cross-compiler toolchain

## Links

- [Ultimate64 Hardware](https://ultimate64.com/)
- [Ultimate-II Documentation](https://1541u-documentation.readthedocs.io/)
- [Rust ultimate64 crate](https://docs.rs/ultimate64/latest/ultimate64/)
- [Amiga GCC Toolchain](https://github.com/bebbo/amiga-gcc)