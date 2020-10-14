/* 
 *  radiomoduleconnector.h is part of the HB-RF-ETH firmware - https://github.com/alexreinert/HB-RF-ETH
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

#include "radiomoduleconnector.h"
#include "radiomoduledetector_utils.h"

typedef enum
{
    RADIO_MODULE_NONE = 0,
    RADIO_MODULE_HMIP_RFUSB = 1,
    RADIO_MODULE_HM_MOD_RPI_PCB = 3,
    RADIO_MODULE_RPI_RF_MOD = 4,
} radio_module_type_t;

typedef enum
{
    DETECT_STATE_START_BL = 0,
    DETECT_STATE_START_APP = 10,

    DETECT_STATE_GET_MCU_TYPE = 20,
    DETECT_STATE_GET_VERSION = 30,
    DETECT_STATE_GET_HMIP_RF_ADDRESS = 40,
    DETECT_STATE_GET_SGTIN = 50,
    DETECT_STATE_GET_BIDCOS_RF_ADDRESS = 60,
    DETECT_STATE_GET_SERIAL = 70,

    DETECT_STATE_LEGACY_GET_VERSION = 31,
    DETECT_STATE_LEGACY_GET_BIDCOS_RF_ADDRESS = 61,
    DETECT_STATE_LEGACY_GET_SERIAL = 71,

    DETECT_STATE_FINISHED = 255,
} detect_radio_module_state_t;

class RadioModuleDetector : private FrameHandler
{
private:
    void handleFrame(unsigned char *buffer, uint16_t len);
    void sendFrame(uint8_t counter, uint8_t destination, uint8_t command, unsigned char *data, uint data_len);

    char _serial[11] = {0};
    uint32_t _bidCosRadioMAC = 0;
    uint32_t _hmIPRadioMAC = 0;
    char _sgtin[25] = {0};
    uint8_t _firmwareVersion[3] = {0};
    radio_module_type_t _radioModuleType = RADIO_MODULE_NONE;

    int _detectState;
    int _detectRetryCount;
    int _detectMsgCounter;
    SemaphoreHandle_t _detectWaitFrameDataSemaphore;
    RadioModuleConnector *_radioModuleConnector;

public:
    void detectRadioModule(RadioModuleConnector *radioModuleConnector);
    const char *getSerial();
    uint32_t getBidCosRadioMAC();
    uint32_t getHmIPRadioMAC();
    const char *getSGTIN();
    const uint8_t *getFirmwareVersion();
    radio_module_type_t getRadioModuleType();
};