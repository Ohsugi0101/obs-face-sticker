#include "detector.hpp"
#include <fstream>
#include <iostream>
#include <chrono>
#include <thread>
#include <stdexcept>
using namespace fpf;
Result wait(Detector &d,double timestamp){
 Result r;for(int i=0;i<1000;++i){if(d.read(r)&&(r.status==2||r.time==timestamp))return r;std::this_thread::sleep_for(std::chrono::milliseconds(10));}
 throw std::runtime_error("detector timeout");
}
int main(int argc,char **argv){
 try{
  if(argc!=3)throw std::runtime_error("usage: detector-test model sample-320x320.rgba");
  Frame f;f.w=f.h=320;f.epoch=1;f.time=10;
  std::ifstream input(argv[2],std::ios::binary);f.rgba.assign(std::istreambuf_iterator<char>(input),{});
  if(f.rgba.size()!=320*320*4)throw std::runtime_error("fixture must be raw 320x320 RGBA");
  {
   Detector d(argv[1]);while(!d.submit(f))std::this_thread::yield();
   auto r=wait(d,10);
   if(r.status!=1||!r.found||r.face.score<.8f)throw std::runtime_error("face/landmark inference failed");
   for(auto p:r.face.points)if(p.x<=0||p.x>=1||p.y<=0||p.y>=1)throw std::runtime_error("landmark outside image");
   std::cout<<"YuNet detected 5 landmarks; confidence="<<r.face.score<<'\n';
   f.w=f.h=320;f.epoch=1;f.time=11;f.rgba.assign(320*320*4,0);
   while(!d.submit(f))std::this_thread::yield();r=wait(d,11);
   if(r.found||r.status!=1)throw std::runtime_error("blank input should not detect a face");
   // Shutdown while work is queued must join safely.
   for(int i=0;i<50;++i){f.w=f.h=320;f.epoch=2;f.time=12+i;f.rgba.resize(320*320*4);d.submit(f);}
  }
  {Detector bad("missing-model.onnx");auto r=wait(bad,-1);if(r.status!=2)throw std::runtime_error("missing-model failure handling");}
  std::cout<<"Inference, blank-frame, queue, shutdown and missing-model tests passed\n";
  return 0;
 }catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}
}
