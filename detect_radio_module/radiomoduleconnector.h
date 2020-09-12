/* 
 *  Copyright 2020 Alexander Reinert
 *  
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *  
 *      http://www.apache.org/licenses/LICENSE-2.0
 *  
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <iostream>
#include <termios.h>
#include <iomanip>
#include <thread>
#include "streamparser.h"

class FrameHandler
{
public:
    virtual void handleFrame(unsigned char *buffer, uint16_t len) = 0;
};

class RadioModuleConnector
{
private:
    StreamParser *_streamParser;
    FrameHandler *_frameHandler = NULL;
    std::thread *_reader = NULL;
    int _fd;

    void _handleFrame(unsigned char *buffer, uint16_t len);

public:
    RadioModuleConnector(int fd);

    void start();
    void stop();

    void setFrameHandler(FrameHandler *handler, bool decodeEscaped);

    void sendFrame(unsigned char *buffer, uint16_t len);
};
