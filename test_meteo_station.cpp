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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

void PrintMenu()
{
    printf("\n=== Meteo Station Control Menu ===\n");
    printf("1. Get device status\n");
    printf("2. Get device configuration\n");
    printf("3. Set device configuration\n");
    printf("4. Open device\n");
    printf("5. Close device\n");
    printf("6. Restart device\n");
    printf("7. Factory reset device\n");
    printf("10. Refresh and display all info\n");
    printf("11. Exit\n");
    printf("> ");
}

void DisplayStatus(int deviceId)
{
    MS_DEVICE_STATUS status;
    MS_ERROR_TYPE result = MSDeviceGetStatus(deviceId, &status);
    if (result == MS_SUCCESS)
    {
        printf("\n--- Device Status ---\n");
        printf("Up time: %d s\n", status.upTime);
        printf("Temperature: %.2f °C\n", status.temperature);
        printf("Humidity: %.2f %%\n", status.humidity);
        printf("Dew Point: %.2f °C\n", status.dewPoint);
        printf("Sky Temperature: %.2f °C\n", status.skyTemperature);
        printf("Cloud Cover: %d %%\n", status.cloudCover);
        printf("Sky State: %d (0=clear,1=cloudy)\n", status.skyState);
        printf("Sky Brightness: %.2f lx\n", status.skyBrightness);
        printf("Sky Quality (SQM): %.2f mag/arcsec^2\n", status.skyQuality);
    }
    else
    {
        printf("[FAIL] Failed to get status (Error: %d)\n", result);
    }
} 

void DisplayConfig(int deviceId)
{
    MS_DEVICE_CONFIG config_v;
    MS_ERROR_TYPE result = MSDeviceGetConfig(deviceId, &config_v);
    if (result == MS_SUCCESS)
    {
        printf("\n--- Current Configuration ---\n");
        printf("Mask: 0x%X\n", config_v.mask);
        printf("Temperature Offset: %.2f °C\n", config_v.temperatureOffset);
        printf("Humidity Offset: %.2f %%\n", config_v.humidityOffset);
        printf("Update Rate: %d s\n", config_v.updateRate);
        printf("Cloud K1..K7: %d %d %d %d %d %d %d\n",
               config_v.cloudK1, config_v.cloudK2, config_v.cloudK3,
               config_v.cloudK4, config_v.cloudK5, config_v.cloudK6, config_v.cloudK7);
        printf("Cloud Temperature Overcast (TO): %d\n", config_v.cloudTemperatureOvercast);
        printf("Cloud Temperature Clear (TC): %d\n", config_v.cloudTemperatureClear);
        printf("Cloud Flag Percent (FP): %d\n", config_v.cloudFlagPercent);
        printf("Lux Scaling: %.4f\n", config_v.luxScaling);
    }
    else
    {
        printf("[FAIL] Failed to get config (Error: %d)\n", result);
    }
}

void SetConfigInteractive(int deviceId)
{
    MS_DEVICE_CONFIG curCfg;
    if (MSDeviceGetConfig(deviceId, &curCfg) != MS_SUCCESS)
    {
        printf("[FAIL] Could not read current configuration\n");
        return;
    }

    MS_DEVICE_CONFIG updates = {};
    unsigned int mask = 0;
    char input[128];

    printf("\nSet new configuration values (leave empty to keep current)\n");

    printf("Temperature Offset (current: %.4f): ", curCfg.temperatureOffset);
    fflush(stdout);
    if (fgets(input, sizeof(input), stdin) && input[0] != '\n')
    {
        updates.temperatureOffset = (float)atof(input);
        mask |= MASK_MS_TEMPERATURE_OFFSET;
    }

    printf("Humidity Offset (current: %.4f): ", curCfg.humidityOffset);
    fflush(stdout);
    if (fgets(input, sizeof(input), stdin) && input[0] != '\n')
    {
        updates.humidityOffset = (float)atof(input);
        mask |= MASK_MS_HUMIDITY_OFFSET;
    }

    printf("Update Rate in seconds (current: %d): ", curCfg.updateRate);
    fflush(stdout);
    if (fgets(input, sizeof(input), stdin) && input[0] != '\n')
    {
        updates.updateRate = atoi(input);
        mask |= MASK_MS_UPDATE_RATE;
    }

    printf("Cloud K1 (current: %d): ", curCfg.cloudK1);
    fflush(stdout);
    if (fgets(input, sizeof(input), stdin) && input[0] != '\n')
    {
        updates.cloudK1 = atoi(input);
        mask |= MASK_MS_CLOUD_K1;
    }

    printf("Cloud K2 (current: %d): ", curCfg.cloudK2);
    fflush(stdout);
    if (fgets(input, sizeof(input), stdin) && input[0] != '\n')
    {
        updates.cloudK2 = atoi(input);
        mask |= MASK_MS_CLOUD_K2;
    }

    printf("Cloud K3 (current: %d): ", curCfg.cloudK3);
    fflush(stdout);
    if (fgets(input, sizeof(input), stdin) && input[0] != '\n')
    {
        updates.cloudK3 = atoi(input);
        mask |= MASK_MS_CLOUD_K3;
    }

    printf("Cloud K4 (current: %d): ", curCfg.cloudK4);
    fflush(stdout);
    if (fgets(input, sizeof(input), stdin) && input[0] != '\n')
    {
        updates.cloudK4 = atoi(input);
        mask |= MASK_MS_CLOUD_K4;
    }

    printf("Cloud K5 (current: %d): ", curCfg.cloudK5);
    fflush(stdout);
    if (fgets(input, sizeof(input), stdin) && input[0] != '\n')
    {
        updates.cloudK5 = atoi(input);
        mask |= MASK_MS_CLOUD_K5;
    }

    printf("Cloud K6 (current: %d): ", curCfg.cloudK6);
    fflush(stdout);
    if (fgets(input, sizeof(input), stdin) && input[0] != '\n')
    {
        updates.cloudK6 = atoi(input);
        mask |= MASK_MS_CLOUD_K6;
    }

    printf("Cloud K7 (current: %d): ", curCfg.cloudK7);
    fflush(stdout);
    if (fgets(input, sizeof(input), stdin) && input[0] != '\n')
    {
        updates.cloudK7 = atoi(input);
        mask |= MASK_MS_CLOUD_K7;
    }

    printf("Cloud Temperature Overcast (TO) (current: %d): ", curCfg.cloudTemperatureOvercast);
    fflush(stdout);
    if (fgets(input, sizeof(input), stdin) && input[0] != '\n')
    {
        updates.cloudTemperatureOvercast = atoi(input);
        mask |= MASK_MS_CLOUD_TO;
    }

    printf("Cloud Temperature Clear (TC) (current: %d): ", curCfg.cloudTemperatureClear);
    fflush(stdout);
    if (fgets(input, sizeof(input), stdin) && input[0] != '\n')
    {
        updates.cloudTemperatureClear = atoi(input);
        mask |= MASK_MS_CLOUD_TC;
    }

    printf("Cloud Flag Percent (FP) (current: %d): ", curCfg.cloudFlagPercent);
    fflush(stdout);
    if (fgets(input, sizeof(input), stdin) && input[0] != '\n')
    {
        updates.cloudFlagPercent = atoi(input);
        mask |= MASK_MS_CLOUD_FP;
    }

    printf("Lux Scaling (current: %.4f): ", curCfg.luxScaling);
    fflush(stdout);
    if (fgets(input, sizeof(input), stdin) && input[0] != '\n')
    {
        updates.luxScaling = (float)atof(input);
        mask |= MASK_MS_LUX_SCALING;
    }

    if (mask == 0)
    {
        printf("No changes provided; nothing to set.\n");
        return;
    }

    updates.mask = mask;
    MS_ERROR_TYPE r = MSDeviceSetConfig(deviceId, &updates);
    if (r == MS_SUCCESS)
    {
        printf("Configuration updated successfully.\n");
    }
    else
    {
        printf("Failed to set configuration (Error: %d)\n", r);
    }
}

void DisplayAllInfo(int deviceId)
{
    MS_VERSION version;
    MS_ERROR_TYPE result = MSDeviceGetVersion(deviceId, &version);
    if (result == MS_SUCCESS)
    {
        printf("\n=== Device Information ===\n");
        printf("Model: %s\n", version.model);
        printf("Firmware: %u\n", version.firmware);
        printf("UUID: %s\n", version.uuid);
        printf("DeviceID: %s\n", version.serial);
    }
    else
    {
        printf("[FAIL] Failed to get version (Error: %d)\n", result);
        return;
    }

    DisplayStatus(deviceId);
    DisplayConfig(deviceId);
}

int main(int argc, char *argv[])
{
    printf("=== Meteo Station Interactive Control ===\n\n");

    /* Get SDK version */
    char sdkVersion[32];
    MS_ERROR_TYPE result = MSGetSDKVersion(sdkVersion);
    if (result == MS_SUCCESS)
    {
        printf("SDK Version: %s\n\n", sdkVersion);
    }

    /* Scan for devices */
    printf("Scanning for Meteo Station devices...\n");
    int deviceCount = 32;
    int deviceIds[32] = {0};

    result = MSDeviceScan(&deviceCount, deviceIds);
    if (result != MS_SUCCESS)
    {
        printf("[FAIL] Scan failed (Error: %d)\n", result);
        return 1;
    }

    printf("[OK] Found %d device(s)\n\n", deviceCount);

    if (deviceCount == 0)
    {
        printf("No Meteo Station devices found. Please connect a device.\n");
        return 0;
    }

    /* List found devices */
    printf("Available devices:\n");
    for (int i = 0; i < deviceCount; i++)
    {
        printf("  [%d] Device ID: %d\n", i, deviceIds[i]);
    }
    printf("\n");

    /* Select device */
    int deviceId = deviceIds[0];
    if (deviceCount > 1)
    {
        printf("Enter device index to use (0-%d) [default: 0]: ", deviceCount - 1);
        char input[10];
        if (fgets(input, sizeof(input), stdin) != NULL)
        {
            int idx = atoi(input);
            if (idx >= 0 && idx < deviceCount)
            {
                deviceId = deviceIds[idx];
            }
        }
    }

    printf("Using device: %d\n\n", deviceId);

    /* Start with device closed; user may open via menu option 4 */
    bool deviceOpened = false;
    printf("Device is currently closed. Use menu option 4 to open it.\n\n");

    /* Interactive menu */
    char input[256];
    bool running = true;

    while (running)
    {
        PrintMenu();
        fflush(stdout);

        if (fgets(input, sizeof(input), stdin) == NULL)
        {
            break;
        }

        int choice = atoi(input);

        switch (choice)
        {
        case 1:
            if (!deviceOpened)
            {
                printf("Device not opened. Use option 4 to open it first.\n");
            }
            else
            {
                DisplayStatus(deviceId);
            }
            break;

        case 2:
            if (!deviceOpened)
            {
                printf("Device not opened. Use option 4 to open it first.\n");
            }
            else
            {
                DisplayConfig(deviceId);
            }
            break;

        case 3:
            if (!deviceOpened)
            {
                printf("Device not opened. Use option 4 to open it first.\n");
            }
            else
            {
                SetConfigInteractive(deviceId);
            }
            break;

        case 4:
            if (deviceOpened)
            {
                printf("Device already open.\n");
            }
            else
            {
                printf("Opening device...\n");
                result = MSDeviceOpen(deviceId);
                if (result == MS_SUCCESS)
                {
                    deviceOpened = true;
                    printf("[OK] Device opened\n");
                }
                else
                {
                    printf("[FAIL] Failed to open device (Error: %d)\n", result);
                }
            }
            break;

        case 5:
            if (!deviceOpened)
            {
                printf("Device already closed.\n");
            }
            else
            {
                printf("Closing device...\n");
                result = MSDeviceClose(deviceId);
                if (result == MS_SUCCESS)
                {
                    deviceOpened = false;
                    printf("[OK] Device closed\n");
                }
                else
                {
                    printf("[FAIL] Failed to close device (Error: %d)\n", result);
                }
            }
            break;

        case 6:
            if (!deviceOpened)
            {
                printf("Device not opened. Use option 4 to open it first.\n");
            }
            else
            {
                printf("Restarting device...\n");
                result = MSDeviceRestart(deviceId);
                if (result == MS_SUCCESS)
                {
                    printf("[OK] Restart command sent\n");
                }
                else
                {
                    printf("[FAIL] Failed to restart device (Error: %d)\n", result);
                }
            }
            break;

        case 7:
            if (!deviceOpened)
            {
                printf("Device not opened. Use option 4 to open it first.\n");
            }
            else
            {
                printf("Factory reset will restore device to defaults. Are you sure? (y/N): ");
                fflush(stdout);
                char conf[8];
                if (fgets(conf, sizeof(conf), stdin) && (conf[0] == 'y' || conf[0] == 'Y'))
                {
                    printf("Performing factory reset...\n");
                    result = MSDeviceFactoryReset(deviceId);
                    if (result == MS_SUCCESS)
                    {
                        printf("[OK] Factory reset command sent\n");
                    }
                    else
                    {
                        printf("[FAIL] Failed to factory reset device (Error: %d)\n", result);
                    }
                }
                else
                {
                    printf("Factory reset cancelled.\n");
                }
            }
            break;

        case 10:
            if (!deviceOpened)
            {
                printf("Device not opened. Use option 4 to open it first.\n");
            }
            else
            {
                DisplayAllInfo(deviceId);
            }
            break;

        case 11:
            running = false;
            break;

        default:
            printf("[FAIL] Unknown command\n");
            break;
        }
    }

    /* Close device if still open */
    if (deviceOpened)
    {
        printf("\nClosing device...\n");
        result = MSDeviceClose(deviceId);
        if (result == MS_SUCCESS)
        {
            printf("[OK] Device closed\n");
        }
        else
        {
            printf("[FAIL] Failed to close device (Error: %d)\n", result);
        }
    } else {
        printf("\nDevice was already closed.\n");
    }

    printf("\n=== Test Complete ===\n");
    return 0;
}
