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
#include <string.h>
#include "radiomoduleconnector.h"
#include "hmframe.h"

static const char *TAG = "RadioModuleConnector";

void readThreadProc(int fd, StreamParser *sp)
{
  int len;
  unsigned char buf[1];

  while (true)
  {
    int len = read(fd, buf, 1);
    if (len > 0)
    {
      sp->append(buf[0]);
    }
  }
}

RadioModuleConnector::RadioModuleConnector(int fd) : _fd(fd)
{

  using namespace std::placeholders;
  _streamParser = new StreamParser(false, std::bind(&RadioModuleConnector::_handleFrame, this, _1, _2));

  struct termios tty;
  if (tcgetattr(_fd, &tty) == 0)
  {
    tty.c_cflag &= ~PARENB;
    tty.c_cflag &= ~CSTOPB;
    tty.c_cflag |= CS8;
    tty.c_cflag &= ~CRTSCTS;
    tty.c_cflag |= CREAD | CLOCAL;

    tty.c_lflag &= ~ICANON;
    tty.c_lflag &= ~ECHO;
    tty.c_lflag &= ~ECHOE;
    tty.c_lflag &= ~ECHONL;
    tty.c_lflag &= ~ISIG;
    tty.c_iflag &= ~(IXON | IXOFF | IXANY);
    tty.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL);

    tty.c_oflag &= ~OPOST;
    tty.c_oflag &= ~ONLCR;

    tty.c_cc[VTIME] = 1;
    tty.c_cc[VMIN] = 0;

    cfsetispeed(&tty, B115200);
    cfsetospeed(&tty, B115200);
    tcsetattr(_fd, TCSANOW, &tty);
  }
}

void RadioModuleConnector::start()
{
  _reader = new std::thread(readThreadProc, _fd, _streamParser);
  _reader->detach();
}

void RadioModuleConnector::stop()
{
}

void RadioModuleConnector::setFrameHandler(FrameHandler *frameHandler, bool decodeEscaped)
{
  _frameHandler = frameHandler;
  _streamParser->setDecodeEscaped(decodeEscaped);
}

void RadioModuleConnector::sendFrame(unsigned char *buffer, uint16_t len)
{
  write(_fd, buffer, len);
  fsync(_fd);
}

void RadioModuleConnector::_handleFrame(unsigned char *buffer, uint16_t len)
{
  FrameHandler *frameHandler = _frameHandler;

  if (frameHandler)
  {
    frameHandler->handleFrame(buffer, len);
  }
}
