/**
 * logger.h
 *
 *  Created on: 16 Sep 2017
 *      Author: junior
 *
 * Copyright 2018 Onalenna Junior Makhura
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation files
 * (the "Software"), to deal in the Software without restriction,
 * including without limitation the rights to use, copy, modify, merge,
 * publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT.  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/** \file logger.h */
#ifndef HDBSCAN_LOGGER_H_
#define HDBSCAN_LOGGER_H_

#ifdef __cplusplus
extern "C" {
#endif
#include <stdio.h>
#include <stdlib.h>

enum LOGTYPE {
    FATAL,      // A fatal error has occured: program will exit immediately
    ERROR,      // An error has occured: program may not exit
    INFO,       // Nessessary information regarding program operation
    WARN,       // Any circumstance that may not affect normal operation
    NONE        // Logging that does need the time and type of the message
};
/*!
 * \brief Initalise the logger. We can choose to open the log file if
 * the library was compiled with -D DEBUG_ENABLE=TRUE 
 */
void logger_init();

/*!
 * \brief Write the log either 
 * 
 * \param type 
 * \param str 
 */
void logger_write(enum LOGTYPE type, const char* str);
void logger_close();
#endif
#ifdef __cplusplus
}

#endif /* HDBSCAN_LOGGER_H_ */
