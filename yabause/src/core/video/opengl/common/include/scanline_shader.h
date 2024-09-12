#ifndef __SCANLINE_INCLUDE_H__
#define __SCANLINE_INCLUDE_H__
static const GLchar Yglprg_blit_scanline_f[] =
"    float alpha = cos(3.1415*lineNumber[1]/decim * (vTexCoord.y+1.0)/2.0);\n" //Analog ray of CRT looks like a square sine
"    alpha = 0.5 + (alpha*alpha)/2.0;\n"
"    fragColor = vec4(fragColor.xyz*alpha, 1.0);\n";

static const GLchar Yglprg_blit_scanline_is_f[] =
"    float alpha = 1.0f;\n"
"    int coord = int(textureSize(u_Src,0).y*2.0*vTexCoord.y);\n"
"    if (mod(int(coord/scale), 2) == field) alpha = 0.3f;\n"
"    fragColor = vec4(fragColor.xyz*alpha, 1.0);\n";

static const GLchar Yglprg_blit_scanline_interlace_is_f[] =
"    float alpha = 1.0f;\n"
"    int coord = int(textureSize(u_Src,0).y*vTexCoord.y);\n"
"    if (mod(int(coord/scale), 2) == field) alpha = 0.3f;\n"
"    fragColor = vec4(fragColor.xyz*alpha, 1.0);\n";

#endif
