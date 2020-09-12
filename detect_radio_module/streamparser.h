/* 
 *  streamparser.h is part of the HB-RF-ETH firmware - https://github.com/alexreinert/HB-RF-ETH
 *  
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

#include <stdint.h>
#include <functional>

typedef enum
{
    NO_DATA,
    RECEIVE_LENGTH_HIGH_BYTE,
    RECEIVE_LENGTH_LOW_BYTE,
    RECEIVE_FRAME_DATA,
    FRAME_COMPLETE
} state_t;

class StreamParser
{
private:
    unsigned char _buffer[2048];
    uint16_t _bufferPos;
    uint16_t _framePos;
    uint16_t _frameLength;
    state_t _state;
    bool _isEscaped;
    bool _decodeEscaped;
    std::function<void(unsigned char *buffer, uint16_t len)> _processor;

public:
    StreamParser(bool decodeEscaped, std::function<void(unsigned char *buffer, uint16_t len)> processor);

    void append(unsigned char chr);
    void append(unsigned char *buffer, uint16_t len);
    void flush();

    bool getDecodeEscaped();
    void setDecodeEscaped(bool decodeEscaped);
};
