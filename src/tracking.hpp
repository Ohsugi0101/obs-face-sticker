// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>
namespace fpf {
constexpr float pi = 3.14159265358979323846f;
struct Point { float x=0,y=0; };
inline Point operator+(Point a,Point b){return {a.x+b.x,a.y+b.y};}
inline Point operator-(Point a,Point b){return {a.x-b.x,a.y-b.y};}
inline Point operator*(Point a,float s){return {a.x*s,a.y*s};}
inline float length(Point a){return std::hypot(a.x,a.y);}
inline float wrap(float x){return std::remainder(x,2*pi);}
struct Face { std::array<Point,5> points{}; float x=0,y=0,w=0,h=0,score=0; };
// YuNet order: subject right eye, left eye, nose, right mouth, left mouth.
enum Anchor { LeftEye,RightEye,EyeCenter,Nose,Mouth,Forehead,LeftCheek,RightCheek,Chin,Custom };
struct Pose { Point position{}; float size=1, angle=0; };
inline Pose pose(const Face &f, int anchor,float width,float height,bool mirror,float ox,float oy){
 auto pixel=[&](Point a){return Point{(mirror?1-a.x:a.x)*width,a.y*height};};
 auto re=pixel(f.points[0]),le=pixel(f.points[1]);
 // Image-space rightward axis. Subject-side labels remain subject-side after reflection.
 Point axis=mirror?re-le:le-re;
 float eye=std::max(1.f,length(axis)); axis=axis*(1/eye);
 Point down{-axis.y,axis.x}, eyes=(re+le)*.5f, nose=pixel(f.points[2]);
 Point mouth=(pixel(f.points[3])+pixel(f.points[4]))*.5f;
 Point a=nose;
 switch(anchor){
 case LeftEye:a=le;break;case RightEye:a=re;break;case EyeCenter:a=eyes;break;
 case Mouth:a=mouth;break;case Forehead:a=eyes-down*(eye*.8f);break;
 case LeftCheek:a=nose+(le-eyes)*1.1f+down*(eye*.25f);break;
 case RightCheek:a=nose+(re-eyes)*1.1f+down*(eye*.25f);break;
 case Chin:a=mouth+down*(eye*.7f);break;
 default:break;
 }
 // Offsets are face-local units of inter-eye distance; Custom uses nose as origin.
 return {a+axis*(ox*eye)+down*(oy*eye),eye,std::atan2(axis.y,axis.x)};
}
class Smoother {
 bool ready=false; Pose value{};
public:
 void reset(){ready=false;}
 Pose update(Pose target,float dt,float strength){
  if(!ready){value=target;ready=true;return value;}
  float tau=.005f+std::clamp(strength,0.f,1.f)*.30f;
  float a=strength<=0?1.f:1-std::exp(-std::clamp(dt,0.f,.25f)/tau);
  value.position=value.position+(target.position-value.position)*a;
  value.size+=a*(target.size-value.size);
  value.angle=wrap(value.angle+a*wrap(target.angle-value.angle));
  return value;
 }
};
class Selector {
 bool locked=false; Face previous{}; double last=0;
public:
 void reset(){locked=false;}
 int choose(const std::vector<Face>& faces,double now){
  if(faces.empty())return -1;
  int best=-1;float cost=1e9;
  bool active=locked && now-last<1.2;
  for(size_t i=0;i<faces.size();++i){
   const auto &f=faces[i];
   float c=-(f.w*f.h)*f.score;
   if(active){
    float dx=(f.x+f.w*.5f)-(previous.x+previous.w*.5f);
    float dy=(f.y+f.h*.5f)-(previous.y+previous.h*.5f);
    float d=std::hypot(dx,dy)/std::max(.02f,std::hypot(previous.w,previous.h));
    float ratio=f.w/std::max(.001f,previous.w);
    if(d>.65f || ratio<.5f || ratio>2.f)continue;
    c=d+.25f*std::abs(std::log(ratio));
   }
   if(c<cost){cost=c;best=(int)i;}
  }
  if(best>=0){previous=faces[best];last=now;locked=true;}
  return best;
 }
};
}
