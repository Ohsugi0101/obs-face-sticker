// SPDX-License-Identifier: GPL-2.0-or-later
#include <obs-module.h>
#include <graphics/image-file.h>
#include <graphics/vec4.h>
#include <util/platform.h>
#include "detector.hpp"
#include <memory>
#include <mutex>
#include <string>
#include <cstring>
#include <new>
#include <atomic>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("face-png-filter", "ja-JP")
MODULE_EXPORT const char *obs_module_description(void) { return "Single-face landmark PNG overlay"; }
namespace {
using namespace fpf;
struct Settings {
 std::string png;
 bool visible=true,autoScale=true,autoRotate=true,mirror=false,hideLost=true;
 bool landmarks=false,marker=false,status=false;
 int anchor=Nose; float scale=1,opacity=1,pivotX=.5f,pivotY=.5f,x=0,y=0,rotation=0,smooth=.35f,hold=.35f;
};
struct Slot { gs_stagesurf_t *surface=nullptr; bool pending=false; double time=0; };
struct Filter {
 obs_source_t *source=nullptr;
 std::mutex settingsMutex; std::shared_ptr<const Settings> settings=std::make_shared<Settings>();
 std::atomic<uint64_t> imageRevision{0}; uint64_t loadedRevision=0;
 std::unique_ptr<Detector> detector;
 gs_texrender_t *full=nullptr,*small=nullptr;
 Slot slots[2]; int next=0; uint32_t sw=0,sh=0,width=0,height=0;
 uint64_t epoch=1; double nextSample=0,lastTick=0,lastSeen=-1,lastRender=0,lossStart=-1;
 bool rendered=false,haveFace=false; Face face{}; Smoother smoother; Pose smoothed{};
 Frame staging; Result result{};
 gs_image_file_t image{}; gs_effect_t *overlay=nullptr;
 std::string loadedPath; bool imageAttempted=false;
 explicit Filter(obs_source_t *s):source(s) {}
 ~Filter(){
  detector.reset(); // join before releasing anything the worker could access
  obs_enter_graphics();
  gs_image_file_free(&image);
  for(auto &slot:slots)gs_stagesurface_destroy(slot.surface);
  gs_texrender_destroy(full);gs_texrender_destroy(small);gs_effect_destroy(overlay);
  obs_leave_graphics();
 }
 void readSettings(obs_data_t *d){
  Settings n;n.png=obs_data_get_string(d,"png");
  n.visible=obs_data_get_bool(d,"visible");n.autoScale=obs_data_get_bool(d,"auto_scale");
  n.autoRotate=obs_data_get_bool(d,"auto_rotate");n.mirror=obs_data_get_bool(d,"mirror");n.hideLost=obs_data_get_bool(d,"hide_lost");
  n.landmarks=obs_data_get_bool(d,"landmarks");n.marker=obs_data_get_bool(d,"marker");n.status=obs_data_get_bool(d,"status");
  n.anchor=(int)std::clamp<int64_t>(obs_data_get_int(d,"anchor"),0,9);
  auto number=[&](const char *key,double low,double high){double v=obs_data_get_double(d,key);return (float)std::clamp(std::isfinite(v)?v:low,low,high);};
  n.scale=number("scale",.01,10);n.opacity=number("opacity",0,100)/100;
  n.pivotX=number("pivot_x",0,1);n.pivotY=number("pivot_y",0,1);
  n.x=number("offset_x",-3,3);n.y=number("offset_y",-3,3);
  n.rotation=number("rotation",-180,180)*pi/180;n.smooth=number("smooth",0,1);n.hold=number("hold",0,5);
  std::lock_guard<std::mutex> lock(settingsMutex);settings=std::make_shared<Settings>(std::move(n));
 }
 void resize(uint32_t w,uint32_t h){
  width=w;height=h;++epoch;rendered=false;haveFace=false;lastSeen=-1;smoother.reset();
  for(auto &s:slots){gs_stagesurface_destroy(s.surface);s={};}
  gs_texrender_destroy(small);small=nullptr;
  float factor=320.f/(float)std::max(w,h);
  sw=std::max(32u,((uint32_t)(w*factor)+31)/32*32);
  sh=std::max(32u,((uint32_t)(h*factor)+31)/32*32);
  small=gs_texrender_create(GS_RGBA,GS_ZS_NONE);
  for(auto &s:slots)s.surface=gs_stagesurface_create(sw,sh,GS_RGBA);
  staging.rgba.resize((size_t)sw*sh*4);nextSample=0;next=0;
 }
 void loadImage(const Settings &s){
  if(imageAttempted && loadedPath==s.png && loadedRevision==imageRevision.load())return;
  imageAttempted=true;loadedRevision=imageRevision.load();loadedPath=s.png;gs_image_file_free(&image);image={};
  if(!s.png.empty()){
   gs_image_file_init(&image,s.png.c_str());
   if(image.loaded)gs_image_file_init_texture(&image);
   if(!image.texture)blog(LOG_WARNING,"[face-png-filter] PNG could not be loaded: %s",s.png.c_str());
  }
 }
 void sprite(gs_texture_t *texture,uint32_t w,uint32_t h){
  auto *effect=obs_get_base_effect(OBS_EFFECT_DEFAULT);
  gs_effect_set_texture(gs_effect_get_param_by_name(effect,"image"),texture);
  while(gs_effect_loop(effect,"Draw"))gs_draw_sprite(texture,0,w,h);
 }
 bool capture(obs_source_t *target,obs_source_t *parent){
  if(!full)full=gs_texrender_create(GS_RGBA,GS_ZS_NONE);
  if(!full)return false;
  gs_texrender_reset(full);
  if(!gs_texrender_begin(full,width,height))return false;
  vec4 clear{};gs_clear(GS_CLEAR_COLOR,&clear,0,0);
  gs_ortho(0,(float)width,0,(float)height,-100,100);
  gs_blend_state_push();gs_blend_function(GS_BLEND_ONE,GS_BLEND_ZERO);
  uint32_t flags=obs_source_get_output_flags(target);
  if(target==parent && !(flags&OBS_SOURCE_CUSTOM_DRAW) && !(flags&OBS_SOURCE_ASYNC))obs_source_default_render(target);
  else obs_source_video_render(target);
  gs_blend_state_pop();gs_texrender_end(full);return true;
 }
 void sample(double now){
  if(now<nextSample || !detector || result.status==2)return;
  nextSample=now+1.0/15.0;
  if(!small||!slots[0].surface||!slots[1].surface)return;
  // Map the previous sample, never the transfer just issued. Driver may still stall.
  Slot &old=slots[1-next];
  if(old.pending){
   uint8_t *data=nullptr;uint32_t pitch=0;
   if(gs_stagesurface_map(old.surface,&data,&pitch)){
    staging.rgba.resize((size_t)sw*sh*4);
    for(uint32_t y=0;y<sh;++y)std::memcpy(staging.rgba.data()+(size_t)y*sw*4,data+(size_t)y*pitch,(size_t)sw*4);
    gs_stagesurface_unmap(old.surface);
    staging.w=sw;staging.h=sh;staging.time=old.time;staging.epoch=epoch;
    detector->submit(staging);
   }
   old.pending=false;
  }
  gs_texrender_reset(small);
  if(gs_texrender_begin(small,sw,sh)){
   gs_ortho(0,(float)sw,0,(float)sh,-100,100);
   gs_blend_state_push();gs_blend_function(GS_BLEND_ONE,GS_BLEND_ZERO);
   sprite(gs_texrender_get_texture(full),sw,sh);
   gs_blend_state_pop();gs_texrender_end(small);
   auto &slot=slots[next];gs_stage_texture(slot.surface,gs_texrender_get_texture(small));slot.pending=true;slot.time=now;
   next=1-next;
  }
 }
 void dot(float x,float y,float size,uint32_t rgba){
  auto *e=obs_get_base_effect(OBS_EFFECT_SOLID);vec4 color;vec4_from_rgba(&color,rgba);
  gs_effect_set_vec4(gs_effect_get_param_by_name(e,"color"),&color);
  gs_matrix_push();gs_matrix_translate3f(x-size*.5f,y-size*.5f,0);
  while(gs_effect_loop(e,"Solid"))gs_draw_sprite(nullptr,0,(uint32_t)size,(uint32_t)size);
  gs_matrix_pop();
 }
 void render(){
  obs_source_t *target=obs_filter_get_target(source),*parent=obs_filter_get_parent(source);
  if(!target||!parent){obs_source_skip_video_filter(source);return;}
  uint32_t w=obs_source_get_base_width(target),h=obs_source_get_base_height(target);
  if(!w||!h){obs_source_skip_video_filter(source);return;}
  // SDR only: fail open for HDR rather than silently clipping the camera.
  const gs_color_space spaces[]={GS_CS_SRGB};
  if(obs_source_get_color_space(target,1,spaces)!=GS_CS_SRGB){obs_source_skip_video_filter(source);return;}
  std::shared_ptr<const Settings> snapshot;{std::lock_guard<std::mutex> lock(settingsMutex);snapshot=settings;}
  const Settings &s=*snapshot;
  double now=os_gettime_ns()/1e9;
  if(width!=w||height!=h)resize(w,h);
  if(!rendered){
   if(!capture(target,parent)){obs_source_skip_video_filter(source);return;}
   rendered=true;sample(now);
  }
  loadImage(s);
  gs_blend_state_push();gs_blend_function(GS_BLEND_ONE,GS_BLEND_INVSRCALPHA);
  sprite(gs_texrender_get_texture(full),w,h);
  if(detector)detector->read(result);
  if(result.status==1 && result.found && result.epoch==epoch && result.time>lastSeen){
   face=result.face;lastSeen=result.time;haveFace=true;
  }
  if(haveFace){
   if(result.status==1 && result.epoch==epoch && result.found && now-lastSeen<.30)lossStart=-1;
   else if(lossStart<0){
    if(result.epoch==epoch && !result.found && result.time>lastSeen)lossStart=result.time;
    else if(now-lastSeen>=.30)lossStart=lastSeen+.30;
   }
  }
  bool live=haveFace && lossStart<0;
  bool held=haveFace && lossStart>=0 && now-lossStart<=s.hold;
  bool show=haveFace && (!s.hideLost||held||live) && result.status!=2;
  if(show){
   Pose p=pose(face,s.anchor,(float)w,(float)h,s.mirror,s.x,s.y);
   if(!s.autoScale)p.size=100;
   if(!s.autoRotate)p.angle=0;
   p.angle=wrap(p.angle+s.rotation);
   // A per-video-tick guard prevents double smoothing when preview and output both render.
   if(lastRender!=lastTick){smoothed=smoother.update(p,(float)(lastTick-lastRender),s.smooth);lastRender=lastTick;}
   p=smoothed;
   if(s.visible && image.texture && overlay){
    float imageScale=s.scale*p.size/std::max(1u,image.cx);
    gs_matrix_push();gs_matrix_translate3f(p.position.x,p.position.y,0);
    gs_matrix_rotaa4f(0,0,1,p.angle);gs_matrix_scale3f(imageScale,imageScale,1);
    gs_matrix_translate3f(-s.pivotX*image.cx,-s.pivotY*image.cy,0);
    gs_effect_set_texture(gs_effect_get_param_by_name(overlay,"image"),image.texture);
    gs_effect_set_float(gs_effect_get_param_by_name(overlay,"opacity"),s.opacity);
    while(gs_effect_loop(overlay,"Draw"))gs_draw_sprite(image.texture,0,image.cx,image.cy);
    gs_matrix_pop();
   }
   if(s.landmarks)for(const auto &v:face.points)dot((s.mirror?1-v.x:v.x)*w,v.y*h,6,0xff00ff00);
   if(s.marker)dot(p.position.x,p.position.y,10,0xff00ffff);
  }
  if(s.status){
   // Traffic-light bar is intentionally part of the filtered output (also recorded).
   uint32_t color=result.status==2?0xff0000ff:live?0xff00ff00:held?0xff00ffff:0xff808080;
   for(int i=0;i<5;++i)dot(10.f+i*10,10,8,color);
  }
  gs_blend_state_pop();
 }
};
const char *name(void*){return obs_module_text("FilterName");}
void defaults(obs_data_t *d){
 for(const char *k:{"visible","auto_scale","auto_rotate","hide_lost"})obs_data_set_default_bool(d,k,true);
 obs_data_set_default_int(d,"anchor",Nose);
 obs_data_set_default_double(d,"scale",1);obs_data_set_default_double(d,"opacity",100);
 obs_data_set_default_double(d,"pivot_x",.5);obs_data_set_default_double(d,"pivot_y",.5);
 obs_data_set_default_double(d,"smooth",.35);obs_data_set_default_double(d,"hold",.35);
}
bool reload(obs_properties_t*,obs_property_t*,void *data){
 if(data)static_cast<Filter*>(data)->imageRevision.fetch_add(1);return true;
}
obs_properties_t *properties(void*){
 auto *p=obs_properties_create();
 obs_properties_add_path(p,"png",obs_module_text("PNG"),OBS_PATH_FILE,"PNG (*.png)",nullptr);
 obs_properties_add_button(p,"reload",obs_module_text("reload"),reload);
 auto check=[&](const char *k){obs_properties_add_bool(p,k,obs_module_text(k));};
 auto number=[&](const char *k,double a,double b,double step){obs_properties_add_float_slider(p,k,obs_module_text(k),a,b,step);};
 check("visible");number("scale",.01,10,.01);number("opacity",0,100,1);
 number("pivot_x",0,1,.01);number("pivot_y",0,1,.01);
 auto *list=obs_properties_add_list(p,"anchor",obs_module_text("anchor"),OBS_COMBO_TYPE_LIST,OBS_COMBO_FORMAT_INT);
 const char *anchors[]={"LeftEye","RightEye","EyeCenter","Nose","Mouth","Forehead","LeftCheek","RightCheek","Chin","Custom"};
 for(int i=0;i<10;++i)obs_property_list_add_int(list,obs_module_text(anchors[i]),i);
 number("offset_x",-3,3,.01);number("offset_y",-3,3,.01);number("rotation",-180,180,1);
 check("auto_scale");check("auto_rotate");check("mirror");number("smooth",0,1,.01);number("hold",0,5,.05);
 check("hide_lost");check("landmarks");check("marker");check("status");return p;
}
void *create(obs_data_t *data,obs_source_t *source){
 try{
  auto f=std::make_unique<Filter>(source);f->readSettings(data);
  char *path=obs_module_file("overlay.effect");
  obs_enter_graphics();if(path)f->overlay=gs_effect_create_from_file(path,nullptr);obs_leave_graphics();bfree(path);
  if(!f->overlay)blog(LOG_ERROR,"[face-png-filter] overlay.effect missing or invalid; overlay disabled");
  path=obs_module_file("models/face_detection_yunet_2023mar.onnx");
  f->detector=std::make_unique<Detector>(path?path:"");bfree(path);
  return f.release();
 }catch(const std::exception &e){blog(LOG_ERROR,"[face-png-filter] create: %s",e.what());return nullptr;}
}
void destroy(void *data){delete static_cast<Filter*>(data);}
void update(void *data,obs_data_t *settings){try{static_cast<Filter*>(data)->readSettings(settings);}catch(...){blog(LOG_ERROR,"[face-png-filter] settings update failed");}}
void tick(void *data,float){auto *f=static_cast<Filter*>(data);f->rendered=false;f->lastTick=os_gettime_ns()/1e9;}
void render(void *data,gs_effect_t*){
 try{static_cast<Filter*>(data)->render();}
 catch(const std::exception &e){blog(LOG_ERROR,"[face-png-filter] render: %s",e.what());obs_source_skip_video_filter(static_cast<Filter*>(data)->source);}
}
}
bool obs_module_load(void){
 obs_source_info info{};info.id="face_png_overlay_filter";info.type=OBS_SOURCE_TYPE_FILTER;
 info.output_flags=OBS_SOURCE_VIDEO|OBS_SOURCE_CUSTOM_DRAW;
 info.get_name=name;info.create=create;info.destroy=destroy;info.update=update;
 info.get_defaults=defaults;info.get_properties=properties;info.video_tick=tick;info.video_render=render;
 obs_register_source(&info);blog(LOG_INFO,"[face-png-filter] version 0.1.0 loaded (YuNet CPU)");return true;
}
