/* 
 *  radiomoduledetector.cpp is part of the HB-RF-ETH firmware - https://github.com/alexreinert/HB-RF-ETH
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "radiomoduledetector.h"
#include "hmframe.h"

static const char *TAG = "RadioModuleConnector";

void RadioModuleDetector::detectRadioModule(RadioModuleConnector *radioModuleConnector)
{
    _radioModuleConnector = radioModuleConnector;

    _detectState = DETECT_STATE_START_BL;
    _detectRetryCount = 0;
    _detectMsgCounter = 0;

    _radioModuleType = RADIO_MODULE_NONE;

    sem_init(_detectWaitFrameDataSemaphore);

    _radioModuleConnector->setFrameHandler(this, true);

    while (_detectState == DETECT_STATE_START_BL && _detectRetryCount < 3)
    {
        sendFrame(_detectMsgCounter++, HM_DST_COMMON, HM_CMD_COMMON_IDENTIFY, NULL, 0);
        if (!sem_take(_detectWaitFrameDataSemaphore, 3))
        {
            sendFrame(_detectMsgCounter++, HM_DST_HMSYSTEM, HM_CMD_HMSYSTEM_IDENTIFY, NULL, 0);
            if (!sem_take(_detectWaitFrameDataSemaphore, 3))
            {
                _detectRetryCount++;
            }
        }
    }

    _detectRetryCount = 0;
    while (_detectState == DETECT_STATE_START_APP && _detectRetryCount < 3)
    {
        sendFrame(_detectMsgCounter++, HM_DST_COMMON, HM_CMD_COMMON_IDENTIFY, NULL, 0);
        if (!sem_take(_detectWaitFrameDataSemaphore, 3))
        {
            sendFrame(_detectMsgCounter++, HM_DST_HMSYSTEM, HM_CMD_HMSYSTEM_IDENTIFY, NULL, 0);
            if (!sem_take(_detectWaitFrameDataSemaphore, 3))
            {
                _detectRetryCount++;
            }
        }
    }

    while (true)
    {
        switch (_detectState)
        {
        case DETECT_STATE_START_BL:
        case DETECT_STATE_START_APP:
            _detectState = DETECT_STATE_FINISHED;
            break;

        case DETECT_STATE_GET_MCU_TYPE:
            sendFrame(_detectMsgCounter++, HM_DST_TRX, HM_CMD_TRX_GET_MCU_TYPE, NULL, 0);
            break;

        case DETECT_STATE_GET_VERSION:
            sendFrame(_detectMsgCounter++, HM_DST_TRX, HM_CMD_TRX_GET_VERSION, NULL, 0);
            break;

        case DETECT_STATE_GET_HMIP_RF_ADDRESS:
            sendFrame(_detectMsgCounter++, HM_DST_HMIP, HM_CMD_HMIP_GET_DEFAULT_RF_ADDR, NULL, 0);
            break;

        case DETECT_STATE_GET_SGTIN:
            sendFrame(_detectMsgCounter++, HM_DST_COMMON, HM_CMD_COMMON_GET_SGTIN, NULL, 0);
            break;

        case DETECT_STATE_GET_BIDCOS_RF_ADDRESS:
            sendFrame(_detectMsgCounter++, HM_DST_LLMAC, HM_CMD_LLMAC_GET_DEFAULT_RF_ADDR, NULL, 0);
            break;

        case DETECT_STATE_GET_SERIAL:
            sendFrame(_detectMsgCounter++, HM_DST_LLMAC, HM_CMD_LLMAC_GET_SERIAL, NULL, 0);
            break;

        case DETECT_STATE_LEGACY_GET_VERSION:
            sendFrame(_detectMsgCounter++, HM_DST_HMSYSTEM, HM_CMD_HMSYSTEM_GET_VERSION, NULL, 0);
            break;

        case DETECT_STATE_LEGACY_GET_BIDCOS_RF_ADDRESS:
            sendFrame(_detectMsgCounter++, HM_DST_TRX, HM_CMD_TRX_GET_DEFAULT_RF_ADDR, NULL, 0);
            break;

        case DETECT_STATE_LEGACY_GET_SERIAL:
            sendFrame(_detectMsgCounter++, HM_DST_HMSYSTEM, HM_CMD_HMSYSTEM_GET_SERIAL, NULL, 0);
            break;

        case DETECT_STATE_FINISHED:
            break;
        }

        if (_detectState == DETECT_STATE_FINISHED || !sem_take(_detectWaitFrameDataSemaphore, 3))
        {
            break;
        }
    }

    _radioModuleConnector->setFrameHandler(NULL, false);
}

void RadioModuleDetector::handleFrame(unsigned char *buffer, uint16_t len)
{
    log_frame("Received HM frame:", buffer, len);

    HMFrame frame;
    if (!HMFrame::TryParse(buffer, len, &frame))
    {
        return;
    }

    switch (_detectState)
    {
    case DETECT_STATE_START_BL:
        if ((frame.destination == HM_DST_COMMON && frame.command == HM_CMD_COMMON_ACK && frame.data_len == 12 && frame.data[0] == 1 && strncmp((char *)(frame.data + 1), "HMIP_TRX_Bl", 11) == 0) || (frame.destination == HM_DST_COMMON && frame.command == 0 && frame.data_len == 11 && strncmp((char *)(frame.data), "HMIP_TRX_Bl", 11) == 0))
        {
            // TRX CoPro in bootloader
            _detectState = DETECT_STATE_START_APP;
            sem_give(_detectWaitFrameDataSemaphore);
        }
        else if ((frame.destination == HM_DST_HMSYSTEM && frame.command == HM_CMD_HMSYSTEM_ACK && frame.data_len == 10 && frame.data[0] == 2 && strncmp((char *)(frame.data + 1), "Co_CPU_BL", 9) == 0) || (frame.destination == HM_DST_HMSYSTEM && frame.command == 0 && frame.data_len == 9 && strncmp((char *)frame.data, "Co_CPU_BL", 9) == 0))
        {
            // Legacy CoPro in bootloader
            _detectState = DETECT_STATE_START_APP;
            sem_give(_detectWaitFrameDataSemaphore);
        }
        else if ((frame.destination == HM_DST_COMMON && frame.command == HM_CMD_COMMON_ACK && frame.data_len == 14 && frame.data[0] == 1 && strncmp((char *)(frame.data + 1), "DualCoPro_App", 13) == 0) || (frame.destination == HM_DST_COMMON && frame.command == 0 && frame.data_len == 13 && strncmp((char *)frame.data, "DualCoPro_App", 13) == 0))
        {
            // Dual CoPro in app --> start bootloader
            sendFrame(_detectMsgCounter++, HM_DST_COMMON, HM_CMD_COMMON_START_BL, NULL, 0);
        }
        else if ((frame.destination == HM_DST_COMMON && frame.command == HM_CMD_COMMON_ACK && frame.data_len == 13 && frame.data[0] == 1 && strncmp((char *)(frame.data + 1), "HMIP_TRX_App", 12) == 0) || (frame.destination == HM_DST_COMMON && frame.command == 0 && frame.data_len == 12 && strncmp((char *)frame.data, "HMIP_TRX_App", 12) == 0))
        {
            // HmIP only in app --> start bootloader
            sendFrame(_detectMsgCounter++, HM_DST_COMMON, HM_CMD_COMMON_START_BL, NULL, 0);
        }
        else if ((frame.destination == HM_DST_HMSYSTEM && frame.command == HM_CMD_HMSYSTEM_ACK && frame.data_len == 11 && frame.data[0] == 2 && strncmp((char *)(frame.data + 1), "Co_CPU_App", 10) == 0) || (frame.destination == HM_DST_HMSYSTEM && frame.command == 0 && frame.data_len == 10 && strncmp((char *)frame.data, "Co_CPU_App", 10) == 0))
        {
            // Legacy CoPro in app --> start bootloader
            sendFrame(_detectMsgCounter++, HM_DST_HMSYSTEM, HM_CMD_HMSYSTEM_CHANGE_APP, NULL, 0);
        }
        break;

    case DETECT_STATE_START_APP:
        if ((frame.destination == HM_DST_COMMON && frame.command == HM_CMD_COMMON_ACK && frame.data_len == 12 && frame.data[0] == 1 && strncmp((char *)(frame.data + 1), "HMIP_TRX_Bl", 11) == 0) || (frame.destination == HM_DST_COMMON && frame.command == 0 && frame.data_len == 11 && strncmp((char *)(frame.data), "HMIP_TRX_Bl", 11) == 0))
        {
            // TRX CoPro in bootloader --> start app
            sendFrame(_detectMsgCounter++, HM_DST_COMMON, HM_CMD_COMMON_START_APP, NULL, 0);
        }
        else if ((frame.destination == HM_DST_HMSYSTEM && frame.command == HM_CMD_HMSYSTEM_ACK && frame.data_len == 10 && frame.data[0] == 2 && strncmp((char *)(frame.data + 1), "Co_CPU_BL", 9) == 0) || (frame.destination == HM_DST_HMSYSTEM && frame.command == 0 && frame.data_len == 9 && strncmp((char *)frame.data, "Co_CPU_BL", 9) == 0))
        {
            // Legacy CoPro in bootloader --> start app
            sendFrame(_detectMsgCounter++, HM_DST_HMSYSTEM, HM_CMD_HMSYSTEM_CHANGE_APP, NULL, 0);
        }
        else if ((frame.destination == HM_DST_COMMON && frame.command == HM_CMD_COMMON_ACK && frame.data_len == 14 && frame.data[0] == 1 && strncmp((char *)(frame.data + 1), "DualCoPro_App", 13) == 0) || (frame.destination == HM_DST_COMMON && frame.command == 0 && frame.data_len == 13 && strncmp((char *)frame.data, "DualCoPro_App", 13) == 0))
        {
            // Dual CoPro in app
            _detectState = DETECT_STATE_GET_MCU_TYPE;
            sem_give(_detectWaitFrameDataSemaphore);
        }
        else if ((frame.destination == HM_DST_COMMON && frame.command == HM_CMD_COMMON_ACK && frame.data_len == 13 && frame.data[0] == 1 && strncmp((char *)(frame.data + 1), "HMIP_TRX_App", 12) == 0) || (frame.destination == HM_DST_COMMON && frame.command == 0 && frame.data_len == 12 && strncmp((char *)frame.data, "HMIP_TRX_App", 12) == 0))
        {
            // HmIP only in app
            _detectState = DETECT_STATE_GET_MCU_TYPE;
            sem_give(_detectWaitFrameDataSemaphore);
        }
        else if ((frame.destination == HM_DST_HMSYSTEM && frame.command == HM_CMD_HMSYSTEM_ACK && frame.data_len == 11 && frame.data[0] == 2 && strncmp((char *)(frame.data + 1), "Co_CPU_App", 10) == 0) || (frame.destination == HM_DST_HMSYSTEM && frame.command == 0 && frame.data_len == 10 && strncmp((char *)frame.data, "Co_CPU_App", 10) == 0))
        {
            // Legacy CoPro in app
            _detectState = DETECT_STATE_LEGACY_GET_VERSION;
            sprintf(_sgtin, "n/a");
            _hmIPRadioMAC = 0;
            _radioModuleType = RADIO_MODULE_HM_MOD_RPI_PCB;
            sem_give(_detectWaitFrameDataSemaphore);
        }
        break;

    case DETECT_STATE_GET_MCU_TYPE:
        if (frame.destination == HM_DST_TRX && frame.command == HM_CMD_TRX_ACK && frame.data_len == 2 && frame.data[0] == 1)
        {
            _radioModuleType = (radio_module_type_t)frame.data[1];
            _detectState = DETECT_STATE_GET_VERSION;
            sem_give(_detectWaitFrameDataSemaphore);
        }
        break;

    case DETECT_STATE_GET_VERSION:
        if (frame.destination == HM_DST_TRX && frame.command == HM_CMD_TRX_ACK && frame.data_len == 10 && frame.data[0] == 1)
        {
            memcpy(_firmwareVersion, frame.data + 1, 3);
            _detectState = DETECT_STATE_GET_HMIP_RF_ADDRESS;
            sem_give(_detectWaitFrameDataSemaphore);
        }
        break;

    case DETECT_STATE_GET_HMIP_RF_ADDRESS:
        if (frame.destination == HM_DST_HMIP && frame.command == HM_CMD_HMIP_ACK && frame.data_len == 4 && frame.data[0] == 1)
        {
            _hmIPRadioMAC = (frame.data[1] << 16) | (frame.data[2] << 8) | frame.data[3];
            _detectState = DETECT_STATE_GET_SGTIN;
            sem_give(_detectWaitFrameDataSemaphore);
        }
        break;

    case DETECT_STATE_GET_SGTIN:
        if (frame.destination == HM_DST_COMMON && frame.command == HM_CMD_COMMON_ACK && frame.data_len == 13 && frame.data[0] == 1)
        {
            sprintf(_sgtin, "%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X", frame.data[1], frame.data[2], frame.data[3], frame.data[4], frame.data[5], frame.data[6], frame.data[7], frame.data[8], frame.data[9], frame.data[10], frame.data[11], frame.data[12]);

            switch (_radioModuleType)
            {
            case RADIO_MODULE_RPI_RF_MOD:
                _bidCosRadioMAC = 0xff0000 | (frame.data[11] << 8) | frame.data[12];
                if (_bidCosRadioMAC == 0xffffff)
                    _bidCosRadioMAC = 0xfffffe;
                sprintf(_serial, "%02X%02X%02X%02X%02X", frame.data[8], frame.data[9], frame.data[10], frame.data[11], frame.data[12]);
                _detectState = DETECT_STATE_GET_BIDCOS_RF_ADDRESS;
                break;

            case RADIO_MODULE_HMIP_RFUSB:
                _bidCosRadioMAC = 0;
                sprintf(_serial, "%02X%02X%02X%02X%02X", frame.data[8], frame.data[9], frame.data[10], frame.data[11], frame.data[12]);
                _detectState = DETECT_STATE_FINISHED;
                break;

            default:
                _detectState = DETECT_STATE_GET_BIDCOS_RF_ADDRESS;
                break;
            }
            sem_give(_detectWaitFrameDataSemaphore);
        }
        break;

    case DETECT_STATE_GET_BIDCOS_RF_ADDRESS:
        if (frame.destination == HM_DST_LLMAC && frame.command == HM_CMD_LLMAC_ACK && frame.data_len == 4 && frame.data[0] == 1)
        {
            uint32_t radioMac = (frame.data[1] << 16) | (frame.data[2] << 8) | frame.data[3];
            if (radioMac != 0 && (radioMac & 0xffff) != 0xffff)
                _bidCosRadioMAC = radioMac;
            _detectState = _radioModuleType == RADIO_MODULE_RPI_RF_MOD ? DETECT_STATE_FINISHED : DETECT_STATE_GET_SERIAL;
            sem_give(_detectWaitFrameDataSemaphore);
        }
        else if (frame.destination == HM_DST_LLMAC && frame.command == HM_CMD_LLMAC_ACK && frame.data_len == 1 && frame.data[0] == 0)
        {
            if (_radioModuleType == RADIO_MODULE_RPI_RF_MOD)
                _detectState = DETECT_STATE_FINISHED;
            sem_give(_detectWaitFrameDataSemaphore);
        }
        break;

    case DETECT_STATE_GET_SERIAL:
        if (frame.destination == HM_DST_LLMAC && frame.command == HM_CMD_LLMAC_ACK && frame.data_len == 11 && frame.data[0] == 1)
        {
            memcpy(_serial, frame.data + 1, 10);
            _detectState = DETECT_STATE_FINISHED;
            sem_give(_detectWaitFrameDataSemaphore);
        }
        break;

    case DETECT_STATE_LEGACY_GET_VERSION:
        if (frame.destination == HM_DST_HMSYSTEM && frame.command == HM_CMD_HMSYSTEM_ACK && frame.data_len == 7 && frame.data[0] == 2)
        {
            memcpy(_firmwareVersion, frame.data + 4, 3);
            _detectState = DETECT_STATE_LEGACY_GET_BIDCOS_RF_ADDRESS;
            sem_give(_detectWaitFrameDataSemaphore);
        }
        break;

    case DETECT_STATE_LEGACY_GET_BIDCOS_RF_ADDRESS:
        if (frame.destination == HM_DST_TRX && frame.command == HM_CMD_TRX_ACK && frame.data_len == 6)
        {
            _bidCosRadioMAC = (frame.data[3] << 16) | (frame.data[4] << 8) | frame.data[5];
            _detectState = DETECT_STATE_LEGACY_GET_SERIAL;
            sem_give(_detectWaitFrameDataSemaphore);
        }
        break;

    case DETECT_STATE_LEGACY_GET_SERIAL:
        if (frame.destination == HM_DST_HMSYSTEM && frame.command == HM_CMD_HMSYSTEM_ACK && frame.data_len == 11 && frame.data[0] == 2)
        {
            memcpy(_serial, frame.data + 1, 10);
            _detectState = DETECT_STATE_FINISHED;
            sem_give(_detectWaitFrameDataSemaphore);
        }
        break;
    }
}

const char *RadioModuleDetector::getSerial()
{
    return _serial;
}

uint32_t RadioModuleDetector::getBidCosRadioMAC()
{
    return _bidCosRadioMAC;
}

uint32_t RadioModuleDetector::getHmIPRadioMAC()
{
    return _hmIPRadioMAC;
}

const char *RadioModuleDetector::getSGTIN()
{
    return _sgtin;
}

const uint8_t *RadioModuleDetector::getFirmwareVersion()
{
    return _firmwareVersion;
}

radio_module_type_t RadioModuleDetector::getRadioModuleType()
{
    return _radioModuleType;
}

void RadioModuleDetector::sendFrame(uint8_t counter, uint8_t destination, uint8_t command, unsigned char *data, uint data_len)
{
    HMFrame frame;
    unsigned char sendBuffer[8 + data_len + 10];

    frame.counter = counter;
    frame.destination = destination;
    frame.command = command;
    frame.data = data;
    frame.data_len = data_len;
    uint16_t len = frame.encode(sendBuffer, sizeof(sendBuffer), true);

    log_frame("Sending HM frame:", sendBuffer, len);

    _radioModuleConnector->sendFrame(sendBuffer, len);
}
