#pragma once
#include <cstdint>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <cstring>
#include <map>

#include "types.hpp"

namespace packet {
  const int MAX_PACKET_SIZE = 512;
  const int MIN_PACKET_SIZE = 8;
  const uint8_t PREAMBLE[] = {0x01, 0x02, 0x03};
  const int PREAMBLE_SIZE = 3;
  const int CRC_VAL = 0xffff;

  struct Packet {
    sockaddr_in clientaddr;
    uint8_t type;
    uint16_t size;
    uint8_t data[MAX_PACKET_SIZE - MIN_PACKET_SIZE];
    uint16_t crc;
  };

  enum PacketType {
    CONNECT = 0,
    CONNECTED = 1,
    COULD_NOT_CONNECT = 2,
    DISCONNECT = 3,
    CREATE_SESSION = 4,
    ASSIGNED_TO_SESSION = 5,
    COULD_NOT_CREATE_SESSION = 6,
    INFORM_CLIENT_READY = 7,
    ASSIGN_TO_SESSION = 8,
    COULD_NOT_ASSIGN_TO_SESSION = 9,
    DISCONNECT_FROM_SESSION = 10,
    SESSION_DISCONNECT_STATUS = 11,
    SET_READY = 12,
    GAME_STARTED = 13,
    SET_BALL_POS = 14,
    INFORM_BALL_POS = 15,
    SET_PLAYER_POS = 16,
    INFORM_PLAYER_POS = 17,
    POINT_SCORED = 18,
    INFORM_POINT_SCORED = 19,
    INFORM_WON = 20,
    IM_ALIVE = 21,
    DISCONNECTED = 22
  };

  static std::map<uint8_t, uint16_t> packet_data_size {
    { CONNECT, 0 },
    { CONNECTED, 2 },
    { COULD_NOT_CONNECT, 2 },
    { DISCONNECT, 2 },
    { CREATE_SESSION, 2 },
    { ASSIGNED_TO_SESSION, 5 },
    { COULD_NOT_CREATE_SESSION, 0 },
    { INFORM_CLIENT_READY, 5 },
    { ASSIGN_TO_SESSION, 4 },
    { COULD_NOT_ASSIGN_TO_SESSION, 2 },
    { DISCONNECT_FROM_SESSION, 4 },
    { SESSION_DISCONNECT_STATUS, 5 },
    { SET_READY, 5 },
    { GAME_STARTED, 2 },
    { SET_BALL_POS, 26 },
    { INFORM_BALL_POS, 24 },
    { SET_PLAYER_POS, 26 },
    { INFORM_PLAYER_POS, 26 },
    { POINT_SCORED, 4 },
    { INFORM_POINT_SCORED, 12 },
    { INFORM_WON, 4 },
    { IM_ALIVE, 0 },
    { DISCONNECTED, 0 }
  };

  enum ClientType {
    MAIN = 0,
    SECONDARY = 1
  };

  enum SessionDisconnectStatus {
    SUCCESS = 1,
    FAILURE = 0
  };

  enum Readiness {
    READY = 1,
    NOT_READY = 0
  };

  struct SendData {
    uint8_t data[MAX_PACKET_SIZE];
    uint16_t size;
  };

  uint16_t crc16_mcrf4xx(uint16_t crc, uint8_t *data, size_t len);
  uint16_t crc16(uint8_t *data, size_t len);
  
  void make_packet(SendData *packet, PacketType type, uint8_t *data, uint16_t size);
  void make_connected_packet(SendData *packet, uint16_t client_id);
  void make_could_not_connect_packet(SendData *packet);
  void make_disconnected_packet(SendData *packet);
  void make_assigned_to_session_packet(SendData *packet, uint16_t session_id, uint16_t client_id, ClientType type);
  void make_could_not_create_session_packet(SendData *packet);
  void make_session_disconnect_status_packet(SendData *packet, uint16_t session_id, uint16_t client_id, SessionDisconnectStatus status);
  void make_could_not_assign_to_session_packet(SendData *packet, uint16_t session_id);
  void make_inform_client_ready_packet(SendData *packet, uint16_t session_id, uint16_t client_id, Readiness readiness);
  void make_game_started_packet(SendData *packet, uint16_t session_id);
  void make_inform_ball_pos_packet(SendData *packet, types::Vector2 ball_pos, types::Vector2 ball_dir);
  void make_inform_player_pos_packet(SendData *packet, uint16_t client_id, types::Vector2 player_pos, types::Vector2 player_dir);
  void make_inform_point_scored_packet(SendData *packet, uint16_t session_id, uint32_t main_score, uint32_t secondary_score, uint16_t client_id);

  bool verify_packet(Packet &packet);
  
  uint16_t get_id_from_packet(Packet &packet, uint16_t offset);
}