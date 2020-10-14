/* 
 *  hmframe.h is part of the HB-RF-ETH firmware - https://github.com/alexreinert/HB-RF-ETH
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

class HMFrame
{
public:
    static bool TryParse(unsigned char *buffer, uint16_t len, HMFrame *frame);
    static uint16_t crc(unsigned char *buffer, uint16_t len);

    HMFrame();
    uint8_t counter;
    uint8_t destination;
    uint8_t command;
    unsigned char *data;
    uint16_t data_len;

    uint16_t encode(unsigned char *buffer, uint16_t len, bool escaped);
};

typedef enum
{
    HM_DST_HMSYSTEM = 0x00,
    HM_DST_TRX = 0x01,
    HM_DST_HMIP = 0x02,
    HM_DST_LLMAC = 0x03,
    HM_DST_COMMON = 0xfe,
} hm_dst_t;

typedef enum
{
    HM_CMD_HMSYSTEM_IDENTIFY = 0x00,
    HM_CMD_HMSYSTEM_GET_VERSION = 0x02,
    HM_CMD_HMSYSTEM_CHANGE_APP = 0x03,
    HM_CMD_HMSYSTEM_ACK = 0x04,
    HM_CMD_HMSYSTEM_GET_SERIAL = 0x0b,
} hm_cmd_hmsystem_t;

typedef enum
{
    HM_CMD_TRX_GET_VERSION = 0x02,
    HM_CMD_TRX_ACK = 0x04,
    HM_CMD_TRX_GET_MCU_TYPE = 0x09,
    HM_CMD_TRX_GET_DEFAULT_RF_ADDR = 0x10,
} hm_cmd_trx_t;

typedef enum
{
    HM_CMD_HMIP_GET_DEFAULT_RF_ADDR = 0x01,
    HM_CMD_HMIP_ACK = 0x06,
} hm_cmd_hmip_t;

typedef enum
{
    HM_CMD_LLMAC_ACK = 0x01,
    HM_CMD_LLMAC_GET_SERIAL = 0x07,
    HM_CMD_LLMAC_GET_DEFAULT_RF_ADDR = 0x08,
} hm_cmd_llmac_t;

typedef enum
{
    HM_CMD_COMMON_IDENTIFY = 0x01,
    HM_CMD_COMMON_START_BL = 0x02,
    HM_CMD_COMMON_START_APP = 0x03,
    HM_CMD_COMMON_GET_SGTIN = 0x04,
    HM_CMD_COMMON_ACK = 0x05,
} hm_cmd_common_t;
