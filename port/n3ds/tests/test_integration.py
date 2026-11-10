"""Host regressions for the dev/PC integration; no game files or SD writes.

Compile the actual settings reader, mapping and quality/format helpers with
in-memory file stubs. Xbox-only UI headers are deliberately excluded. Pass a
host compiler command after -- (including any Windows SDK include/lib flags).
"""
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[3]
controls = (ROOT / "port/n3ds/src/engine_controls.c").read_text()
start = controls.index("static const struct native_control_settings defaults")
end = controls.index("int n3ds_controls_active(void)")
body = controls[start:end]
save_start = body.index("static int save_settings(void)")
save_end = body.index("void n3ds_controls_initialize(void)", save_start)
body = body[:save_start] + body[save_end:]
row_start = controls.index("int n3ds_controls_row_field(int row)\n{")
row_end = controls.index("static void adjust(", row_start)
body += controls[row_start:row_end]
network = (ROOT / "port/n3ds/src/engine_network.c").read_text()
net_start = network.index("int n3ds_campaign_network_blocked(void)")
net_end = network.index("static unsigned coop_pending", net_start)

prefix = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "engine_controls.h"
#include "engine_files.h"
#include "engine_map_format.h"
#include "engine_combat_fx.h"
static unsigned int input_file[14], input_size;
static int original, pc_present, coop_menu, coop_selected, connection;
enum { _game_connection_local, _game_connection_network_client, _game_connection_network_server };
int n3ds_network_coop_menu(void){return coop_menu;}
int n3ds_network_coop_selected(void){return coop_selected;}
int game_connection(void){return connection;}
int n3ds_input_platform_original_model(void){return original;}
int n3ds_pc_maps_present(void){return pc_present;}
void n3ds_log(const char *s){(void)s;}
unsigned int n3ds_file_open(const char *p,unsigned int m){(void)p;(void)m;return 1;}
unsigned int n3ds_file_size(unsigned int h){(void)h;return input_size;}
int n3ds_file_read(unsigned int h,void *p,unsigned int n){(void)h;memcpy(p,input_file,n);return 1;}
int n3ds_file_close(unsigned int h){(void)h;return 1;}
'''
tests = r'''
static struct native_control_settings load(unsigned int version,unsigned int words,unsigned int layout){
 memset(input_file,0,sizeof(input_file));input_size=words*4;
 input_file[0]=0x3343544c;input_file[1]=version;input_file[2]=135;input_file[3]=85;input_file[4]=layout;
 if(version==2)input_file[6]=1; /* shipped dev CPP slot */
 if(version>=3){input_file[6]=1;input_file[7]=1;input_file[8]=1;}
 if(version>=4)input_file[9]=1;
 if(version>=5)input_file[10]=1;
 if(version==6){input_file[11]=1;input_file[12]=NATIVE_QUALITY_QUALITY;}
 input_file[words-1]=checksum_bytes(input_file,input_size);
 struct native_control_settings result;
 assert(read_settings("fixture",&result));return result;
}
int main(void){
 const unsigned int words[]={0,7,8,10,11,12,14};
 for(unsigned int version=1;version<=6;++version)for(unsigned int layout=0;layout<2;++layout){
  settings=load(version,words[version],layout);
  assert(settings.cstick==135 && settings.touch==85 && settings.xbox_buttons==layout);
  assert(settings.circle_pad_pro==(version==2 || version==6));
  assert(settings.high_textures==(version>=3));
  assert(settings.swap_xy==(version>=3));assert(settings.swap_ab==(version>=5));
  assert(settings.quality==(version==6?NATIVE_QUALITY_QUALITY:NATIVE_QUALITY_BALANCED));
  assert(n3ds_controls_buttons(NATIVE_KEY_A,0,1)==((layout^(version>=5))?NATIVE_KEY_B:NATIVE_KEY_A));
  assert(n3ds_controls_buttons(NATIVE_KEY_X,0,1)==((layout^(version>=3))?NATIVE_KEY_Y:NATIVE_KEY_X));
  struct settings_file roundtrip={0x3343544c,6,settings,0};roundtrip.checksum=checksum(&roundtrip);
  struct native_control_settings decoded;
  assert(decode_settings((const unsigned int*)&roundtrip,sizeof(roundtrip),&decoded));
  assert(!memcmp(&decoded,&settings,sizeof(settings)));
 }
 struct native_control_settings decoded;
 input_file[3]^=1;assert(!read_settings("corrupt",&decoded));
 initialized=1;settings=defaults;settings.quality=NATIVE_QUALITY_PERFORMANCE;
 assert(valid(&settings) && n3ds_controls_performance() && !n3ds_controls_quality() && !n3ds_controls_decals());
 struct settings_file performance={0x3343544c,6,settings,0};performance.checksum=checksum(&performance);
 assert(decode_settings((const unsigned int*)&performance,sizeof(performance),&decoded));
 assert(decoded.quality==NATIVE_QUALITY_PERFORMANCE);
 settings.quality=3;assert(!valid(&settings));
 settings=defaults;assert(!n3ds_combat_fx_name("weapons\\plasma rifle\\impact"));
 assert(!n3ds_controls_decals());
 settings.quality=NATIVE_QUALITY_QUALITY;assert(n3ds_combat_fx_name("weapons\\plasma rifle\\impact"));
 assert(n3ds_combat_fx_name("effects\\explosion"));assert(!n3ds_combat_fx_name("scenery\\leaves"));
 assert(n3ds_controls_decals());
 for(original=0;original<2;++original)for(pc_present=0;pc_present<2;++pc_present){
  unsigned int mask=0;int count=n3ds_controls_row_count();
  assert(count<=11);
  for(int row=0;row<count;++row){int field=n3ds_controls_row_field(row);assert(field>=0 && field<=10);assert(!(mask&(1u<<field)));mask|=1u<<field;}
  assert(!!(mask&(1u<<3))==!original);assert(!!(mask&(1u<<4))==(!original && pc_present));
  assert(mask&(1u<<9));assert(mask&(1u<<10));assert(n3ds_controls_row_field(count)==-1);
 }
 for(int i=0;i<19;++i){char path[64];snprintf(path,sizeof(path),"pc\\levels\\test\\%s\\%s",n3ds_pc_diagnostic_maps[i],n3ds_pc_diagnostic_maps[i]);
  assert(n3ds_map_uses_pc(path));assert(n3ds_pc_map_selection(path)==i);assert(n3ds_pc_map_selection(path+3)==-1);
 }
 assert(n3ds_pc_map_selection("pc\\levels\\test\\bloodgulch\\wizard")==-1);
 assert(n3ds_pc_map_selection("pc\\levels\\test\\bloodgulch\\bloodgulch-extra")==-1);
 assert(n3ds_pc_map_selection("levels\\a10\\a10")==-1);
 for(coop_menu=0;coop_menu<2;++coop_menu)for(coop_selected=0;coop_selected<2;++coop_selected)
  for(connection=0;connection<3;++connection)assert(!!n3ds_campaign_network_blocked()==!!(coop_menu || coop_selected || connection));
 puts("PASS: v1-v6 migration, layout preservation, CPP/texture slots, settings roundtrips and corruption");
 puts("PASS: three quality presets and persistence, model/PC settings rows, 19 qualified maps, co-op blocking");
 return 0;
}
'''

command = sys.argv[1:]
if command and command[0] == "--":
    command.pop(0)
if not command:
    command = ["clang"]
with tempfile.TemporaryDirectory(prefix="halo-integration-", ignore_cleanup_errors=True) as temp:
    path = Path(temp)
    source = path / "integration.c"
    executable = path / ("integration.exe" if sys.platform == "win32" else "integration")
    source.write_text(prefix + body + network[net_start:net_end] + tests)
    subprocess.run(command + ["-std=c11", "-I" + str(ROOT / "port/n3ds/include"), str(source), "-o", str(executable)], check=True)
    subprocess.run([str(executable)], check=True)
