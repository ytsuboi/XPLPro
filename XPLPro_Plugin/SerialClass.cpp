#include "SerialClass.h"
#include "XPLProCommon.h"

#include <ctime>
#include <string.h>

extern FILE* errlog;				// Used for logging problems

serialClass::serialClass()
{

}

serialClass::~serialClass()
{
    shutDown();
}

#if IBM
// ===================== Windows implementation =====================

int serialClass::resolvePortName(int portNumber)
{
    sprintf_s(portName, sizeof(portName), "\\\\.\\COM%u", portNumber);
    return 0;
}

int serialClass::begin(int portNumber)
{
 //We're not yet connected
    this->connected = false;
    this->status;

    //Form the Raw device name
    resolvePortName(portNumber);

    //Try to connect to the given port throuh CreateFile
    this->hSerial = CreateFileA(portName,
        GENERIC_READ | GENERIC_WRITE,
        0,
        NULL,
        OPEN_EXISTING,
        0,                            //FILE_FLAG_OVERLAPPED,
        NULL);

    //Check if the connection was successfull
    if (this->hSerial == INVALID_HANDLE_VALUE)
    {
        return -1;
    }
    else
    {
        //If connected we try to set the comm parameters
        DCB dcbSerialParams = { 0 };

        //Try to get the current
        if (!GetCommState(this->hSerial, &dcbSerialParams))
        {
            //If impossible, show an error
            //printf("failed to get current serial parameters!");
        }
        else
        {
            //Define serial connection parameters for the arduino board
            dcbSerialParams.BaudRate = XPL_BAUDRATE;
            dcbSerialParams.ByteSize = 8;
            dcbSerialParams.StopBits = ONESTOPBIT;
            dcbSerialParams.Parity = NOPARITY;
            //Setting the DTR to Control_Enable ensures that the Arduino is properly
            //reset upon establishing a connection
            dcbSerialParams.fDtrControl = DTR_CONTROL_ENABLE;

            //Set the parameters and check for their proper application
            if (!SetCommState(hSerial, &dcbSerialParams))
            {
                // printf("ALERT: Could not set Serial Port parameters");
            }
            else
            {
                //If everything went fine we're connected
                this->connected = true;
                //Flush any remaining characters in the buffers
                PurgeComm(this->hSerial, PURGE_RXCLEAR | PURGE_TXCLEAR);
                //We wait 2s as the arduino board will be resetting
                Sleep(ARDUINO_WAIT_TIME);
                fprintf(errlog, "Serial:  port \"%s\" opened successfully.\n", portName);
                return portNumber;
            }
        }
    }
    return -1;      // general error code
}

int serialClass::shutDown(void)
{
    //Check if we are connected before trying to disconnect
    if (this->connected)
    {
        //We're no longer connected
        this->connected = false;
        //Close the serial handler
        CloseHandle(this->hSerial);
        fprintf(errlog, "...Closing port %s\n", portName);
    }
    return 0;
}



int serialClass::readData(char* buffer, size_t nbChar)
{
    //Number of bytes we'll have read
    DWORD bytesRead;
    //Number of bytes we'll really ask to read
    size_t toRead;

    //Use the ClearCommError function to get status info on the Serial port
    ClearCommError(this->hSerial, &this->errors, &this->status);

    //Check if there is something to read
    if (this->status.cbInQue > 0)
    {
        //If there is we check if there is enough data to read the required number
        //of characters, if not we'll read only the available characters to prevent
        //locking of the application.
        if (this->status.cbInQue > nbChar)
        {
            toRead = nbChar;
        }
        else
        {
            toRead = this->status.cbInQue;
        }

        //Try to read the require number of chars, and return the number of read bytes on success
        if (ReadFile(this->hSerial, buffer, toRead, &bytesRead, NULL))
        {
            return bytesRead;
        }
    }
    //If nothing has been read, or that an error was detected return 0
    return 0;
}


bool serialClass::writeData(const char* buffer, size_t nbChar)
{
    DWORD bytesSend;

    //Try to write the buffer on the Serial port
    if (!WriteFile(this->hSerial, (void*)buffer, nbChar, &bytesSend, 0))
    {
        //In case it doesnt work get comm error and return false
        ClearCommError(this->hSerial, &this->errors, &this->status);

        return false;
    }
    else
    {
        // FlushFileBuffers(this->hSerial);
        return true;
    }
}

bool serialClass::IsConnected(void)
{
    //Simply return the connection status
    return this->connected;
}

#endif // IBM

#if APL
// ===================== macOS implementation =====================

int serialClass::resolvePortName(int portNumber)
{
    DIR* dir = opendir("/dev");
    if (!dir) return -1;

    int currentIndex = 0;
    struct dirent* entry;

    while ((entry = readdir(dir)) != NULL)
    {
        if (strncmp(entry->d_name, "tty.usbmodem", 12) == 0 ||
            strncmp(entry->d_name, "tty.usbserial", 13) == 0 ||
            strncmp(entry->d_name, "tty.wchusbserial", 16) == 0)
        {
            currentIndex++;
            if (currentIndex == portNumber)
            {
                snprintf(portName, sizeof(portName), "/dev/%s", entry->d_name);
                closedir(dir);
                return 0;
            }
        }
    }
    closedir(dir);
    return -1;
}

int serialClass::begin(int portNumber)
{
    this->connected = false;

    // Resolve port name first
    if (resolvePortName(portNumber) != 0)
    {
        return -1;
    }

    // Open the serial port
    this->fd = open(portName, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (this->fd == -1)
    {
        fprintf(errlog, "Serial: Unable to open port %s\n", portName);
        return -1;
    }

    // Prevent other processes from using this port
    if (ioctl(this->fd, TIOCEXCL) == -1)
    {
        fprintf(errlog, "Serial: Unable to set exclusive access on %s\n", portName);
        close(this->fd);
        this->fd = -1;
        return -1;
    }

    // Now that the port is open, clear the O_NONBLOCK flag so subsequent reads will block
    // Actually, we want non-blocking reads for the plugin's flight loop
    // Keep O_NONBLOCK set

    // Get the current terminal attributes and save them
    struct termios options;
    if (tcgetattr(this->fd, &options) == -1)
    {
        fprintf(errlog, "Serial: Unable to get port attributes for %s\n", portName);
        close(this->fd);
        this->fd = -1;
        return -1;
    }
    this->originalTTYAttrs = options;

    // Set raw input mode (no echo, no canonical processing, etc.)
    cfmakeraw(&options);

    // Set baud rate
    cfsetispeed(&options, B115200);
    cfsetospeed(&options, B115200);

    // 8N1 (8 data bits, no parity, 1 stop bit)
    options.c_cflag |= (CS8 | CLOCAL | CREAD);
    options.c_cflag &= ~(PARENB | CSTOPB | CRTSCTS);

    // Set DTR to reset Arduino
    int status_bits;
    ioctl(this->fd, TIOCMGET, &status_bits);
    status_bits |= TIOCM_DTR;
    ioctl(this->fd, TIOCMSET, &status_bits);

    // Apply settings
    if (tcsetattr(this->fd, TCSANOW, &options) == -1)
    {
        fprintf(errlog, "Serial: Unable to set port attributes for %s\n", portName);
        close(this->fd);
        this->fd = -1;
        return -1;
    }

    // Flush any remaining data
    tcflush(this->fd, TCIOFLUSH);

    this->connected = true;

    // Wait for Arduino to reset (same as Windows side)
    usleep(ARDUINO_WAIT_TIME * 1000);

    fprintf(errlog, "Serial:  port \"%s\" opened successfully.\n", portName);
    return portNumber;
}

int serialClass::shutDown(void)
{
    if (this->connected)
    {
        this->connected = false;

        // Restore original terminal attributes
        tcsetattr(this->fd, TCSANOW, &this->originalTTYAttrs);

        close(this->fd);
        this->fd = -1;
        fprintf(errlog, "...Closing port %s\n", portName);
    }
    return 0;
}

int serialClass::readData(char* buffer, size_t nbChar)
{
    if (!this->connected || this->fd == -1) return 0;

    // Check how many bytes are available
    int bytesAvailable = 0;
    ioctl(this->fd, FIONREAD, &bytesAvailable);

    if (bytesAvailable > 0)
    {
        size_t toRead = (size_t)bytesAvailable > nbChar ? nbChar : (size_t)bytesAvailable;

        ssize_t bytesRead = read(this->fd, buffer, toRead);
        if (bytesRead > 0)
        {
            return (int)bytesRead;
        }
    }
    return 0;
}

bool serialClass::writeData(const char* buffer, size_t nbChar)
{
    if (!this->connected || this->fd == -1) return false;

    ssize_t bytesWritten = write(this->fd, buffer, nbChar);
    if (bytesWritten == -1)
    {
        return false;
    }

    return true;
}

bool serialClass::IsConnected(void)
{
    return this->connected;
}

#endif // APL
