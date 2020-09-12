/* 
 *  hmframe.cpp is part of the HB-RF-ETH firmware - https://github.com/alexreinert/HB-RF-ETH
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

#include "hmframe.h"
#include <string.h>

uint16_t HMFrame::crc(unsigned char *buffer, uint16_t len)
{
    uint16_t crc = 0xd77f;
    int i;

    while (len--)
    {
        crc ^= *buffer++ << 8;
        for (i = 0; i < 8; i++)
        {
            if (crc & 0x8000)
            {
                crc <<= 1;
                crc ^= 0x8005;
            }
            else
            {
                crc <<= 1;
            }
        }
    }

    return crc;
}

bool HMFrame::TryParse(unsigned char *buffer, uint16_t len, HMFrame *frame)
{
    uint16_t crc;

    if (len < 8)
        return false;

    if (buffer[0] != 0xfd)
        return false;

    frame->data_len = ((buffer[1] << 8) | buffer[2]) - 3;
    if (frame->data_len + 8 != len)
        return false;

    crc = (buffer[len - 2] << 8) | buffer[len - 1];
    if (crc != HMFrame::crc(buffer, len - 2))
        return false;

    frame->destination = buffer[3];
    frame->counter = buffer[4];
    frame->command = buffer[5];
    frame->data = &buffer[6];

    return true;
}

HMFrame::HMFrame() : data_len(0)
{
}

uint16_t HMFrame::encode(unsigned char *buffer, uint16_t len, bool escaped)
{
    uint16_t crc;

    if (data_len + 8 > len)
        return 0;

    buffer[0] = 0xfd;
    buffer[1] = ((data_len + 3) >> 8) & 0xff;
    buffer[2] = (data_len + 3) & 0xff;
    buffer[3] = destination;
    buffer[4] = counter;
    buffer[5] = command;
    if (data_len > 0)
        memcpy(&(buffer[6]), data, data_len);

    crc = HMFrame::crc(buffer, data_len + 6);
    buffer[data_len + 6] = (crc >> 8) & 0xff;
    buffer[data_len + 7] = crc & 0xff;

    uint16_t res = data_len + 8;

    if (escaped)
    {
        for (uint16_t i = 1; i < res; i++)
        {
            if (buffer[i] == 0xfc || buffer[i] == 0xfd)
            {
                memmove(buffer + i + 1, buffer + i, res - i);
                buffer[i++] = 0xfc;
                buffer[i] &= 0x7f;
                res++;
            }
        }
    }

    return res;
}
