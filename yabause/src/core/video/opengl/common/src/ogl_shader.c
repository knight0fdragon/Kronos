/*  Copyright 2005-2006 Guillaume Duhamel
    Copyright 2005-2006 Theo Berkau
    Copyright 2011-2015 Shinya Miyamoto(devmiyax)

    This file is part of Yabause.

    Yabause is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    Yabause is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Yabause; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA
*/


//#ifdef __ANDROID__
#include <stdlib.h>
#include <math.h>
#include "ygl.h"
#include "yui.h"
#include "vidshared.h"
#include "bicubic_shader.h"
#include "scanline_shader.h"
#include "common_glshader.h"

#undef YGLLOG
#define YGLLOG //YuiMsg

#define ALIGN(A,B) (((A)%(B))? A + (B - ((A)%(B))) : A)

static int saveFB;
static void Ygl_useTmpBuffer();
static void Ygl_releaseTmpBuffer(void);
int YglBlitMosaic(u32 srcTexture, float w, float h, float * matrix, int * mosaic);
int YglBlitPerLineAlpha(u32 srcTexture, float w, float h, float * matrix, u32 perline);

extern int GlHeight;
extern int GlWidth;

static YglVdp1CommonParam _ids[PG_MAX] = { 0 };
extern void vdp1_compute_reset(void);

static void Ygl_printShaderError( GLuint shader )
{
  GLsizei bufSize;

  glGetShaderiv(shader, GL_INFO_LOG_LENGTH , &bufSize);

  if (bufSize > 1) {
    GLchar *infoLog;

    infoLog = (GLchar *)malloc(bufSize);
    if (infoLog != NULL) {
      GLsizei length;
      glGetShaderInfoLog(shader, bufSize, &length, infoLog);
      YuiMsg("Shaderlog:\n%s\n", infoLog);
      free(infoLog);
    }
  }
}
/*------------------------------------------------------------------------------------
 *  Normal Draw
 * ----------------------------------------------------------------------------------*/
const GLchar Yglprg_normal_v[] =
      SHADER_VERSION
      "uniform mat4 u_mvpMatrix;    \n"
      "layout (location = 0) in vec4 a_position;   \n"
      "layout (location = 1) in vec4 a_texcoord;   \n"
      "out  highp vec4 v_texcoord;     \n"
      "void main()       \n"
      "{ \n"
      "   gl_Position = a_position*u_mvpMatrix; \n"
      "   v_texcoord  = a_texcoord; \n"
      "} ";
const GLchar * pYglprg_vdp2_normal_v[] = {Yglprg_normal_v, NULL};

const GLchar Yglprg_normal_f[] =
SHADER_VERSION
"#ifdef GL_ES\n"
"precision highp float;\n"
"precision highp int;\n"
"#endif\n"
"in vec4 v_texcoord;\n"
"uniform highp sampler2D s_texture;\n"
"out vec4 fragColor;\n"
"void main()   \n"
"{  \n"
"  fragColor = texelFetch( s_texture, ivec2(v_texcoord.xy),0 );\n"
"}  \n";

const GLchar * pYglprg_vdp2_normal_f[] = {Yglprg_normal_f, NULL};
static int id_normal_s_texture = -1;
static int id_normal_matrix = -1;


int Ygl_uniformNormal(void * p, YglTextureManager *tm, Vdp2 *varVdp2Regs, int id)
{

  YglProgram * prg;
  prg = (YglProgram*)p;
  glEnableVertexAttribArray(0);
  glEnableVertexAttribArray(1);
  glUniform1i(id_normal_s_texture, 0);
  return 0;
}

int Ygl_cleanupNormal(void * p, YglTextureManager *tm)
{
  YglProgram * prg;
  prg = (YglProgram*)p;
  return 0;
}


//---------------------------------------------------------
// Draw pxels refering color ram
//---------------------------------------------------------

const GLchar Yglprg_normal_cram_f[] =
SHADER_VERSION
"#ifdef GL_ES\n"
"precision highp float;\n"
"precision highp int;\n"
"#endif\n"
"in vec4 v_texcoord;\n"
"uniform float u_emu_height;\n"
"uniform float u_vheight; \n"
"uniform float u_vwidth; \n"
"uniform float u_hratio; \n"
"uniform int u_interlace;\n"
"uniform int u_frame;\n"
"uniform highp sampler2D s_texture;\n"
"uniform sampler2D s_color;\n"
"uniform sampler2D s_color_map;\n"
"out vec4 fragColor;\n"
"int getColorLine(int line){\n"
"  vec4 val = texelFetch(s_color_map, ivec2(line, 0), 0);\n"
"  return (int(val.g*255.0)<<8)|int(val.r*255.0);\n"
"}\n"
"void main()\n"
"{\n"
"  ivec2 linepos; \n "
"  linepos.y = 0; \n "
"  linepos.x = int( (u_vheight-gl_FragCoord.y) * u_emu_height);\n"
"  ivec2 pos = ivec2(v_texcoord.xy);\n"
// "  if (u_interlace != 0) {\n"
// // "    pos.x = int(v_texcoord.x);\n"
// // "    pos.y = int(floor((v_texcoord.y*2.0+u_frame)/u_scale.y));\n"
// "    pos.y = (int(v_texcoord.y)&(~0x1))+u_frame;\n"
// "  }\n"
"  vec4 txindex = texelFetch( s_texture, pos ,0 );\n"
"  if(txindex.a == 0.0) { discard; }\n"
"  int msb = int(txindex.b * 255.0)&0x1; \n"
"  int tx = int(txindex.g*255.0)<<8 | int(txindex.r*255.0);\n"
"  int ty = getColorLine(int(float(linepos.x)/u_hratio));\n"
"  fragColor = texelFetch( s_color,  ivec2( tx , ty )  , 0 );\n"
"  int blue = int(fragColor.b * 255.0) & 0xFE;\n"
"  fragColor.b = float(blue|msb)/255.0;\n" //Blue LSB bit is used for special color calculation
"  fragColor.a = txindex.a; \n"
// "  if (u_interlace != 0) fragColor.g = (float(pos.y))/255.0;\n"
// "  if (u_interlace != 0) fragColor.b = (float(v_texcoord.y))/255.0;\n"
"}\n";


const GLchar * pYglprg_normal_cram_f[] = { Yglprg_normal_cram_f, NULL };
static int id_normal_cram_s_texture = -1;
static int id_normal_cram_emu_height = -1;
static int id_normal_cram_vdp2_hratio = -1;
static int id_normal_cram_vdp2_interlace = -1;
static int id_normal_cram_vdp2_frame = -1;
static int id_normal_cram_emu_width = -1;
static int id_normal_cram_vheight = -1;
static int id_normal_cram_vwidth = -1;
static int id_normal_cram_s_color = -1;
static int id_normal_cram_s_color_map = -1;
static int id_normal_cram_matrix = -1;


int Ygl_uniformNormalCram(void * p, YglTextureManager *tm, Vdp2 *varVdp2Regs, int id)
{

  YglProgram * prg;
  prg = (YglProgram*)p;
  glEnableVertexAttribArray(0);
  glEnableVertexAttribArray(1);
  glUniform1i(id_normal_cram_s_texture, 0);
  glUniform1i(id_normal_cram_s_color, 1);
  glUniform1i(id_normal_cram_s_color_map, 2);
  glUniform1f(id_normal_cram_vdp2_hratio, (float)_Ygl->vdp2hdensity);
  glUniform1i(id_normal_cram_vdp2_interlace, (_Ygl->interlace==DOUBLE_INTERLACE)?1:0);
  glUniform1i(id_normal_cram_vdp2_frame, ((varVdp2Regs->TVSTAT>>1)&0x1)==1);
  if ((id == RBG0)||(id == RBG1)){
    glUniform1f(id_normal_cram_emu_height, (float)_Ygl->rheight / (float)_Ygl->height);
    glUniform1f(id_normal_cram_emu_width, (float)_Ygl->rwidth /(float)_Ygl->width);
    glUniform1f(id_normal_cram_vheight, (float)_Ygl->height);
    glUniform1f(id_normal_cram_vwidth, (float)_Ygl->width);
  } else {
    glUniform1f(id_normal_cram_emu_height, 1.0f);
    glUniform1f(id_normal_cram_emu_width, 1.0f);
    glUniform1f(id_normal_cram_vheight, (float)_Ygl->rheight);
    glUniform1f(id_normal_cram_vwidth, (float)_Ygl->rwidth);
  }
  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D, _Ygl->cram_tex);
  glActiveTexture(GL_TEXTURE2);
  glBindTexture(GL_TEXTURE_2D, _Ygl->cram_map_tex);
  return 0;
}

int Ygl_cleanupNormalCram(void * p, YglTextureManager *tm)
{
  YglProgram * prg;
  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D, 0);
  glActiveTexture(GL_TEXTURE2);
  glBindTexture(GL_TEXTURE_2D, 0);
  prg = (YglProgram*)p;
  return 0;
}

const GLchar Yglprg_normal_cram_addcol_f[] =
SHADER_VERSION
"#ifdef GL_ES\n"
"precision highp float;\n"
"precision highp int;\n"
"#endif\n"
"in vec4 v_texcoord;\n"
"uniform highp sampler2D s_texture;\n"
"uniform sampler2D s_color;\n"
"uniform sampler2D s_color_map;\n"
"out vec4 fragColor;\n"
"int getColorLine(int line){\n"
"  vec4 val = texelFetch(s_color_map, ivec2(line, 0), 0);\n"
"  return (int(val.g*255.0)<<8)|int(val.r*255.0);\n"
"}\n"
"void main()\n"
"{\n"
"  vec4 txindex = texelFetch( s_texture, ivec2(int(v_texcoord.x),int(v_texcoord.y)) ,0 );\n"
"  if(txindex.a == 0.0) { discard; }\n"
"  vec4 fragColor = texelFetch( s_color,  ivec2( ( int(txindex.g*255.0)<<8 | int(txindex.r*255.0)) , getColorLine(0) )  , 0 );\n"
"  fragColor.a = txindex.a;\n"
"}\n";

const GLchar * pYglprg_normal_cram_addcol_f[] = { Yglprg_normal_cram_addcol_f, NULL };
static int id_normal_cram_s_texture_addcol = -1;
static int id_normal_cram_s_color_addcol = -1;
static int id_normal_cram_s_color_map_addcol = -1;
static int id_normal_cram_matrix_addcol = -1;


int Ygl_uniformAddColCram(void * p, YglTextureManager *tm, Vdp2 *varVdp2Regs, int id)
{

  YglProgram * prg;
  prg = (YglProgram*)p;
  glEnableVertexAttribArray(0);
  glEnableVertexAttribArray(1);
  glUniform1i(id_normal_cram_s_texture_addcol, 0);
  glUniform1i(id_normal_cram_s_color_addcol, 1);
  glUniform1i(id_normal_cram_s_color_map_addcol, 2);
  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D, _Ygl->cram_tex);
  glActiveTexture(GL_TEXTURE2);
  glBindTexture(GL_TEXTURE_2D, _Ygl->cram_map_tex);
  return 0;
}

static void Ygl_useTmpBuffer(){
  float col[4] = {0.0f,0.0f,0.0f,0.0f};
  // Create Screen size frame buffer
  glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &saveFB);
  if (_Ygl->tmpfbo == 0) {

    GLuint error;
    glGenTextures(1, &_Ygl->tmpfbotex);
    glBindTexture(GL_TEXTURE_2D, _Ygl->tmpfbotex);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, _Ygl->rwidth, _Ygl->rheight, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glGenFramebuffers(1, &_Ygl->tmpfbo);
    glBindFramebuffer(GL_FRAMEBUFFER, _Ygl->tmpfbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, _Ygl->tmpfbotex, 0);
  }
  // bind Screen size frame buffer
  else{
    glBindFramebuffer(GL_FRAMEBUFFER, _Ygl->tmpfbo);
    glClearBufferfv(GL_COLOR, 0, col);
  }
}

static void Ygl_releaseTmpBuffer(void) {
  glBindFramebuffer(GL_FRAMEBUFFER, saveFB);
}

int Ygl_useUpscaleBuffer(void){
  // Create Screen size frame buffer
  int up_scale = 1;
  switch (_Ygl->upmode) {
    case UP_6XBRZ:
      up_scale = 6;
      break;
    case UP_HQ4X:
    case UP_4XBRZ:
      up_scale = 4;
      break;
    default:
      up_scale = 1;
  }
  if (_Ygl->upfbo == 0) {
    GLuint error;
    glGenTextures(1, &_Ygl->upfbotex);
    glBindTexture(GL_TEXTURE_2D, _Ygl->upfbotex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, up_scale*_Ygl->rwidth, up_scale*_Ygl->rheight, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glGenFramebuffers(1, &_Ygl->upfbo);
    glBindFramebuffer(GL_FRAMEBUFFER, _Ygl->upfbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, _Ygl->upfbotex, 0);
  }
  // bind Screen size frame buffer
  else{
    glBindFramebuffer(GL_FRAMEBUFFER, _Ygl->upfbo);
  }
  return up_scale;
}

/*------------------------------------------------------------------------------------
*  Mosaic Draw
* ----------------------------------------------------------------------------------*/
int Ygl_uniformMosaic(void * p, YglTextureManager *tm, Vdp2 *varVdp2Regs, int id)
{
  YglProgram * prg;
  prg = (YglProgram*)p;

  Ygl_useTmpBuffer();
  glViewport(0, 0, _Ygl->rwidth, _Ygl->rheight);
  glScissor(0, 0, _Ygl->rwidth, _Ygl->rheight);

  if (prg->prgid == PG_VDP2_MOSAIC_CRAM ) {
    glEnableVertexAttribArray(prg->vertexp);
    glEnableVertexAttribArray(prg->texcoordp);
    glUniform1i(id_normal_cram_s_texture, 0);
    glUniform1i(id_normal_cram_s_color, 1);
    glUniform1i(id_normal_cram_s_color_map, 2);

    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, _Ygl->cram_map_tex);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, _Ygl->cram_tex);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tm->textureID);

  }
  else {
    glEnableVertexAttribArray(prg->vertexp);
    glEnableVertexAttribArray(prg->texcoordp);
    glUniform1i(id_normal_s_texture, 0);
    glBindTexture(GL_TEXTURE_2D, YglTM_vdp2->textureID);
  }

  return 0;
}

int Ygl_cleanupMosaic(void * p, YglTextureManager *tm)
{
  YglProgram * prg;
  prg = (YglProgram*)p;

  // Bind Default frame buffer
  Ygl_releaseTmpBuffer();

  // Restore Default Matrix
  glViewport(_Ygl->m_viewport[0], _Ygl->m_viewport[1], _Ygl->m_viewport[2], _Ygl->m_viewport[3]);
  glScissor(_Ygl->m_viewport[0], _Ygl->m_viewport[1], _Ygl->m_viewport[2], _Ygl->m_viewport[3]);

  // call blit method
  YglBlitMosaic(_Ygl->tmpfbotex, _Ygl->rwidth, _Ygl->rheight, prg->matrix, prg->mosaic);

  glBindTexture(GL_TEXTURE_2D, tm->textureID);

  return 0;
}

/*------------------------------------------------------------------------------------
 *  VDP2 Draw Frame buffer Operation
 * ----------------------------------------------------------------------------------*/

typedef struct  {
  int idvdp1FrameBuffer;
  int idvdp1Mesh;
  int idvdp2regs;
  int idcram;
  int idcram_map;
} DrawFrameBufferUniform;

#define MAX_FRAME_BUFFER_UNIFORM (BLIT_TEXTURE_NB_PROG)

DrawFrameBufferUniform g_draw_framebuffer_uniforms[MAX_FRAME_BUFFER_UNIFORM];

const GLchar Yglprg_vdp1_drawfb_v[] =
      SHADER_VERSION
      "layout (location = 0) in vec2 a_position;   \n"
      "layout (location = 1) in vec2 a_texcoord;   \n"
      "out vec2 v_texcoord;     \n"
      "void main()       \n"
      "{ \n"
      " gl_Position = vec4(a_position.x, a_position.y, 0.0, 1.0); \n"
      " v_texcoord  = a_texcoord; \n"
      "} ";
const GLchar * pYglprg_vdp2_blit_v[] = {Yglprg_vdp1_drawfb_v, NULL};

/*
+-+-+-+-+-+-+-+-+
|S|C|A|A|A|P|P|P|
+-+-+-+-+-+-+-+-+
S show flag
C index or direct color
A alpha index
P priority

refrence:
  hard/vdp2/hon/p09_10.htm
  hard/vdp2/hon/p12_14.htm#CCCTL_

*/
#define COMMON_START "\
in vec2 v_texcoord; \n \
out vec4 finalColor; \n \
uniform sampler2D s_back;  \n \
uniform sampler2D s_lncl;  \n \
uniform sampler2D s_lncl_off_rgb0;  \n \
uniform sampler2D s_lncl_off_rgb1;  \n \
uniform sampler2D s_vdp1FrameBuffer;\n \
uniform sampler2D s_vdp1Mesh;\n \
uniform sampler2D s_win0;  \n \
uniform sampler2D s_win1;  \n \
uniform sampler2D s_color; \n \
uniform sampler2D s_color_map; \n \
uniform sampler2D s_vdp2reg; \n \
uniform sampler2D s_perline; \n \
uniform float u_emu_height;\n \
uniform float u_vdp2_h_density;\n \
uniform vec2 u_emu_vdp1_ratio;\n \
uniform float u_emu_vdp2_width;\n \
uniform float u_vheight; \n \
uniform vec2 vdp1Ratio; \n \
uniform int ram_mode; \n \
uniform int extended_cc; \n \
uniform int u_lncl;  \n \
uniform int isRGB; \n \
uniform int isBlur; \n \
uniform int isShadow; \n \
uniform int is_perline[8];\n \
uniform int mode[7];  \n \
uniform int is_lncl_off[6]; \n \
uniform int use_sp_win; \n \
uniform int use_trans_shadow; \n \
uniform ivec2 tvSize;\n \
uniform int win_s; \n \
uniform int win_s_mode; \n \
uniform int win0; \n \
uniform int win0_mode; \n \
uniform int win1; \n \
uniform int win1_mode; \n \
uniform int win_op; \n \
uniform int nbFrame; \n \
uniform vec2 vdp1Shift; \n \
uniform mat4 rotVdp1; \n \
int PosY = int(gl_FragCoord.y);\n \
int PosX = int(gl_FragCoord.x);\n \
ivec2 getFBCoord() {\n \
 vec4 scaledPos = gl_FragCoord;\n \
 scaledPos.xy *= u_emu_vdp1_ratio;\n \
 return ivec2((rotVdp1*scaledPos).xy+vdp1Shift*vdp1Ratio) ;\n \
"

#define SAMPLER_TEX(ID) "\
uniform sampler2D s_texture"Stringify(ID)";\n \
"
//--------------------------------------------------------------------------------------------------------------
static const char vdp2blit_gl_start_f_6[] =
SHADER_VERSION
"#ifdef GL_ES\n"
"precision highp float; \n"
"#endif\n"
#ifdef DEBUG_BLIT
"out vec4 topColor; \n"
"out vec4 secondColor; \n"
"out vec4 thirdColor; \n"
"out vec4 fourthColor; \n"
#endif
"layout(origin_upper_left) in vec4 gl_FragCoord; \n"
SAMPLER_TEX(0)
SAMPLER_TEX(1)
SAMPLER_TEX(2)
SAMPLER_TEX(3)
SAMPLER_TEX(4)
SAMPLER_TEX(5)
COMMON_START
"}\n";

static const char vdp2blit_gl_start_f_5[] =
SHADER_VERSION
"#ifdef GL_ES\n"
"precision highp float; \n"
"#endif\n"
#ifdef DEBUG_BLIT
"out vec4 topColor; \n"
"out vec4 secondColor; \n"
"out vec4 thirdColor; \n"
"out vec4 fourthColor; \n"
#endif
"layout(origin_upper_left) in vec4 gl_FragCoord; \n"
SAMPLER_TEX(0)
SAMPLER_TEX(1)
SAMPLER_TEX(2)
SAMPLER_TEX(3)
SAMPLER_TEX(4)
COMMON_START
"}\n";

static const char vdp2blit_gl_start_f_4[] =
SHADER_VERSION
"#ifdef GL_ES\n"
"precision highp float; \n"
"#endif\n"
#ifdef DEBUG_BLIT
"out vec4 topColor; \n"
"out vec4 secondColor; \n"
"out vec4 thirdColor; \n"
"out vec4 fourthColor; \n"
#endif
"layout(origin_upper_left) in vec4 gl_FragCoord; \n"
SAMPLER_TEX(0)
SAMPLER_TEX(1)
SAMPLER_TEX(2)
SAMPLER_TEX(3)
COMMON_START
"}\n";

static const char vdp2blit_gl_start_f_3[] =
SHADER_VERSION
"#ifdef GL_ES\n"
"precision highp float; \n"
"#endif\n"
#ifdef DEBUG_BLIT
"out vec4 topColor; \n"
"out vec4 secondColor; \n"
"out vec4 thirdColor; \n"
"out vec4 fourthColor; \n"
#endif
"layout(origin_upper_left) in vec4 gl_FragCoord; \n"
SAMPLER_TEX(0)
SAMPLER_TEX(1)
SAMPLER_TEX(2)
COMMON_START
"}\n";

static const char vdp2blit_gl_start_f_2[] =
SHADER_VERSION
"#ifdef GL_ES\n"
"precision highp float; \n"
"#endif\n"
#ifdef DEBUG_BLIT
"out vec4 topColor; \n"
"out vec4 secondColor; \n"
"out vec4 thirdColor; \n"
"out vec4 fourthColor; \n"
#endif
"layout(origin_upper_left) in vec4 gl_FragCoord; \n"
SAMPLER_TEX(0)
SAMPLER_TEX(1)
COMMON_START
"}\n";

static const char vdp2blit_gl_start_f_1[] =
SHADER_VERSION
"#ifdef GL_ES\n"
"precision highp float; \n"
"#endif\n"
#ifdef DEBUG_BLIT
"out vec4 topColor; \n"
"out vec4 secondColor; \n"
"out vec4 thirdColor; \n"
"out vec4 fourthColor; \n"
#endif
"layout(origin_upper_left) in vec4 gl_FragCoord; \n"
SAMPLER_TEX(0)
COMMON_START
"}\n";

static const char vdp2blit_gl_start_f_0[] =
SHADER_VERSION
"#ifdef GL_ES\n"
"precision highp float; \n"
"#endif\n"
#ifdef DEBUG_BLIT
"out vec4 topColor; \n"
"out vec4 secondColor; \n"
"out vec4 thirdColor; \n"
"out vec4 fourthColor; \n"
#endif
"layout(origin_upper_left) in vec4 gl_FragCoord; \n"
COMMON_START
"}\n";

const GLchar * vdp2blit_gl_start_f[7]= {
  vdp2blit_gl_start_f_0,
  vdp2blit_gl_start_f_1,
  vdp2blit_gl_start_f_2,
  vdp2blit_gl_start_f_3,
  vdp2blit_gl_start_f_4,
  vdp2blit_gl_start_f_5,
  vdp2blit_gl_start_f_6
};

const GLchar Yglprg_vdp2_drawfb_gl_cram_f[] =
"int getVDP2Reg(int id, int line) {\n"
"  return int(texelFetch(s_vdp2reg, ivec2(id, line), 0).r*255.0);\n"
"}\n"
"FBCol getFB(int x, ivec2 addr){ \n"
"  vec4 lineCoord = vec4(gl_FragCoord.x, gl_FragCoord.y, 0.0, 0.0);\n"
"  int line = int(lineCoord.y * u_emu_height);\n";

static const GLchar vdp2blit_gl_end_f[] =
"";

static const GLchar vdp2blit_gl_final_f[] =
#ifdef DEBUG_BLIT
"  topColor = colortop;\n"
"  secondColor = colorsecond;\n"
"  thirdColor = FBTest;\n"
"  fourthColor = FBShadow;\n"
#endif
"} \n";

const GLchar * prg_input_f[PG_MAX][9];
const GLchar * prg_input_v[PG_MAX][3];

void initDrawShaderCode() {
  int nbMode = 2;
#if defined(_OGL3_)
  int maj, min;
  glGetIntegerv(GL_MAJOR_VERSION, &maj);
  glGetIntegerv(GL_MINOR_VERSION, &min);
  if ((maj <=3) && (min <=3)) {
    nbMode = 1;
  }
#endif
  initVDP2DrawCode(vdp2blit_gl_start_f, Yglprg_vdp2_drawfb_gl_cram_f, vdp2blit_gl_end_f, vdp2blit_gl_final_f);
  //VDP1 Programs
  for (int m = 0; m<nbMode; m++) {
    //Normal or tesselation mode
    for (int i = 0; i<2; i++) {
       // MSB or not MSB
      for (int j = 0; j<3; j++) {
       // Mesh, Mesh improve or None
        for (int k = 0; k<2; k++) {
          //SPD or not
          for (int k1 = 0; k1<2; k1++) {
            //END code or not
            for (int l = 0; l<14; l++) {
              //7 color calculation mode
              int index = l+14*(k1+2*(k+2*(j+3*(i+2*m))));
            }
          }
        }
      }
    }
  }
}

int YglInitDrawFrameBufferShaders(int id, int CS) {
 //printf ("Use prog %d\n", id); //16
 int arrayid = id-PG_VDP2_DRAWFRAMEBUFF_NONE;
 //printf ("getArray %d\n", arrayid); //16
  compileVDP2Prog(id, pYglprg_vdp2_blit_v, CS);
  if ( arrayid < 0 || arrayid >= MAX_FRAME_BUFFER_UNIFORM) {
    abort();
  }
  g_draw_framebuffer_uniforms[arrayid].idvdp1FrameBuffer = glGetUniformLocation(_prgid[id], (const GLchar *)"s_vdp1FrameBuffer");
  g_draw_framebuffer_uniforms[arrayid].idvdp1Mesh = glGetUniformLocation(_prgid[id], (const GLchar *)"s_vdp1Mesh");
  g_draw_framebuffer_uniforms[arrayid].idvdp2regs = glGetUniformLocation(_prgid[id], (const GLchar *)"s_vdp2reg");
  g_draw_framebuffer_uniforms[arrayid].idcram = glGetUniformLocation(_prgid[id], (const GLchar *)"s_color");
  g_draw_framebuffer_uniforms[arrayid].idcram_map = glGetUniformLocation(_prgid[id], (const GLchar *)"s_color_map");
  return 0;
}


/*------------------------------------------------------------------------------------
*  VDP2 Draw Frame buffer Operation( Shadow drawing for ADD color mode )
* ----------------------------------------------------------------------------------*/

int Ygl_uniformVDP2DrawFramebuffer(float * offsetcol, int nb_screen, Vdp2* varVdp2Regs)
{
   int arrayid;

   int pgid = setupVDP2Prog(varVdp2Regs, nb_screen, 0);

  arrayid = pgid - PG_VDP2_DRAWFRAMEBUFF_NONE;

  if (_prgid[pgid] == 0) {
    if (YglInitDrawFrameBufferShaders(pgid, 0) != 0) {
      return -1;
    }
  }
  GLUSEPROG(_prgid[pgid]);

  glEnableVertexAttribArray(0);
  glEnableVertexAttribArray(1);
  glDisableVertexAttribArray(2);
  glDisableVertexAttribArray(3);

  glUniform1i(g_draw_framebuffer_uniforms[arrayid].idcram, 11);
  glActiveTexture(GL_TEXTURE11);
  glBindTexture(GL_TEXTURE_2D, _Ygl->cram_tex);

  glUniform1i(g_draw_framebuffer_uniforms[arrayid].idcram_map, 20);
  glActiveTexture(GL_TEXTURE20);
  glBindTexture(GL_TEXTURE_2D, _Ygl->cram_map_tex);

  glUniform1i(g_draw_framebuffer_uniforms[arrayid].idvdp2regs, 12);
  glActiveTexture(GL_TEXTURE12);
  glBindTexture(GL_TEXTURE_2D, _Ygl->vdp2reg_tex);

  glUniform1i(g_draw_framebuffer_uniforms[arrayid].idvdp1FrameBuffer, 9);
  glUniform1i(g_draw_framebuffer_uniforms[arrayid].idvdp1Mesh, 19);
  return _prgid[pgid];
}

/*------------------------------------------------------------------------------------
 *  VDP2 Add Blend operaiotn
 * ----------------------------------------------------------------------------------*/
int Ygl_uniformAddBlend(void * p, YglTextureManager *tm, Vdp2 *varVdp2Regs, int id )
{
   glBlendFunc(GL_ONE,GL_ONE);
   return 0;
}

int Ygl_cleanupAddBlend(void * p, YglTextureManager *tm)
{
   glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
   return 0;
}

int YglProgramInit()
{
   YGLLOG("PG_VDP2_NORMAL\n");

   initDrawShaderCode();
   //
   if (YglInitShader(PG_VDP2_NORMAL, pYglprg_vdp2_normal_v, 1, pYglprg_vdp2_normal_f, 1) != 0)
      return -1;
//vdp2 normal looks not to be setup as it should
  id_normal_s_texture = glGetUniformLocation(_prgid[PG_VDP2_NORMAL], (const GLchar *)"s_texture");
  id_normal_matrix = glGetUniformLocation(_prgid[PG_VDP2_NORMAL], (const GLchar *)"u_mvpMatrix");

   YGLLOG("PG_VDP2_NORMAL_CRAM\n");

  if (YglInitShader(PG_VDP2_NORMAL_CRAM, pYglprg_vdp2_normal_v, 1, pYglprg_normal_cram_f, 1) != 0)
    return -1;

  id_normal_cram_s_texture = glGetUniformLocation(_prgid[PG_VDP2_NORMAL_CRAM], (const GLchar *)"s_texture");
  id_normal_cram_s_color = glGetUniformLocation(_prgid[PG_VDP2_NORMAL_CRAM], (const GLchar *)"s_color");
  id_normal_cram_s_color_map = glGetUniformLocation(_prgid[PG_VDP2_NORMAL_CRAM], (const GLchar *)"s_color_map");
  id_normal_cram_matrix = glGetUniformLocation(_prgid[PG_VDP2_NORMAL_CRAM], (const GLchar *)"u_mvpMatrix");
  id_normal_cram_emu_height = glGetUniformLocation(_prgid[PG_VDP2_NORMAL_CRAM], (const GLchar *)"u_emu_height");
  id_normal_cram_vdp2_hratio = glGetUniformLocation(_prgid[PG_VDP2_NORMAL_CRAM], (const GLchar *)"u_hratio");
  id_normal_cram_vdp2_interlace = glGetUniformLocation(_prgid[PG_VDP2_NORMAL_CRAM], (const GLchar *)"u_interlace");
  id_normal_cram_vdp2_frame = glGetUniformLocation(_prgid[PG_VDP2_NORMAL_CRAM], (const GLchar *)"u_frame");
  id_normal_cram_vheight = glGetUniformLocation(_prgid[PG_VDP2_NORMAL_CRAM], (const GLchar *)"u_vheight");
  id_normal_cram_vwidth = glGetUniformLocation(_prgid[PG_VDP2_NORMAL_CRAM], (const GLchar *)"u_vwidth");


#if 0
  YGLLOG("PG_VDP2_MOSAIC\n");
  if (YglInitShader(PG_VDP2_MOSAIC, pYglprg_vdp1_replace_v, 1, pYglprg_mosaic_f) != 0)
    return -1;
  id_mosaic_s_texture = glGetUniformLocation(_prgid[PG_VDP2_MOSAIC], (const GLchar *)"s_texture");
  id_mosaic = glGetUniformLocation(_prgid[PG_VDP2_MOSAIC], (const GLchar *)"u_mosaic");
#endif

   _prgid[PG_VDP2_MOSAIC] = _prgid[PG_VDP2_NORMAL];

   _prgid[PG_VDP2_MOSAIC_CRAM] = _prgid[PG_VDP2_NORMAL_CRAM];

   return 0;
}

void initVDPProg(YglProgram* prog, int id) {
  int ret = -1;
  int init = 0;
  prog->vaid = 0;
  prog->id = 0;

  if (_prgid[id] == 0) {
    YuiMsg("Compile program %d\n",id);
    init = 1;
    YglInitShader(id, prg_input_v[id], 2, prg_input_f[id], 8);
  }
  if (_prgid[id] == 0) {
    YuiMsg("Prog %d is not able to compile\n", id);
    abort();
  }
  if (init == 1) {
    _ids[id].sprite = glGetUniformLocation(_prgid[id], (const GLchar *)"u_sprite");
    _ids[id].fbo = glGetUniformLocation(_prgid[id], (const GLchar *)"u_fbo");
    _ids[id].texsize = glGetUniformLocation(_prgid[id], (const GLchar *)"u_texsize");
    _ids[id].mtxModelView = glGetUniformLocation(_prgid[id], (const GLchar *)"u_mvpMatrix");
    _ids[id].tex0 = glGetUniformLocation(_prgid[id], (const GLchar *)"s_texture");
    _ids[id].vaid = glGetAttribLocation(_prgid[id], (const GLchar *)"a_grcolor");
    _ids[id].vertexp = glGetAttribLocation(_prgid[id], (const GLchar *)"a_position");
    _ids[id].texcoordp = glGetAttribLocation(_prgid[id], (const GLchar *)"a_texcoord");
    _ids[id].sysclip = glGetUniformLocation(_prgid[id], (const GLchar *)"sysClip");
  }
  prog->prgid=id;
  prog->prg=_prgid[id];
  prog->vaid = _ids[id].vaid;
  prog->mtxModelView = _ids[id].mtxModelView;
  switch(id) {
    default:
      prog->vertexp = _ids[id].vertexp;
      prog->texcoordp = _ids[id].texcoordp;
  }
  prog->ids = &_ids[id];
  // YGLLOG("Compile program %d success\n",id);
}

int YglProgramChange( YglLevel * level, int prgid )
{
   YglProgram* tmp;
   YglProgram* current;

   if( level->prgcurrent >= level->prgcount)
   {
      level->prgcount++;
      tmp = (YglProgram*)malloc(sizeof(YglProgram)*level->prgcount);
      if( tmp == NULL ) return -1;
      memset(tmp,0,sizeof(YglProgram)*level->prgcount);
      memcpy(tmp,level->prg,sizeof(YglProgram)*(level->prgcount-1));
      free(level->prg);
      level->prg = tmp;

      level->prg[level->prgcurrent].currentQuad = 0;

      level->prg[level->prgcurrent].maxQuad = 12*64;
      if ((level->prg[level->prgcurrent].quads = (float *) malloc(level->prg[level->prgcurrent].maxQuad * sizeof(float))) == NULL)
         return -1;

      if ((level->prg[level->prgcurrent].textcoords = (float *) malloc(level->prg[level->prgcurrent].maxQuad * sizeof(float) * 2)) == NULL)
         return -1;

       if ((level->prg[level->prgcurrent].vertexAttribute = (float *) malloc(level->prg[level->prgcurrent].maxQuad * sizeof(float)*2)) == NULL)
         return -1;
   }
   current = &level->prg[level->prgcurrent];
   current->systemClipX2 = Vdp1Regs->systemclipX2;
   current->systemClipY2 = Vdp1Regs->systemclipY2;
   initVDPProg(current, prgid);

   if (prgid == PG_VDP2_NORMAL)
   {
     current->setupUniform = Ygl_uniformNormal;
     current->cleanupUniform = Ygl_cleanupNormal;
     current->vertexp = 0;
     current->texcoordp = 1;
     current->mtxModelView = id_normal_matrix;
   }
   else if (prgid == PG_VDP2_NORMAL_CRAM)
   {
     current->setupUniform = Ygl_uniformNormalCram;
     current->cleanupUniform = Ygl_cleanupNormalCram;
     current->vertexp = 0;
     current->texcoordp = 1;
     current->mtxModelView = id_normal_cram_matrix;

   }
   else if (prgid == PG_VDP2_MOSAIC)
   {
     current->setupUniform = Ygl_uniformMosaic;
     current->cleanupUniform = Ygl_cleanupMosaic;
     current->vertexp = 0;
     current->texcoordp = 1;
     current->mtxModelView = id_normal_matrix;

   }
   else if (prgid == PG_VDP2_MOSAIC_CRAM)
   {
     current->setupUniform = Ygl_uniformMosaic;
     current->cleanupUniform = Ygl_cleanupMosaic;
     current->vertexp = 0;
     current->texcoordp = 1;
     current->mtxModelView = id_normal_cram_matrix;
   }
   return 0;

}

//----------------------------------------------------------------------------------------
static int clear_prg = -1;

static const char vclear_img[] =
  SHADER_VERSION
  "layout (location = 0) in vec2 aPosition;   \n"
  "  \n"
  " void main(void) \n"
  " { \n"
  " gl_Position = vec4(aPosition.x, aPosition.y, 0.0, 1.0); \n"
  " } \n";


static const char fclear_img[] =
  SHADER_VERSION
  "#ifdef GL_ES\n"
  "precision highp float;       \n"
  "#endif\n"
  "layout(origin_upper_left) in vec4 gl_FragCoord; \n"
  "uniform sampler2D u_Clear;     \n"
  "out vec4 fragColor; \n"
  "void main()   \n"
  "{  \n"
"    ivec2 linepos; \n "
"    linepos.y = 0; \n "
"    linepos.x = int(gl_FragCoord.y);\n"
  "  fragColor = texelFetch( u_Clear, linepos,0 ); \n"
  "} \n";

int YglDrawBackScreen() {

  float const vertexPosition[] = {
    1.0f, -1.0f,
    -1.0f, -1.0f,
    1.0f, 1.0f,
    -1.0f, 1.0f };

  if (clear_prg == -1){
    GLuint vshader;
    GLuint fshader;
    GLint compiled, linked;

    const GLchar * vclear_img_v[] = { vclear_img, NULL };
    const GLchar * fclear_img_v[] = { fclear_img, NULL };

    clear_prg = glCreateProgram();
    if (clear_prg == 0){
      clear_prg = -1;
      return -1;
    }

    YGLLOG("DRAW_BACK_SCREEN\n");

    vshader = glCreateShader(GL_VERTEX_SHADER);
    fshader = glCreateShader(GL_FRAGMENT_SHADER);

    glShaderSource(vshader, 1, vclear_img_v, NULL);
    glCompileShader(vshader);
    glGetShaderiv(vshader, GL_COMPILE_STATUS, &compiled);
    if (compiled == GL_FALSE) {
      YGLLOG("Compile error in vertex shader.\n");
      Ygl_printShaderError(vshader);
      clear_prg = -1;
      return -1;
    }
    glShaderSource(fshader, 1, fclear_img_v, NULL);
    glCompileShader(fshader);
    glGetShaderiv(fshader, GL_COMPILE_STATUS, &compiled);
    if (compiled == GL_FALSE) {
      YGLLOG("Compile error in fragment shader.\n");
      Ygl_printShaderError(fshader);
      clear_prg = -1;
      return -1;
    }

    glAttachShader(clear_prg, vshader);
    glAttachShader(clear_prg, fshader);
    glLinkProgram(clear_prg);
    glGetProgramiv(clear_prg, GL_LINK_STATUS, &linked);
    if (linked == GL_FALSE) {
      YGLLOG("Link error..\n");
      Ygl_printShaderError(clear_prg);
      clear_prg = -1;
      return -1;
    }

    GLUSEPROG(clear_prg);
    glUniform1i(glGetUniformLocation(clear_prg, "u_Clear"), 0);
  }
  else{
    GLUSEPROG(clear_prg);
  }
  glDisable(GL_STENCIL_TEST);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_BLEND);

  glEnableVertexAttribArray(0);
  glBindBuffer(GL_ARRAY_BUFFER, _Ygl->vertexPosition_buf);
  glBufferData(GL_ARRAY_BUFFER, sizeof(vertexPosition), vertexPosition, GL_STREAM_DRAW);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, 0);
  glEnableVertexAttribArray(0);

  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, _Ygl->back_tex);
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

  // Clean up
  glDisableVertexAttribArray(0);

  return 0;
}

//----------------------------------------------------------------------------------------
static int fill_prg = -1;

static const char vfill_img[] =
  SHADER_VERSION
  "layout (location = 0) in vec2 a_position;   \n"
  "layout (location = 1) in vec2 a_texcoord;   \n"
  "out vec2 v_texcoord;     \n"
  "void main()       \n"
  "{ \n"
  " gl_Position = vec4(a_position.x, a_position.y, 0.0, 1.0); \n"
  " v_texcoord  = a_texcoord; \n"
  "} \n";


static const char ffill_img[] =
  SHADER_VERSION
  "#ifdef GL_ES\n"
  "precision highp float;       \n"
  "#endif\n"
  "layout(origin_upper_left) in vec4 gl_FragCoord; \n"
  "uniform sampler2D u_Fill;     \n"
  "in vec2 v_texcoord;     \n"
  "out vec4 fragColor; \n"
  "void main()   \n"
  "{  \n"
  "  fragColor = texture(u_Fill, v_texcoord);\n"
  "} \n";


int YglFillWithBackScreen() {

  float const vertexPosition[] = {
    1.0f, -1.0f,
    -1.0f, -1.0f,
    1.0f, 1.0f,
    -1.0f, 1.0f };
  float const textureCoord[] = {
    1.0f, 0.0f,
    0.0f, 0.0f,
    1.0f, 1.0f,
    0.0f, 1.0f
  };

  if (fill_prg == -1){
    GLuint vshader;
    GLuint fshader;
    GLint compiled, linked;

    const GLchar * vfill_img_v[] = { vfill_img, NULL };
    const GLchar * ffill_img_v[] = { ffill_img, NULL };

    fill_prg = glCreateProgram();
    if (fill_prg == 0){
      fill_prg = -1;
      return -1;
    }

    YGLLOG("DRAW_BACK_SCREEN\n");

    vshader = glCreateShader(GL_VERTEX_SHADER);
    fshader = glCreateShader(GL_FRAGMENT_SHADER);

    glShaderSource(vshader, 1, vfill_img_v, NULL);
    glCompileShader(vshader);
    glGetShaderiv(vshader, GL_COMPILE_STATUS, &compiled);
    if (compiled == GL_FALSE) {
      YGLLOG("Compile error in vertex shader.\n");
      Ygl_printShaderError(vshader);
      clear_prg = -1;
      return -1;
    }
    glShaderSource(fshader, 1, ffill_img_v, NULL);
    glCompileShader(fshader);
    glGetShaderiv(fshader, GL_COMPILE_STATUS, &compiled);
    if (compiled == GL_FALSE) {
      YGLLOG("Compile error in fragment shader.\n");
      Ygl_printShaderError(fshader);
      clear_prg = -1;
      return -1;
    }

    glAttachShader(fill_prg, vshader);
    glAttachShader(fill_prg, fshader);
    glLinkProgram(fill_prg);
    glGetProgramiv(fill_prg, GL_LINK_STATUS, &linked);
    if (linked == GL_FALSE) {
      YGLLOG("Link error..\n");
      Ygl_printShaderError(fill_prg);
      fill_prg = -1;
      return -1;
    }

    GLUSEPROG(fill_prg);
    glUniform1i(glGetUniformLocation(fill_prg, "u_Fill"), 0);
  }
  else{
    GLUSEPROG(fill_prg);
  }
  glDisable(GL_STENCIL_TEST);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_BLEND);

  glEnableVertexAttribArray(0);
  glBindBuffer(GL_ARRAY_BUFFER, _Ygl->vertexPosition_buf);
  glBufferData(GL_ARRAY_BUFFER, sizeof(vertexPosition), vertexPosition, GL_STREAM_DRAW);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, 0);
  glEnableVertexAttribArray(0);
  glEnableVertexAttribArray(1);
  glBindBuffer(GL_ARRAY_BUFFER, _Ygl->textureCoord_buf);
  glBufferData(GL_ARRAY_BUFFER, sizeof(textureCoord), textureCoord, GL_STREAM_DRAW);
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, 0);
  glEnableVertexAttribArray(1);

  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, _Ygl->back_fbotex);
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

  // Clean up
  glDisableVertexAttribArray(0);

  return 0;
}

u32 lineColOffBuf[2][704*256] = {{0},{0}};
static int lineColOffBufdirty[2] = {0};

u32 * YglGetLineColorOffsetPointer(int id, int start, int size){
  return &lineColOffBuf[id][start*_Ygl->rwidth];
}

void YglSetLineColorOffset(u32 * pbuf, int start, int size, int id){
  lineColOffBufdirty[id] = 1;
  return;
}

void YglUpdateLineColorOffset(int id){
  if (lineColOffBufdirty[id] !=0) {
    u32* buf;
    lineColOffBufdirty[id] = 0;
    glBindTexture(GL_TEXTURE_2D, _Ygl->linecolorcoef_tex[id]);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, _Ygl->rwidth, _Ygl->rheight, GL_RGBA, GL_UNSIGNED_BYTE, &lineColOffBuf[id][0]);
    glBindTexture(GL_TEXTURE_2D, 0 );
  }
  return;
}

extern vdp2rotationparameter_struct  Vdp1ParaA;

int YglBlitTexture(int* prioscreens, int* modescreens, int* isRGB, int * isBlur, int* isPerline, int* isShadow, int* lncl, GetFBFunc vdp1fb, int Win_s, int Win_s_mode, int Win0, int Win0_mode, int Win1, int Win1_mode, int Win_op, int* use_lncl_off, Vdp2 *varVdp2Regs) {
  int perLine = 0;
  int nbScreen = 6;
  int vdp2blit_prg;
  int lncl_val = 0;
  int isRGB_val = 0;
  int isBlur_val = 0;
  int isShadow_val = 0;

  float const vertexPosition[] = {
    1.0, -1.0f,
    -1.0, -1.0f,
    1.0, 1.0f,
    -1.0, 1.0f };

  float const textureCoord[] = {
    1.0f, 0.0f,
    0.0f, 0.0f,
    1.0f, 1.0f,
    0.0f, 1.0f
  };
    float offsetcol[4];
    YglMatrix vdp1Mat;

    YglLoadIdentity(&vdp1Mat);
    YglUpdateLineColorOffset(0);
    YglUpdateLineColorOffset(1);

    glBindVertexArray(_Ygl->vao);

    int id = 0;
    for (int i=0; i<nbScreen; i++) {
      if (prioscreens[i] != 0) {
        id++;
      }
    }

    vdp2blit_prg = Ygl_uniformVDP2DrawFramebuffer(offsetcol, id, varVdp2Regs );


  int gltext[21] = {GL_TEXTURE0, GL_TEXTURE1, GL_TEXTURE2, GL_TEXTURE3, GL_TEXTURE4, GL_TEXTURE5, GL_TEXTURE6, GL_TEXTURE7, GL_TEXTURE8, GL_TEXTURE9, GL_TEXTURE10, GL_TEXTURE11, GL_TEXTURE12, GL_TEXTURE13, GL_TEXTURE14, GL_TEXTURE15, GL_TEXTURE16, GL_TEXTURE17, GL_TEXTURE18, GL_TEXTURE19, GL_TEXTURE20};
  int useLnclRBG0 = 0;
  int useLnclRBG1 = 0;
  for (int i = 0; i< 6; i++) {
    if (use_lncl_off[i] != 0) {
      if (use_lncl_off[i] == _Ygl->linecolorcoef_tex[0]) useLnclRBG0 |= 1;
      if (use_lncl_off[i] == _Ygl->linecolorcoef_tex[1]) useLnclRBG1 |= 1;
      use_lncl_off[i] = (use_lncl_off[i]==_Ygl->linecolorcoef_tex[0])?1:2;
    }
  }

  for(int i=0; i<7; i++) {
    if (lncl[i] != 0) lncl_val |= 1<<i;
    if (isBlur[i] != 0) isBlur_val |= 1<<i;
  }

  for(int i=0; i<7; i++) {
    if (isRGB[i] != 0) isRGB_val |= 1<<i;
    if (isShadow[i] != 0) isShadow_val |= 1<<i;
  }

#ifdef _OGL3_
#ifdef DEBUG_BLIT
    glBindFragDataLocation(vdp2blit_prg, 1, "topColor");
    glBindFragDataLocation(vdp2blit_prg, 2, "secondColor");
    glBindFragDataLocation(vdp2blit_prg, 3, "thirdColor");
    glBindFragDataLocation(vdp2blit_prg, 4, "fourthColor");
#endif
    glBindFragDataLocation(vdp2blit_prg, 0, "finalColor");
#endif
  switch(id){
    case 6:
      glUniform1i(glGetUniformLocation(vdp2blit_prg, "s_texture5"), 5);
    case 5:
      glUniform1i(glGetUniformLocation(vdp2blit_prg, "s_texture4"), 4);
    case 4:
      glUniform1i(glGetUniformLocation(vdp2blit_prg, "s_texture3"), 3);
    case 3:
      glUniform1i(glGetUniformLocation(vdp2blit_prg, "s_texture2"), 2);
    case 2:
      glUniform1i(glGetUniformLocation(vdp2blit_prg, "s_texture1"), 1);
    case 1:
      glUniform1i(glGetUniformLocation(vdp2blit_prg, "s_texture0"), 0);
    default:
      break;
  }
  glUniform1i(glGetUniformLocation(vdp2blit_prg, "s_back"), 7);
  glUniform1i(glGetUniformLocation(vdp2blit_prg, "s_lncl"), 8);
  glUniform1i(glGetUniformLocation(vdp2blit_prg, "s_win0"), 14);
  glUniform1i(glGetUniformLocation(vdp2blit_prg, "s_win1"), 15);
  glUniform1i(glGetUniformLocation(vdp2blit_prg, "s_perline"), 16);
  glUniform1i(glGetUniformLocation(vdp2blit_prg, "s_lncl_off_rgb0"), 17);
  glUniform1i(glGetUniformLocation(vdp2blit_prg, "s_lncl_off_rgb1"), 18);
  glUniform1f(glGetUniformLocation(vdp2blit_prg, "u_emu_height"),(float)_Ygl->rheight / (float)_Ygl->height);
  glUniform1f(glGetUniformLocation(vdp2blit_prg, "u_vdp2_h_density"), _Ygl->vdp2hdensity);
  YGLLOG("All: \n\
    rheight %d\n\
    height %d\n\
    vdp1 height %d\n \
    vdp1 Ratio %f\n\
    vdp1hdensity %f\n\
    vdp2hdensity %f\n\
    ",
    _Ygl->rheight,
    _Ygl->height,
    _Ygl->vdp1height,
    _Ygl->vdp1ratio,
    _Ygl->vdp1hdensity,
    _Ygl->vdp2hdensity
    );
  YGLLOG("result => %f\n", _Ygl->vdp1ratio*_Ygl->vdp1hdensity/_Ygl->vdp2hdensity * (float)_Ygl->rheight/(float)_Ygl->height);

  glUniform2f(glGetUniformLocation(vdp2blit_prg, "u_emu_vdp1_ratio"),
    _Ygl->vdp1ratio*_Ygl->vdp1wdensity/_Ygl->vdp2wdensity * (float)_Ygl->rwidth/(float)_Ygl->width,
    _Ygl->vdp1ratio*_Ygl->vdp1hdensity/_Ygl->vdp2hdensity * (float)_Ygl->rheight/(float)_Ygl->height
  );
  glUniform1f(glGetUniformLocation(vdp2blit_prg, "u_emu_vdp2_width"),(float)(_Ygl->width) / (float)(_Ygl->rwidth));
  glUniform1f(glGetUniformLocation(vdp2blit_prg, "u_vheight"), (float)_Ygl->height);
  glUniform2f(glGetUniformLocation(vdp2blit_prg, "vdp1Ratio"), _Ygl->vdp1ratio, _Ygl->vdp1ratio);//((float)_Ygl->rwidth*(float)_Ygl->vdp1ratio * (float)_Ygl->vdp1wdensity)/((float)_Ygl->vdp1width*(float)_Ygl->vdp2wdensity), ((float)_Ygl->rheight*(float)_Ygl->vdp1ratio * (float)_Ygl->vdp1hdensity)/((float)_Ygl->vdp1height * (float)_Ygl->vdp2hdensity));
  glUniform1i(glGetUniformLocation(vdp2blit_prg, "ram_mode"), Vdp2Internal.ColorMode);
  glUniform1i(glGetUniformLocation(vdp2blit_prg, "extended_cc"), ((varVdp2Regs->CCCTL & 0x8400) == 0x400) );
  glUniform1i(glGetUniformLocation(vdp2blit_prg, "u_lncl"),lncl_val); //_Ygl->prioVa
  glUniform1i(glGetUniformLocation(vdp2blit_prg, "isRGB"), isRGB_val);
  glUniform1i(glGetUniformLocation(vdp2blit_prg, "isBlur"), isBlur_val);
  glUniform1i(glGetUniformLocation(vdp2blit_prg, "isShadow"), isShadow_val);
  glUniform1iv(glGetUniformLocation(vdp2blit_prg, "is_perline"), 8, isPerline);
  glUniform1iv(glGetUniformLocation(vdp2blit_prg, "mode"), 7, modescreens);
  glUniform1iv(glGetUniformLocation(vdp2blit_prg, "is_lncl_off"), 6, use_lncl_off);
  glUniform1i(glGetUniformLocation(vdp2blit_prg, "use_sp_win"), ((varVdp2Regs->SPCTL>>4)&0x1));
  glUniform1i(glGetUniformLocation(vdp2blit_prg, "use_trans_shadow"), ((varVdp2Regs->SDCTL>>8)&0x1));
  glUniform2i(glGetUniformLocation(vdp2blit_prg, "tvSize"), (int)(_Ygl->rwidth*_Ygl->vdp1wdensity/_Ygl->vdp2wdensity), (int)(_Ygl->rheight*_Ygl->vdp1hdensity/_Ygl->vdp2hdensity));

  glUniform1i(glGetUniformLocation(vdp2blit_prg, "win_s"), Win_s);
  glUniform1i(glGetUniformLocation(vdp2blit_prg, "win_s_mode"), Win_s_mode);
  glUniform1i(glGetUniformLocation(vdp2blit_prg, "win0"), Win0);
  glUniform1i(glGetUniformLocation(vdp2blit_prg, "win0_mode"), Win0_mode);
  glUniform1i(glGetUniformLocation(vdp2blit_prg, "win1"), Win1);
  glUniform1i(glGetUniformLocation(vdp2blit_prg, "win1_mode"), Win1_mode);
  glUniform1i(glGetUniformLocation(vdp2blit_prg, "win_op"), Win_op);
// #ifndef __LIBRETRO__
  if (_Ygl->interlace == NORMAL_INTERLACE){
    //double density interlaced or progressive _ Do not mix fields. Maybe required by double density. To check
    glUniform1i(glGetUniformLocation(vdp2blit_prg, "nbFrame"),2);
  } else {
    //Single density
    if (((varVdp2Regs->TVSTAT>>1)&0x1)==0)
    glUniform1i(glGetUniformLocation(vdp2blit_prg, "nbFrame"),1);
    else
    glUniform1i(glGetUniformLocation(vdp2blit_prg, "nbFrame"),0);
  }
// #else
  // glUniform1i(glGetUniformLocation(vdp2blit_prg, "nbFrame"),-1);
// #endif

  YglMatrix m;

  float mX = 0.0f, mY = 0.0f;
  YglLoadIdentity(&m);
  if (Vdp1Regs->TVMR & 0x02) {
    float Xsp = Vdp1ParaA.deltaXst;
    float Xp = Vdp1ParaA.Xst;
    float Ysp = Vdp1ParaA.deltaYst;
    float Yp = Vdp1ParaA.Yst;

    float dX = Vdp1ParaA.deltaX;
    float dY = Vdp1ParaA.deltaY;

    m.m[0][0] = dX;
    m.m[0][1] = dY;
    m.m[1][0] = Xsp;
    m.m[1][1] = Ysp;

    // showMatrix(&m, "Rotation");
    mX = Xp;
    mY = Yp;
  }
  glUniform2f(glGetUniformLocation(vdp2blit_prg, "vdp1Shift"), mX, mY);
  glUniformMatrix4fv(glGetUniformLocation(vdp2blit_prg, "rotVdp1"), 1, 0, (GLfloat*)m.m);

  glDisable(GL_DEPTH_TEST);
  glDisable(GL_BLEND);

  glEnableVertexAttribArray(0);
  glEnableVertexAttribArray(1);
  glBindBuffer(GL_ARRAY_BUFFER, _Ygl->vertexPosition_buf);
  glBufferData(GL_ARRAY_BUFFER, sizeof(vertexPosition), vertexPosition, GL_STREAM_DRAW);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, 0);
  glEnableVertexAttribArray(0);
  glBindBuffer(GL_ARRAY_BUFFER, _Ygl->textureCoord_buf);
  glBufferData(GL_ARRAY_BUFFER, sizeof(textureCoord), textureCoord, GL_STREAM_DRAW);
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, 0);
  glEnableVertexAttribArray(1);

  glActiveTexture(GL_TEXTURE14);
  glBindTexture(GL_TEXTURE_2D, _Ygl->window_tex[0]);
  glActiveTexture(GL_TEXTURE15);
  glBindTexture(GL_TEXTURE_2D, _Ygl->window_tex[1]);
  glActiveTexture(GL_TEXTURE16);
  glBindTexture(GL_TEXTURE_2D, _Ygl->coloroffset_tex);

  for (int i=0; i<nbScreen; i++) {
    if (prioscreens[i] != 0) {
      glActiveTexture(gltext[i]);
      glBindTexture(GL_TEXTURE_2D, prioscreens[i]);
    }
  }

  glActiveTexture(gltext[7]);
  glBindTexture(GL_TEXTURE_2D, _Ygl->back_fbotex);

  if (_Ygl->linecolorscreen_tex != 0){
    glActiveTexture(gltext[8]);
    glBindTexture(GL_TEXTURE_2D, _Ygl->linecolorscreen_tex);
  }

  if(useLnclRBG0 != 0) {
    glActiveTexture(gltext[17]);
    glBindTexture(GL_TEXTURE_2D, RBGGenerator_getLnclTexture(0));
  }

  if(useLnclRBG1 != 0) {
    glActiveTexture(gltext[18]);
    glBindTexture(GL_TEXTURE_2D, RBGGenerator_getLnclTexture(1));
  }

  if (vdp1fb != NULL) {
    glActiveTexture(GL_TEXTURE9);
    glBindTexture(GL_TEXTURE_2D, vdp1fb(0));
    glActiveTexture(GL_TEXTURE19);
    glBindTexture(GL_TEXTURE_2D, vdp1fb(1));
  }
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

  // Clean up
  for (int i = 0; i<21; i++) {
    glActiveTexture(gltext[i]);
    glBindTexture(GL_TEXTURE_2D, 0);
  }
  glDisableVertexAttribArray(0);
  glDisableVertexAttribArray(1);
  glDisable(GL_BLEND);

  return 0;
}

//--------------------------------------------------------------------------------------------------------------
static int winprio_prg = -1;

static const char winprio_v[] =
      SHADER_VERSION
      "layout (location = 0) in vec2 a_position;   \n"
      "layout (location = 1) in vec2 a_texcoord;   \n"
      "out vec2 v_texcoord;     \n"
      "void main()       \n"
      "{ \n"
      " gl_Position = vec4(a_position.x, a_position.y, 0.0, 1.0); \n"
      " v_texcoord  = a_texcoord; \n"
      "} ";

static const char winprio_f[] =
SHADER_VERSION
"#ifdef GL_ES\n"
"precision highp float; \n"
"#endif\n"
"in vec2 v_texcoord; \n"
"uniform sampler2D s_texture;  \n"
"out vec4 fragColor; \n"
"void main()   \n"
"{  \n"
"  ivec2 addr = ivec2(textureSize(s_texture, 0) * v_texcoord.st); \n"
"  fragColor = texelFetch( s_texture, addr,0 );         \n"
"  if (all(equal(fragColor.rg,vec2(0.0)))) discard;  \n"
//"  fragColor.a = 1.0;  \n"
"}  \n";

int YglBlitSimple(int texture, int blend) {
  const GLchar * fblit_winprio_v[] = { winprio_v, NULL };
  const GLchar * fblit_winprio_f[] = { winprio_f, NULL };

  float const vertexPosition[] = {
    1.0, -1.0f,
    -1.0, -1.0f,
    1.0, 1.0f,
    -1.0, 1.0f };

  float const textureCoord[] = {
    1.0f, 0.0f,
    0.0f, 0.0f,
    1.0f, 1.0f,
    0.0f, 1.0f
  };

  if (winprio_prg == -1){
    GLuint vshader;
    GLuint fshader;
    GLint compiled, linked;
    if (winprio_prg != -1) glDeleteProgram(winprio_prg);
    winprio_prg = glCreateProgram();
    if (winprio_prg == 0){
      winprio_prg = -1;
      return -1;
    }

    vshader = glCreateShader(GL_VERTEX_SHADER);
    fshader = glCreateShader(GL_FRAGMENT_SHADER);

    glShaderSource(vshader, 1, fblit_winprio_v, NULL);
    glCompileShader(vshader);
    glGetShaderiv(vshader, GL_COMPILE_STATUS, &compiled);
    if (compiled == GL_FALSE) {
      YGLLOG("Compile error in vertex shader.\n");
      Ygl_printShaderError(vshader);
      winprio_prg = -1;
      return -1;
    }

    glShaderSource(fshader, 1, fblit_winprio_f, NULL);
    glCompileShader(fshader);
    glGetShaderiv(fshader, GL_COMPILE_STATUS, &compiled);
    if (compiled == GL_FALSE) {
      YGLLOG("Compile error in fragment shader.\n");
      Ygl_printShaderError(fshader);
      winprio_prg = -1;
      abort();
    }

    glAttachShader(winprio_prg, vshader);
    glAttachShader(winprio_prg, fshader);
    glLinkProgram(winprio_prg);
    glGetProgramiv(winprio_prg, GL_LINK_STATUS, &linked);
    if (linked == GL_FALSE) {
      YGLLOG("Link error..\n");
      Ygl_printShaderError(winprio_prg);
      winprio_prg = -1;
      abort();
    }

    GLUSEPROG(winprio_prg);
    glUniform1i(glGetUniformLocation(winprio_prg, "s_texture"), 0);
  }
  else{
    GLUSEPROG(winprio_prg);
  }


  if (blend) {
    glEnable(GL_BLEND);
    glBlendEquation(GL_FUNC_ADD);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  } else {
    glDisable(GL_BLEND);
  }
  glEnableVertexAttribArray(0);
  glEnableVertexAttribArray(1);
  glBindBuffer(GL_ARRAY_BUFFER, _Ygl->vertexPosition_buf);
  glBufferData(GL_ARRAY_BUFFER, sizeof(vertexPosition), vertexPosition, GL_STREAM_DRAW);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, 0);
  glEnableVertexAttribArray(0);
  glBindBuffer(GL_ARRAY_BUFFER, _Ygl->textureCoord_buf);
  glBufferData(GL_ARRAY_BUFFER, sizeof(textureCoord), textureCoord, GL_STREAM_DRAW);
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, 0);
  glEnableVertexAttribArray(1);

  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, texture);
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

  // Clean up
  glActiveTexture(GL_TEXTURE0);
  glDisableVertexAttribArray(0);
  glDisableVertexAttribArray(1);
  glDisable(GL_BLEND);

  return 0;
}



//--------------------------------------------------------------------------------------------------------------
static int vdp1_write_prg = -1;
static int vdp1_read_prg = -1;
static GLint vdp1MtxModelView = 0;

static const char vdp1_v[] =
      SHADER_VERSION
      "layout (location = 0) in vec2 a_position;   \n"
      "layout (location = 1) in vec2 a_texcoord;   \n"
      "out  highp vec2 v_texcoord;     \n"
      "void main()       \n"
      "{ \n"
      " gl_Position = vec4(a_position.x, a_position.y, 0.0, 1.0); \n"
      "   v_texcoord  = a_texcoord; \n"
      "} ";

static const char vdp1_write_f[] =
SHADER_VERSION
"#ifdef GL_ES\n"
"precision highp float; \n"
"#endif\n"
"in highp vec2 v_texcoord; \n"
"uniform sampler2D s_texture;  \n"
"out vec4 fragColor; \n"
"void main()   \n"
"{  \n"
"  ivec2 addr; \n"
"  addr.x = int( v_texcoord.x);          \n"
"  addr.y = int(v_texcoord.y);          \n"
"  vec4 tex = texelFetch( s_texture, addr,0 );         \n"
"  if (tex.a == 0.0) discard;   \n"
"  fragColor.r = tex.r;         \n"
"  fragColor.g = tex.g;         \n"
"  fragColor.b = 0.0;         \n"
"  fragColor.a = 0.0;         \n"
"}  \n";

static const char vdp1_read_f[] =
SHADER_VERSION
"#ifdef GL_ES\n"
"precision highp float; \n"
"#endif\n"
"in highp vec2 v_texcoord; \n"
"uniform sampler2D s_texture;  \n"
"out vec4 fragColor; \n"
"void main()   \n"
"{  \n"
"  ivec2 addr; \n"
"  addr.x = int( v_texcoord.x);          \n"
"  addr.y = int(v_texcoord.y);          \n"
"  vec4 tex = texelFetch( s_texture, addr,0 );         \n"
//"  if (tex.agb == vec3(0.0)) tex.ragb = vec4(0.5, 0.0, 0.0, 0.0);   \n"
"  fragColor.r = 0.0;         \n"
"  fragColor.g = 0.0;         \n"
"  fragColor.b = tex.g;         \n"
"  fragColor.a = tex.r;         \n"
"}  \n";

int YglBlitVDP1(u32 srcTexture, float w, float h, int write) {
  const GLchar * fblit_vdp1_v[] = { vdp1_v, NULL };
  const GLchar * fblit_vdp1_write_f[] = { vdp1_write_f, NULL };
  const GLchar * fblit_vdp1_read_f[] = { vdp1_read_f, NULL };
  const int flip = 0;

  float const vertexPosition[] = {
    1.0, -1.0f,
    -1.0, -1.0f,
    1.0, 1.0f,
    -1.0, 1.0f };

  float const textureCoord[] = {
    w, h,
    0.0f, h,
    w, 0.0f,
    0.0f, 0.0f
  };
  float const textureCoordFlip[] = {
    w, 0.0f,
    0.0f, 0.0f,
    w, h,
    0.0f, h
  };

  if (vdp1_write_prg == -1){
    GLuint vshader;
    GLuint fshader;
    GLint compiled, linked;
    vdp1_write_prg = glCreateProgram();
    if (vdp1_write_prg == 0){
      vdp1_write_prg = -1;
      return -1;
    }

    YGLLOG("BLIT_VDP1_WRITE\n");

    vshader = glCreateShader(GL_VERTEX_SHADER);
    fshader = glCreateShader(GL_FRAGMENT_SHADER);

    glShaderSource(vshader, 1, fblit_vdp1_v, NULL);
    glCompileShader(vshader);
    glGetShaderiv(vshader, GL_COMPILE_STATUS, &compiled);
    if (compiled == GL_FALSE) {
      YGLLOG("Compile error in vertex shader.\n");
      Ygl_printShaderError(vshader);
      vdp1_write_prg = -1;
      return -1;
    }

    glShaderSource(fshader, 1, fblit_vdp1_write_f, NULL);
    glCompileShader(fshader);
    glGetShaderiv(fshader, GL_COMPILE_STATUS, &compiled);
    if (compiled == GL_FALSE) {
      YGLLOG("Compile error in fragment shader.\n");
      Ygl_printShaderError(fshader);
      vdp1_write_prg = -1;
      abort();
    }

    glAttachShader(vdp1_write_prg, vshader);
    glAttachShader(vdp1_write_prg, fshader);
    glLinkProgram(vdp1_write_prg);
    glGetProgramiv(vdp1_write_prg, GL_LINK_STATUS, &linked);
    if (linked == GL_FALSE) {
      YGLLOG("Link error..\n");
      Ygl_printShaderError(vdp1_write_prg);
      vdp1_write_prg = -1;
      abort();
    }
    glUniform1i(glGetUniformLocation(vdp1_write_prg, "s_texture"), 0);
  }

  if (vdp1_read_prg == -1){
    GLuint vshader;
    GLuint fshader;
    GLint compiled, linked;
    vdp1_read_prg = glCreateProgram();
    if (vdp1_read_prg == 0){
      vdp1_read_prg = -1;
      return -1;
    }

    YGLLOG("BLIT_VDP1_WRITE\n");

    vshader = glCreateShader(GL_VERTEX_SHADER);
    fshader = glCreateShader(GL_FRAGMENT_SHADER);

    glShaderSource(vshader, 1, fblit_vdp1_v, NULL);
    glCompileShader(vshader);
    glGetShaderiv(vshader, GL_COMPILE_STATUS, &compiled);
    if (compiled == GL_FALSE) {
      YGLLOG("Compile error in vertex shader.\n");
      Ygl_printShaderError(vshader);
      vdp1_read_prg = -1;
      return -1;
    }

    glShaderSource(fshader, 1, fblit_vdp1_read_f, NULL);
    glCompileShader(fshader);
    glGetShaderiv(fshader, GL_COMPILE_STATUS, &compiled);
    if (compiled == GL_FALSE) {
      YGLLOG("Compile error in fragment shader.\n");
      Ygl_printShaderError(fshader);
      vdp1_read_prg = -1;
      abort();
    }

    glAttachShader(vdp1_read_prg, vshader);
    glAttachShader(vdp1_read_prg, fshader);
    glLinkProgram(vdp1_read_prg);
    glGetProgramiv(vdp1_read_prg, GL_LINK_STATUS, &linked);
    if (linked == GL_FALSE) {
      YGLLOG("Link error..\n");
      Ygl_printShaderError(vdp1_read_prg);
      vdp1_read_prg = -1;
      abort();
    }
    glUniform1i(glGetUniformLocation(vdp1_read_prg, "s_texture"), 0);
  }

  if (write == 0){
    GLUSEPROG(vdp1_read_prg);
  } else {
    GLUSEPROG(vdp1_write_prg);
  }

  glDisable(GL_DEPTH_TEST);
  glDisable(GL_BLEND);

  glEnableVertexAttribArray(0);
  glEnableVertexAttribArray(1);
  glBindBuffer(GL_ARRAY_BUFFER, _Ygl->vertexPosition_buf);
  glBufferData(GL_ARRAY_BUFFER, sizeof(vertexPosition), vertexPosition, GL_STREAM_DRAW);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, 0);
  glEnableVertexAttribArray(0);
  if (flip == 1){
     glBindBuffer(GL_ARRAY_BUFFER, _Ygl->textureCoordFlip_buf);
     glBufferData(GL_ARRAY_BUFFER, sizeof(textureCoordFlip), textureCoordFlip, GL_STREAM_DRAW);
     glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, 0);
     glEnableVertexAttribArray(1);
  }
  else{
     glBindBuffer(GL_ARRAY_BUFFER, _Ygl->textureCoord_buf);
     glBufferData(GL_ARRAY_BUFFER, sizeof(textureCoord), textureCoord, GL_STREAM_DRAW);
     glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, 0);
     glEnableVertexAttribArray(1);
  }

  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, srcTexture);
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

  // Clean up
  glActiveTexture(GL_TEXTURE0);
  glDisableVertexAttribArray(0);
  glDisableVertexAttribArray(1);

  return 0;
}
static u32 write_fb[2][512*256];
void vdp1_write_gl() {
  GLenum DrawBuffers[2]= {GL_COLOR_ATTACHMENT0,GL_COLOR_ATTACHMENT2};
  VIDCore->setupFrame();
  glBindTexture(GL_TEXTURE_2D, _Ygl->vdp1AccessTex[_Ygl->drawframe]);
  glBindFramebuffer(GL_FRAMEBUFFER, _Ygl->vdp1fbo);
  glDrawBuffers(1, (const GLenum*)&DrawBuffers[_Ygl->drawframe]);
  glViewport(0,0, _Ygl->vdp1width, _Ygl->vdp1height);
  YglBlitVDP1(_Ygl->vdp1AccessTex[_Ygl->drawframe], 512, 256, 1);
  // clean up
  glBindFramebuffer(GL_FRAMEBUFFER, _Ygl->default_fbo);
}

u32* vdp1_read_gl(int frame) {
  glBindFramebuffer(GL_FRAMEBUFFER, _Ygl->vdp1AccessFB[frame]);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, _Ygl->vdp1AccessTex[frame], 0);
  glViewport(0,0,512,256);
  YglBlitVDP1(_Ygl->vdp1FrameBuff[_Ygl->drawframe*2], _Ygl->vdp1width, _Ygl->vdp1height, 0);
  glReadPixels(0, 0, 512, 256, GL_RGBA, GL_UNSIGNED_BYTE, &write_fb[frame][0]);
  glBindFramebuffer(GL_FRAMEBUFFER, _Ygl->default_fbo);
	return &write_fb[frame][0];
}

//----------------------------------------------------------------------------------------
static int blit_prg = -1;
static int blit_mode = -1;
static int u_w = -1;
static int u_h = -1;
static int u_l = -1;
static int u_d = -1;
static int u_f = -1;
static int u_s = -1;
static int outputSize = -1;
static int inputSize = -1;

static const char vblit_img[] =
  SHADER_VERSION
  "layout (location = 0) in vec2 aPosition;   \n"
  "layout (location = 1) in vec2 aTexCoord;   \n"
  "out  highp vec2 vTexCoord;     \n"
  "  \n"
  " void main(void) \n"
  " { \n"
  " vTexCoord = aTexCoord;     \n"
  " gl_Position = vec4(aPosition.x, aPosition.y, 0.0, 1.0); \n"
  " } \n";


static const char fblit_head[] =
  SHADER_VERSION
  "#ifdef GL_ES\n"
  "precision highp float;       \n"
  "#endif\n"
  "uniform float fWidth; \n"
  "uniform float fHeight; \n"
  "uniform vec2 lineNumber; \n"
  "uniform float decim; \n"
  "uniform int field; \n"
  "uniform int scale; \n"
  "in highp vec2 vTexCoord;     \n"
  "uniform sampler2D u_Src;     \n"
  "out vec4 fragColor; \n";

static const char fblit_img[] =
  "void main()   \n"
  "{   \n"
"	fragColor = Filter( u_Src, vTexCoord ); \n";

  static const char fblit_retro_img[] =
  "void main()\n"
  "{\n"
  " fragColor = Filter( u_Src, vTexCoord ); \n";

static const char fblit_img_end[] =
  "} \n";

static const char fblitnear_img[] =
  "vec4 Filter( sampler2D textureSampler, vec2 TexCoord ) \n"
  "{ \n"
  "     return texture( textureSampler, TexCoord ) ; \n"
  "} \n";
static const char fblitnear_interlace_img[] =
  "vec4 Filter( sampler2D textureSampler, vec2 TexCoord ) \n"
  "{ \n"
  "     ivec2 texSize = textureSize(textureSampler,0);\n"
  "     ivec2 coord = ivec2(texSize*TexCoord);\n"
  "     return texelFetch( textureSampler, coord, 0);\n"
  "} \n";

  static const char fboadaptative_img[] =
    "vec4 Filter( sampler2D textureSampler, vec2 TexCoord ) \n"
    "{ \n"
    "    ivec2 coord = ivec2(vec2(textureSize(textureSampler,0))*TexCoord);\n"
    "    vec4 cur = texture( textureSampler, TexCoord ); \n"
    "    if ((int(coord.y/scale)&0x1)==0x1) {\n"
    "     vec4 val1 = texelFetch( textureSampler, ivec2(coord.x,coord.y-scale) , 0 ); \n"
    "     vec4 val2 = texelFetch( textureSampler, ivec2(coord.x,coord.y+scale) , 0 ); \n"
    "     vec4 interpol = mix( val1, val2,vec4(0.5)); \n"
    "     if (distance(val1, val2) > 0.5f) return interpol;\n"
    "     if (distance(cur, interpol) < 0.15f) return cur; else return interpol;\n"
    "    }\n"
    "    else"
    "     return cur; \n"
    "} \n";

    static const char fboadaptative_debug_img[] =
      "vec4 Filter( sampler2D textureSampler, vec2 TexCoord ) \n"
      "{ \n"
      "    ivec2 coord = ivec2(vec2(textureSize(textureSampler,0))*TexCoord);\n"
      "    vec4 cur = texture( textureSampler, TexCoord ); \n"
      "    if ((int(coord.y/scale)&0x1)==0x1) {\n"
      "     vec4 val1 = texelFetch( textureSampler, ivec2(coord.x,coord.y-scale) , 0 ); \n"
      "     vec4 val2 = texelFetch( textureSampler, ivec2(coord.x,coord.y+scale) , 0 ); \n"
      "     vec4 interpol = mix( val1, val2,vec4(0.5)); \n"
      "     if (distance(val1, val2) > 0.5f) return vec4(0.0,1.0,0.0,1.0);\n"
      "     if (distance(cur, interpol) < 0.15f) return vec4(1.0,0.0,0.0,1.0); else return vec4(0.0,1.0,0.0,1.0);\n"
      "    }\n"
      "    else"
      "     return cur; \n"
      "} \n";

    static const char fbobob_img[] =
      "vec4 Filter( sampler2D textureSampler, vec2 TexCoord ) \n"
      "{ \n"
      "    ivec2 coord = ivec2(vec2(textureSize(textureSampler,0))*TexCoord);\n"
      "    coord.y = (((coord.y/scale)&~0x1) + field)*scale;\n"
      "    return texelFetch( textureSampler, ivec2(coord.x,coord.y) , 0 ); \n"
      "} \n";


static const char fblitbilinear_img[] =
  "// Function to get a texel data from a texture with GL_NEAREST property. \n"
  "// Bi-Linear interpolation is implemented in this function with the  \n"
  "// help of nearest four data. \n"
  "vec4 Filter( sampler2D textureSampler_i, vec2 texCoord_i ) \n"
  "{ \n"
  "	float texelSizeX = 1.0 / fWidth; //size of one texel  \n"
  "	float texelSizeY = 1.0 / fHeight; //size of one texel  \n"
  "	int nX = int( texCoord_i.x * fWidth ); \n"
  "	int nY = int( texCoord_i.y * fHeight ); \n"
  "	vec2 texCoord_New = vec2( ( float( nX ) + 0.5 ) / fWidth, ( float( nY ) + 0.5 ) / fHeight ); \n"
  "	// Take nearest two data in current row. \n"
  "    vec4 p0q0 = texture(textureSampler_i, texCoord_New); \n"
  "    vec4 p1q0 = texture(textureSampler_i, texCoord_New + vec2(texelSizeX, 0)); \n"
  "	// Take nearest two data in bottom row. \n"
  "    vec4 p0q1 = texture(textureSampler_i, texCoord_New + vec2(0, texelSizeY)); \n"
  "    vec4 p1q1 = texture(textureSampler_i, texCoord_New + vec2(texelSizeX , texelSizeY)); \n"
  "    float a = fract( texCoord_i.x * fWidth ); // Get Interpolation factor for X direction. \n"
  "	// Fraction near to valid data. \n"
  "	// Interpolation in X direction. \n"
  "    vec4 pInterp_q0 = mix( p0q0, p1q0, a ); // Interpolates top row in X direction. \n"
  "    vec4 pInterp_q1 = mix( p0q1, p1q1, a ); // Interpolates bottom row in X direction. \n"
  "    float b = fract( texCoord_i.y * fHeight ); // Get Interpolation factor for Y direction. \n"
  "    return mix( pInterp_q0, pInterp_q1, b ); // Interpolate in Y direction. \n"
  "} \n";

/////

GLuint textureCoord_buf[2] = {0,0};

static int last_upmode = 0;
static InterlaceMode last_interlace = NORMAL_INTERLACE;

int YglBlitFramebuffer(u32 srcTexture, float w, float h, float dispw, float disph) {
  float width = w;
  float height = h;
  int decim;
  u32 tex = srcTexture;
  const GLchar * fblit_img_retro_v[] = { fblit_head, fblitnear_img, fblit_retro_img, fblit_img_end, NULL };
  const GLchar * fblit_img_retro_interlace_v[] = { fblit_head, fblitnear_interlace_img, fblit_retro_img, fblit_img_end, NULL };
  const GLchar * fblit_img_v[] = { fblit_head, fblitnear_img, fblit_img, fblit_img_end, NULL };
  const GLchar * fblit_img_interlace_v[] = { fblit_head, fblitnear_interlace_img, fblit_img, fblit_img_end, NULL };
  const GLchar * fblitbilinear_img_v[] = { fblit_head, fblitnear_img, fblit_img, fblit_img_end, NULL };
  const GLchar * fblitbilinear_img_interlace_v[] = { fblit_head, fblitnear_interlace_img, fblit_img, fblit_img_end, NULL };
  const GLchar * fblitbicubic_img_v[] = { fblit_head, fblitbicubic_img, fblit_img, fblit_img_end, NULL };
  const GLchar * fblit_img_scanline_is_v[] = { fblit_head, fblitnear_img, fblit_img, Yglprg_blit_scanline_is_f, fblit_img_end, NULL };
  const GLchar * fblit_img_scanline_is_interlace_v[] = { fblit_head, fblitnear_interlace_img, fblit_img, Yglprg_blit_scanline_interlace_is_f, fblit_img_end, NULL };

  const GLchar * fblit_adaptative_img_v[] = { fblit_head, fboadaptative_img, fblit_img, fblit_img_end, NULL };
  const GLchar * fblit_adaptative_debug_img_v[] = { fblit_head, fboadaptative_debug_img, fblit_img, fblit_img_end, NULL };

  const GLchar * fblit_bob_img_v[] = { fblit_head, fbobob_img, fblit_img, fblit_img_end, NULL };

  int aamode = _Ygl->aamode;

  float const vertexPosition[] = {
    1.0f, -1.0f,
    -1.0f, -1.0f,
    1.0f, 1.0f,
    -1.0f, 1.0f };

  float const textureCoord[16] = {
    1.0f, 0.0f,
    0.0f, 0.0f,
    1.0f, 1.0f,
    0.0f, 1.0f,
    0.0f, 0.0f,
    0.0f, 1.0f,
    1.0f, 0.0f,
    1.0f, 1.0f };

  float nbLines = h;//yabsys.IsPal?625.0f:525.0f;
#ifdef __LIBRETRO__
  nbLines = height;
#endif
  if ((_Ygl->stretch == INTEGER_RATIO) || (_Ygl->stretch == INTEGER_RATIO_FULL)) nbLines = height;

  if (_Ygl->upmode != UP_NONE) {
    int scale = 1;
    if (last_upmode != _Ygl->upmode) {
      if (_Ygl->upfbotex != 0) glDeleteTextures(1, &_Ygl->upfbotex);
      if (_Ygl->upfbo != 0) glDeleteFramebuffers(1, &_Ygl->upfbo);
      _Ygl->upfbo = 0;
      _Ygl->upfbotex = 0;
      last_upmode = _Ygl->upmode;
    }
    scale = Ygl_useUpscaleBuffer();
    glGetIntegerv( GL_VIEWPORT, _Ygl->m_viewport );
    glViewport(0, 0, scale*_Ygl->rwidth, scale*_Ygl->rheight);
    glScissor(0, 0, scale*_Ygl->rwidth, scale*_Ygl->rheight);
    YglUpscaleFramebuffer(srcTexture, _Ygl->upfbo, _Ygl->rwidth, _Ygl->rheight, _Ygl->width, _Ygl->height);
    glViewport(_Ygl->m_viewport[0], _Ygl->m_viewport[1], _Ygl->m_viewport[2], _Ygl->m_viewport[3]);
    glScissor(_Ygl->m_viewport[0], _Ygl->m_viewport[1], _Ygl->m_viewport[2], _Ygl->m_viewport[3]);
    tex = _Ygl->upfbotex;
    width = scale*_Ygl->rwidth;
    height = scale*_Ygl->rheight;
  }
  //if ((aamode == AA_NONE) && ((w != dispw) || (h != disph))) aamode = AA_BILINEAR_FILTER;
  if (_Ygl->interlace == NORMAL_INTERLACE) {
    if ((aamode >= AA_ADAPTATIVE_FILTER) && (aamode < AA_SCANLINE)) {
      aamode = AA_NONE;
    }
  }
  if ((blit_prg == -1) || (blit_mode != aamode) || (last_interlace != _Ygl->interlace)){
    GLuint vshader;
    GLuint fshader;
    GLint compiled, linked;
    const GLchar * vblit_img_v[] = { vblit_img, NULL };
    if (blit_prg != -1) glDeleteProgram(blit_prg);
    blit_prg = glCreateProgram();
    if (blit_prg == 0){
      blit_prg = -1;
      return -1;
    }

    blit_mode = aamode;
    last_interlace = _Ygl->interlace;

    YGLLOG("BLIT_FRAMEBUFFER\n");

    vshader = glCreateShader(GL_VERTEX_SHADER);
    fshader = glCreateShader(GL_FRAGMENT_SHADER);

    glShaderSource(vshader, 1, vblit_img_v, NULL);
    glCompileShader(vshader);
    glGetShaderiv(vshader, GL_COMPILE_STATUS, &compiled);
    if (compiled == GL_FALSE) {
      YGLLOG("Compile error in vertex shader.\n");
      Ygl_printShaderError(vshader);
      blit_prg = -1;
      return -1;
    }
#ifndef __LIBRETRO__
    switch(aamode) {
      case AA_NONE:
        if (_Ygl->interlace == NORMAL_INTERLACE)
          glShaderSource(fshader, 4, fblit_img_v, NULL);
        else
          glShaderSource(fshader, 4, fblit_img_interlace_v, NULL);
        break;
      case AA_BILINEAR_FILTER:
        if (_Ygl->interlace == NORMAL_INTERLACE)
          glShaderSource(fshader, 4, fblitbilinear_img_v, NULL);
        else
          glShaderSource(fshader, 4, fblitbilinear_img_interlace_v, NULL);
        break;
      case AA_BICUBIC_FILTER:
        glShaderSource(fshader, 4, fblitbicubic_img_v, NULL);
        break;
      case AA_ADAPTATIVE_FILTER:
        glShaderSource(fshader, 4, fblit_adaptative_img_v, NULL);
        break;
      case AA_ADAPTATIVE_DEBUG_FILTER:
        glShaderSource(fshader, 4, fblit_adaptative_debug_img_v, NULL);
        break;
      case AA_BOB_FILTER:
        glShaderSource(fshader, 4, fblit_bob_img_v, NULL);
        break;
      case AA_SCANLINE:
        if (_Ygl->interlace == NORMAL_INTERLACE)
          glShaderSource(fshader, 5, fblit_img_scanline_is_v, NULL);
        else
          glShaderSource(fshader, 5, fblit_img_scanline_is_interlace_v, NULL);
        break;
    }
#else
    if (_Ygl->interlace == NORMAL_INTERLACE)
      glShaderSource(fshader, 4, fblit_img_retro_v, NULL);
    else
      glShaderSource(fshader, 4, fblit_img_retro_interlace_v, NULL);
#endif
    glCompileShader(fshader);
    glGetShaderiv(fshader, GL_COMPILE_STATUS, &compiled);
    if (compiled == GL_FALSE) {
      YuiMsg("Compile error in fragment shader.\n");
      Ygl_printShaderError(fshader);
      blit_prg = -1;
	  return -1;
    }

    glAttachShader(blit_prg, vshader);
    glAttachShader(blit_prg, fshader);
    glLinkProgram(blit_prg);
    glGetProgramiv(blit_prg, GL_LINK_STATUS, &linked);
    if (linked == GL_FALSE) {
		YuiMsg("Link error..\n");
      Ygl_printShaderError(blit_prg);
      blit_prg = -1;
	  return -1;
    }

    GLUSEPROG(blit_prg);
    glUniform1i(glGetUniformLocation(blit_prg, "u_Src"), 0);
    u_w = glGetUniformLocation(blit_prg, "fWidth");
    u_h = glGetUniformLocation(blit_prg, "fHeight");
    u_l = glGetUniformLocation(blit_prg, "lineNumber");
    u_d = glGetUniformLocation(blit_prg, "decim");
    u_f = glGetUniformLocation(blit_prg, "field");
    u_s = glGetUniformLocation(blit_prg, "scale");
  }
  else{
    GLUSEPROG(blit_prg);
  }

  glDisable(GL_DEPTH_TEST);
  glDisable(GL_BLEND);

  glEnableVertexAttribArray(0);
  glEnableVertexAttribArray(1);
  glBindBuffer(GL_ARRAY_BUFFER, _Ygl->vertexPosition_buf);
  glBufferData(GL_ARRAY_BUFFER, sizeof(vertexPosition), vertexPosition, GL_STREAM_DRAW);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, 0);
  glEnableVertexAttribArray(0);
#ifndef __LIBRETRO__
  u32 isRotated = yabsys.isRotated;
#else
  u32 isRotated = 0;
#endif
  if (textureCoord_buf[isRotated] == 0)
     glGenBuffers(1, &textureCoord_buf[isRotated]);
  glBindBuffer(GL_ARRAY_BUFFER, textureCoord_buf[isRotated]);
  glBufferData(GL_ARRAY_BUFFER, 8*sizeof(float), &textureCoord[isRotated * 8], GL_STREAM_DRAW);
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, 0);
  glEnableVertexAttribArray(1);
  glUniform1f(u_w, width);
  glUniform1f(u_h, height);
  glUniform2f(u_l, nbLines, disph);
  decim = (disph + nbLines) / nbLines;
  if (decim < 2) decim = 2;
  glUniform1f(u_d, (float)decim);
  glUniform1i(u_f, (Vdp2Regs->TVSTAT>>1)&0x1);
  glUniform1i(u_s, _Ygl->vdp1ratio);

  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, tex);
  if (aamode == AA_BILINEAR_FILTER) {
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  } else {
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  }
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

  // Clean up
  glActiveTexture(GL_TEXTURE0);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glDisableVertexAttribArray(0);
  glDisableVertexAttribArray(1);
  glBindFramebuffer(GL_FRAMEBUFFER, _Ygl->default_fbo);

  return 0;
}

/*
hard/vdp2/hon/p12_13.htm
*/

const GLchar mosaic_blit_v[] =
SHADER_VERSION
"uniform mat4 u_mvpMatrix;    \n"
"layout (location = 0) in vec4 a_position;   \n"
"layout (location = 1) in vec2 a_texcoord;   \n"
"out  highp vec2 v_texcoord;     \n"
"void main()       \n"
"{ \n"
"   gl_Position = a_position*u_mvpMatrix; \n"
"   v_texcoord  = a_texcoord; \n"
"} ";

const GLchar mosaic_blit_f[] =
SHADER_VERSION
"#ifdef GL_ES\n"
"precision highp float; \n"
"#endif\n"
"in highp vec2 v_texcoord; \n"
"uniform sampler2D u_Src;  \n"
"uniform float u_tw; \n"
"uniform float u_th; \n"
"uniform ivec2 u_mosaic;			 \n"
"out vec4 fragColor; \n"
"void main()   \n"
"{  \n"
"  ivec2 addr; \n"
"  addr.x = int(u_tw * v_texcoord.x);          \n"
"  addr.y = int(u_th) - int(u_th * v_texcoord.y);          \n"
"  addr.x = addr.x / u_mosaic.x * u_mosaic.x;    \n"
"  addr.y = addr.y / u_mosaic.y * u_mosaic.y;    \n"
"  vec4 txcol = texelFetch( u_Src, addr,0 ) ;      \n"
"  if(txcol.a > 0.0)\n      "
"     fragColor = txcol; \n  "
"  else \n      "
"     discard;\n"
"}  \n";

static int mosaic_prg = -1;
static int u_mosaic_mtxModelView = -1;
static int u_mosaic_tw = -1;
static int u_mosaic_th = -1;
static int u_mosaic = -1;

int YglBlitMosaic(u32 srcTexture, float w, float h, GLfloat* matrix, int * mosaic) {
  float vb[] = { 0, 0,
    2.0, 0.0,
    2.0, 2.0,
    0, 2.0, };

  float tb[] = { 0.0, 0.0,
    1.0, 0.0,
    1.0, 1.0,
    0.0, 1.0 };

  vb[0] = 0;
  vb[1] = 0 - 1.0;
  vb[2] = w;
  vb[3] = 0 - 1.0;
  vb[4] = w;
  vb[5] = h - 1.0;
  vb[6] = 0;
  vb[7] = h - 1.0;

  if (mosaic_prg == -1){
    GLuint vshader;
    GLuint fshader;
    GLint compiled, linked;

    const GLchar * vblit_img_v[] = { mosaic_blit_v, NULL };
    const GLchar * fblit_img_v[] = { mosaic_blit_f, NULL };

    mosaic_prg = glCreateProgram();
    if (mosaic_prg == 0) {
      mosaic_prg = -1;
      return -1;
    }

    YGLLOG("BLIT_MOSAIC\n");

    vshader = glCreateShader(GL_VERTEX_SHADER);
    fshader = glCreateShader(GL_FRAGMENT_SHADER);

    glShaderSource(vshader, 1, vblit_img_v, NULL);
    glCompileShader(vshader);
    glGetShaderiv(vshader, GL_COMPILE_STATUS, &compiled);
    if (compiled == GL_FALSE) {
      YGLLOG("Compile error in vertex shader.\n");
      Ygl_printShaderError(vshader);
      mosaic_prg = -1;
      return -1;
    }

    glShaderSource(fshader, 1, fblit_img_v, NULL);
    glCompileShader(fshader);
    glGetShaderiv(fshader, GL_COMPILE_STATUS, &compiled);
    if (compiled == GL_FALSE) {
      YGLLOG("Compile error in fragment shader.\n");
      Ygl_printShaderError(fshader);
      mosaic_prg = -1;
      return -1;
    }

    glAttachShader(mosaic_prg, vshader);
    glAttachShader(mosaic_prg, fshader);
    glLinkProgram(mosaic_prg);
    glGetProgramiv(mosaic_prg, GL_LINK_STATUS, &linked);
    if (linked == GL_FALSE) {
      YGLLOG("Link error..\n");
      Ygl_printShaderError(mosaic_prg);
      mosaic_prg = -1;
      return -1;
    }

    glUniform1i(glGetUniformLocation(mosaic_prg, "u_Src"), 0);
    u_mosaic_mtxModelView = glGetUniformLocation(mosaic_prg, (const GLchar *)"u_mvpMatrix");
    u_mosaic_tw = glGetUniformLocation(mosaic_prg, "u_tw");
    u_mosaic_th = glGetUniformLocation(mosaic_prg, "u_th");
    u_mosaic = glGetUniformLocation(mosaic_prg, "u_mosaic");

  }
  else{
    GLUSEPROG(mosaic_prg);
  }

  glEnableVertexAttribArray(0);
  glEnableVertexAttribArray(1);
  glBindBuffer(GL_ARRAY_BUFFER, _Ygl->vb_buf);
  glBufferData(GL_ARRAY_BUFFER, sizeof(vb), vb, GL_STREAM_DRAW);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, 0);
  glEnableVertexAttribArray(0);
  glBindBuffer(GL_ARRAY_BUFFER, _Ygl->tb_buf);
  glBufferData(GL_ARRAY_BUFFER, sizeof(tb), tb, GL_STREAM_DRAW);
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, 0);
  glEnableVertexAttribArray(1);
  glUniformMatrix4fv(u_mosaic_mtxModelView, 1, GL_FALSE, matrix);
  glUniform1f(u_mosaic_tw, w);
  glUniform1f(u_mosaic_th, h);
  glUniform2iv(u_mosaic, 1, mosaic);

  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, srcTexture);
  glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

  // Clean up
  glActiveTexture(GL_TEXTURE0);
  glDisableVertexAttribArray(0);
  glDisableVertexAttribArray(1);

  return 0;
}

void Ygl_prog_Destroy(void) {
  if (clear_prg != -1)      glDeleteProgram(clear_prg);       clear_prg = -1;
  if (winprio_prg != -1)    glDeleteProgram(winprio_prg);     winprio_prg = -1;
  if (vdp1_write_prg != -1) glDeleteProgram(vdp1_write_prg);  vdp1_write_prg = -1;
  if (vdp1_read_prg != -1)  glDeleteProgram(vdp1_read_prg);   vdp1_read_prg = -1;
  if (blit_prg != -1)       glDeleteProgram(blit_prg);        blit_prg = -1;
  if (mosaic_prg != -1)     glDeleteProgram(mosaic_prg);      mosaic_prg = -1;
  for(int i = 0; i<PG_MAX; i++) {
    if(_prgid[i] != 0) {
      glDeleteProgram(_prgid[i]);
      for(int j = i+1; j<PG_MAX; j++)
        if (_prgid[i] == _prgid[j]) _prgid[j] = 0;
      _prgid[i] = 0;
    }
  }
  vdp1_compute_reset();
}
