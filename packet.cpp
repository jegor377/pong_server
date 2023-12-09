#include "packet.hpp"

namespace packet {
  uint16_t crc16_mcrf4xx(uint16_t crc, uint8_t *data, size_t len)
  {
    if (!data || len < 0) return crc;

    while (len--) {
      crc ^= *data++;
      for (int i=0; i<8; i++) {
        if (crc & 1)  crc = (crc >> 1) ^ 0x8408;
        else          crc = (crc >> 1);
      }
    }
    return crc;
  }

  uint16_t crc16(uint8_t *data, size_t len) {
    return crc16_mcrf4xx(CRC_VAL, data, len);
  }

  void make_packet(SendData *packet, PacketType type, uint8_t *data, uint16_t size) {
    memcpy(packet->data, PREAMBLE, PREAMBLE_SIZE);
    packet->data[3] = type;
    memcpy(&packet->data[4], &size, sizeof(size));
    uint16_t crc = crc16(packet->data, PREAMBLE_SIZE + 1 + size);
    memcpy(&packet->data[4 + size], &crc, sizeof(crc));
  }

  void make_connected_packet(SendData *packet, uint16_t client_id) {
    make_packet(
      packet,
      PacketType::CONNECTED,
      (uint8_t*)&client_id,
      sizeof(uint16_t)
    );
  }

  void make_could_not_connect_packet(SendData *packet) {
    make_packet(
      packet,
      PacketType::COULD_NOT_CONNECT,
      nullptr,
      0
    );
  }

  void make_session_no_longer_exist_packet(SendData *packet, uint16_t session_id, uint8_t reason) {
    uint8_t data[3];
    memcpy(data, (uint8_t*)&session_id, sizeof(uint16_t));
    data[2] = reason;
    make_packet(
      packet,
      PacketType::SESSION_NO_LONGER_EXIST,
      data,
      3
    );
  }

  void make_disconnected_packet(SendData *packet) {
    make_packet(
      packet,
      PacketType::DISCONNECTED,
      nullptr,
      0
    );
  }

  bool verify_packet(Packet &packet) {
    return packet.size == packet_data_size[packet.type];
  }

  uint16_t get_client_id_from_packet(Packet &packet, uint16_t offset) {
    return *reinterpret_cast<uint16_t*>(&packet.data[offset]);
  }
}