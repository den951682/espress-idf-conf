#pragma once
#include "protocol.hpp"
#include <memory>

std::unique_ptr<Protocol> createProtocol(const char* passPhrase);
bool isFast();
