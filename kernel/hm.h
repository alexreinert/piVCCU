/*-----------------------------------------------------------------------------
 * Copyright (c) 2018 by Alexander Reinert
 * Author: Alexander Reinert
 * Uses parts of bcm2835_raw_uart.c. (c) 2015 by eQ-3 Entwicklung GmbH
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *---------------------------------------------------------------------------*/
enum hm_dst
{
  HM_DST_TRX                   = 0x01,
  HM_DST_HMIP                  = 0x02,
  HM_DST_LLMAC                 = 0x03,
  HM_DST_COMMON                = 0xfe,
};

enum hm_trx_cmd
{
  HM_TRX_GET_VERSION           = 0x02,
  HM_TRX_GET_DUTYCYCLE         = 0x03,
  HM_TRX_SET_DCUTYCYCLE_LIMIT  = 0x07,
  HM_TRX_GET_MCU_TYPE          = 0x09,
};

enum hm_llmac_cmd
{
  HM_LLMAC_GET_TIMESTAMP       = 0x02,
  HM_LLMAC_RFD_INIT            = 0x06,
  HM_LLMAC_GET_SERIAL          = 0x07,
  HM_LLMAC_GET_DEFAULT_RF_ADDR = 0x08,
};

enum hm_common_cmd
{
  HM_COMMON_IDENTIFY           = 0x01,
  HM_COMMON_GET_SGTIN          = 0x04,
};

enum hm_hmip_cmd
{
  HM_HMIP_SET_RADIO_ADDR       = 0x00,
  HM_HMIP_SEND                 = 0x03,
  HM_HMIP_ADD_LINK_PARTNER     = 0x04,
  HM_HMIP_GET_SECURITY_COUNTER = 0x0a,
  HM_HMIP_SET_SECURITY_COUNTER = 0x08,
  HM_HMIP_SET_MAX_SENT_ATTEMPS = 0x0d,
  HM_HMIP_GET_LINK_PARTNER     = 0x12,
  HM_HMIP_GET_NWKEY            = 0x13,
  HM_HMIP_SET_NWKEY            = 0x14,
};


static uint16_t hm_crc(unsigned char *buf, size_t len)
{
  uint16_t crc = 0xd77f;
  int i;

  while (len--)
  {
    crc ^= *buf++ << 8;
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

struct hm_frame
{
  uint8_t dst;
  uint8_t cnt;
  unsigned char *cmd;
  int cmdlen;
};

static bool tryParseFrame(unsigned char *buf, size_t len, struct hm_frame *frame)
{
  uint16_t crc;

  if (len < 8)
    return false;

  if (buf[0] != 0xfd)
    return false;

  frame->cmdlen = ((buf[1] << 8) | buf[2]) - 2;
  if (frame->cmdlen + 7 != len)
    return false;

  crc = (buf[len - 2] << 8) | buf[len - 1];
  if (crc != hm_crc(buf, len - 2))
    return false;

  frame->dst = buf[3];
  frame->cnt = buf[4];
  frame->cmd = &buf[5];

  return true;
}

static size_t encodeFrame(unsigned char *buf, size_t len, struct hm_frame *frame)
{
  uint16_t crc;

  if (frame->cmdlen + 7 > len)
    return -EMSGSIZE;

  buf[0] = 0xfd;
  buf[1] = ((frame->cmdlen + 2) >> 8) & 0xff;
  buf[2] = (frame->cmdlen + 2) & 0xff;
  buf[3] = frame->dst;
  buf[4] = frame->cnt;
  memcpy(&(buf[5]), frame->cmd, frame->cmdlen);

  crc = hm_crc(buf, frame->cmdlen + 5);
  buf[frame->cmdlen + 5] = (crc >> 8) & 0xff;
  buf[frame->cmdlen + 6] = crc & 0xff;

  return frame->cmdlen + 7;
}

static size_t encodeFrameBuffer(unsigned char* src, unsigned char* dst, size_t len)
{
  size_t ret = 0;
  unsigned char cur;

  while (len--)
  {
    cur = *src++;
    if (cur == 0xfc || (cur == 0xfd && len == 1))
    {
      *dst++ = 0xfc;
      ret++;
      cur &= 0x7f;
    }
    *dst++ = cur;
    ret++;
  }

  return ret;
}

static size_t decodeFrameBuffer(unsigned char* src, unsigned char* dst, size_t len)
{
  size_t ret = 0;
  unsigned char cur;

  while (len--)
  {
    cur = *src++;
    if (cur == 0xfc)
    {
      cur = *src++ | 0x80;
      len--;
    }
    *dst++ = cur;

    ret++;
  }

  return ret;
}

