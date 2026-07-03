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

#ifndef METEO_STATION_DEVICE_H
#define METEO_STATION_DEVICE_H

#include "MeteoStationSerialPort.h"
#include "MeteoStationSDK.h"
#include <memory>
#include <string>
#include <map>
#include <mutex>
#include <thread>
#include <atomic>
#include <chrono>
#include <condition_variable>

namespace MeteoStation
{
    /**
     * Device represents a Meteo Station device with its current state.
     */
    struct Device
    {
        std::shared_ptr<SerialPort> port;
        std::string portName;
        std::string modelType;
        std::string uuid;
        std::string serial;
        int firmwareVersion = 0;

        // Status
        int upTime = 0;
        float temperature = 0.0f;
        float humidity = 0.0f;
        float dewPoint = 0.0f;
        float ambientTemperature = 0.0f;
        float skyTemperature = 0.0f;
        int cloudCover = 0;
        int skyState = 0;
        float skyBrightness = 0.0f;
        float skyQuality = 0.0f;

        // Config
        float temperatureOffset = 0.0f;
        float humidityOffset = 0.0f;
        int envUpdateRate = 3;
        int cloudK1 = 0;
        int cloudK2 = 0;
        int cloudK3 = 0;
        int cloudK4 = 0;
        int cloudK5 = 0;
        int cloudK6 = 0;
        int cloudK7 = 0;
        int cloudTO = 0;
        int cloudTC = 0;
        int cloudFP = 0;
        float luxScaling = 0.0f;

        /* Config mutexes etc */
        std::mutex handshakeMutex;
        std::mutex envModelMutex;
        std::mutex mlxConfigMutex;
        std::mutex tslConfigMutex;
        std::condition_variable handshakeCV;
        std::condition_variable envModelCV;
        std::condition_variable mlxConfigCV;
        std::condition_variable tslConfigCV;
        std::atomic<bool> handshakePending{false};
        std::atomic<bool> envModelPending{false};
        std::atomic<bool> mlxConfigPending{false};
        std::atomic<bool> tslConfigPending{false};

        /* Listener thread state */
        std::atomic<bool> statusListenerRunning{false};
        std::atomic<bool> isOpen{false};
        std::thread statusListenerThread;

        /* Serializes MSDeviceOpen/MSDeviceClose's handling of statusListenerThread
         * on this device, so callers never need to hold g_globalMutex across the
         * potentially multi-second port-open/listener-join/handshake sequence. */
        std::mutex openCloseMutex;

        /* Guards the Status and Config field groups above: the listener thread
         * writes them (from parsed telemetry/config messages) with no other
         * synchronization, while MSDeviceGetStatus/GetConfig/SetConfig read and
         * write them from API calls. Must be held on both sides. */
        std::mutex stateMutex;

        /* Guards every write to port: the listener thread's telemetry watchdog
         * writes :HS#/:BS# directly to recover from a stalled stream, while API
         * calls (SetConfig/Restart/FactoryReset/Close) write commands via
         * SendCommand. Neither path synchronized with the other before, so two
         * threads could write to the same fd concurrently and interleave bytes
         * on the wire. Must be held around every device->port->Write() call. */
        std::mutex writeMutex;

        /* Telemetry watchdog: tracks last time a valid message was received */
        std::chrono::steady_clock::time_point lastMessageTime{std::chrono::steady_clock::now()};

        /* Destructor: thread uses raw ptr so Device can never be destroyed from within
         * the thread itself. Safe to always join here. */
        ~Device() {
            statusListenerRunning = false;
            if(statusListenerThread.joinable()) {
                statusListenerThread.join();
            }
        }
    };

    /**
     * Global device registry mapping device IDs to Device objects.
     */
    extern std::map<int, std::shared_ptr<Device>> g_devices;

    /**
     * Global mutex protecting access to g_devices.
     */
    extern std::mutex g_globalMutex;

} /* namespace MeteoStation */

#endif /* METEO_STATION_DEVICE_H */
