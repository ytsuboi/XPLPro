#pragma once

#define XPL_MAX_SERIALPORTS

#if IBM
#include <windows.h>
#endif

#if APL
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <dirent.h>
#endif

#include <stdio.h>
#include <stdlib.h>

class serialClass
{
private:

#if IBM
    //Serial comm handler
    HANDLE hSerial = NULL;
    //Connection status
    bool connected = false;
    //Get various information about the connection
    COMSTAT status;
    //Keep track of last error
    DWORD errors;
#endif

#if APL
    int fd = -1;
    bool connected = false;
    struct termios originalTTYAttrs;
#endif

public:
    //Initialize Serial communication with the given COM port
    serialClass();
    //Close the connection
    ~serialClass();

    // Resolve the port name for the given index without opening it.
    // Returns 0 on success (portName is filled in), -1 if no port at this index.
    int resolvePortName(int portNumber);

    int begin(int portNumber);
    int shutDown(void);
    int findAvailablePort(void);

    //Read data in a buffer, if nbChar is greater than the
    //maximum number of bytes available, it will return only the
    //bytes available. The function return -1 when nothing could
    //be read, the number of bytes actually read.
    int readData(char* buffer, size_t nbChar);
    //Writes data from a buffer through the Serial connection
    //return true on success.
    bool writeData(const char* buffer, size_t nbChar);
    //Check if we are actually connected
    bool IsConnected(void);

    char   portName[64];					// port name (longer for macOS /dev/tty.* paths)
    int valid;
};
