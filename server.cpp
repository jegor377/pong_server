#include <iostream>
#include <cstdlib>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#include "types.hpp"

const int PORT = 8080;
const int BUFF_SIZE = 1024;

void set_server_sock(struct sockaddr_in &servaddr);

void print_bytes(uint8_t *bytes, int n) {
  for(int i = 0; i < n; i++) {
      std::cout << +bytes[i] << " ";
    }
    std::cout << "\n";
}

int main() {
  int sockfd;

  sockaddr_in servaddr, clientaddr;

  if((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
    perror("socket creation failed");
    return 1;
  }

  set_server_sock(servaddr);

  if(bind(sockfd, (const struct sockaddr *)&servaddr, sizeof(servaddr)) < 0) {
    perror("bind failed");
    return 1;
  }

  socklen_t len = sizeof(clientaddr);
  int n;
  uint8_t buffer[BUFF_SIZE], vec_bytes[12];
  types::Vector2 vec = {1, 3}, vec_res;
  char32_t text[4096] = U"biały biały niedźwiedź";
  while(true) {
    n = recvfrom(sockfd, buffer, BUFF_SIZE, MSG_WAITALL, (struct sockaddr *) &clientaddr, &len);
    // vec_res = types::decode_vec2(buffer);
    // std::cout << vec_res.x << " " << vec_res.y << "\n";
    types::encode_vec2(vec, vec_bytes);
    sendto(sockfd, vec_bytes, 12, MSG_CONFIRM, (const struct sockaddr*)&clientaddr, sizeof(clientaddr));
  }

  return 0;
}

void set_server_sock(struct sockaddr_in &servaddr) {
  servaddr.sin_family = AF_INET;
  servaddr.sin_addr.s_addr = INADDR_ANY;
  servaddr.sin_port = htons(PORT);
}