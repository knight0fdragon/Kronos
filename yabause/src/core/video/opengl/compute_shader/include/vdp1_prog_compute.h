#ifndef VDP1_PROG_COMPUTE_H
#define VDP1_PROG_COMPUTE_H

#include "ygl.h"

#ifdef __cplusplus
extern "C" {
#endif

#define QuoteIdent(ident) #ident
#define Stringify(macro) QuoteIdent(macro)

static const char vdp1_draw_line_start_f[] =
"struct cmdparameter_struct{ \n"
"  uint CMDPMOD;\n" //OK ->Tjrs la meme
"  uint CMDSRCA;\n" //OK
"  uint CMDSIZE;\n" //OK => Tjrs la meme
"  int CMDXA;\n" //OK
"  int CMDYA;\n" //OK
"  int CMDXB;\n" //OK
"  int CMDYB;\n" //OK
"  uint CMDCOLR;\n" //Ok=>Tjrs la meme
"  float G[6];\n"
"  int misc;\n" //Ok - 2 bits
"  int idx;\n"
"};\n"
"layout(rgba8, binding = 0) writeonly uniform image2D outSurface;\n"
"layout(rgba8, binding = 1) writeonly uniform image2D outMeshSurface;\n"
"layout(std430, binding = 2) readonly buffer CMD_LIST {\n"
"  cmdparameter_struct cmd[];\n"
"};\n"
"layout(std430, binding = 3) readonly buffer VDP1RAM { uint Vdp1Ram[]; };\n"
"layout(location = 4) uniform bool is8bit;\n"
"layout(location = 8) uniform ivec2 sysClip;\n"
"layout(location = 9) uniform ivec4 usrClip;\n"
"layout(location = 11) uniform bool antialiased;\n"
"layout(location = 12) uniform int nbLines;\n"
"layout(location = 13) uniform int nbLineDraw;\n"
"layout(location = 14) uniform ivec2 bound;\n"

"shared uint endIndex[256];\n"

"bool clip(ivec2 P, ivec4 usrClip, ivec2 sysClip, uint CMDPMOD) {\n"
" if (((CMDPMOD >> 9) & 0x3u) == 2u) {\n"
"  //Draw inside\n"
"  if (any(lessThan(P,usrClip.xy)) || any(greaterThan(P,usrClip.zw))) return false;\n"
" }\n"
" if (((CMDPMOD >> 9) & 0x3u) == 3u) {\n"
"  //Draw outside\n"
"  if (all(greaterThanEqual(P,usrClip.xy)) && all(lessThanEqual(P,usrClip.zw))) return false;\n"
" }\n"
" if (any(greaterThan(P, sysClip.xy))) return false;\n"
" return true;\n"
"}\n"
"vec4 VDP1COLOR(uint color) {\n"
"  return vec4(float((color>>0)&0xFFu)/255.0,float((color>>8)&0xFFu)/255.0,0.0,0.0);\n"
"}\n";

static const char vdp1_start_end_base[] =
SHADER_VERSION_COMPUTE
"#ifdef GL_ES\n"
"precision highp float;\n"
"#endif\n"
"layout(local_size_x = %d, local_size_y = %d) in;\n";
static char vdp1_start_end[ sizeof(vdp1_start_end_base) + 64 ];

static const char vdp1_draw_line_start_f_rw[] =
"struct cmdparameter_struct{ \n"
"  uint CMDPMOD;\n"
"  uint CMDSRCA;\n"
"  uint CMDSIZE;\n"
"  int CMDXA;\n"
"  int CMDYA;\n"
"  int CMDXB;\n"
"  int CMDYB;\n"
"  uint CMDCOLR;\n"
"  float G[6];\n"
"  int misc;\n"
"  int idx;\n"
"};\n"
"layout(rgba8, binding = 0) uniform image2D outSurface;\n"
"layout(rgba8, binding = 1) writeonly uniform image2D outMeshSurface;\n"
"layout(std430, binding = 2) readonly buffer CMD_LIST {\n"
"  cmdparameter_struct cmd[];\n"
"};\n"
"layout(std430, binding = 3) readonly buffer VDP1RAM { uint Vdp1Ram[]; };\n"
"layout(location = 4) uniform bool is8bit;\n"
"layout(location = 8) uniform ivec2 sysClip;\n"
"layout(location = 9) uniform ivec4 usrClip;\n"
"layout(location = 11) uniform bool antialiased;\n"
"layout(location = 12) uniform int nbLines;\n"
"layout(location = 13) uniform int nbLineDraw;\n"
"layout(location = 14) uniform ivec2 bound;\n"

"shared uint endIndex[256];\n"

"bool clip(ivec2 P, ivec4 usrClip, ivec2 sysClip, uint CMDPMOD) {\n"
" if (((CMDPMOD >> 9) & 0x3u) == 2u) {\n"
"  //Draw inside\n"
"  if (any(lessThan(P,usrClip.xy)) || any(greaterThan(P,usrClip.zw))) return false;\n"
" }\n"
" if (((CMDPMOD >> 9) & 0x3u) == 3u) {\n"
"  //Draw outside\n"
"  if (all(greaterThanEqual(P,usrClip.xy)) && all(lessThanEqual(P,usrClip.zw))) return false;\n"
" }\n"
" if (any(greaterThan(P, sysClip.xy))) return false;\n"
" return true;\n"
"}\n"
"vec4 VDP1COLOR(uint color) {\n"
"  return vec4(float((color>>0)&0xFFu)/255.0,float((color>>8)&0xFFu)/255.0,0.0,0.0);\n"
"}\n";

static const char vdp1_get_non_textured_f[] =
"uint getEndIndex(cmdparameter_struct pixcmd, uint y)\n"
"{\n"
" return 1024;\n"
"}\n"
"uint getColor(cmdparameter_struct pixcmd, vec2 uv, out bool valid){\n"
"  valid = true;\n"
" if (is8bit)"
"  return (pixcmd.CMDCOLR&0xFF);\n"
" else"
"  return pixcmd.CMDCOLR;\n"
"}\n";

static const char vdp1_get_textured_f[] =
"uint Vdp1RamReadByte(uint addr) {\n"
"  addr &= 0x7FFFFu;\n"
"  uint read = Vdp1Ram[addr>>2];\n"
"  return (read>>(8*(addr&0x3u)))&0xFFu;\n"
"}\n"
"uint Vdp1RamReadWord(uint addr) {\n"
"  addr &= 0x7FFFFu;\n"
"  uint read = Vdp1Ram[addr>>2];\n"
"  if( (addr & 0x02u) != 0u ) { read >>= 16; } \n"
"  return (((read) >> 8 & 0xFFu) | ((read) & 0xFFu) << 8);\n"
"}\n"
"uint getEndIndex(cmdparameter_struct pixcmd, uint y)\n"
"{\n"
"  uint color = 0;\n"
"  uint nbEnd = 0;\n"
"  ivec2 texSize = ivec2(((pixcmd.CMDSIZE >> 8) & 0x3F)<<3,pixcmd.CMDSIZE & 0xFF );\n"
"  for (uint x=0; x<texSize.x; x++) {\n"
"   uint pos = y*texSize.x+x;\n"
"   uint charAddr = ((pixcmd.CMDSRCA * 8)& 0x7FFFFu) + pos;\n"
"   uint dot;\n"
"   switch ((pixcmd.CMDPMOD >> 3) & 0x7u)\n"
"   {\n"
"    case 0:\n"
"    {\n"
"      // 4 bpp Bank mode\n"
"      uint colorBank = pixcmd.CMDCOLR & 0xFFF0u;\n"
"      charAddr = pixcmd.CMDSRCA * 8 + pos/2;\n"
"      dot = Vdp1RamReadByte(charAddr);\n"
"      if ((x & 0x1u) == 0u) dot = (dot>>4)&0xFu;\n"
"      else dot = (dot)&0xFu;\n"
"      if (dot == 0x0Fu) {\n"
"       nbEnd += 1;\n"
"      }\n"
"      break;\n"
"    }\n"
"    case 1:\n"
"    {\n"
"      // 4 bpp LUT mode\n"
"       charAddr = pixcmd.CMDSRCA * 8 + pos/2;\n"
"       dot = Vdp1RamReadByte(charAddr);\n"
"       if ((x & 0x1u) == 0u) dot = (dot>>4)&0xFu;\n"
"       else dot = (dot)&0xFu;\n"
"       if (dot == 0x0Fu) {\n"
"        nbEnd += 1;\n"
"       }\n"
"       break;\n"
"    }\n"
"    case 2:\n"
"    {\n"
"      // 8 bpp(64 color) Bank mode\n"
"      uint colorBank = pixcmd.CMDCOLR & 0xFFC0u;\n"
"      dot = Vdp1RamReadByte(charAddr);\n"
"      if (dot == 0xFFu) {\n"
"       nbEnd += 1;\n"
"      }\n"
"      break;\n"
"    }\n"
"    case 3:\n"
"    {\n"
"      // 8 bpp(128 color) Bank mode\n"
"      uint colorBank = pixcmd.CMDCOLR & 0xFF80u;\n"
"      dot = Vdp1RamReadByte(charAddr);\n"
"      if (dot == 0xFFu) {\n"
"       nbEnd += 1;\n"
"      }\n"
"      break;\n"
"    }\n"
"    case 4:\n"
"    {\n"
"      // 8 bpp(256 color) Bank mode\n"
"      uint colorBank = pixcmd.CMDCOLR & 0xFF00u;\n"
"      dot = Vdp1RamReadByte(charAddr);\n"
"      if (dot == 0xFFu) {\n"
"       nbEnd += 1;\n"
"      }\n"
"      break;\n"
"    }\n"
"    case 5:\n"
"    {\n"
"      // 16 bpp Bank mode\n"
"      uint temp;\n"
"      charAddr += pos;\n"
"      temp = Vdp1RamReadWord(charAddr);\n"
"      if (temp == 0x7FFFu) {\n"
"       nbEnd += 1;\n"
"      }\n"
"      break;\n"
"    }\n"
"    default:\n"
"      break;\n"
"   }\n"
"   if (nbEnd > 2) return x;"
"  }\n"
" return 1024;\n"
"}\n"
"uint getColor(cmdparameter_struct pixcmd, vec2 uv, out bool valid)\n"
"{\n"
"  uint color = 0;\n"
"  ivec2 texSize = ivec2(((pixcmd.CMDSIZE >> 8) & 0x3F)<<3,pixcmd.CMDSIZE & 0xFF );\n"
"  uint y = uint(floor(uv.y*(texSize.y)));\n"
"  uint x = uint(floor(uv.x*(texSize.x)));\n"
"  if ((pixcmd.misc & 0x1u) == 0x1u) x = (texSize.x-1) - x;\n"
"  if ((pixcmd.misc & 0x2u) == 0x2u) y = (texSize.y-1) - y;\n"
"  uint pos = y*texSize.x+x;\n"
"  uint charAddr = ((pixcmd.CMDSRCA * 8)& 0x7FFFFu) + pos;\n"
"  uint dot;\n"
"  bool SPD = ((pixcmd.CMDPMOD & 0x40u) != 0);\n"
"  bool END = ((pixcmd.CMDPMOD & 0x80u) == 0);\n"
"  valid = true;\n"
"  if (!valid) return 0x0;\n"
"  switch ((pixcmd.CMDPMOD >> 3) & 0x7u)\n"
"  {\n"
"    case 0:\n"
"    {\n"
"      // 4 bpp Bank mode\n"
"      uint colorBank = pixcmd.CMDCOLR & 0xFFF0u;\n"
"      uint i;\n"
"      charAddr = pixcmd.CMDSRCA * 8 + pos/2;\n"
"      dot = Vdp1RamReadByte(charAddr);\n"
"      if ((x & 0x1u) == 0u) dot = (dot>>4)&0xFu;\n"
"      else dot = (dot)&0xFu;\n"
"      if ((dot == 0x0Fu) && END) {\n"
"       valid = false;\n"
"      }\n"
"      if ((dot == 0) && (!SPD)) {\n"
"        valid = false;\n"
"      }\n"
"      else color = dot | colorBank;\n"
"      break;\n"
"    }\n"
"    case 1:\n"
"    {\n"
"      // 4 bpp LUT mode\n"
"       uint temp;\n"
"       charAddr = pixcmd.CMDSRCA * 8 + pos/2;\n"
"       uint colorLut = pixcmd.CMDCOLR * 8;\n"
"       dot = Vdp1RamReadByte(charAddr);\n"
"       if ((x & 0x1u) == 0u) dot = (dot>>4)&0xFu;\n"
"       else dot = (dot)&0xFu;\n"
"       if ((dot == 0x0Fu) && END) {\n"
"        valid = false;\n"
"       }\n"
"      if ((dot == 0) && (!SPD)) {\n"
"        valid = false;\n"
"      }\n"
"       else {\n"
"         temp = Vdp1RamReadWord((dot * 2 + colorLut));\n"
"         color = temp;\n"
"       }\n"
"       break;\n"
"    }\n"
"    case 2:\n"
"    {\n"
"      // 8 bpp(64 color) Bank mode\n"
"      uint colorBank = pixcmd.CMDCOLR & 0xFFC0u;\n"
"      dot = Vdp1RamReadByte(charAddr);\n"
"      if ((dot == 0xFFu) && END) {\n"
"       valid = false;\n"
"      }\n"
"      if ((dot == 0) && (!SPD)) {\n"
"        valid = false;\n"
"      }\n"
"      else {\n"
"        color = (dot & 0x3Fu) | colorBank;\n"
"      }\n"
"      break;\n"
"    }\n"
"    case 3:\n"
"    {\n"
"      // 8 bpp(128 color) Bank mode\n"
"      uint colorBank = pixcmd.CMDCOLR & 0xFF80u;\n"
"      dot = Vdp1RamReadByte(charAddr);\n"
"      if ((dot == 0xFFu) && END) {\n"
"       valid = false;\n"
"      }\n"
"      if ((dot == 0) && (!SPD)) {\n"
"        valid = false;\n"
"      }\n"
"      else {\n"
"        color = (dot & 0x7Fu) | colorBank;\n"
"      }\n"
"      break;\n"
"    }\n"
"    case 4:\n"
"    {\n"
"      // 8 bpp(256 color) Bank mode\n"
"      uint colorBank = pixcmd.CMDCOLR & 0xFF00u;\n"
"      dot = Vdp1RamReadByte(charAddr);\n"
"      if ((dot == 0xFFu) && END) {\n"
"       valid = false;\n"
"      }\n"
"      if ((dot == 0) && (!SPD)) {\n"
"        valid = false;\n"
"      }\n"
"      else {\n"
"          color = dot | colorBank;\n"
"      }\n"
"      break;\n"
"    }\n"
"    case 5:\n"
"    {\n"
"      // 16 bpp Bank mode\n"
"      uint temp;\n"
"      charAddr += pos;\n"
"      temp = Vdp1RamReadWord(charAddr);\n"
"      if ((temp == 0x7FFFu) && END) {\n"
"       valid = false;\n"
"      }\n"
"      if (((temp & 0x8000u) == 0) && (!SPD)) {\n"
"        valid = false;\n"
"      }\n"
"      else {\n"
"        color = temp;\n"
"      }\n"
"      break;\n"
"    }\n"
"    default:\n"
"      break;\n"
"  }\n"
" valid = valid && ((!END) || (END && (x <= endIndex[y])));\n"
" if (is8bit)"
"  return (color&0xFF);\n"
" else"
"  return color;\n"
"}\n";

static const char vdp1_get_pixel_msb_shadow_f[] =
"vec4 getColoredPixel(cmdparameter_struct pixcmd, vec2 uv, ivec2 P, out bool valid)\n""{\n"
"  uint testcolor = getColor(pixcmd, uv, valid);\n"
"  vec4 oldcol = imageLoad(outSurface, P);\n"
"  uint color = uint(oldcol.r*255.0) + (uint(oldcol.g*255.0)<<8);\n"
"  uint MSBht = 0x8000;\n"
"  color = MSBht | color;\n"
"  return VDP1COLOR(color);\n"
"}\n";

static const char vdp1_get_pixel_replace_f[] =
"vec4 getColoredPixel(cmdparameter_struct pixcmd, vec2 uv, ivec2 P, out bool valid)\n""{\n"
"  uint color = getColor(pixcmd, uv, valid);\n"
"  return VDP1COLOR(color);\n"
"}\n";

static const char vdp1_get_pixel_shadow_f[] =
"vec4 getColoredPixel(cmdparameter_struct pixcmd, vec2 uv, ivec2 P, out bool valid)\n""{\n"
"  uint color = getColor(pixcmd, uv, valid);\n"
"  vec4 oldcol = imageLoad(outSurface, P);\n"
"  uint oldcolor = uint(oldcol.r*255.0) + (uint(oldcol.g*255.0)<<8);\n"
"  if ((oldcolor & 0x8000) != 0) {\n"
"   uint Rht = ((oldcolor >> 00) & 0x1F)>>1;\n"
"   uint Ght = ((oldcolor >> 05) & 0x1F)>>1;\n"
"   uint Bht = ((oldcolor >> 10) & 0x1F)>>1;\n"
"   uint MSBht = oldcolor & 0x8000;\n"
"   color = MSBht | Rht | (Ght<<05) | (Bht<<10);\n"
"  }\n"
"  return VDP1COLOR(color);\n"
"}\n";

static const char vdp1_get_pixel_half_luminance_f[] =
"vec4 getColoredPixel(cmdparameter_struct pixcmd, vec2 uv, ivec2 P, out bool valid)\n""{\n"
"  uint color = getColor(pixcmd, uv, valid);\n"
"  uint Rht = ((color >> 00) & 0x1F)>>1;\n"
"  uint Ght = ((color >> 05) & 0x1F)>>1;\n"
"  uint Bht = ((color >> 10) & 0x1F)>>1;\n"
"  uint MSBht = color & 0x8000;\n"
"  color = MSBht | Rht | (Ght<<05) | (Bht<<10);\n"
"  return VDP1COLOR(color);\n"
"}\n";

static const char vdp1_get_pixel_half_transparent_f[] =
"vec4 getColoredPixel(cmdparameter_struct pixcmd, vec2 uv, ivec2 P, out bool valid)\n"
"{\n"
"  uint color = getColor(pixcmd, uv, valid);\n"
"  vec4 oldcol = imageLoad(outSurface, P);\n"
"  uint oldcolor = uint(oldcol.r*255.0) + (uint(oldcol.g*255.0)<<8);\n"
"  if ((oldcolor & 0x8000) != 0) {\n"
"   uint Rht = (((color >> 00) & 0x1F) + ((oldcolor >> 00) & 0x1F))>>1;\n"
"   uint Ght = (((color >> 05) & 0x1F) + ((oldcolor >> 05) & 0x1F))>>1;\n"
"   uint Bht = (((color >> 10) & 0x1F) + ((oldcolor >> 10) & 0x1F))>>1;\n"
"   uint MSBht = color & 0x8000;\n"
"   color = MSBht | Rht | (Ght<<05) | (Bht<<10);\n"
"  }\n"
"  return VDP1COLOR(color);\n"
"}\n";

static const char vdp1_get_pixel_gouraud_f[] =
"vec4 getColoredPixel(cmdparameter_struct pixcmd, vec2 uv, ivec2 P, out bool valid)\n""{\n"
"  uint color = getColor(pixcmd, uv, valid);\n"
"  //Gouraud\n"
"  float Rg = float((color >> 00) & 0x1F)/31.0;\n"
"  float Gg = float((color >> 05) & 0x1F)/31.0;\n"
"  float Bg = float((color >> 10) & 0x1F)/31.0;\n"
"  int MSBg = int(color & 0x8000);\n"
"  Rg = clamp(Rg + mix(pixcmd.G[0], pixcmd.G[3], uv.x), 0.0, 1.0);\n"
"  Gg = clamp(Gg + mix(pixcmd.G[1], pixcmd.G[4], uv.x), 0.0, 1.0);\n"
"  Bg = clamp(Bg + mix(pixcmd.G[2], pixcmd.G[5], uv.x), 0.0, 1.0);\n"
"  color = MSBg | (int(Rg*31.0)&0x1F) | ((int(Gg*31.0)&0x1F)<<05) | ((int(Bg*31.0)&0x1F)<<10);\n"
"  vec4 ret = VDP1COLOR(color);\n"
"  return ret;\n"
"}\n";

static const char vdp1_get_pixel_gouraud_extended_f[] =
"vec4 getColoredPixel(cmdparameter_struct pixcmd, vec2 uv, ivec2 P, out bool valid)\n""{\n"
"  uint color = getColor(pixcmd, uv, valid);\n"
"  //Gouraud\n"
"  float Rg = float((color >> 00) & 0x1F)/31.0;\n"
"  float Gg = float((color >> 05) & 0x1F)/31.0;\n"
"  float Bg = float((color >> 10) & 0x1F)/31.0;\n"
"  int MSBg = int(color & 0x8000);\n"
"  Rg = clamp(Rg + mix(pixcmd.G[0], pixcmd.G[3], uv.x), 0.0, 1.0);\n"
"  Gg = clamp(Gg + mix(pixcmd.G[1], pixcmd.G[4], uv.x), 0.0, 1.0);\n"
"  Bg = clamp(Bg + mix(pixcmd.G[2], pixcmd.G[5], uv.x), 0.0, 1.0);\n"
"  color = MSBg | ((int(Rg*255.0)>>3)&0x1F) | (((int(Gg*255.0)>>3)&0x1F)<<05) | (((int(Bg*255.0)>>3)&0x1F)<<10);\n"
"  vec4 ret = VDP1COLOR(color);\n"
"  ret.b = float((int(Rg*255.0)&0x7) | (int(Gg*255.0)&0x7)<< 4  )/255.0;\n"
"  ret.a = float((int(Bg*255.0)&0x7))/255.0;\n"
"  return ret;\n"
"}\n";

static const char vdp1_get_pixel_gouraud_half_luminance_f[] =
"vec4 getColoredPixel(cmdparameter_struct pixcmd, vec2 uv, ivec2 P, out bool valid)\n""{\n"
"  uint color = getColor(pixcmd, uv, valid);\n"
"  //Gouraud Half-luminance\n"
"  float Rg = float((color >> 00) & 0x1F)/31.0;\n"
"  float Gg = float((color >> 05) & 0x1F)/31.0;\n"
"  float Bg = float((color >> 10) & 0x1F)/31.0;\n"
"  int MSBg = int(color & 0x8000);\n"
"  Rg = clamp(Rg + mix(pixcmd.G[0], pixcmd.G[3], uv.x), 0.0, 1.0);\n"
"  Gg = clamp(Gg + mix(pixcmd.G[1], pixcmd.G[4], uv.x), 0.0, 1.0);\n"
"  Bg = clamp(Bg + mix(pixcmd.G[2], pixcmd.G[5], uv.x), 0.0, 1.0);\n"
"  color = MSBg | (int(Rg*31.0)&0x1F) | ((int(Gg*31.0)&0x1F)<<05) | ((int(Bg*31.0)&0x1F)<<10);\n"
"  uint Rht = ((color >> 00) & 0x1F)>>1;\n"
"  uint Ght = ((color >> 05) & 0x1F)>>1;\n"
"  uint Bht = ((color >> 10) & 0x1F)>>1;\n"
"  uint MSBht = color & 0x8000;\n"
"  color = MSBht | Rht | (Ght<<05) | (Bht<<10);\n"
"  return VDP1COLOR(color);\n"
"}\n";

static const char vdp1_get_pixel_gouraud_extended_half_luminance_f[] =
"vec4 getColoredPixel(cmdparameter_struct pixcmd, float uv, ivec2 P, out bool valid)\n""{\n"
"  uint color = getColor(pixcmd, uv, valid);\n"
"  //Gouraud Half-luminance\n"
"  float Rg = float((color >> 00) & 0x1F)/31.0;\n"
"  float Gg = float((color >> 05) & 0x1F)/31.0;\n"
"  float Bg = float((color >> 10) & 0x1F)/31.0;\n"
"  int MSBg = int(color & 0x8000);\n"
"  Rg = clamp(Rg + mix(pixcmd.G[0], pixcmd.G[3], uv.x), 0.0, 1.0);\n"
"  Gg = clamp(Gg + mix(pixcmd.G[1], pixcmd.G[4], uv.x), 0.0, 1.0);\n"
"  Bg = clamp(Bg + mix(pixcmd.G[2], pixcmd.G[5], uv.x), 0.0, 1.0);\n"
"  color = MSBg | ((int(Rg*255.0)>>3)&0x1F) | (((int(Gg*255.0)>>3)&0x1F)<<05) | (((int(Bg*255.0)>>3)&0x1F)<<10);\n"
"  uint Rht = ((color >> 00) & 0x1F)>>1;\n"
"  uint Ght = ((color >> 05) & 0x1F)>>1;\n"
"  uint Bht = ((color >> 10) & 0x1F)>>1;\n"
"  uint MSBht = color & 0x8000;\n"
"  color = MSBht | Rht | (Ght<<05) | (Bht<<10);\n"
"  vec4 ret = VDP1COLOR(color);\n"
"  ret.b = float(((int(Rg*255.0)&0x7)>>1) | ((int(Gg*255.0)&0x7)>>1)<< 4  )/255.0;\n"
"  ret.a = float(((int(Bg*255.0)&0x7)>>1))/255.0;\n"
"  return ret;\n"
"}\n";

static const char vdp1_get_pixel_gouraud_half_transparent_f[] =
"vec4 getColoredPixel(cmdparameter_struct pixcmd, vec2 uv, ivec2 P, out bool valid)\n"
"{\n"
"  uint color = getColor(pixcmd, uv, valid);\n"
"  //Gouraud half transparent\n"
"  float Rg = float((color >> 00) & 0x1F)/31.0;\n"
"  float Gg = float((color >> 05) & 0x1F)/31.0;\n"
"  float Bg = float((color >> 10) & 0x1F)/31.0;\n"
"  int MSBg = int(color & 0x8000);\n"
"  Rg = clamp(Rg + mix(pixcmd.G[0], pixcmd.G[3], uv.x), 0.0, 1.0);\n"
"  Gg = clamp(Gg + mix(pixcmd.G[1], pixcmd.G[4], uv.x), 0.0, 1.0);\n"
"  Bg = clamp(Bg + mix(pixcmd.G[2], pixcmd.G[5], uv.x), 0.0, 1.0);\n"
"  color = MSBg | (int(Rg*31.0)&0x1F) | ((int(Gg*31.0)&0x1F)<<05) | ((int(Bg*31.0)&0x1F)<<10);\n"
"  vec4 oldcol = imageLoad(outSurface, P);\n"
"  uint oldcolor = uint(oldcol.r*255.0) + (uint(oldcol.g*255.0)<<8);\n"
"  if ((oldcolor & 0x8000) != 0) {\n"
"   uint Rht = (((color >> 00) & 0x1F) + ((oldcolor >> 00) & 0x1F))>>1;\n"
"   uint Ght = (((color >> 05) & 0x1F) + ((oldcolor >> 05) & 0x1F))>>1;\n"
"   uint Bht = (((color >> 10) & 0x1F) + ((oldcolor >> 10) & 0x1F))>>1;\n"
"   uint MSBht = color & 0x8000;\n"
"   color = MSBht | Rht | (Ght<<05) | (Bht<<10);\n"
"  }\n"
"  return VDP1COLOR(color);\n"
"}\n";
static const char vdp1_get_pixel_gouraud_extended_half_transparent_f[] =
"vec4 getColoredPixel(cmdparameter_struct pixcmd, vec2 uv, ivec2 P, out bool valid)\n"
"{\n"
"  uint color = getColor(pixcmd, uv, valid);\n"
"  //Gouraud half transparent\n"
"  float Rg = float((color >> 00) & 0x1F)/31.0;\n"
"  float Gg = float((color >> 05) & 0x1F)/31.0;\n"
"  float Bg = float((color >> 10) & 0x1F)/31.0;\n"
"  int MSBg = int(color & 0x8000);\n"
"  Rg = clamp(Rg + mix(pixcmd.G[0], pixcmd.G[3], uv.x), 0.0, 1.0);\n"
"  Gg = clamp(Gg + mix(pixcmd.G[1], pixcmd.G[4], uv.x), 0.0, 1.0);\n"
"  Bg = clamp(Bg + mix(pixcmd.G[2], pixcmd.G[5], uv.x), 0.0, 1.0);\n"
"  color = MSBg | ((int(Rg*255.0)>>3)&0x1F) | (((int(Gg*255.0)>>3)&0x1F)<<05) | (((int(Bg*255.0)>>3)&0x1F)<<10);\n"
"  vec4 oldcol = imageLoad(outSurface, P);\n"
"  uint oldcolor = uint(oldcol.r*255.0) + (uint(oldcol.g*255.0)<<8);\n"
"  if ((oldcolor & 0x8000) != 0) {\n"
"   uint Rht = (((color >> 00) & 0x1F) + ((oldcolor >> 00) & 0x1F))>>1;\n"
"   uint Ght = (((color >> 05) & 0x1F) + ((oldcolor >> 05) & 0x1F))>>1;\n"
"   uint Bht = (((color >> 10) & 0x1F) + ((oldcolor >> 10) & 0x1F))>>1;\n"
"   uint MSBht = color & 0x8000;\n"
"   color = MSBht | Rht | (Ght<<05) | (Bht<<10);\n"
"  }\n"
"  vec4 ret = VDP1COLOR(color);\n"
"  ret.b = float(((int(Rg*255.0)&0x7)>>1) | ((int(Gg*255.0)&0x7)>>1)<< 4  )/255.0;\n"
"  ret.a = float(((int(Bg*255.0)&0x7)>>1))/255.0;\n"
"  return ret;\n"
"}\n";

static char vdp1_draw_mesh_f[] =
"vec4 getMeshedPixel(cmdparameter_struct pixcmd, vec2 uv, ivec2 P, out bool valid, out bool discarded)\n"
"{\n"
" valid = true;\n"
" discarded = false;\n"
" if( (int(P.y) & 0x01) == 0 ){\n"
"  if( (int(P.x) & 0x01) == 0 ){\n"
"   valid = false;\n"
"  }\n"
" }else{\n"
"  if( (int(P.x) & 0x01) == 1 ){\n"
"    valid = false;\n"
"  }\n"
" }\n"
" if (valid) return getColoredPixel(pixcmd, uv, P, valid);\n"
" else return vec4(0.0);\n"
"}\n";

static char vdp1_draw_improved_mesh_f[] =
"vec4 getMeshedPixel(cmdparameter_struct pixcmd, vec2 uv, ivec2 P, out bool valid, out bool discarded)\n"
"{\n"
" valid = true;\n"
" discarded = false;\n"
" vec4 ret = getColoredPixel(pixcmd, uv, P, valid);\n"
" if (valid) {\n"
"  ret.b = 1.0;\n"
"  imageStore(outMeshSurface,P,ret);\n"
"  discarded = true;\n"
" }\n"
" return vec4(0.0);\n"
"}\n";

static char vdp1_draw_no_mesh_f[] =
"vec4 getMeshedPixel(cmdparameter_struct pixcmd, vec2 uv, ivec2 P, out bool valid, out bool discarded)\n"
"{\n"
" discarded = false;\n"
" return getColoredPixel(pixcmd, uv, P, valid);\n"
"}\n";

static char vdp1_draw_no_mesh_improved_f[] =
"vec4 getMeshedPixel(cmdparameter_struct pixcmd, vec2 uv, ivec2 P, out bool valid, out bool discarded)\n"
"{\n"
" discarded = false;\n"
" vec4 ret = getColoredPixel(pixcmd, uv, P, valid);\n"
" if (valid) imageStore(outMeshSurface,P,vec4(0.0));\n"
" return ret;\n"
"}\n";

static const char vdp1_draw_poly_test[] =
"bool isOnLine(ivec2 P, ivec2 P0, float slope){\n"
" int testY = int(floor(abs(float(P.x-P0.x)*slope) + 0.5));\n"
" return (testY == abs(P.y-P0.y));\n"
"}\n"
"bool onChangeAliased(ivec2 P, ivec2 P0, float slope, ivec2 a, int orientation) {\n"
"  if ((a.x == a.y)^^(orientation==1)) {\n"
"   if (!isOnLine(P+ivec2( a.x,0), P0, slope)) return false;\n"
"   if (!isOnLine(P+ivec2(0,-a.y), P0, slope)) return false;\n"
"   return true;\n"
"  }\n"
"  else {\n"
"   if (!isOnLine(P+ivec2(-a.x,0), P0, slope)) return false;\n"
"   if (!isOnLine(P+ivec2(0, a.y), P0, slope)) return false;\n"
"   return true;\n"
"  }\n"
"  return false;\n"
"}\n"
"void main()\n"
"{\n"
" if ((cmd[0].CMDPMOD & 0x80u) == 0) {\n"
"  uint index = gl_LocalInvocationID.y*gl_WorkGroupSize.x+gl_LocalInvocationID.x;\n"
"  ivec2 texSize = ivec2(((cmd[0].CMDSIZE >> 8) & 0x3F)<<3,cmd[0].CMDSIZE & 0xFF );\n"
"  while (index <= texSize.y) {\n"
"   endIndex[index] = getEndIndex(cmd[0], index);\n"
"   index += gl_WorkGroupSize.x*gl_WorkGroupSize.y;\n"
"  }\n"
"  memoryBarrierShared();"
"  barrier();\n"
" }\n"
" ivec2 P = ivec2(gl_GlobalInvocationID.xy)+bound;\n"
" bool valid = clip(P,usrClip, sysClip, cmd[0].CMDPMOD);\n"
" if (!valid) return;\n"
" for (int l = 0; l < nbLineDraw; l++) {"
"  int idx = nbLineDraw-1 - l;\n"
"  cmdparameter_struct pixcmd = cmd[idx];\n"
"  ivec2 P0 = ivec2(pixcmd.CMDXA, pixcmd.CMDYA);\n"
"  ivec2 P1 = ivec2(pixcmd.CMDXB, pixcmd.CMDYB);\n"
"  ivec2 a = sign(P1-P0);\n"
"  if (a.x == 0) a.x = 1;\n"
"  if (a.y == 0) a.y = 1;\n"
"  ivec2 line = ivec2(pixcmd.CMDXB, pixcmd.CMDYB) - ivec2(pixcmd.CMDXA, pixcmd.CMDYA);\n"
"  float orientation = (abs(line.x) >= abs(line.y))?1.0:0.0;\n"
"  mat2 trans = mat2(orientation, 1.0-orientation, 1.0-orientation, orientation);\n"
"  mat2 transrev = trans;\n"
"  P0 = ivec2(trans*P0);\n"
"  P1 = ivec2(trans*P1);\n"
"  ivec2 Pn = ivec2(trans*P);\n"
"  a = sign(P1-P0);\n"
"  if (a.x == 0) a.x = 1;\n"
"  if (a.y == 0) a.y = 1;\n"
"  int veclong = P1.x-P0.x+a.x;\n"
"  bool pixelOnLine = (P1.x == P0.x);\n"
"  float slope = 1.0;\n"
"  if (P.x < min(pixcmd.CMDXA,pixcmd.CMDXB)) continue;\n"
"  if (P.x > max(pixcmd.CMDXA,pixcmd.CMDXB)) continue;\n"
"  if (P.y < min(pixcmd.CMDYA,pixcmd.CMDYB)) continue;\n"
"  if (P.y > max(pixcmd.CMDYA,pixcmd.CMDYB)) continue;\n"
"  if (!pixelOnLine) {\n"
"    slope = float(P1.y-P0.y)/float(P1.x-P0.x);\n"
"    pixelOnLine = isOnLine(Pn, P0, slope);\n"
"  }\n"
"  if (antialiased && !pixelOnLine) pixelOnLine = (onChangeAliased((Pn), P0, slope, a, int(orientation)));\n"
"  if (pixelOnLine) {\n"
"   float dp = 0.5*float(a.x);\n"
"   bool discarded = false;\n"
"   if (veclong != 0) dp = (float(Pn.x-P0.x)+0.5*float(a.x))/float(veclong);\n"
"   vec4 pixout = getMeshedPixel(pixcmd, vec2(dp,float((pixcmd.idx)+0.5)/float(nbLines)), P, valid, discarded);\n"
"   if(valid) {\n"
"    if (!discarded) imageStore(outSurface,P,pixout);\n"
"    return;\n"
"   }\n"
"  }\n"
" }\n"
"}\n";

static const char vdp1_clear_mesh_f_base[] =
SHADER_VERSION_COMPUTE
"#ifdef GL_ES\n"
"precision highp float;\n"
"#endif\n"
"layout(local_size_x = %d, local_size_y = %d) in;\n"
"layout(rgba8, binding = 0) writeonly uniform image2D outMesh0;\n"
"layout(rgba8, binding = 1) writeonly uniform image2D outMesh1;\n"
"void main()\n"
"{\n"
"  ivec2 size = imageSize(outMesh0);\n"
"  ivec2 texel = ivec2(gl_GlobalInvocationID.x, gl_GlobalInvocationID.y);\n"
"  if (texel.x >= size.x || texel.y >= size.y ) return;\n"
"  imageStore(outMesh0, texel, vec4(0.0));\n"
"  imageStore(outMesh1, texel, vec4(0.0));\n"
"}\n";

static char vdp1_clear_mesh_f[ sizeof(vdp1_clear_mesh_f_base) + 64 ];

#ifdef __cplusplus
}
#endif

#endif //VDP1_PROG_COMPUTE_H
