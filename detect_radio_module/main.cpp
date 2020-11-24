#include <stdio.h>
#include <stdlib.h>
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

  switch (detector.getRadioModuleType())
  {
  case RADIO_MODULE_HMIP_RFUSB:
    moduleType = "HmIP-RFUSB";
    break;
  case RADIO_MODULE_HM_MOD_RPI_PCB:
    moduleType = "HM-MOD-RPI-PCB";
    break;
  case RADIO_MODULE_RPI_RF_MOD:
    moduleType = "RPI-RF-MOD";
    break;
  default:
    printf("Error: Radio module was not detected\n");
    ;
    return -1;
  }
  const uint8_t *firmwareVersion = detector.getFirmwareVersion();
  printf("%s %s %s 0x%06X 0x%06X %d.%d.%d\n", moduleType, detector.getSerial(), detector.getSGTIN(), detector.getBidCosRadioMAC(), detector.getHmIPRadioMAC(), firmwareVersion[0], firmwareVersion[1], firmwareVersion[2]);
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

void log_frame(const char *text, unsigned char buffer[], uint16_t len)
{
  if (!debug)
    return;

  fputs(text, stdout);
  for (int i = 0; i < len; i++)
  {
    printf(" %02x", buffer[i]);
  }
  puts("");
}
