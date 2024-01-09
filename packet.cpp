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
    memcpy(&packet->data[6], data, size);
    uint16_t crc = crc16(packet->data, PREAMBLE_SIZE + 3 + size);
    memcpy(&packet->data[6 + size], &crc, sizeof(crc));
    packet->size = PREAMBLE_SIZE + sizeof(uint8_t) + sizeof(uint16_t) + size + sizeof(uint16_t);
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

  void make_disconnected_packet(SendData *packet) {
    make_packet(
      packet,
      PacketType::DISCONNECTED,
      nullptr,
      0
    );
  }

  void make_assigned_to_session_packet(SendData *packet, uint16_t session_id, uint16_t client_id, ClientType type) {
    uint8_t data[5];
    memcpy(data, reinterpret_cast<uint8_t*>(&session_id), sizeof(uint16_t));
    memcpy(&data[2], reinterpret_cast<uint8_t*>(&client_id), sizeof(uint16_t));
    data[4] = (uint8_t)type;
    make_packet(
      packet,
      PacketType::ASSIGNED_TO_SESSION,
      data,
      5
    );
  }

  void make_could_not_create_session_packet(SendData *packet) {
    make_packet(
      packet,
      PacketType::COULD_NOT_CREATE_SESSION,
      nullptr,
      0
    );
  }

  void make_session_disconnect_status_packet(SendData *packet, uint16_t session_id, uint16_t client_id, SessionDisconnectStatus status) {
    uint8_t data[5];
    memcpy(data, reinterpret_cast<uint8_t*>(&session_id), sizeof(uint16_t));
    memcpy(&data[2], reinterpret_cast<uint8_t*>(&client_id), sizeof(uint16_t));
    data[4] = (uint8_t)status;
    make_packet(
      packet,
      PacketType::SESSION_DISCONNECT_STATUS,
      data,
      5
    );
  }

  void make_could_not_assign_to_session_packet(SendData *packet, uint16_t session_id) {
    make_packet(
      packet,
      PacketType::COULD_NOT_ASSIGN_TO_SESSION,
      reinterpret_cast<uint8_t*>(&session_id),
      sizeof(uint16_t)
    );
  }

  void make_inform_client_ready_packet(SendData *packet, uint16_t session_id, uint16_t client_id, Readiness readiness) {
    uint8_t data[5];
    memcpy(data, reinterpret_cast<uint8_t*>(&session_id), sizeof(uint16_t));
    memcpy(&data[2], reinterpret_cast<uint8_t*>(&client_id), sizeof(uint16_t));
    data[4] = (uint8_t)readiness;
    make_packet(
      packet,
      PacketType::INFORM_CLIENT_READY,
      data,
      5
    );
  }

  void make_game_started_packet(SendData *packet, uint16_t session_id) {
    make_packet(
      packet,
      PacketType::GAME_STARTED,
      reinterpret_cast<uint8_t*>(&session_id),
      sizeof(uint16_t)
    );
  }

  void make_inform_ball_pos_packet(SendData *packet, types::Vector2 ball_pos, types::Vector2 ball_dir) {
    uint8_t data[12 * 2];
    types::encode_vec2(ball_pos, &data[0]);
    types::encode_vec2(ball_dir, &data[12]);
    make_packet(
      packet,
      PacketType::INFORM_BALL_POS,
      data,
      12 * 2
    );
  }

  void make_inform_player_pos_packet(SendData *packet, uint16_t client_id, types::Vector2 player_pos, types::Vector2 player_dir) {
    const int size = 2 + 12 * 2;
    uint8_t data[size];
    memcpy(data, reinterpret_cast<uint8_t*>(client_id), sizeof(uint16_t));
    types::encode_vec2(player_pos, &data[2]);
    types::encode_vec2(player_dir, &data[2 + 12]);
    make_packet(
      packet,
      PacketType::INFORM_PLAYER_POS,
      data,
      size
    );
  }

  void make_inform_point_scored_packet(SendData *packet, uint16_t session_id, uint32_t main_score, uint32_t secondary_score) {
    const int size = 2 + 4 + 4;
    uint8_t data[size];
    memcpy(data, reinterpret_cast<uint8_t*>(&session_id), sizeof(uint16_t));
    memcpy(&data[2], reinterpret_cast<uint8_t*>(&main_score), sizeof(uint32_t));
    memcpy(&data[6], reinterpret_cast<uint8_t*>(&secondary_score), sizeof(uint32_t));
    make_packet(
      packet,
      PacketType::INFORM_POINT_SCORED,
      data,
      size
    );
  }

  bool verify_packet(Packet &packet) {
    return packet.size == packet_data_size[packet.type];
  }

  uint16_t get_id_from_packet(Packet &packet, uint16_t offset) {
    return *reinterpret_cast<uint16_t*>(&packet.data[offset]);
  }
}