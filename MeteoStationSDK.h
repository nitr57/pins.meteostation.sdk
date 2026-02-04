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

#ifndef METEO_STATION_SDK_H
#define METEO_STATION_SDK_H

#ifdef __cplusplus
extern "C"
{
#endif

#ifdef _WINDOWS
#define MSAPI __declspec(dllexport)
#else
#define MSAPI
#endif

#define MS_MAX_NUM 32               /* Maximum device numbers supported by this SDK */
#define MS_NAME_LEN 32              /* Buffer length for name strings */
#define MS_VERSION_LEN 32           /* Buffer length for version strings */
#define MS_UUID_LEN 37              /* Buffer length for UUID */

    typedef enum _MS_ERROR_TYPE
    {
        MS_SUCCESS = 0,             /* Success */
        MS_ERROR_INVALID_ID,        /* Device ID is invalid */
        MS_ERROR_INVALID_PARAMETER, /* One or more parameters are invalid */
        MS_ERROR_INVALID_STATE,     /* Device is not in correct state for specific API call */
        MS_ERROR_COMMUNICATION,     /* Data communication error such as device has been removed from USB port */
        MS_ERROR_NULL_POINTER,      /* Caller passes null-pointer parameter which is not expected */
    } MS_ERROR_TYPE;

/*
 * Used by MSxxxSetConfig() to indicate which field wants to be set
 */
#define MASK_MS_TEMPERATURE_OFFSET 0x0001
#define MASK_MS_HUMIDITY_OFFSET    0x0002
#define MASK_MS_UPDATE_RATE        0x0004
#define MASK_MS_CLOUD_K1           0x0008
#define MASK_MS_CLOUD_K2           0x0010
#define MASK_MS_CLOUD_K3           0x0020
#define MASK_MS_CLOUD_K4           0x0040
#define MASK_MS_CLOUD_K5           0x0080
#define MASK_MS_CLOUD_K6           0x0100
#define MASK_MS_CLOUD_K7           0x0200
#define MASK_MS_CLOUD_TO           0x0400
#define MASK_MS_CLOUD_TC           0x0800
#define MASK_MS_CLOUD_FP           0x1000
#define MASK_MS_LUX_SCALING        0x2000
#define MASK_MS_ALL                0x3FFF

    typedef struct _MS_VERSION
    {
        unsigned int firmware;          /* Device firmware version */
        char model[MS_NAME_LEN];        /* Model type (e.g., "Lite", "Mini") */
        char uuid[MS_UUID_LEN];         /* Unique identifier */
        char serial[MS_VERSION_LEN];    /* Device ID */
    } MS_VERSION;

    typedef struct _MS_DEVICE_CONFIG
    {
        unsigned int mask;              /* Used by MSDeviceSetConfig() to indicate which field wants to be set */
        float temperatureOffset;        /* Temperature offset [-12.5, +12.5] [°C] */
        float humidityOffset;           /* Temperature offset [-12.5, +12.5] [%] */
        int updateRate;                 /* Sensor refresh rate [s], default: 3s */
        int cloudK1;                    /* Cloud modeling parameter K1 */
        int cloudK2;                    /* Cloud modeling parameter K2 */
        int cloudK3;                    /* Cloud modeling parameter K3 */
        int cloudK4;                    /* Cloud modeling parameter K4 */
        int cloudK5;                    /* Cloud modeling parameter K5 */
        int cloudK6;                    /* Cloud modeling parameter K6 */
        int cloudK7;                    /* Cloud modeling parameter K7 */
        int cloudTemperatureOvercast;   /* Cloud temperature overcast parameter */
        int cloudTemperatureClear;      /* Cloud temperature clear parameter */
        int cloudFlagPercent;           /* Cloud flag percent parameter */
        float luxScaling;               /* Scaling factor for sky brightness */
    } MS_DEVICE_CONFIG;

    typedef struct _MS_DEVICE_STATUS
    {
        int upTime;                 /* Up time [s] */
        float temperature;          /* Ambient temperature [°C] */
        float humidity;             /* Humidity [%] */
        float dewPoint;             /* Dew Point [°C] */
        float skyTemperature;       /* Sky temperature [°C] */
        int cloudCover;             /* Cloud cover [%] */
        int skyState;               /* Sky state [0: sky clear, 1: sky cloudy]*/
        float skyBrightness;        /* Sky brightness [lx] */
        float skyQuality;           /* Sky quality SQM [Mag/arcsec^2] */
    } MS_DEVICE_STATUS;

    /* Device scanning and management */
    MSAPI MS_ERROR_TYPE MSDeviceScan(int *number, int *ids);
    MSAPI MS_ERROR_TYPE MSDeviceOpen(int id);
    MSAPI MS_ERROR_TYPE MSDeviceClose(int id);
    MSAPI MS_ERROR_TYPE MSDeviceGetSerial(int id, char *serial);

    /* Configuration */
    MSAPI MS_ERROR_TYPE MSDeviceGetConfig(int id, MS_DEVICE_CONFIG *config);
    MSAPI MS_ERROR_TYPE MSDeviceSetConfig(int id, MS_DEVICE_CONFIG *config);

    /* Status and information */
    MSAPI MS_ERROR_TYPE MSDeviceGetStatus(int id, MS_DEVICE_STATUS *status);
    MSAPI MS_ERROR_TYPE MSDeviceGetVersion(int id, MS_VERSION *version);

    /* Utility */
    MSAPI MS_ERROR_TYPE MSDeviceRestart(int id);
    MSAPI MS_ERROR_TYPE MSDeviceFactoryReset(int id);
    MSAPI MS_ERROR_TYPE MSGetSDKVersion(char *version);

#ifdef __cplusplus
}
#endif

#endif /* METEO_STATION_SDK_H */
