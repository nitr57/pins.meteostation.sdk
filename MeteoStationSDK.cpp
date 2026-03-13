/* *******************************************************************************
 * MIT License
 *
 * Copyright (c) 2026 Nico Trost
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 * **************************************************************************** */

#include "MeteoStationSDK.h"
#include "MeteoStationLogging.h"
#include "MeteoStationDevice.h"
#include "MeteoStationProtocol.h"
#include "MeteoStationSerialPort.h"
#include <map>
#include <mutex>
#include <thread>
#include <memory>
#include <string>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <cmath>
#include <cctype>
#include <mutex>
#ifdef __unix__
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <dirent.h>
#include <libudev.h>
#elif defined(_WIN32)
#include <windows.h>
#include <setupapi.h>
#include <devguid.h>
#pragma comment(lib, "setupapi.lib")
#endif

#define SDK_VERSION "1.1.0"

/* Handshake retry configuration */
#define HANDSHAKE_MAX_RETRIES 3
#define HANDSHAKE_RETRY_DELAY_MS 20
#define HANDSHAKE_TIMEOUT 800

/* Import internal implementation for use in public C API */
using namespace MeteoStation;

/* Helper function to send a command and wait for the response with timeout */
static bool SendAndWaitForReply(std::shared_ptr<MeteoStation::Device> device,
                                const char *command,
                                std::mutex &configMutex,
                                std::condition_variable &configCV,
                                std::atomic<bool> &configPending,
                                const char *timeoutMsg,
                                int timeoutMs = 200)
{
    {
        std::lock_guard<std::mutex> lock(configMutex);
        configPending = true;
    }

    if (!device->port->Write((const unsigned char *)command, strlen(command)))
    {
        MS_DEBUG("SendAndWaitForReply: Failed to send %s command", command);
        std::lock_guard<std::mutex> lock(configMutex);
        configPending = false;
        return false;
    }

    /* Wait for config to be received with specified timeout */
    {
        std::unique_lock<std::mutex> lock(configMutex);
        configCV.wait_for(lock, std::chrono::milliseconds(timeoutMs),
                         [&configPending]() { return !configPending; });
        if (configPending)
        {
            MS_DEBUG("SendAndWaitForReply: Timeout waiting for %s (timeout=%dms)", timeoutMsg, timeoutMs);
            configPending = false;
            return false;
        }
    }

    return true;
}

/* Helper function to send a command and wait for the response with retry mechanism */
static bool SendAndWaitForReplyWithRetry(std::shared_ptr<MeteoStation::Device> device,
                                         const char *command,
                                         std::mutex &configMutex,
                                         std::condition_variable &configCV,
                                         std::atomic<bool> &configPending,
                                         const char *timeoutMsg,
                                         int timeoutMs = HANDSHAKE_TIMEOUT,
                                         int maxRetries = HANDSHAKE_MAX_RETRIES,
                                         int retryDelayMs = HANDSHAKE_RETRY_DELAY_MS)
{
    for (int attempt = 1; attempt <= maxRetries; ++attempt)
    {
        MS_DEBUG("SendAndWaitForReplyWithRetry: Attempt %d/%d for %s (timeout=%dms)", attempt, maxRetries, timeoutMsg, timeoutMs);

        if (SendAndWaitForReply(device, command, configMutex, configCV, configPending, timeoutMsg, timeoutMs))
        {
            MS_DEBUG("SendAndWaitForReplyWithRetry: Success on attempt %d for %s", attempt, timeoutMsg);
            return true;
        }

        if (attempt < maxRetries)
        {
            MS_DEBUG("SendAndWaitForReplyWithRetry: Failed on attempt %d, retrying after %d ms", attempt, retryDelayMs);
            std::this_thread::sleep_for(std::chrono::milliseconds(retryDelayMs));
        }
    }

    MS_DEBUG("SendAndWaitForReplyWithRetry: All %d attempts failed for %s", maxRetries, timeoutMsg);
    return false;
}

/* Structure for parallel device scanning */
struct ScanWorkerTask
{
    std::string portName;
    std::shared_ptr<MeteoStation::Device> device;
    bool isValid;
    
    ScanWorkerTask(const char *port) : portName(port), isValid(false) {}
};

/* Worker thread function for testing a single device */
static void ScanWorkerThread(ScanWorkerTask &task)
{
    auto port = std::make_shared<SerialPort>();
    
    /* Use minimal retry for scanning - fail fast if port is busy */
    /* This prevents hanging when other apps are also scanning */
    port->SetRetryParams(1, 10);  /* 1 retry, 10ms delay = ~10ms total wait */
    
    if (!port->Open(task.portName.c_str()))
    {
        MS_DEBUG("ScanWorkerThread: Failed to open port %s (skipped, may be in use by another app)", task.portName.c_str());
        return;
    }

    auto tempDevice = std::make_shared<Device>();
    tempDevice->port = port;
    tempDevice->portName = task.portName;

    // Send HS to wake up device
    SendCommand(tempDevice, ":HS#", 200);

    // Start status listener thread
    StartStatusListener(tempDevice);

    // Perform handshake with retry mechanism (use full timeout for handshake reliability)
    if(SendAndWaitForReplyWithRetry(tempDevice, ":HS#", tempDevice->handshakeMutex, tempDevice->handshakeCV,
                                    tempDevice->handshakePending, "handshake"))
    {
        MS_DEBUG("ScanWorkerThread: Valid device found on %s", task.portName.c_str());

        /* Stop listener */
        StopStatusListener(tempDevice);

        /* Valid device found - close port, will be reopened in MSOpen */
        port->Close();
        
        task.device = tempDevice;
        task.isValid = true;
    }
    else
    {
        MS_DEBUG("ScanWorkerThread: No response from device on %s", task.portName.c_str());
        /* Not a valid device, stop listener and close port */
        StopStatusListener(tempDevice);
        port->Close();
    }
}

/* ============================================================================
 * PUBLIC SDK API IMPLEMENTATION
 * ============================================================================ */

MSAPI MS_ERROR_TYPE MSGetSDKVersion(char *version)
{
    if (!version)
    {
        return MS_ERROR_NULL_POINTER;
    }

    strncpy(version, SDK_VERSION, MS_VERSION_LEN - 1);
    version[MS_VERSION_LEN - 1] = '\0';
    return MS_SUCCESS;
}

MSAPI MS_ERROR_TYPE MSDeviceScan(int *number, int *ids)
{
    if (!number || !ids)
    {
        return MS_ERROR_NULL_POINTER;
    }

    std::lock_guard<std::mutex> lock(g_globalMutex);

    // Stop telemetry and listener threads on all currently open devices
    for (auto &pair : g_devices)
    {
        auto device = pair.second;
        if (device && device->isOpen)
        {
            SendCommand(device, ":ES#");
            StopStatusListener(device);
        }
    }

    int count = 0;

#ifdef __unix__
    /* Create udev context */
    struct udev *udev = udev_new();
    if (!udev)
    {
        return MS_ERROR_COMMUNICATION;
    }

    /* Create enumeration for tty devices */
    struct udev_enumerate *enumerate = udev_enumerate_new(udev);
    if (!enumerate)
    {
        udev_unref(udev);
        return MS_ERROR_COMMUNICATION;
    }

    /* Filter for tty subsystem */
    udev_enumerate_add_match_subsystem(enumerate, "tty");
    udev_enumerate_scan_devices(enumerate);

    struct udev_list_entry *devices = udev_enumerate_get_list_entry(enumerate);
    struct udev_list_entry *entry;

    /* Step 1: Collect all candidate CH340 devices */
    std::vector<std::string> candidatePorts;
    udev_list_entry_foreach(entry, devices)
    {
        const char *path = udev_list_entry_get_name(entry);
        struct udev_device *device = udev_device_new_from_syspath(udev, path);
        if (!device)
        {
            continue;
        }

        /* Get the parent USB device */
        struct udev_device *parent = udev_device_get_parent_with_subsystem_devtype(
            device, "usb", "usb_device");

        if (!parent)
        {
            udev_device_unref(device);
            continue;
        }

        /* Check VID and PID for CH340 (1a86:7523) */
        const char *vid = udev_device_get_sysattr_value(parent, "idVendor");
        const char *pid = udev_device_get_sysattr_value(parent, "idProduct");

        if (!vid || !pid)
        {
            udev_device_unref(device);
            continue;
        }

        if (strcmp(vid, "1a86") != 0 || strcmp(pid, "7523") != 0)
        {
            udev_device_unref(device);
            continue;
        }

        /* Get the device node (e.g., /dev/ttyUSB0) */
        const char *deviceNode = udev_device_get_devnode(device);
        if (deviceNode)
        {
            MS_DEBUG("Found CH340 device: %s", deviceNode);
            candidatePorts.push_back(std::string(deviceNode));
        }

        udev_device_unref(device);
    }

    /* Step 2: Scan candidate devices in parallel */
    std::vector<ScanWorkerTask> tasks;
    std::vector<std::thread> workerThreads;

    for (const auto &port : candidatePorts)
    {
        if (count >= MS_MAX_NUM)
            break;
        tasks.emplace_back(port.c_str());
        count++;
    }

    /* Spawn worker threads for each candidate port */
    for (auto &task : tasks)
    {
        workerThreads.emplace_back(ScanWorkerThread, std::ref(task));
    }

    /* Wait for all threads to complete */
    for (auto &thread : workerThreads)
    {
        if (thread.joinable())
        {
            thread.join();
        }
    }

    /* Step 3: Collect valid devices */
    count = 0;
    for (auto &task : tasks)
    {
        if (task.isValid && count < MS_MAX_NUM)
        {
            int id = count;
            g_devices[id] = task.device;
            ids[count] = id;
            count++;
        }
    }

    /* Clean up udev resources */
    udev_enumerate_unref(enumerate);
    udev_unref(udev);
#elif defined(_WIN32)
    MS_DEBUG("MSDeviceScan: Enumerating serial ports on Windows");

    // Enumerate devices in the Ports class and look for USB devices with matching VID/PID
    HDEVINFO hDevInfo = SetupDiGetClassDevsA(&GUID_DEVCLASS_PORTS, NULL, NULL, DIGCF_PRESENT);
    if (hDevInfo == INVALID_HANDLE_VALUE)
    {
        MS_DEBUG("SetupDiGetClassDevs failed");
        *number = 0;
        return MS_SUCCESS;
    }

    /* Step 1: Collect all candidate CH340 ports */
    std::vector<std::string> candidatePorts;

    for (DWORD idx = 0; ; ++idx)
    {
        SP_DEVINFO_DATA devInfo;
        devInfo.cbSize = sizeof(devInfo);
        if (!SetupDiEnumDeviceInfo(hDevInfo, idx, &devInfo))
        {
            DWORD err = GetLastError();
            if (err == ERROR_NO_MORE_ITEMS)
                break;
            else
                continue;
        }

        // Try to get the device instance ID (contains VID/PID for USB-serial)
        char instanceId[512] = {0};
        if (!SetupDiGetDeviceInstanceIdA(hDevInfo, &devInfo, instanceId, (DWORD)sizeof(instanceId), NULL))
        {
            // ignore
        }

        // Look for VID_1A86 and PID_7523 in instance ID (case-insensitive)
        bool isTarget = false;
        if (instanceId[0])
        {
            std::string iid(instanceId);
            for (auto &c : iid) c = (char)toupper((unsigned char)c);
            if (iid.find("VID_1A86") != std::string::npos && iid.find("PID_7523") != std::string::npos)
            {
                isTarget = true;
            }
        }

        if (!isTarget)
            continue;

        // Try to extract COM port name. First try PortName from device registry, fallback to FriendlyName
        char portName[128] = {0};

        HKEY hKey = SetupDiOpenDevRegKey(hDevInfo, &devInfo, DICS_FLAG_GLOBAL, 0, DIREG_DEV, KEY_READ);
        if (hKey != INVALID_HANDLE_VALUE)
        {
            DWORD type = 0;
            DWORD cb = (DWORD)sizeof(portName);
            if (RegQueryValueExA(hKey, "PortName", NULL, &type, (LPBYTE)portName, &cb) == ERROR_SUCCESS)
            {
                // portName now contains e.g. "COM3"
            }
            RegCloseKey(hKey);
        }

        if (!portName[0])
        {
            // Fallback: get friendly name and parse (e.g., "USB-SERIAL CH340 (COM3)")
            char friendly[256] = {0};
            if (SetupDiGetDeviceRegistryPropertyA(hDevInfo, &devInfo, SPDRP_FRIENDLYNAME, NULL, (PBYTE)friendly, (DWORD)sizeof(friendly), NULL))
            {
                char *p = strstr(friendly, "(COM");
                if (p)
                {
                    char *q = strchr(p, ')');
                    if (q && q > p)
                    {
                        size_t len = (size_t)(q - p - 1); // skip '(' and ')'
                        if (len < sizeof(portName))
                        {
                            // p points to "(COM3" so copy from p+1
                            strncpy(portName, p + 1, len);
                            portName[len] = '\0';
                        }
                    }
                }
            }
        }

        if (!portName[0])
            continue; // can't determine COM port

        MS_DEBUG("Found target device instance=%s port=%s", instanceId, portName);
        candidatePorts.push_back(std::string(portName));
    }

    SetupDiDestroyDeviceInfoList(hDevInfo);

    /* Step 2: Scan candidate devices in parallel */
    std::vector<ScanWorkerTask> tasks;
    std::vector<std::thread> workerThreads;

    for (const auto &port : candidatePorts)
    {
        if (count >= MS_MAX_NUM)
            break;
        tasks.emplace_back(port.c_str());
        count++;
    }

    /* Spawn worker threads for each candidate port */
    for (auto &task : tasks)
    {
        workerThreads.emplace_back(ScanWorkerThread, std::ref(task));
    }

    /* Wait for all threads to complete */
    for (auto &thread : workerThreads)
    {
        if (thread.joinable())
        {
            thread.join();
        }
    }

    /* Step 3: Collect valid devices */
    count = 0;
    for (auto &task : tasks)
    {
        if (task.isValid && count < MS_MAX_NUM)
        {
            int id = count;
            g_devices[id] = task.device;
            ids[count] = id;
            count++;
        }
    }
#endif

    *number = count;
    return MS_SUCCESS;
}

MSAPI MS_ERROR_TYPE MSDeviceOpen(int id)
{
    std::lock_guard<std::mutex> lock(g_globalMutex);
    MS_DEBUG("MSDeviceOpen: Opening device id=%d", id);

    auto it = g_devices.find(id);
    if (it == g_devices.end())
    {
        MS_ERROR("MSDeviceOpen: Device id=%d not found", id);
        return MS_ERROR_INVALID_ID;
    }

    auto device = it->second;
    MS_DEBUG("MSDeviceOpen: Found device, portName=%s", device->portName.c_str());

    /* Create a new SerialPort instance and open it */
    if (!device->port)
    {
        MS_DEBUG("MSDeviceOpen: Creating new SerialPort instance");
        device->port = std::make_shared<SerialPort>();
        /* Use standard retry parameters for normal device open (more tolerant than scan) */
        /* Default: 3 retries with 200ms delay = ~600ms max wait time */
        device->port->SetRetryParams(3, 200);
    }

    MS_DEBUG("MSDeviceOpen: Attempting to open port %s", device->portName.c_str());
    if (!device->port->Open(device->portName.c_str()))
    {
        MS_ERROR("MSDeviceOpen: Failed to open port");
        return MS_ERROR_COMMUNICATION;
    }

    MS_DEBUG("MSDeviceOpen: Port opened successfully, performing handshake");

    // Send HS to wake up device
    SendCommand(device, ":HS#", 200);

    // Start status listener thread
    StartStatusListener(device);

    // Perform handshake with retry mechanism
    if(!SendAndWaitForReplyWithRetry(device, ":HS#", device->handshakeMutex, device->handshakeCV,
                                     device->handshakePending, "handshake"))
    {
        MS_ERROR("MSDeviceOpen: Handshake failed");
        device->port->Close();
        return MS_ERROR_COMMUNICATION;
    }

    // Fetch ENV config
    if(!SendAndWaitForReply(device, ":GES#", device->envModelMutex, device->envModelCV,
                            device->envModelPending, "environment model"))
    {
        MS_ERROR("MSOpen: Failed to fetch environment model");
        return MS_ERROR_COMMUNICATION;
    }

    // Fetch MLX config
    if(!SendAndWaitForReply(device, ":GMS#", device->mlxConfigMutex, device->mlxConfigCV,
                           device->mlxConfigPending, "cloud model"))
    {
        MS_ERROR("MSOpen: Failed to fetch cloud model");
        return MS_ERROR_COMMUNICATION;
    }

    // Fetch TSL config
    if(!SendAndWaitForReply(device, ":GTS#", device->tslConfigMutex, device->tslConfigCV,
                           device->tslConfigPending, "sqm model"))
    {
        MS_ERROR("MSOpen: Failed to fetch sqm model");
        return MS_ERROR_COMMUNICATION;
    }

    // Start telemetry
    if (!SendCommand(device, ":BS#"))
    {
        MS_ERROR("MSDeviceOpen: Failed to start telemetry");
        StopStatusListener(device);
        device->port->Close();
        return MS_ERROR_COMMUNICATION;
    }

    device->isOpen = true;
    MS_INFO("[OK] Device opened");
    return MS_SUCCESS;
}

MSAPI MS_ERROR_TYPE MSDeviceClose(int id)
{
    std::lock_guard<std::mutex> lock(g_globalMutex);

    auto it = g_devices.find(id);
    if (it == g_devices.end())
    {
        return MS_ERROR_INVALID_ID;
    }

    auto device = it->second;

    // Stop telemetry
    if (device->port && device->port->IsOpen())
    {
        SendCommand(device, ":ES#");
    }

    StopStatusListener(device);

    if (device->port)
    {
        device->port->Close();
    }

    device->isOpen = false;
    MS_INFO("[OK] Device closed");
    return MS_SUCCESS;
}

MSAPI MS_ERROR_TYPE MSDeviceGetSerial(int id, char *serial)
{
    if (!serial)
    {
        return MS_ERROR_NULL_POINTER;
    }

    std::lock_guard<std::mutex> lock(g_globalMutex);

    auto it = g_devices.find(id);
    if (it == g_devices.end())
    {
        return MS_ERROR_INVALID_ID;
    }

    auto device = it->second;
    strncpy(serial, device->serial.c_str(), MS_VERSION_LEN - 1);
    serial[MS_VERSION_LEN - 1] = '\0';

    return MS_SUCCESS;
}

MSAPI MS_ERROR_TYPE MSDeviceGetConfig(int id, MS_DEVICE_CONFIG *config)
{
    if (!config)
    {
        return MS_ERROR_NULL_POINTER;
    }

    std::lock_guard<std::mutex> lock(g_globalMutex);

    auto it = g_devices.find(id);
    if (it == g_devices.end())
    {
        return MS_ERROR_INVALID_ID;
    }

    auto device = it->second;

    config->temperatureOffset = device->temperatureOffset;
    config->humidityOffset = device->humidityOffset;
    config->updateRate = device->envUpdateRate;
    config->cloudK1 = device->cloudK1;
    config->cloudK2 = device->cloudK2;
    config->cloudK3 = device->cloudK3;
    config->cloudK4 = device->cloudK4;
    config->cloudK5 = device->cloudK5;
    config->cloudK6 = device->cloudK6;
    config->cloudK7 = device->cloudK7;
    config->cloudTemperatureOvercast = device->cloudTO;
    config->cloudTemperatureClear = device->cloudTC;
    config->cloudFlagPercent = device->cloudFP;
    config->luxScaling = device->luxScaling;

    return MS_SUCCESS;
}

MSAPI MS_ERROR_TYPE MSDeviceSetConfig(int id, MS_DEVICE_CONFIG *config)
{
    if (!config)
    {
        return MS_ERROR_NULL_POINTER;
    }

    std::lock_guard<std::mutex> lock(g_globalMutex);

    auto it = g_devices.find(id);
    if (it == g_devices.end())
    {
        return MS_ERROR_INVALID_ID;
    }

    auto device = it->second;

    if (config->mask & MASK_MS_TEMPERATURE_OFFSET)
    {
        if (config->temperatureOffset < -12.5f || config->temperatureOffset > 12.5f)
        {
            return MS_ERROR_INVALID_PARAMETER;
        }

        // Send command
        char cmd[16];
        snprintf(cmd, sizeof(cmd), ":SET%f#", config->temperatureOffset);

        if (!SendCommand(device, cmd))
        {
            return MS_ERROR_COMMUNICATION;
        }

        device->temperatureOffset = config->temperatureOffset;
    }

    if(config->mask & MASK_MS_HUMIDITY_OFFSET)
    {
        if(config->humidityOffset < -12.5f || config->humidityOffset > 12.5f)
        {
            return MS_ERROR_INVALID_PARAMETER;
        }

        // Send command
        char cmd[16];
        snprintf(cmd, sizeof(cmd), ":SEH%f#", config->humidityOffset);

        if (!SendCommand(device, cmd))
        {
            return MS_ERROR_COMMUNICATION;
        }

        device->humidityOffset = config->humidityOffset;
    }

    if(config->mask & MASK_MS_UPDATE_RATE)
    {
        if(config->updateRate < 1 || config->humidityOffset > 255)
        {
            return MS_ERROR_INVALID_PARAMETER;
        }

        // Send command
        char cmd[16];
        snprintf(cmd, sizeof(cmd), ":SEU%d#", config->updateRate);

        if (!SendCommand(device, cmd))
        {
            return MS_ERROR_COMMUNICATION;
        }

        device->envUpdateRate = config->updateRate;
    }

    if(config->mask & MASK_MS_CLOUD_K1)
    {
        // Send command
        char cmd[16];
        snprintf(cmd, sizeof(cmd), ":SK1%d#", config->cloudK1);

        if (!SendCommand(device, cmd))
        {
            return MS_ERROR_COMMUNICATION;
        }

        device->cloudK1 = config->cloudK1;
    }

    if(config->mask & MASK_MS_CLOUD_K2)
    {
        // Send command
        char cmd[16];
        snprintf(cmd, sizeof(cmd), ":SK2%d#", config->cloudK2);

        if (!SendCommand(device, cmd))
        {
            return MS_ERROR_COMMUNICATION;
        }

        device->cloudK2 = config->cloudK2;
    }

    if(config->mask & MASK_MS_CLOUD_K3)
    {
        // Send command
        char cmd[16];
        snprintf(cmd, sizeof(cmd), ":SK3%d#", config->cloudK3);

        if (!SendCommand(device, cmd))
        {
            return MS_ERROR_COMMUNICATION;
        }

        device->cloudK3 = config->cloudK3;
    }

    if(config->mask & MASK_MS_CLOUD_K4)
    {
        // Send command
        char cmd[16];
        snprintf(cmd, sizeof(cmd), ":SK4%d#", config->cloudK4);

        if (!SendCommand(device, cmd))
        {
            return MS_ERROR_COMMUNICATION;
        }

        device->cloudK4 = config->cloudK4;
    }

    if(config->mask & MASK_MS_CLOUD_K5)
    {
        // Send command
        char cmd[16];
        snprintf(cmd, sizeof(cmd), ":SK5%d#", config->cloudK5);

        if (!SendCommand(device, cmd))
        {
            return MS_ERROR_COMMUNICATION;
        }

        device->cloudK5 = config->cloudK5;
    }

    if(config->mask & MASK_MS_CLOUD_K6)
    {
        // Send command
        char cmd[16];
        snprintf(cmd, sizeof(cmd), ":SK6%d#", config->cloudK6);

        if (!SendCommand(device, cmd))
        {
            return MS_ERROR_COMMUNICATION;
        }

        device->cloudK6 = config->cloudK6;
    }

    if(config->mask & MASK_MS_CLOUD_K7)
    {
        // Send command
        char cmd[16];
        snprintf(cmd, sizeof(cmd), ":SK7%d#", config->cloudK7);

        if (!SendCommand(device, cmd))
        {
            return MS_ERROR_COMMUNICATION;
        }

        device->cloudK7 = config->cloudK7;
    }

    if(config->mask & MASK_MS_CLOUD_TO)
    {
        // Send command
        char cmd[16];
        snprintf(cmd, sizeof(cmd), ":STO%d#", config->cloudTemperatureOvercast);

        if (!SendCommand(device, cmd))
        {
            return MS_ERROR_COMMUNICATION;
        }

        device->cloudTO = config->cloudTemperatureOvercast;
    }

    if(config->mask & MASK_MS_CLOUD_TC)
    {
        // Send command
        char cmd[16];
        snprintf(cmd, sizeof(cmd), ":STC%d#", config->cloudTemperatureClear);

        if (!SendCommand(device, cmd))
        {
            return MS_ERROR_COMMUNICATION;
        }

        device->cloudTC = config->cloudTemperatureClear;
    }

    if(config->mask & MASK_MS_CLOUD_FP)
    {
        // Send command
        char cmd[16];
        snprintf(cmd, sizeof(cmd), ":SFP%d#", config->cloudFlagPercent);

        if (!SendCommand(device, cmd))
        {
            return MS_ERROR_COMMUNICATION;
        }

        device->cloudFP = config->cloudFlagPercent;
    }

    if(config->mask & MASK_MS_LUX_SCALING)
    {
        // Send command
        char cmd[16];
        snprintf(cmd, sizeof(cmd), ":SLS%f#", config->luxScaling);

        if (!SendCommand(device, cmd))
        {
            return MS_ERROR_COMMUNICATION;
        }

        device->luxScaling = config->luxScaling;
    }

    return MS_SUCCESS;
}

MSAPI MS_ERROR_TYPE MSDeviceGetStatus(int id, MS_DEVICE_STATUS *status)
{
    if (!status)
    {
        return MS_ERROR_NULL_POINTER;
    }

    std::lock_guard<std::mutex> lock(g_globalMutex);

    auto it = g_devices.find(id);
    if (it == g_devices.end())
    {
        return MS_ERROR_INVALID_ID;
    }

    auto device = it->second;

    status->upTime = device->upTime;
    status->temperature = device->temperature;
    status->humidity = device->humidity;
    status->dewPoint = device->dewPoint;
    status->skyTemperature = device->skyTemperature;
    status->cloudCover = device->cloudCover;
    status->skyState = device->skyState;
    status->skyBrightness = device->skyBrightness;
    status->skyQuality = device->skyQuality;

    return MS_SUCCESS;
}

MSAPI MS_ERROR_TYPE MSDeviceRestart(int id)
{
    std::lock_guard<std::mutex> lock(g_globalMutex);

    auto it = g_devices.find(id);
    if (it == g_devices.end())
    {
        return MS_ERROR_INVALID_ID;
    }

    auto device = it->second;
    if (!device || !device->port || !device->port->IsOpen())
    {
        return MS_ERROR_INVALID_STATE;
    }

    /* Send factory reset command */
    if (!SendCommand(device, ":RD#"))
    {
        return MS_ERROR_COMMUNICATION;
    }

    return MS_SUCCESS;
}

MSAPI MS_ERROR_TYPE MSDeviceFactoryReset(int id)
{
    std::lock_guard<std::mutex> lock(g_globalMutex);

    auto it = g_devices.find(id);
    if (it == g_devices.end())
    {
        return MS_ERROR_INVALID_ID;
    }

    auto device = it->second;
    if (!device || !device->port || !device->port->IsOpen())
    {
        return MS_ERROR_INVALID_STATE;
    }

    /* Send factory reset command */
    if (!SendCommand(device, ":FR#"))
    {
        return MS_ERROR_COMMUNICATION;
    }

    return MS_SUCCESS;
}

MSAPI MS_ERROR_TYPE MSDeviceGetVersion(int id, MS_VERSION *version)
{
    if (!version)
    {
        return MS_ERROR_NULL_POINTER;
    }

    std::lock_guard<std::mutex> lock(g_globalMutex);

    auto it = g_devices.find(id);
    if (it == g_devices.end())
    {
        return MS_ERROR_INVALID_ID;
    }

    auto device = it->second;
    version->firmware = device->firmwareVersion;

    // Model
    strncpy(version->model, device->modelType.c_str(), sizeof(version->model) - 1);
    version->model[sizeof(version->model) - 1] = '\0';

    // UUID
    strncpy(version->uuid, device->uuid.c_str(), 37);

    // Serial
    strncpy(version->serial, device->serial.c_str(), 9);

    return MS_SUCCESS;
}
