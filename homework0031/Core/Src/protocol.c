/**
  ******************************************************************************
  * @file    protocol.c
  * @brief   Running-light command packet definition, checksum and byte-wise
  *          frame parser.
  ******************************************************************************
  */
#include "protocol.h"

#include <string.h>

/**
  * @brief  Reset a parser so that it waits for the next frame header.
  */
void PacketParser_Init(PacketParser_t *parser)
{
  if (parser == 0)
  {
    return;
  }
  memset(parser, 0, sizeof(*parser));
}

/**
  * @brief  Checksum of the 5 first bytes (header included), low 8 bits.
  */
uint8_t Packet_Checksum(const Packet_t *packet)
{
  uint32_t sum = 0u;

  for (uint8_t i = 0u; i < PKT_INDEX_CHECK; i++)
  {
    sum += packet->raw[i];
  }
  return (uint8_t)(sum & 0xFFu);
}

/**
  * @brief  Interval in milliseconds stored in the packet.
  */
uint16_t Packet_StepMs(const Packet_t *packet)
{
  return (uint16_t)((uint16_t)packet->field.step_l | ((uint16_t)packet->field.step_h << 8));
}

/**
  * @brief  Validate a complete packet.
  * @retval PKT_OK when the frame header, the checksum and every field are valid
  */
PacketResult_t Packet_Check(const Packet_t *packet)
{
  uint16_t step;

  if (packet->field.header != PKT_HEADER)
  {
    return PKT_ERR_HEADER;
  }
  if (packet->field.check != Packet_Checksum(packet))
  {
    return PKT_ERR_CHECKSUM;
  }
  if ((packet->field.run > 1u) || (packet->field.dir > 1u))
  {
    return PKT_ERR_RANGE;
  }
  step = Packet_StepMs(packet);
  if ((step < PKT_STEP_MIN_MS) || (step > PKT_STEP_MAX_MS))
  {
    return PKT_ERR_RANGE;
  }
  return PKT_OK;
}

/**
  * @brief  English text of a parser result, used for the serial feedback.
  */
const char *Packet_ResultText(PacketResult_t result)
{
  switch (result)
  {
    case PKT_OK:           return "OK";
    case PKT_ERR_HEADER:   return "ERR=HEADER";
    case PKT_ERR_CHECKSUM: return "ERR=CHECKSUM";
    case PKT_ERR_RANGE:    return "ERR=RANGE";
    case PKT_NONE:
    default:               return "NONE";
  }
}

/**
  * @brief  Drop the current (invalid) frame but keep the bytes that may
  *         already belong to the next one, so that a 0xAA inside the payload
  *         cannot desynchronise the parser.
  */
static void PacketParser_Resync(PacketParser_t *parser)
{
  for (uint8_t i = 1u; i < PKT_LENGTH; i++)
  {
    if (parser->packet.raw[i] == PKT_HEADER)
    {
      uint8_t remaining = (uint8_t)(PKT_LENGTH - i);
      memmove(parser->packet.raw, &parser->packet.raw[i], remaining);
      parser->index = remaining;
      return;
    }
  }
  parser->index = 0u;
}

/**
  * @brief  Feed one received byte to the parser.
  * @retval PKT_NONE while the frame is incomplete, PKT_OK / PKT_ERR_* when a
  *         complete frame has just been consumed
  */
PacketResult_t PacketParser_Feed(PacketParser_t *parser, uint8_t byte)
{
  PacketResult_t result;

  /* wait for the frame header */
  if ((parser->index == 0u) && (byte != PKT_HEADER))
  {
    return PKT_NONE;
  }

  parser->packet.raw[parser->index] = byte;
  parser->index++;
  if (parser->index < PKT_LENGTH)
  {
    return PKT_NONE;
  }

  result = Packet_Check(&parser->packet);
  if (result == PKT_OK)
  {
    parser->index = 0u;
  }
  else
  {
    PacketParser_Resync(parser);
  }
  return result;
}
