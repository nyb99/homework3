/**
  ******************************************************************************
  * @file    protocol.h
  * @brief   Running-light command packet definition, checksum and byte-wise
  *          frame parser.
  *
  *          Frame layout (6 bytes, little endian interval):
  *
  *              +------+------+------+--------+--------+-------+
  *              | 0xAA | RUN  | DIR  | STEP_L | STEP_H | CHECK |
  *              +------+------+------+--------+--------+-------+
  *
  *            RUN   : 0x00 = pause,  0x01 = run
  *            DIR   : 0x00 = forward,0x01 = reverse
  *            STEP  : 100..2000 ms, low byte first
  *            CHECK : (0xAA + RUN + DIR + STEP_L + STEP_H) % 256
  ******************************************************************************
  */
#ifndef __PROTOCOL_H
#define __PROTOCOL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define PKT_HEADER        0xAAu
#define PKT_LENGTH        6u
#define PKT_INDEX_HEADER  0u
#define PKT_INDEX_RUN     1u
#define PKT_INDEX_DIR     2u
#define PKT_INDEX_STEP_L  3u
#define PKT_INDEX_STEP_H  4u
#define PKT_INDEX_CHECK   5u

#define PKT_STEP_MIN_MS   100u
#define PKT_STEP_MAX_MS   2000u

/* Packet fields, accessed either by name or by byte offset (-> raw[]). */
typedef struct
{
  uint8_t header;   /*!< fixed frame header, PKT_HEADER                     */
  uint8_t run;      /*!< 0 = pause, 1 = run                                 */
  uint8_t dir;      /*!< 0 = forward, 1 = reverse                           */
  uint8_t step_l;   /*!< interval, low byte                                 */
  uint8_t step_h;   /*!< interval, high byte                                */
  uint8_t check;    /*!< checksum of the first 5 bytes                      */
} PacketFields_t;

typedef union
{
  PacketFields_t field;
  uint8_t        raw[PKT_LENGTH];
} Packet_t;

/* Result of a validation / of one parser step. */
typedef enum
{
  PKT_NONE = 0,        /*!< no complete frame yet                           */
  PKT_OK,              /*!< complete and valid frame                        */
  PKT_ERR_HEADER,      /*!< first byte is not 0xAA                          */
  PKT_ERR_CHECKSUM,    /*!< CHECK does not match the sum of the 5 first bytes*/
  PKT_ERR_RANGE        /*!< RUN/DIR not 0/1 or interval out of 100..2000 ms */
} PacketResult_t;

/* Byte-wise frame assembler, one instance is enough. */
typedef struct
{
  Packet_t packet;
  uint8_t  index;   /*!< number of bytes already stored in packet        */
} PacketParser_t;

void           PacketParser_Init(PacketParser_t *parser);
PacketResult_t PacketParser_Feed(PacketParser_t *parser, uint8_t byte);
uint8_t        Packet_Checksum(const Packet_t *packet);
PacketResult_t Packet_Check(const Packet_t *packet);
uint16_t       Packet_StepMs(const Packet_t *packet);
const char    *Packet_ResultText(PacketResult_t result);

#ifdef __cplusplus
}
#endif

#endif /* __PROTOCOL_H */
