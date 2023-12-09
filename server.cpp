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
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <semaphore.h>
#include <mutex>
#include <queue>
#include <iomanip>
#include <ctime>
#include <sstream>

#include "types.hpp"
#include "packet.hpp"

const int PORT = 8080;

const int MAIN_LOOP_DELAY_MS = 1000;
const int MAIN_LOOP_DELAY_US = MAIN_LOOP_DELAY_MS * 1000;

const int CLIENT_COUNT = 1024;
const int SESSION_COUNT = CLIENT_COUNT / 2;

const float MAX_STALE_TIME_S = 10.0;

const int MAX_PACKET_COUNT = 100'000;

int sockfd;
sockaddr_in servaddr;
bool server_running = true;
sem_t full_space;
sem_t free_space;
std::mutex packet_mutex;
std::mutex send_mutex;

std::mutex log_mutex;
std::queue<std::string> logs;
sem_t logs_available;

typedef std::chrono::time_point<std::chrono::system_clock> timestamp;
typedef std::lock_guard<std::mutex> lock_guard;

struct Session;

struct Client {
  uint16_t id;
  Session *session;
  bool available;
  sockaddr_in addr;
  timestamp last_msg_timestamp;
};

struct Session {
  uint16_t id;
  bool available;
  Client *main;
  Client *secondary;
};

Client clients[CLIENT_COUNT];
Session sessions[SESSION_COUNT];
std::mutex clients_sessions_mutex;

std::queue<packet::Packet> packets;

enum PacketReadSteps {
  READ_PREAMBLE = 0,
  READ_TYPE = 1,
  READ_SIZE = 2,
  READ_DATA = 3,
  READ_CRC  = 4
};

void set_server_sock();
void listen_for_packets();
void process_packets();
void process_logs();
void log_message(std::string message);
void send_packet(sockaddr_in *addr, packet::SendData *packet);
void init_clients();
void init_sessions();
int find_available_client_id();
int find_available_session_id();
void use_client(uint16_t id, sockaddr_in addr);
void use_session(uint16_t id);
void disconnect_stale_clients();
void disconnect_client(uint16_t id, bool inform);
void destroy_session(uint16_t id, uint8_t reason);
void connect_client(sockaddr_in addr);

int main() {
  sem_init(&free_space, 0, MAX_PACKET_COUNT);
  sem_init(&full_space, 0, 0);
  sem_init(&logs_available, 0, 0);

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
  std::thread process_thread(process_packets);
  std::thread logs_thread(process_logs);

  // this loop has to work rarely and iteration should be very quick
  while(server_running) {
    {
      lock_guard lock(clients_sessions_mutex);
      disconnect_stale_clients();
    }
    usleep(MAIN_LOOP_DELAY_US);
  }

  listen_thread.join();
  process_thread.join();
  logs_thread.join();

  sem_destroy(&free_space);
  sem_destroy(&full_space);
  sem_destroy(&logs_available);
  return 0;
}

void set_server_sock() {
  servaddr.sin_family = AF_INET;
  servaddr.sin_addr.s_addr = INADDR_ANY;
  servaddr.sin_port = htons(PORT);
}

void listen_for_packets() {
  sockaddr_in clientaddr;
  socklen_t len = sizeof(clientaddr);
  int n;
  uint8_t buffer[packet::MAX_PACKET_SIZE];

  PacketReadSteps current_step = READ_PREAMBLE;
  packet::Packet packet;
  
  uint8_t bytes[packet::MAX_PACKET_SIZE];
  uint16_t byte_pos;

  memcpy(bytes, packet::PREAMBLE, packet::PREAMBLE_SIZE);
  byte_pos = packet::PREAMBLE_SIZE;

  while(server_running) {
    n = recvfrom(sockfd, buffer, packet::MAX_PACKET_SIZE, MSG_WAITALL, (struct sockaddr *) &clientaddr, &len);

    std::ostringstream oss;
    oss << "DATA ";
    for(int i = 0; i < n; i++) {
      oss << std::hex << +(uint8_t)buffer[i] << " ";
    }
    log_message(oss.str());
    
    for(int i = 0; i < n;) {
      unsigned long bytes_available = n - i;

      switch(current_step) {
        case READ_PREAMBLE: {
          if(n >= packet::PREAMBLE_SIZE && std::memcmp(buffer, packet::PREAMBLE, packet::PREAMBLE_SIZE) == 0) {
            current_step = READ_TYPE;
            i += packet::PREAMBLE_SIZE;
            packet.clientaddr = clientaddr;
          } else {
            i++;
          }
        } break;
        case READ_TYPE: {
          packet.type = buffer[i];
          bytes[byte_pos] = buffer[i];
          current_step = READ_SIZE;
          byte_pos++;
          i++;
        } break;
        case READ_SIZE: {
          bytes_available = std::min(bytes_available, sizeof(uint16_t));
          memcpy(&packet.size, &buffer[i], bytes_available);
          memcpy(&bytes[byte_pos], &buffer[i], bytes_available);
          current_step = READ_DATA;
          byte_pos += bytes_available;
          i += bytes_available;
        } break;
        case READ_DATA: {
          bytes_available = std::min(bytes_available, (unsigned long)packet.size);
          memcpy(packet.data, &buffer[i], bytes_available);
          memcpy(&bytes[byte_pos], &buffer[i], bytes_available);
          current_step = READ_CRC;
          byte_pos += bytes_available;
          i += bytes_available;
        } break;
        case READ_CRC: {
          bytes_available = std::min(bytes_available, sizeof(uint16_t));
          memcpy(&packet.crc, &buffer[i], bytes_available);
          i += bytes_available;
          
          uint16_t calc_crc = packet::crc16(bytes, byte_pos);

          if(calc_crc == packet.crc) { // crc correct
            sem_wait(&free_space);
            {
              lock_guard lock(packet_mutex);
              packets.push(packet);
            }
            sem_post(&full_space);
          }
          // do nothing when packet crc is incorrect

          byte_pos = packet::PREAMBLE_SIZE;
          current_step = READ_PREAMBLE;
        } break;
      }
    }
  }
}

void process_packets() {
  while(server_running) {
    packet::Packet packet;
    sem_wait(&full_space);
    {
      lock_guard lock(packet_mutex);
      packet = packets.front();
      packets.pop();
    }
    sem_post(&free_space);

    if(packet::verify_packet(packet)) {
      lock_guard data_lock(clients_sessions_mutex);
      switch(packet.type) {
        case packet::PacketType::CONNECT: {
          connect_client(packet.clientaddr);
        } break;
        case packet::PacketType::DISCONNECT: {
          uint16_t client_id = packet::get_client_id_from_packet(packet, 0);
          disconnect_client(client_id, false);
        } break;
      }
    }
  }
}

void send_packet(sockaddr_in *addr, packet::SendData &packet) {
  lock_guard lock(send_mutex);
  sendto(sockfd, packet.data, packet.size, MSG_CONFIRM, (const struct sockaddr*)addr, sizeof(sockaddr_in));
}

void init_clients() {
  for(int i = 0; i < CLIENT_COUNT; i++) {
    clients[i].available = true;
    clients[i].id = i;
  }
}

void init_sessions() {
  for(int i = 0; i < SESSION_COUNT; i++) {
    sessions[i].available = true;
    sessions[i].id = i;
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

void use_client(uint16_t id, sockaddr_in addr) {
  clients[id].available = false;
  clients[id].last_msg_timestamp = std::chrono::system_clock::now();
  clients[id].addr = addr;
}

void use_session(uint16_t id) {
  sessions[id].available = false;
}

void disconnect_stale_clients() {
  auto end = std::chrono::system_clock::now();
  for(int id = 0; id < CLIENT_COUNT; id++) {
    if(!clients[id].available) {
      std::chrono::duration<double> elapsed_seconds = end - clients[id].last_msg_timestamp;
      if(elapsed_seconds.count() > MAX_STALE_TIME_S) {
        disconnect_client(id, true);
      }
    }
  }
}

void disconnect_client(uint16_t id, bool inform) {
  if(clients[id].available == true) {
    log_message("Tried to disconnect already disconnected client with id = " + std::to_string(id));
    return;
  }
  packet::SendData packet;
  packet::make_disconnected_packet(&packet);
  sockaddr_in client_addr = clients[id].addr;
  clients[id].available = true;
  if(clients[id].session != nullptr) {
    destroy_session(clients[id].session->id, packet::SessionDestroyedReason::PLAYER_LEFT);
  }
  clients[id].session = nullptr;
  if(inform) send_packet(&client_addr, packet);
  log_message("Disconnected client with id = " + std::to_string(id));
}

void destroy_session(uint16_t id, uint8_t reason) {
  packet::SendData packet;
  packet::make_session_no_longer_exist_packet(&packet, id, reason);
  sockaddr_in main_addr, secondary_addr;
  bool main_exist = false, secondary_exist = false;

  if(sessions[id].main != nullptr) {
    main_addr = sessions[id].main->addr;
    main_exist = true;
  }

  if(sessions[id].secondary != nullptr) {
    secondary_addr = sessions[id].secondary->addr;
    secondary_exist = true;
  }

  sessions[id].available = true;
  sessions[id].main = nullptr;
  sessions[id].secondary = nullptr;

  if(main_exist) send_packet(&main_addr, packet);
  if(secondary_exist) send_packet(&secondary_addr, packet);
}

void connect_client(sockaddr_in addr) {
  uint16_t available_id = find_available_client_id();
  packet::SendData response;
  if(available_id != -1) {
    use_client(available_id, addr);
    packet::make_connected_packet(&response, available_id);
    char *ip = inet_ntoa(addr.sin_addr);
    log_message("Client (" + std::string(ip) + ") connected: " + std::to_string(available_id));
  } else {
    packet::make_could_not_connect_packet(&response);
    log_message("Failed to connect the client");
  }
  send_packet(&addr, response);
}

void log_message(std::string message) {
  auto t = std::time(nullptr);
  auto tm = *std::localtime(&t);
  std::ostringstream oss;
  oss << std::put_time(&tm, "%d-%m-%Y %H-%M-%S");
  {
    lock_guard lock(log_mutex);
    logs.push(oss.str() + ": " + message);
  }
  sem_post(&logs_available);
}

void process_logs() {
  std::string log_message;

  while(server_running) {
    sem_wait(&logs_available);
    {
      lock_guard lock(log_mutex);
      log_message = logs.front();
      logs.pop();
    }

    std::cout << log_message << "\n";
  }
}