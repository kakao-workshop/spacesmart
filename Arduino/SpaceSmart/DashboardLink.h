#pragma once
#include <stddef.h>
#include <stdint.h>
namespace DashboardLink {
constexpr const char* SERVICE_UUID = "b72c0001-6e35-4b4f-8ad8-08d90a5c9170";
constexpr const char* RX_UUID = "b72c0002-6e35-4b4f-8ad8-08d90a5c9170";
constexpr const char* TX_UUID = "b72c0003-6e35-4b4f-8ad8-08d90a5c9170";
bool canSend();
bool send(const char* line);
void tick(uint32_t now);
bool begin();
bool connected();
bool receive(char* destination, size_t capacity);
}  // namespace DashboardLink
