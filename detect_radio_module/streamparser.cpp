/* 
 *  streamparser.cpp is part of the HB-RF-ETH firmware - https://github.com/alexreinert/HB-RF-ETH
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

#include "streamparser.h"
#include <stdint.h>

StreamParser::StreamParser(bool decodeEscaped, std::function<void(unsigned char *buffer, uint16_t len)> processor) : _bufferPos(0), _state(NO_DATA), _isEscaped(false), _decodeEscaped(decodeEscaped), _processor(processor)
{
}

void StreamParser::append(unsigned char chr)
{
    switch (chr)
    {
    case 0xfd:
        _bufferPos = 0;
        _isEscaped = false;
        _state = RECEIVE_LENGTH_HIGH_BYTE;
        break;

    case 0xfc:
        _isEscaped = true;
        if (_decodeEscaped)
            return;
        break;

    default:
        if (_isEscaped && _decodeEscaped)
            chr |= 0x80;

        switch (_state)
        {
        case NO_DATA:
        case FRAME_COMPLETE:
            return; // Do nothing until the first frame prefix occurs

        case RECEIVE_LENGTH_HIGH_BYTE:
            _frameLength = (_isEscaped ? chr | 0x80 : chr) << 8;
            _state = RECEIVE_LENGTH_LOW_BYTE;
            break;

        case RECEIVE_LENGTH_LOW_BYTE:
            _frameLength |= (_isEscaped ? chr | 0x80 : chr);
            _frameLength += 2; // handle crc as frame data
            _framePos = 0;
            _state = RECEIVE_FRAME_DATA;
            break;

        case RECEIVE_FRAME_DATA:
            _framePos++;
            _state = (_framePos == _frameLength) ? FRAME_COMPLETE : RECEIVE_FRAME_DATA;
            break;
        }
        _isEscaped = false;
    }

    _buffer[_bufferPos++] = chr;

    if (_bufferPos == sizeof(_buffer))
        _state = FRAME_COMPLETE;

    if (_state == FRAME_COMPLETE)
    {
        _processor(_buffer, _bufferPos);
        _state = NO_DATA;
    }
}

void StreamParser::append(unsigned char *buffer, uint16_t len)
{
    int i;
    for (i = 0; i < len; i++)
    {
        append(buffer[i]);
    }
}

void StreamParser::flush()
{
    _state = NO_DATA;
    _bufferPos = 0;
    _isEscaped = false;
}

bool StreamParser::getDecodeEscaped()
{
    return _decodeEscaped;
}
void StreamParser::setDecodeEscaped(bool decodeEscaped)
{
    _decodeEscaped = decodeEscaped;
}
