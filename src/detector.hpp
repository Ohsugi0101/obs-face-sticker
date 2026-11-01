// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "tracking.hpp"
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
namespace fpf {
struct Frame { std::vector<uint8_t> rgba; uint32_t w=0,h=0; uint64_t epoch=0; double time=0; };
struct Result { Face face{}; bool found=false; uint64_t epoch=0; double time=0; int status=0; }; // 0 loading,1 OK,2 model/inference error
class Detector {
 std::mutex mutex; std::condition_variable wake; bool stopping=false,pending=false;
 Frame input; Result output; std::thread worker; std::string model;
 void run() noexcept;
public:
 explicit Detector(std::string path);
 ~Detector();
 Detector(const Detector&)=delete; Detector& operator=(const Detector&)=delete;
 bool submit(Frame &frame); // latest-only exchange, never waits for inference
 bool read(Result &result);
};
}
