#include <iostream>
#include <cstdlib>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <thread>
#include <chrono>
#include <mutex>
#include <cstdint>
#include <cstring>
#include <algorithm>

#include "types.hpp"

const int PORT = 8080;
const int MAX_PACKET_SIZE = 512;
const int MIN_PACKET_SIZE = 8;

const int MAIN_LOOP_DELAY_MS = 1000;
const int MAIN_LOOP_DELAY_US = MAIN_LOOP_DELAY_MS * 1000;

const int CLIENT_COUNT = 1024;
const int SESSION_COUNT = CLIENT_COUNT / 2;

const float MAX_STALE_TIME_S = 10.0;

const uint8_t PREAMBLE[] = {0x01, 0x02, 0x03};
const int PREAMBLE_SIZE = 3;

int sockfd;
sockaddr_in servaddr;
bool server_running = true;
std::mutex sock_mutex;

typedef std::chrono::time_point<std::chrono::system_clock> timestamp;

struct Session;

struct Client {
  Session *session;
  bool available;
  timestamp last_msg_timestamp;
};

struct Session {
  bool available;
  Client *main;
  Client *secondary;
};

Client clients[CLIENT_COUNT];
Session sessions[SESSION_COUNT];

struct Packet {
  uint8_t type;
  uint16_t size;
  uint8_t data[MAX_PACKET_SIZE - MIN_PACKET_SIZE];
  uint16_t crc;
  uint8_t bytes[MAX_PACKET_SIZE];
  uint16_t byte_pos;
};

enum PacketReadSteps {
  READ_PREAMBLE = 0,
  READ_TYPE = 1,
  READ_SIZE = 2,
  READ_DATA = 3,
  READ_CRC  = 4
};

void set_server_sock();
void listen_for_packets();
void init_clients();
void init_sessions();
int find_available_client_id();
int find_available_session_id();
void use_client(int id);
void use_session(int id);
void disconnect_stale_clients();
void disconnect_client(int id);

int main() {
  init_clients();
  init_sessions();

  if((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
    perror("socket creation failed");
    return 1;
  }

  set_server_sock();

  if(bind(sockfd, (const struct sockaddr *)&servaddr, sizeof(servaddr)) < 0) {
    perror("bind failed");
    return 1;
  }

  std::thread listen_thread(listen_for_packets);

  while(server_running) {
    disconnect_stale_clients();
    usleep(MAIN_LOOP_DELAY_US);
  }

  listen_thread.join();

  return 0;
}

void set_server_sock() {
  servaddr.sin_family = AF_INET;
  servaddr.sin_addr.s_addr = INADDR_ANY;
  servaddr.sin_port = htons(PORT);
}

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

void listen_for_packets() {
  sockaddr_in clientaddr;
  socklen_t len = sizeof(clientaddr);
  int n;
  uint8_t buffer[MAX_PACKET_SIZE];

  PacketReadSteps current_step = READ_PREAMBLE;
  Packet packet;
  memcpy(packet.bytes, PREAMBLE, PREAMBLE_SIZE);
  packet.byte_pos = PREAMBLE_SIZE;

  while(server_running) {
    n = recvfrom(sockfd, buffer, MAX_PACKET_SIZE, MSG_WAITALL, (struct sockaddr *) &clientaddr, &len);

    for(int i = 0; i < n; i++) {
      std::cout << std::hex << +(uint8_t)buffer[i] << " ";
    }
    std::cout << "\n";
    
    for(int i = 0; i < n;) {
      unsigned long bytes_available = n - i;

      switch(current_step) {
        case READ_PREAMBLE: {
          if(n >= PREAMBLE_SIZE && std::memcmp(buffer, PREAMBLE, PREAMBLE_SIZE) == 0) {
            current_step = READ_TYPE;
            i += PREAMBLE_SIZE;
          } else {
            i++;
          }
        } break;
        case READ_TYPE: {
          packet.type = buffer[i];
          packet.bytes[packet.byte_pos] = buffer[i];
          current_step = READ_SIZE;
          packet.byte_pos++;
          i++;
        } break;
        case READ_SIZE: {
          bytes_available = std::min(bytes_available, sizeof(uint16_t));
          memcpy(&packet.size, &buffer[i], bytes_available);
          memcpy(&packet.bytes[packet.byte_pos], &buffer[i], bytes_available);
          current_step = READ_DATA;
          packet.byte_pos += bytes_available;
          i += bytes_available;
        } break;
        case READ_DATA: {
          bytes_available = std::min(bytes_available, (unsigned long)packet.size);
          memcpy(packet.data, &buffer[i], bytes_available);
          memcpy(&packet.bytes[packet.byte_pos], &buffer[i], bytes_available);
          current_step = READ_CRC;
          packet.byte_pos += bytes_available;
          i += bytes_available;
        } break;
        case READ_CRC: {
          bytes_available = std::min(bytes_available, sizeof(uint16_t));
          memcpy(&packet.crc, &buffer[i], bytes_available);
          i += bytes_available;
          
          uint16_t calc_crc = crc16_mcrf4xx(0xffff, packet.bytes, packet.byte_pos);

          if(calc_crc == packet.crc) { // crc correct
            std::cout << "CORRECT PACKET\n";
            std::cout << "TYPE = " << +(uint8_t)packet.type << " | SIZE = " << +(uint16_t)packet.size << " | DATA = ";
            for(int j = 0; j < packet.size; j++) std::cout << +(uint8_t)packet.data[j] << " ";
            std::cout << " | CRC = " << +(uint16_t)packet.crc << "\n";
          } else { // crc incorrect
            std::cout << "INCORRECT PACKET\n";
          }

          packet.byte_pos = PREAMBLE_SIZE;
          current_step = READ_PREAMBLE;
        } break;
      }
    }

    //sendto(sockfd, vec_bytes, 12, MSG_CONFIRM, (const struct sockaddr*)&clientaddr, sizeof(clientaddr));
  }
}

void init_clients() {
  for(int i = 0; i < CLIENT_COUNT; i++) {
    clients[i].available = true;
  }
}

void init_sessions() {
  for(int i = 0; i < SESSION_COUNT; i++) {
    sessions[i].available = true;
  }
}

int find_available_client_id() {
  for(int id = 0; id < CLIENT_COUNT; id++) {
    if(clients[id].available) return id;
  }
  return -1;
}

int find_available_session_id() {
  for(int id = 0; id < SESSION_COUNT; id++) {
    if(sessions[id].available) return id;
  }
  return -1;
}

void use_client(int id) {
  clients[id].available = false;
  clients[id].last_msg_timestamp = std::chrono::system_clock::now();
}

void use_session(int id) {
  sessions[id].available = false;
}

void disconnect_stale_clients() {
  auto end = std::chrono::system_clock::now();
  for(int id = 0; id < CLIENT_COUNT; id++) {
    if(!clients[id].available) {
      std::chrono::duration<double> elapsed_seconds = end - clients[id].last_msg_timestamp;
      if(elapsed_seconds.count() > MAX_STALE_TIME_S) {
        disconnect_client(id);
        std::cout << "Disconnected client with id = " << id << "\n";
      }
    }
  }
}

void disconnect_client(int id) {
  clients[id].available = true;
  clients[id].session = nullptr;
}

void destroySession(int id) {
  sessions[id].available = true;
  sessions[id].main = nullptr;
  sessions[id].secondary = nullptr;
}