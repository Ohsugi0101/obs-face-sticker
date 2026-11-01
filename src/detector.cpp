// SPDX-License-Identifier: GPL-2.0-or-later
#include "detector.hpp"
#include <obs-module.h>
#include <opencv2/objdetect/face.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/dnn.hpp>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <filesystem>
namespace fpf {
Detector::Detector(std::string path):worker(),model(std::move(path)) {worker=std::thread(&Detector::run,this);}
Detector::~Detector(){ {std::lock_guard<std::mutex> lock(mutex);stopping=true;}wake.notify_one();if(worker.joinable())worker.join(); }
bool Detector::submit(Frame &frame){
 std::unique_lock<std::mutex> lock(mutex,std::try_to_lock);if(!lock.owns_lock()||stopping)return false;
 std::swap(input,frame);pending=true;wake.notify_one();return true;
}
bool Detector::read(Result &r){std::unique_lock<std::mutex> lock(mutex,std::try_to_lock);if(!lock.owns_lock())return false;r=output;return true;}
void Detector::run() noexcept {
 try {
  cv::setNumThreads(1); // Limit inference CPU use (our private static OpenCV copy).
  // Read UTF-8 paths via filesystem: OpenCV's narrow filename handling on Windows varies.
  std::ifstream file(std::filesystem::u8path(model),std::ios::binary);
  if(!file)throw std::runtime_error("YuNet model is missing");
  std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(file)),{});
  auto net=cv::FaceDetectorYN::create("onnx",bytes,{},cv::Size(320,320),.80f,.3f,500,
                                    cv::dnn::DNN_BACKEND_OPENCV,cv::dnn::DNN_TARGET_CPU);
  Frame frame;cv::Mat bgr,faces;std::vector<Face> candidates;candidates.reserve(32);
  Selector selector;uint64_t epoch=0;
  {std::lock_guard<std::mutex> lock(mutex);output.status=1;}
  while(true){
   {std::unique_lock<std::mutex> lock(mutex);wake.wait(lock,[&]{return stopping||pending;});if(stopping)break;std::swap(frame,input);pending=false;}
   if(epoch!=frame.epoch){selector.reset();epoch=frame.epoch;}
   cv::Mat rgba((int)frame.h,(int)frame.w,CV_8UC4,frame.rgba.data());
   cv::cvtColor(rgba,bgr,cv::COLOR_RGBA2BGR);
   net->setInputSize(bgr.size());net->detect(bgr,faces);candidates.clear();
   for(int i=0;i<faces.rows;++i){
    const float *v=faces.ptr<float>(i);Face f;f.x=v[0]/frame.w;f.y=v[1]/frame.h;f.w=v[2]/frame.w;f.h=v[3]/frame.h;f.score=v[14];
    bool finite=true;for(int k=0;k<15;++k)finite=finite&&std::isfinite(v[k]);
    if(!finite||f.w<=0||f.h<=0)continue;
    for(int j=0;j<5;++j)f.points[j]={v[4+2*j]/frame.w,v[5+2*j]/frame.h};
    candidates.push_back(f);
   }
   int index=selector.choose(candidates,frame.time);
   Result r;r.found=index>=0;r.time=frame.time;r.epoch=frame.epoch;r.status=1;
   if(r.found)r.face=candidates[index];
   {std::lock_guard<std::mutex> lock(mutex);output=r;}
  }
 } catch(const std::exception &e){
  blog(LOG_ERROR,"[face-png-filter] tracker stopped: %s",e.what());
  std::lock_guard<std::mutex> lock(mutex);output.status=2;output.found=false;
 } catch(...){
  blog(LOG_ERROR,"[face-png-filter] unknown tracker failure");
  std::lock_guard<std::mutex> lock(mutex);output.status=2;output.found=false;
 }
}
}
