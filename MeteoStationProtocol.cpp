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

#include "MeteoStationProtocol.h"
#include "MeteoStationLogging.h"
#include <cstring>
#include <cstdio>
#include <memory>
#include <chrono>
#include <thread>

/* Telemetry watchdog: if no valid message is received for this many seconds,
 * flush serial buffers and re-send :BS# to restart streaming.
 * This handles cases where firmware stops streaming due to noise on the line
 * being interpreted as :HS# or :ES#, USB glitches, or watchdog resets. */
#define TELEMETRY_WATCHDOG_TIMEOUT_S 15

namespace MeteoStation
{
    bool SendCommand(std::shared_ptr<Device> device, const char *command, int timeoutMs)
    {
        if (!device)
        {
            return MS_ERROR_NULL_POINTER;
        }

        if (!device->port || !device->port->IsOpen())
        {
            MS_DEBUG("SendCommand: device=%p, port=%p, isOpen=%d",
                     device.get(), device ? device->port.get() : nullptr,
                     device && device->port ? device->port->IsOpen() : 0);
            return false;
        }

        MS_DEBUG("SendCommand: Writing '%s'", command);
        if (!device->port->Write((const unsigned char *)command, strlen(command)))
        {
            MS_DEBUG("SendCommand: Write failed");
            return MS_ERROR_COMMUNICATION;
        }

        return true;
    }

    /* Message parsing helper functions */
    static void ParseHandshakeMessage(Device* device, const char *buffer)
    {
        char model[33];
        char uuid[41];
        char serial[33];
        int firmware;
        if (sscanf(buffer, "PINS:%32[^:]:%40[^:]:%32[^:]:%d#",
                   model, uuid, serial, &firmware) == 4)
        {
            // Store in device
            device->modelType = model;
            device->uuid = uuid;
            device->serial = serial;
            device->firmwareVersion = firmware;
            device->handshakePending = false;
        }

        device->handshakeCV.notify_one();
    }

    static void ParseUptimeMessage(Device* device, const char *buffer)
    {
        sscanf(buffer, "UP:%d#", &device->upTime);
    }

    static void ParseEnvironmentMessage(Device* device, const char *buffer)
    {
        sscanf(buffer, "ENV:%f:%f:%f#", &device->temperature, &device->humidity, &device->dewPoint);
    }

    static void ParseEnvModelMessage(Device* device, const char *buffer)
    {
        float tempOffset, humOffset;
        int envUpdate;
        if (sscanf(buffer, "ENVMODEL:%f:%f:%d#", &tempOffset, &humOffset, &envUpdate) == 3)
        {
            // Store in device
            {
                device->temperatureOffset = tempOffset;
                device->humidityOffset = humOffset;
                device->envUpdateRate = envUpdate;
                device->envModelPending = false;
            }

            device->envModelCV.notify_one();
        }
    }

    static void ParseMLXConfigMessage(Device* device, const char *buffer)
    {
        int cloud[10];
        if (sscanf(buffer, "MLXMODEL:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d#",
                   &cloud[0], &cloud[1], &cloud[2], &cloud[3], &cloud[4],
                   &cloud[5], &cloud[6], &cloud[7], &cloud[8], &cloud[9]) == 10)
        {
            // Store in device
            {
                device->cloudK1 = cloud[0];
                device->cloudK2 = cloud[1];
                device->cloudK3 = cloud[2];
                device->cloudK4 = cloud[3];
                device->cloudK5 = cloud[4];
                device->cloudK6 = cloud[5];
                device->cloudK7 = cloud[6];
                device->cloudTO = cloud[7];
                device->cloudTC = cloud[8];
                device->cloudFP = cloud[9];
                device->mlxConfigPending = false;
            }

            device->mlxConfigCV.notify_one();
        }
    }

    static void ParseTSLConfigMessage(Device* device, const char *buffer)
    {
        float lux;
        if (sscanf(buffer, "TSLMODEL:%f#", &lux) == 1)
        {
            // Store in device
            {
                device->luxScaling = lux;
                device->tslConfigPending = false;
            }

            device->tslConfigCV.notify_one();
        }
    }

    static void ParseMLXMessage(Device* device, const char *buffer)
    {
        float ambient;
        sscanf(buffer, "MLX:%f:%f:%d:%d#", &ambient, &device->skyTemperature, &device->cloudCover, &device->skyState);
    }

    static void ParseTSLMessage(Device* device, const char *buffer)
    {
        sscanf(buffer, "TSL:%f:%f#", &device->skyBrightness, &device->skyQuality);
    }

    /* Background listener thread function for status messages */
    static void StatusListenerThreadFunc(Device* device)
    {
        char buffer[256];

        /* Initialize the watchdog timer when the listener starts */
        device->lastMessageTime = std::chrono::steady_clock::now();

        while(device->statusListenerRunning)
        {
            if (!device || !device->port)
            {
                MS_DEBUG("StatusListener: Port unavailable, exiting");
                device->statusListenerRunning = false;
                return;
            }

            if (!device->port->IsOpen())
            {
                MS_DEBUG("StatusListener: Port not open, exiting");
                device->statusListenerRunning = false;
                return;
            }

            if (device->port->Read((unsigned char *)buffer, 256, '#', 5000))
            {
                /* Only count a received message as valid for watchdog purposes
                 * when it is a complete, terminated message ending with '#'.
                 * Partial messages (from OS buffer underruns or serial noise)
                 * must not reset the timer or the watchdog will never fire. */
                size_t len = strlen(buffer);
                bool completeMessage = len > 0 && buffer[len - 1] == '#';
                if (completeMessage)
                {
                    device->lastMessageTime = std::chrono::steady_clock::now();
                }

                /* Parse different message types based on prefix */
                if (strstr(buffer, "PINS:") == buffer)
                {
                    /* Handshake message */
                    ParseHandshakeMessage(device, buffer);
                }
                else if (strstr(buffer, "UP:") == buffer)
                {
                    /* Uptime status */
                    ParseUptimeMessage(device, buffer);
                }
                else if (strstr(buffer, "ENVMODEL:") == buffer)
                {
                    /* Environment model */
                    ParseEnvModelMessage(device, buffer);
                }
                else if (strstr(buffer, "MLXMODEL:") == buffer)
                {
                    /* MLX model config */
                    ParseMLXConfigMessage(device, buffer);
                }
                else if (strstr(buffer, "TSLMODEL:") == buffer)
                {
                    /* TSL model config */
                    ParseTSLConfigMessage(device, buffer);
                }
                else if (strstr(buffer, "ENV:") == buffer)
                {
                    /* Environment status */
                    ParseEnvironmentMessage(device, buffer);
                }
                else if (strstr(buffer, "MLX:") == buffer)
                {
                    /* MLX status */
                    ParseMLXMessage(device, buffer);
                }
                else if (strstr(buffer, "TSL:") == buffer)
                {
                    /* TSL status */
                    ParseTSLMessage(device, buffer);
                }
            }
            else
            {
                /* Read timed out with no complete message - check telemetry watchdog */
                auto elapsed = std::chrono::steady_clock::now() - device->lastMessageTime;
                auto elapsedSec = std::chrono::duration_cast<std::chrono::seconds>(elapsed).count();

                if (device->isOpen && elapsedSec >= TELEMETRY_WATCHDOG_TIMEOUT_S)
                {
                    MS_DEBUG("StatusListener: No telemetry for %lld seconds, attempting recovery",
                             (long long)elapsedSec);

                    /* Flush serial buffers (OS + application level) to clear any corrupted data */
                    device->port->Flush();

                    /* Send :HS# first: if the device firmware has reset (watchdog, USB
                     * glitch, etc.) it requires a handshake before it will honour :BS#.
                     * Sending :HS# unconditionally is safe - a running device will just
                     * reply with its identification string which the parser handles fine. */
                    const char *hs_cmd = ":HS#";
                    device->port->Write((const unsigned char *)hs_cmd, strlen(hs_cmd));

                    /* Give the device time to process the handshake before requesting
                     * streaming. A reset device typically boots in < 300 ms. */
                    std::this_thread::sleep_for(std::chrono::milliseconds(500));

                    /* Re-send begin streaming command to restart telemetry */
                    const char *cmd = ":BS#";
                    device->port->Write((const unsigned char *)cmd, strlen(cmd));

                    /* Reset watchdog timer to avoid rapid re-sends */
                    device->lastMessageTime = std::chrono::steady_clock::now();

                    MS_DEBUG("StatusListener: Recovery :HS# + :BS# sent, waiting for telemetry to resume");
                }
            }
        }

        MS_DEBUG("StatusListener: exiting");
    }

    void StartStatusListener(std::shared_ptr<Device> device)
    {
        if (!device)
        {
            return;
        }

        /* Stop any existing listener by setting the flag */
        device->statusListenerRunning = false;

        /* Wait for old thread to exit if it's still running */
        if(device->statusListenerThread.joinable()) {
            device->statusListenerThread.join();
        }

        /* Start new listener thread */
        device->statusListenerRunning = true;
        device->statusListenerThread = std::thread(StatusListenerThreadFunc, device.get());
        MS_DEBUG("StartStatusListener: Listener thread started");
    }

    void StopStatusListener(std::shared_ptr<Device> device)
    {
        if (!device)
        {
            return;
        }

        /* Signal listener thread to stop */
        device->statusListenerRunning = false;
        MS_DEBUG("StopStatusListener: Listener stop requested");
        
        /* Wait for the listener thread to actually exit */
        if(device->statusListenerThread.joinable()) {
            device->statusListenerThread.join();
            MS_DEBUG("StopStatusListener: Listener thread joined");
        }
    }
} /* namespace MeteoStation */
