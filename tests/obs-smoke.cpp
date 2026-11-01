// Optional real-libobs graphics integration test, outside the user's running OBS.
#include <obs-module.h>
#include <graphics/vec4.h>
#include <chrono>
#include <thread>
#include <fstream>
#include <iostream>
#include <vector>
#include <cstring>
struct Sample {gs_texture_t *texture=nullptr;};
const char *sample_name(void*){return "FPF integration fixture";}
void *sample_create(obs_data_t *d,obs_source_t*){
 auto *s=new Sample;std::ifstream file(obs_data_get_string(d,"raw"),std::ios::binary);
 std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)),{});if(bytes.size()!=320*320*4){delete s;return nullptr;}
 const uint8_t *data=bytes.data();obs_enter_graphics();s->texture=gs_texture_create(320,320,GS_RGBA,1,&data,0);obs_leave_graphics();return s;
}
void sample_destroy(void *d){auto *s=(Sample*)d;obs_enter_graphics();gs_texture_destroy(s->texture);obs_leave_graphics();delete s;}
uint32_t size(void*){return 320;}
void sample_render(void *d,gs_effect_t*){
 auto *e=obs_get_base_effect(OBS_EFFECT_DEFAULT);gs_effect_set_texture(gs_effect_get_param_by_name(e,"image"),((Sample*)d)->texture);
 while(gs_effect_loop(e,"Draw"))gs_draw_sprite(((Sample*)d)->texture,0,320,320);
}
std::vector<uint8_t> snapshot(obs_source_t *source){
 obs_enter_graphics();auto *rt=gs_texrender_create(GS_RGBA,GS_ZS_NONE);auto *stage=gs_stagesurface_create(320,320,GS_RGBA);
 std::vector<uint8_t> bytes(320*320*4);
 if(gs_texrender_begin(rt,320,320)){
  vec4 clear{};gs_clear(GS_CLEAR_COLOR,&clear,0,0);gs_ortho(0,320,0,320,-100,100);
  gs_matrix_push();gs_matrix_identity();obs_source_video_render(source);gs_matrix_pop();gs_texrender_end(rt);
  gs_stage_texture(stage,gs_texrender_get_texture(rt));uint8_t *data;uint32_t pitch;
  if(gs_stagesurface_map(stage,&data,&pitch)){
   for(int y=0;y<320;++y)std::memcpy(bytes.data()+y*1280,data+y*pitch,1280);gs_stagesurface_unmap(stage);
  }
 }
 gs_stagesurface_destroy(stage);gs_texrender_destroy(rt);obs_leave_graphics();return bytes;
}
int main(int argc,char **argv){
 // plugin binary, data directory, raw fixture, PNG, graphics module, libobs data
 if(argc!=7)return 2;
 if(!obs_startup("ja-JP",nullptr,nullptr))return 3;
 obs_add_data_path(argv[6]);obs_video_info v{};v.graphics_module=argv[5];v.fps_num=30;v.fps_den=1;
 v.base_width=v.output_width=320;v.base_height=v.output_height=320;v.output_format=VIDEO_FORMAT_RGBA;
 v.colorspace=VIDEO_CS_709;v.range=VIDEO_RANGE_FULL;v.scale_type=OBS_SCALE_BILINEAR;
 if(obs_reset_video(&v)!=OBS_VIDEO_SUCCESS){obs_shutdown();return 4;}
 obs_module_t *module=nullptr;
 if(obs_open_module(&module,argv[1],argv[2])!=MODULE_SUCCESS||!obs_init_module(module)){obs_shutdown();return 5;}
 bool registered=false;const char *id;for(size_t i=0;obs_enum_filter_types(i,&id);++i)if(!std::strcmp(id,"face_png_overlay_filter"))registered=true;
 if(!registered){obs_shutdown();return 6;}
 obs_source_info fixture{};fixture.id="fpf_fixture";fixture.type=OBS_SOURCE_TYPE_INPUT;fixture.output_flags=OBS_SOURCE_VIDEO|OBS_SOURCE_CUSTOM_DRAW;
 fixture.get_name=sample_name;fixture.create=sample_create;fixture.destroy=sample_destroy;fixture.get_width=size;fixture.get_height=size;fixture.video_render=sample_render;
 obs_register_source(&fixture);
 auto *settings=obs_data_create();obs_data_set_string(settings,"raw",argv[3]);
 auto *source=obs_source_create_private("fpf_fixture","Fixture",settings);obs_data_release(settings);
 if(!source){obs_shutdown();return 7;}
 obs_set_output_source(0,source);std::this_thread::sleep_for(std::chrono::milliseconds(100));
 auto baseline=snapshot(source);
 settings=obs_data_create();obs_data_set_double(settings,"smooth",0);obs_data_set_string(settings,"png",argv[4]);
 auto *filter=obs_source_create_private("face_png_overlay_filter","Filter",settings);obs_data_release(settings);
 if(!filter){obs_source_release(source);obs_shutdown();return 8;}
 obs_source_filter_add(source,filter);
 std::this_thread::sleep_for(std::chrono::milliseconds(1500));auto combined=snapshot(source);
 int changed=0;for(size_t i=0;i<baseline.size();i+=4)if(std::abs((int)baseline[i]-(int)combined[i])>10)++changed;
 std::ofstream("obs-smoke-output.rgba",std::ios::binary).write((const char*)combined.data(),combined.size());
 std::cout<<"Pixels changed by overlay: "<<changed<<'\n';
 // Missing PNG must leave the original frame intact.
 settings=obs_data_create();obs_data_set_string(settings,"png","/missing.png");obs_source_update(filter,settings);obs_data_release(settings);
 std::this_thread::sleep_for(std::chrono::milliseconds(100));auto without=snapshot(source);
 int passDiff=0;for(size_t i=0;i<baseline.size();++i)if(std::abs((int)baseline[i]-(int)without[i])>1)++passDiff;
 std::cout<<"Pass-through differing bytes: "<<passDiff<<'\n';
 obs_source_filter_remove(source,filter);obs_source_release(filter);
 obs_set_output_source(0,nullptr);obs_source_release(source);obs_shutdown();
 return changed>50&&passDiff==0?0:9;
}
