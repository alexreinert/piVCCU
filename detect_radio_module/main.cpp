/*
 *  Copyright 2023 Alexander Reinert
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
#include <stdarg.h>
#include <chrono>
#include <ctime>
#include <sstream>
#include <iomanip>
#include <sys/ioctl.h>
#include "radiomoduleconnector.h"
#include "radiomoduledetector.h"

#define MAX_DEVICE_TYPE_LEN 64
#define IOCTL_MAGIC 'u'
#define IOCTL_IOCRESET_RADIO_MODULE _IO(IOCTL_MAGIC, 0x81)
#define IOCTL_IOCGDEVINFO _IOW(IOCTL_MAGIC, 0x82, char[MAX_DEVICE_TYPE_LEN])

void handleFrame(unsigned char *buffer, uint16_t len);
void readThreadProc(int fd, StreamParser *sp);
void log(const char *text, ...);

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

  unsigned char deviceType[MAX_DEVICE_TYPE_LEN];
  if (!ioctl(fd, IOCTL_IOCGDEVINFO, deviceType))
  {
    log("Raw UART device: %s", deviceType);
  }
  else
  {
    switch (errno)
    {
    case ENOTTY:
      // current generic_raw_uart kernel module does not support getting device type
      break;
    default:
      log("Raw UART device: unknown");
      break;
    }
  }

  if (ioctl(fd, IOCTL_IOCRESET_RADIO_MODULE))
  {
    switch (errno)
    {
    case EBUSY:
      close(fd);
      printf("Raw UART device is in use, aborting.\n");
      return -1;
    case ENOTTY:
      log("Resetting radio module via current device is not supported.");
      break;
    case ENOSYS:
      log("Resetting radio module is not supported.");
      break;
    default:
      log("Reset of radio module failed (%d).", errno);
      break;
    }
  }
  else
  {
    log("Sucessfully resetted radio module.");
  }

  RadioModuleConnector connector(fd);
  connector.start();

  RadioModuleDetector detector;
  detector.detectRadioModule(&connector);

  if (!ioctl(fd, IOCTL_IOCRESET_RADIO_MODULE))
    log("Sucessfully resetted radio module.");

  close(fd);

  const char *moduleType;
  const char *sgtin = detector.getSGTIN();

  switch (detector.getRadioModuleType())
  {
  case RADIO_MODULE_HMIP_RFUSB:
    moduleType = (strstr(sgtin, "3014F5AC") == sgtin) ? "HMIP-RFUSB-TK" : "HMIP-RFUSB";
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

void log(const char *text, ...)
{
  if (!debug)
    return;

  std::cout << timestamp() << " ";

  va_list args;
  va_start (args, text);
  vprintf (text, args);
  va_end (args);

  puts("");
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
