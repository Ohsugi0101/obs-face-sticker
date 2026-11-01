#include "tracking.hpp"
#include <cstdlib>
#include <iostream>
using namespace fpf;
void check(bool b,const char *s){if(!b){std::cerr<<s<<'\n';std::exit(1);}}
int main(){
 Face f;f.x=.3f;f.y=.2f;f.w=.4f;f.h=.6f;f.score=.99f;
 f.points={Point{.4f,.4f},Point{.6f,.4f},Point{.52f,.5f},Point{.43f,.6f},Point{.57f,.6f}};
 Pose p=pose(f,Nose,1000,1000,false,0,0);
 check(std::abs(p.position.x-520)<.01f && std::abs(p.size-200)<.01f,"nose and scale");
 auto le=pose(f,LeftEye,1000,1000,false,0,0);check(std::abs(le.position.x-600)<.01,"subject left eye");
 auto mirrored=pose(f,Nose,1000,1000,true,0,0);check(std::abs(mirrored.position.x-480)<.01,"mirror coordinate");
 check(std::abs(mirrored.angle)<.001,"mirror must not rotate image 180 degrees");
 auto custom=pose(f,Custom,1000,1000,false,1,-1);check(std::abs(custom.position.x-720)<.01&&std::abs(custom.position.y-300)<.01,"face-local offset");
 f.points[1].y=.5f;p=pose(f,Nose,1000,1000,false,0,0);check(p.angle>.4&&p.angle<.5,"roll detection");
 auto mirroredTilt=pose(f,Nose,1000,1000,true,0,0);check(std::abs(p.angle+mirroredTilt.angle)<.001,"mirrored roll");
 Smoother smoother;Pose a{{0,0},100,pi-.02f},b{{100,100},200,-pi+.02f};smoother.update(a,.016f,.5f);
 auto c=smoother.update(b,.016f,.5f);check(std::abs(c.angle)>3,"shortest angle path across pi boundary");check(c.position.x>0&&c.position.x<100,"smoothing position");
 smoother.reset();c=smoother.update(b,.016f,.5f);check(c.position.x==100,"reset");
 Selector selector;Face other=f;other.x=.85;other.w=.1;other.h=.2;
 check(selector.choose({f},0)==0,"initial lock");check(selector.choose({other},.1)==-1,"reject transient stranger");
 check(selector.choose({other,f},.2)==1,"retain original among two faces");check(selector.choose({other},2)==0,"reacquire after timeout");
 check(selector.choose({},3)==-1,"empty detections");
 std::cout<<"All tracking tests passed\n";
}
