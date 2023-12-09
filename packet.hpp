#pragma once
#include <cstdint>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <cstring>
#include <map>

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
    MAKE_SESSION = 4,
    ASSIGNED_TO_SESSION = 5,
    COULD_NOT_CREATE_SESSION = 6,
    SESSION_NO_LONGER_EXIST = 7,
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
    { MAKE_SESSION, 2 },
    { ASSIGNED_TO_SESSION, 6 },
    { COULD_NOT_CREATE_SESSION, 0 },
    { SESSION_NO_LONGER_EXIST, 3 },
    { ASSIGN_TO_SESSION, 4 },
    { COULD_NOT_ASSIGN_TO_SESSION, 2 },
    { DISCONNECT_FROM_SESSION, 4 },
    { SESSION_DISCONNECT_STATUS, 5 },
    { SET_READY, 5 },
    { GAME_STARTED, 2 },
    { SET_BALL_POS, 28 },
    { INFORM_BALL_POS, 24 },
    { SET_PLAYER_POS, 28 },
    { INFORM_PLAYER_POS, 28 },
    { POINT_SCORED, 4 },
    { INFORM_POINT_SCORED, 10 },
    { INFORM_WON, 4 },
    { IM_ALIVE, 0 },
    { DISCONNECTED, 0 }
  };

  enum SessionDestroyedReason {
    UNKNOWN = 0,
    PLAYER_LEFT = 1
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
  void make_session_no_longer_exist_packet(SendData *packet, uint16_t session_id, uint8_t reason);

  bool verify_packet(Packet &packet);
  
  uint16_t get_client_id_from_packet(Packet &packet, uint16_t offset);
}