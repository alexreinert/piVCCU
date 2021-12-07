/*
 *  Copyright 2021 Alexander Reinert
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
#include <chrono>
#include <ctime>
#include <sstream>
#include <iomanip>
#include "radiomoduleconnector.h"
#include "radiomoduledetector.h"

void handleFrame(unsigned char *buffer, uint16_t len);
void readThreadProc(int fd, StreamParser *sp);

int _fd;

bool debug = false;

int main(int argc, char *argv[])
{
  if ((argc >= 2) && (strcmp(argv[1], "--debug") == 0))
  {
    debug = true;
  }

  if (argc != (debug ? 3 : 2))
  {
    printf("Usage: %s [--debug] <path>\n", argv[0]);
    return -1;
  }

  char *path = argv[debug ? 2 : 1];

  int fd = open(path, O_RDWR | O_NOCTTY | O_SYNC);
  if (fd < 0)
  {
    close(fd);
    printf("%s could not be opened\n", path);
    return -1;
  }

  RadioModuleConnector connector(fd);
  connector.start();

  RadioModuleDetector detector;
  detector.detectRadioModule(&connector);

  close(fd);

  const char *moduleType;
  const char *sgtin = detector.getSGTIN();

  switch (detector.getRadioModuleType())
  {
  case RADIO_MODULE_HMIP_RFUSB:
    moduleType = (strstr(sgtin, "3014F5") == sgtin) ? "HMIP-RFUSB-TK" : "HMIP-RFUSB";
    break;
  case RADIO_MODULE_HM_MOD_RPI_PCB:
    moduleType = "HM-MOD-RPI-PCB";
    break;
  case RADIO_MODULE_RPI_RF_MOD:
    moduleType = "RPI-RF-MOD";
    break;
  case RADIO_MODULE_NONE:
    printf("Error: Radio module was not detected\n");
    return -1;
  case RADIO_MODULE_UNKNOWN:
    printf("Error: Radio module was found, but did not respond correctly (maybe bricked App in flashrom)\n");
    return -1;
  default:
    printf("Error: Radio module was found, but type is unknown or not supported (0x%02X)\n", detector.getRadioModuleType());
    return -1;
  }
  const uint8_t *firmwareVersion = detector.getFirmwareVersion();
  printf("%s %s %s 0x%06X 0x%06X %d.%d.%d\n", moduleType, detector.getSerial(), sgtin, detector.getBidCosRadioMAC(), detector.getHmIPRadioMAC(), firmwareVersion[0], firmwareVersion[1], firmwareVersion[2]);
  return 0;
}

bool sem_wait_timeout(sem_t *sem, int timeout)
{
  int s;
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  ts.tv_sec += timeout;

  while ((s = sem_timedwait(sem, &ts)) == -1 && errno == EINTR)
  {
    continue;
  }

  return s == 0;
}

std::string timestamp()
{
    auto now_clock {std::chrono::system_clock::now()};
    auto now_time {std::chrono::system_clock::to_time_t(now_clock)};
    auto now_local {*std::localtime(&now_time)};
    auto now_epoch {now_clock.time_since_epoch()};
    auto msec {std::chrono::duration_cast<std::chrono::microseconds>(now_epoch).count() % 1000000};

    std::ostringstream stream;
    stream << std::put_time (&now_local, "%T") << "." << std::setw(6) << std::setfill ('0') << msec;
    return stream.str();
}

void log_frame(const char *text, unsigned char buffer[], uint16_t len)
{
  if (!debug)
    return;

  std::cout << timestamp() << " ";
  fputs(text, stdout);
  for (int i = 0; i < len; i++)
  {
    printf(" %02x", buffer[i]);
  }
  puts("");
}
