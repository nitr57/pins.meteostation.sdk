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

#ifndef METEO_STATION_LOGGING_H
#define METEO_STATION_LOGGING_H

/* ============================================================================
 * METEO STATION SDK - LOGGING MODULE
 *
 * Compile-time controlled logging system for developers.
 * All logging is disabled in release builds unless explicitly enabled.
 * ============================================================================ */

namespace MeteoStation
{
    /* Compile-time logging configuration */
    static constexpr bool MS_DEBUG_ENABLED = false; /* Disable debug logging by default */
    static constexpr bool MS_INFO_ENABLED = false;    /* Enable info logging */
    static constexpr bool MS_ERROR_ENABLED = true;    /* Enable error logging */
    static constexpr bool MS_TIMESTAMP_ENABLED = true; /* Enable timestamps in logs */

/* Logging macros - use these throughout the SDK */

/**
 * Debug logging macro.
 * Use for detailed diagnostic messages during development.
 * Controlled by MS_DEBUG_ENABLED compile-time flag.
 */
#define MS_DEBUG(fmt, ...)                                   \
    do                                                       \
    {                                                        \
        if (MeteoStation::MS_DEBUG_ENABLED)               \
        {                                                    \
            MeteoStation::MSLogDebug(fmt, ##__VA_ARGS__); \
        }                                                    \
    } while (0)

/**
 * Info logging macro.
 * Use for informational messages about normal operations.
 * Controlled by MS_INFO_ENABLED compile-time flag.
 */
#define MS_INFO(fmt, ...)                                   \
    do                                                      \
    {                                                       \
        if (MeteoStation::MS_INFO_ENABLED)               \
        {                                                   \
            MeteoStation::MSLogInfo(fmt, ##__VA_ARGS__); \
        }                                                   \
    } while (0)

/**
 * Error logging macro.
 * Use for error messages and failure conditions.
 * Always enabled by default (MS_ERROR_ENABLED = true).
 */
#define MS_ERROR(fmt, ...)                                   \
    do                                                       \
    {                                                        \
        if (MeteoStation::MS_ERROR_ENABLED)               \
        {                                                    \
            MeteoStation::MSLogError(fmt, ##__VA_ARGS__); \
        }                                                    \
    } while (0)

    /**
     * Log a debug message with optional timestamp.
     * Only outputs if MS_DEBUG_ENABLED is true.
     * Supports printf-style format strings.
     */
    void MSLogDebug(const char *fmt, ...);

    /**
     * Log an info message with optional timestamp.
     * Only outputs if MS_INFO_ENABLED is true.
     * Supports printf-style format strings.
     */
    void MSLogInfo(const char *fmt, ...);

    /**
     * Log an error message with optional timestamp.
     * Enabled by default and should always be called on error conditions.
     * Supports printf-style format strings.
     */
    void MSLogError(const char *fmt, ...);

    /**
     * Get current timestamp string for logging.
     * Optionally includes in log output if MS_TIMESTAMP_ENABLED is true.
     *
     * @return Pointer to static timestamp string buffer
     */
    const char *MSGetTimestamp();

} /* namespace MeteoStation */

#endif /* METEO_STATION_LOGGING_H */
