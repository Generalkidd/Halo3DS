/* Only authored readouts/monitor artwork get extra texels. This is evaluated
 * on a cache miss, not on every draw. Do not classify the monitor character,
 * bump maps, reflection cubes or general scenery by a loose name match. */
static unsigned int display_texture_resolution(const char *name,int original)
{
 if(original || !name)return 0;
 static const struct {const char *name;unsigned int limit;} artwork[]={
  {"levels\\a10\\devices\\command display\\bitmaps\\command display",256},
  {"levels\\a10\\devices\\command display\\bitmaps\\command nos",128},
  {"levels\\a10\\bitmaps\\monitors",256},
  {"levels\\a10\\bitmaps\\monitors glow",256},
  {"levels\\a10\\bitmaps\\monitor huge",256},
  {"levels\\a10\\bitmaps\\monitor glass",128},
  {"levels\\a10\\bitmaps\\hushed casket display",256},
  {"vehicles\\fighterbomber\\bitmaps\\fb_monitor",128},
  {"levels\\b30\\devices\\interior tech objects\\holo control\\bitmaps\\holo control",128},
  {"levels\\a50\\devices\\interior tech objects\\holo control\\bitmaps\\holo control",128}
 };
 for(unsigned int i=0;i<sizeof(artwork)/sizeof(*artwork);++i)
  if(!strcmp(name,artwork[i].name))return artwork[i].limit;
 return 0;
}
