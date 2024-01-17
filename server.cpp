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

const int POINTS_TO_WIN = 2;

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
  bool ready;
  uint32_t score;
  types::Vector2 pos;
  types::Vector2 dir;
  bool scheduled_to_disconnect;
};

struct Session {
  uint16_t id;
  bool available;
  Client *main;
  Client *secondary;
  bool game_active;
  types::Vector2 ball_pos;
  types::Vector2 ball_dir;
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
int find_available_client_id(bool include_scheduled_to_disconnect);
int find_available_session_id();
void use_client(uint16_t id, sockaddr_in addr);
void use_session(uint16_t id, uint16_t main_id);
void disconnect_stale_clients();
void disconnect_client(uint16_t id, bool inform);
void destroy_session(uint16_t id);
void connect_client(sockaddr_in addr);
void create_session(uint16_t main_id);
void disconnect_from_session(uint16_t session_id, uint16_t client_id);
void assign_to_session(uint16_t session_id, uint16_t client_id);
void set_client_ready(uint16_t client_id, uint16_t session_id, packet::Readiness readiness);
void set_ball_pos(uint16_t session_id, types::Vector2 &ball_pos, types::Vector2 &ball_dir);
void set_player_pos(uint16_t client_id, types::Vector2 &player_pos, types::Vector2 &player_dir);
void handle_client_alive(sockaddr_in addr, uint16_t client_id);
void score_point(uint16_t session_id, uint16_t client_id);
void set_client_msg_time(uint16_t client_id);

// send packet functions
void send_connected_packet(sockaddr_in *addr, uint16_t client_id);
void send_could_not_connect_packet(sockaddr_in *addr);
void send_disconnected_packet(sockaddr_in *addr);
void send_assigned_to_session_packet(sockaddr_in *addr, uint16_t session_id, uint16_t client_id, packet::ClientType type);
void send_could_not_create_session(sockaddr_in *addr);
void send_session_disconnect_status_packet(sockaddr_in *addr, uint16_t session_id, uint16_t client_id, packet::SessionDisconnectStatus status);
void send_could_not_assign_to_session_packet(sockaddr_in *addr, uint16_t session_id);
void send_inform_client_ready_packet(sockaddr_in *addr, uint16_t session_id, uint16_t client_id, packet::Readiness readiness);
void send_game_started_packet(sockaddr_in *addr, uint16_t session_id);
void send_ball_pos_packet(sockaddr_in *addr, Session *session);
void send_point_scored_packet(sockaddr_in *addr, Session *session, uint16_t client_id);
void send_player_pos_packet(sockaddr_in *addr, Client *client);
void send_player_won_packet(Session *session, Client *client);

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

    // std::ostringstream oss;
    // oss << "DATA ";
    // for(int i = 0; i < n; i++) {
    //   oss << std::hex << +(uint8_t)buffer[i] << " ";
    // }
    // log_message(oss.str());

#ifdef CALC_PROCESSED
    int packets_processed = 0;
#endif  
  
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
        case READ_SIZE: { // do poprawienia pobieranie size
          bytes_available = std::min(bytes_available, sizeof(uint16_t));
          memcpy(&packet.size, &buffer[i], bytes_available);
          memcpy(&bytes[byte_pos], &buffer[i], bytes_available);
          current_step = READ_DATA;
          byte_pos += bytes_available;
          i += bytes_available;
        } break;
        case READ_DATA: { // do poprawienia pobieranie data
          bytes_available = std::min(bytes_available, (unsigned long)packet.size);
          memcpy(packet.data, &buffer[i], bytes_available);
          memcpy(&bytes[byte_pos], &buffer[i], bytes_available);
          current_step = READ_CRC;
          byte_pos += bytes_available;
          i += bytes_available;
        } break;
        case READ_CRC: { // do poprawienia pobieranie crc
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
#ifdef CALC_PROCESSED
          packets_processed++;
#endif
        } break;
      }
    }
#ifdef CALC_PROCESSED
    log_message("Processed " + std::to_string(packets_processed) + " packets.");
#endif
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
          uint16_t client_id = packet::get_id_from_packet(packet, 0);
          disconnect_client(client_id, false);
        } break;
        case packet::PacketType::CREATE_SESSION: {
          uint16_t main_id = packet::get_id_from_packet(packet, 0);
          create_session(main_id);
        } break;
        case packet::PacketType::ASSIGN_TO_SESSION: {
          uint16_t client_id = packet::get_id_from_packet(packet, 0);
          uint16_t session_id = packet::get_id_from_packet(packet, 2);
          assign_to_session(session_id, client_id);
        } break;
        case packet::PacketType::DISCONNECT_FROM_SESSION: {
          uint16_t session_id = packet::get_id_from_packet(packet, 0);
          uint16_t client_id = packet::get_id_from_packet(packet, 2);
          disconnect_from_session(session_id, client_id);
        } break;
        case packet::PacketType::SET_READY: {
          uint16_t client_id = packet::get_id_from_packet(packet, 0);
          uint16_t session_id = packet::get_id_from_packet(packet, 2);
          packet::Readiness readiness = static_cast<packet::Readiness>(packet.data[4]);
          set_client_ready(client_id, session_id, readiness);
        } break;
        case packet::PacketType::SET_BALL_POS: {
          uint16_t session_id = packet::get_id_from_packet(packet, 0);
          types::Vector2 ball_pos, ball_dir;
          ball_pos = types::decode_vec2(&packet.data[2]);
          ball_dir = types::decode_vec2(&packet.data[2+12]);
          set_ball_pos(session_id, ball_pos, ball_dir);
        } break;
        case packet::PacketType::SET_PLAYER_POS: {
          uint16_t client_id = packet::get_id_from_packet(packet, 0);

          types::Vector2 player_pos, player_dir;
          player_pos = types::decode_vec2(&packet.data[2]);
          player_dir = types::decode_vec2(&packet.data[2+12]);
          set_player_pos(client_id, player_pos, player_dir);
        } break;
        case packet::PacketType::POINT_SCORED: {
          uint16_t session_id = packet::get_id_from_packet(packet, 0);
          uint16_t client_id = packet::get_id_from_packet(packet, 2);
          score_point(session_id, client_id);
        } break;
        case packet::PacketType::IM_ALIVE: {
          uint16_t client_id = packet::get_id_from_packet(packet, 0);
          handle_client_alive(packet.clientaddr, client_id);
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

int find_available_client_id(bool include_scheduled_to_disconnect) {
  for(int id = 0; id < CLIENT_COUNT; id++) {
    if(clients[id].available || (include_scheduled_to_disconnect && clients[id].scheduled_to_disconnect)) return id;
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
  Client *client = &clients[id];
  client->available = false;
  client->last_msg_timestamp = std::chrono::system_clock::now();
  client->addr = addr;
  client->scheduled_to_disconnect = false;
}

void use_session(uint16_t id, uint16_t main_id) {
  sessions[id].available = false;
  sessions[id].main = &clients[main_id];
}

void disconnect_stale_clients() {
  auto end = std::chrono::system_clock::now();
  for(int id = 0; id < CLIENT_COUNT; id++) {
    if(!clients[id].available) {
      std::chrono::duration<double> elapsed_seconds = end - clients[id].last_msg_timestamp;
      if(elapsed_seconds.count() > MAX_STALE_TIME_S) {
        clients[id].scheduled_to_disconnect = true;
      }
    }
  }
}

void disconnect_client(uint16_t id, bool inform) {
  Client *client = &clients[id];
  if(clients[id].available == true) {
    log_message("Tried to disconnect already disconnected client (id = " + std::to_string(id) + ")");
    return;
  }
  packet::SendData packet;
  packet::make_disconnected_packet(&packet);
  sockaddr_in client_addr = client->addr;
  if(client->session != nullptr) {
    disconnect_from_session(client->session->id, id);
  }
  client->available = true;
  if(inform) send_packet(&client_addr, packet);
  log_message("Disconnected client (id = " + std::to_string(id) + ")");
}

void destroy_session(uint16_t id) {
  Session *session = &sessions[id];
  session->available = true;
  session->main = nullptr;
  session->secondary = nullptr;
  session->game_active = false;
  log_message("Destroyed session (session_id = " + std::to_string(id) + ")");
}

void connect_client(sockaddr_in addr) {
  int available_id = find_available_client_id(true);
  packet::SendData response;
  if(available_id != -1) {
    if(clients[available_id].scheduled_to_disconnect) {
        log_message("Disconnected stale client when new tried to connect on id = " + std::to_string(available_id));
        disconnect_client(available_id, true);
    }
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

void create_session(uint16_t main_id) {
  set_client_msg_time(main_id);

  int available_id = find_available_session_id();
  Client *client = &clients[main_id];
  packet::SendData packet;
  if(available_id != -1 && !client->available) {
    if(client->session != nullptr) {
      log_message("RESEND: Created session (session_id = "+std::to_string(available_id)+") and assigned client (client_id = "+std::to_string(main_id)+") as main.");
      packet::make_assigned_to_session_packet(&packet, available_id, main_id, packet::ClientType::MAIN);
    } else {
      use_session(available_id, main_id);
      client->session = &sessions[available_id];
      log_message("Created session (session_id = "+std::to_string(available_id)+") and assigned client (client_id = "+std::to_string(main_id)+") as main.");
      packet::make_assigned_to_session_packet(&packet, available_id, main_id, packet::ClientType::MAIN);
    }
  } else {
    log_message("Failed at creating session.");
    packet::make_could_not_create_session_packet(&packet);
  }
  send_packet(&clients[main_id].addr, packet);
}

void disconnect_from_session(uint16_t session_id, uint16_t client_id) {
  Session *session = &sessions[session_id];
  Client *client = &clients[client_id];
  Client *main = session->main;
  Client *secondary = session->secondary;

  set_client_msg_time(client_id);

  packet::SendData packet;

  if(!session->available) {  
    bool has_main = main != nullptr;
    bool has_secondary = secondary != nullptr;

    if(main == client) {
      main->ready = false;
      log_message("Disconnected client (client_id = "+std::to_string(client_id)+") from session (session_id = "+std::to_string(session_id)+")");
      packet::make_session_disconnect_status_packet(&packet, session_id, client_id, packet::SessionDisconnectStatus::SUCCESS);
      main->session = nullptr;
      session->main = nullptr;
      send_packet(&client->addr, packet);
      if(has_secondary) {
        secondary->ready = false;
        log_message("Client (client_id = "+std::to_string(secondary->id)+") became MAIN in session (session_id = "+std::to_string(session_id)+")");
        send_packet(&secondary->addr, packet);
        session->main = secondary;
        session->secondary = nullptr;
      } else {
        destroy_session(session_id);
      }
    } else if(secondary == client) {
      secondary->ready = false;
      log_message("Disconnected client (client_id = "+std::to_string(client_id)+") from session (session_id = "+std::to_string(session_id)+")");
      packet::make_session_disconnect_status_packet(&packet, session_id, client_id, packet::SessionDisconnectStatus::SUCCESS);
      secondary->session = nullptr;
      session->secondary = nullptr;
      send_packet(&client->addr, packet);
      if(has_main) {
        main->ready = false;
        send_packet(&main->addr, packet);
      }
    } else { // if there are no players in session then it means that client did not receive last message about status
      log_message("RESEND: Disconnected client (client_id = "+std::to_string(client_id)+") from session (session_id = "+std::to_string(session_id)+")");
      packet::make_session_disconnect_status_packet(&packet, session_id, client_id, packet::SessionDisconnectStatus::SUCCESS);
      send_packet(&client->addr, packet);
    }
  } else { // if session is available that means that client did not receive last message about status
    log_message("RESEND: Disconnected client (client_id = "+std::to_string(client_id)+") from session (session_id = "+std::to_string(session_id)+")");
    packet::make_session_disconnect_status_packet(&packet, session_id, client_id, packet::SessionDisconnectStatus::SUCCESS);
    send_packet(&client->addr, packet);
  }
}

void assign_to_session(uint16_t session_id, uint16_t client_id) {
  Session *session = &sessions[session_id];
  Client *client = &clients[client_id];

  set_client_msg_time(client_id);

  packet::SendData packet;

  if(session->available) {
    log_message("Failed assigning client (client_id = "+std::to_string(client_id)+") to session (session_id = "+std::to_string(session_id)+"). Session is not used.");
    send_could_not_assign_to_session_packet(&client->addr, session_id);
    return;
  }

  bool has_main = session->main != nullptr;
  bool has_secondary = session->secondary != nullptr;

  if(has_main && has_secondary) { // both places are occupied
    if(session->main == client) {
      log_message("RESEND: Assigned client (client_id = "+std::to_string(client_id)+") to session (session_id = "+std::to_string(session_id)+") as main");
      send_assigned_to_session_packet(&client->addr, session_id, client_id, packet::ClientType::MAIN);
    } else if(session->secondary != client) {
      log_message("RESEND: Assigned client (client_id = "+std::to_string(client_id)+") to session (session_id = "+std::to_string(session_id)+") as secondary");
      send_assigned_to_session_packet(&client->addr, session_id, client_id, packet::ClientType::SECONDARY);
      send_assigned_to_session_packet(&client->addr, session_id, session->main->id, packet::ClientType::MAIN);
    } else {
      log_message("Failed assigning client (client_id = "+std::to_string(client_id)+") to session (session_id = "+std::to_string(session_id)+"). Session is full.");
      send_could_not_assign_to_session_packet(&client->addr, session_id);
    }
  } else { // assign secondary
    log_message("Assigned client (client_id = "+std::to_string(client_id)+") to session (session_id = "+std::to_string(session_id)+") as secondary");
    packet::make_assigned_to_session_packet(&packet, session_id, client_id, packet::ClientType::SECONDARY);
    session->secondary = client;
    client->session = session;
    send_packet(&session->main->addr, packet);
    send_packet(&session->secondary->addr, packet);
    send_assigned_to_session_packet(&client->addr, session_id, session->main->id, packet::ClientType::MAIN);
  } // does not need to assign main. Every session has main if it's available.
}

void set_client_ready(uint16_t client_id, uint16_t session_id, packet::Readiness readiness) {
  Client *client = &clients[client_id];
  Session *session = &sessions[session_id];

  set_client_msg_time(client_id);

  bool has_main = session->main != nullptr;
  bool has_secondary = session->secondary != nullptr;

  Client *main = session->main, *secondary = session->secondary;

  if(!client->available && client->session == session) {
    if(client->session->game_active) {
      log_message("Game session id = " + std::to_string(session->id) +  " already started");
      send_game_started_packet(&main->addr, session_id);
      send_game_started_packet(&secondary->addr, session_id);
      return;
    }

    client->ready = readiness == packet::Readiness::READY;

    if(main == client && has_secondary) {
      send_inform_client_ready_packet(&client->addr, session_id, client_id, readiness);
      send_inform_client_ready_packet(&secondary->addr, session_id, client_id, readiness);
    } else {
      send_inform_client_ready_packet(&client->addr, session_id, client_id, readiness);
      send_inform_client_ready_packet(&main->addr, session_id, client_id, readiness);
    }

    if(has_main && has_secondary && main->ready && secondary->ready) {
      session->game_active = true;
      log_message("Game session id = " + std::to_string(session->id) +  " just started");
      main->score = 0;
      secondary->score = 0;
      send_game_started_packet(&main->addr, session_id);
      send_game_started_packet(&secondary->addr, session_id);
    }
  }
}

void set_ball_pos(uint16_t session_id, types::Vector2 &ball_pos, types::Vector2 &ball_dir) {
  Session *session = &sessions[session_id];

  if(session->game_active) {
    session->ball_pos = ball_pos;
    session->ball_dir = ball_dir;

    set_client_msg_time(session->main->id);

    send_ball_pos_packet(&session->secondary->addr, session);
  }
}

void set_player_pos(uint16_t client_id, types::Vector2 &player_pos, types::Vector2 &player_dir) {
  Client *client = &clients[client_id];
  Session *session = client->session;

  set_client_msg_time(client_id);

  if(client->available) return;
  if(session == nullptr) return;

  client->pos = player_pos;
  client->dir = player_dir;

  if(client != session->main) {
    send_player_pos_packet(&session->main->addr, client);
  } else {
    send_player_pos_packet(&session->secondary->addr, client);
  }
}

void score_point(uint16_t session_id, uint16_t client_id) {
  Session *session = &sessions[session_id];
  Client *client = &clients[client_id];

  set_client_msg_time(client_id);

  if(session->available || !session->game_active) return;
  if(client->available || client->session != session) return;

  client->score++;

  if(session->main->score >= POINTS_TO_WIN) {
    session->game_active = false;
    send_player_won_packet(session, session->main);
  } else if(session->secondary->score >= POINTS_TO_WIN) {
    session->game_active = false;
    send_player_won_packet(session, session->secondary);
  } else {
    send_point_scored_packet(&session->secondary->addr, session, client->id);
  }
}

void handle_client_alive(sockaddr_in addr, uint16_t client_id) {
  Client *client = &clients[client_id];

  if(client->available) {
    log_message("Send info that client " + std::to_string(client_id) + " is not available.");
    send_disconnected_packet(&addr);
    return;
  }

  set_client_msg_time(client_id);
}

void set_client_msg_time(uint16_t client_id) {
  clients[client_id].last_msg_timestamp = std::chrono::system_clock::now();
}

// send packet functions
void send_connected_packet(sockaddr_in *addr, uint16_t client_id) {
  packet::SendData packet;
  packet::make_connected_packet(&packet, client_id);
  send_packet(addr, packet);
}

void send_could_not_connect_packet(sockaddr_in *addr) {
  packet::SendData packet;
  packet::make_could_not_connect_packet(&packet);
  send_packet(addr, packet);
}

void send_disconnected_packet(sockaddr_in *addr) {
  packet::SendData packet;
  packet::make_disconnected_packet(&packet);
  send_packet(addr, packet);
}

void send_assigned_to_session_packet(sockaddr_in *addr, uint16_t session_id, uint16_t client_id, packet::ClientType type) {
  packet::SendData packet;
  packet::make_assigned_to_session_packet(&packet, session_id, client_id, type);
  send_packet(addr, packet);
}

void send_could_not_create_session(sockaddr_in *addr) {
  packet::SendData packet;
  packet::make_could_not_create_session_packet(&packet);
  send_packet(addr, packet);
}

void send_session_disconnect_status_packet(sockaddr_in *addr, uint16_t session_id, uint16_t client_id, packet::SessionDisconnectStatus status) {
  packet::SendData packet;
  packet::make_session_disconnect_status_packet(&packet, session_id, client_id, status);
  send_packet(addr, packet);
}

void send_could_not_assign_to_session_packet(sockaddr_in *addr, uint16_t session_id) {
  packet::SendData packet;
  packet::make_could_not_assign_to_session_packet(&packet, session_id);
  send_packet(addr, packet);
}

void send_inform_client_ready_packet(sockaddr_in *addr, uint16_t session_id, uint16_t client_id, packet::Readiness readiness) {
  packet::SendData packet;
  packet::make_inform_client_ready_packet(&packet, session_id, client_id, readiness);
  send_packet(addr, packet);
}

void send_game_started_packet(sockaddr_in *addr, uint16_t session_id) {
  packet::SendData packet;
  packet::make_game_started_packet(&packet, session_id);
  send_packet(addr, packet);
}

void send_ball_pos_packet(sockaddr_in *addr, Session *session) {
  packet::SendData packet;
  packet::make_inform_ball_pos_packet(&packet, session->ball_pos, session->ball_dir);
  send_packet(addr, packet);
}

void send_player_pos_packet(sockaddr_in *addr, Client *client) {
  packet::SendData packet;
  packet::make_inform_player_pos_packet(&packet, client->id, client->pos, client->dir);
  send_packet(addr, packet);
}

void send_point_scored_packet(sockaddr_in *addr, Session *session, uint16_t client_id) {
  packet::SendData packet;
  packet::make_inform_point_scored_packet(&packet, session->id, session->main->score, session->secondary->score, client_id);
  send_packet(addr, packet);
}

void send_player_won_packet(Session *session, Client *client) {
  packet::SendData packet;
  packet::make_inform_player_won_packet(&packet, session->id, client->id);
  send_packet(&session->main->addr, packet);
  send_packet(&session->secondary->addr, packet);
}