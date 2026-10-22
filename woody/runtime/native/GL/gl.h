#ifndef WOODY_NATIVE_GL_H
#define WOODY_NATIVE_GL_H
/* Numeric vocabulary retained for the engine's local GU frontend. */
#define GL_INVALID_VALUE 0x0501
#define GL_LINES 0x0001
#define GL_LINE_STRIP 0x0003
#define GL_NEAREST_MIPMAP_NEAREST 0x2700
#define GL_NEAREST_MIPMAP_LINEAR 0x2702
#include <stddef.h>
#include <stdint.h>
#define APIENTRY
#define GLAPIENTRY
#define GLAPI extern
typedef unsigned int GLenum, GLuint, GLbitfield;
typedef int GLint, GLsizei;
typedef unsigned char GLboolean, GLubyte;
typedef unsigned short GLushort;
typedef float GLfloat, GLclampf;
typedef double GLdouble, GLclampd;
typedef void GLvoid;
typedef ptrdiff_t GLsizeiptrARB;
#define GL_ADD 0x0104
#define GL_ALL_ATTRIB_BITS 0x000FFFFF
#define GL_ALPHA_TEST 0x0BC0
#define GL_ALWAYS 0x0207
#define GL_ARRAY_BUFFER_ARB 0x8892
#define GL_BGRA_EXT 0x80E1
#define GL_BLEND 0x0BE2
#define GL_BLEND_DST 0x0BE0
#define GL_BLEND_SRC 0x0BE1
#define GL_BYTE 0x1400
#define GL_CLAMP 0x2900
#define GL_CLAMP_TO_EDGE 0x812F
#define GL_COLOR_ARRAY 0x8076
#define GL_COLOR_BUFFER_BIT 0x00004000
#define GL_COLOR_CLEAR_VALUE 0x0C22
#define GL_COLOR_INDEX8_EXT 0x80E5
#define GL_COLOR_MATERIAL 0x0B57
#define GL_COLOR_WRITEMASK 0x0C23
#define GL_COMBINE 0x8570
#define GL_COMBINE_ALPHA 0x8572
#define GL_COMBINE_ALPHA_ARB 0x8572
#define GL_COMBINE_ARB 0x8570
#define GL_COMBINE_RGB 0x8571
#define GL_COMBINE_RGB_ARB 0x8571
#define GL_COMPILE_STATUS 0x8B81
#define GL_CULL_FACE 0x0B44
#define GL_DECAL 0x2101
#define GL_DEPTH_BUFFER_BIT 0x00000100
#define GL_DEPTH_COMPONENT 0x1902
#define GL_DEPTH_COMPONENT24 0x81A6
#define GL_DEPTH_TEST 0x0B71
#define GL_DEPTH_TEXTURE_MODE 0x884B
#define GL_DEPTH_WRITEMASK 0x0B72
#define GL_DITHER 0x0BD0
#define GL_DST_COLOR 0x0306
#define GL_ELEMENT_ARRAY_BUFFER_ARB 0x8893
#define GL_EQUAL 0x0202
#define GL_EXTENSIONS 0x1F03
#define GL_FALSE 0x0
#define GL_FILL 0x1B02
#define GL_FLOAT 0x1406
#define GL_FOG 0x0B60
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_FRONT_AND_BACK 0x0408
#define GL_GEQUAL 0x0206
#define GL_GREATER 0x0204
#define GL_INCR 0x1E02
#define GL_KEEP 0x1E00
#define GL_LEQUAL 0x0203
#define GL_LESS 0x0201
#define GL_LIGHTING 0x0B50
#define GL_LINE 0x1B01
#define GL_LINE_SMOOTH 0x0B20
#define GL_LINEAR 0x2601
#define GL_LINEAR_MIPMAP_LINEAR 0x2703
#define GL_LINEAR_MIPMAP_NEAREST 0x2701
#define GL_LINK_STATUS 0x8B82
#define GL_LUMINANCE 0x1909
#define GL_MAX_TEXTURE_SIZE 0x0D33
#define GL_MODELVIEW 0x1700
#define GL_MODULATE 0x2100
#define GL_MULTISAMPLE 0x809D
#define GL_NEAREST 0x2600
#define GL_NEVER 0x0200
#define GL_NO_ERROR 0x0
#define GL_NONE 0x0
#define GL_NORMAL_ARRAY 0x8075
#define GL_NORMALIZE 0x0BA1
#define GL_NOTEQUAL 0x0205
#define GL_ONE 0x1
#define GL_ONE_MINUS_SRC_ALPHA 0x0303
#define GL_OUT_OF_MEMORY 0x0505
#define GL_PACK_ALIGNMENT 0x0D05
#define GL_POINT_SMOOTH 0x0B10
#define GL_POLYGON 0x0009
#define GL_POLYGON_OFFSET_FILL 0x8037
#define GL_PREVIOUS 0x8578
#define GL_PRIMARY_COLOR 0x8577
#define GL_PROJECTION 0x1701
#define GL_QUAD_STRIP 0x0008
#define GL_QUADS 0x0007
#define GL_RED 0x1903
#define GL_RENDERER 0x1F01
#define GL_REPEAT 0x2901
#define GL_REPLACE 0x1E01
#define GL_RGB 0x1907
#define GL_RGB_SCALE 0x8573
#define GL_RGB_SCALE_ARB 0x8573
#define GL_RGBA 0x1908
#define GL_RGBA8 0x8058
#define GL_SCISSOR_TEST 0x0C11
#define GL_SHADING_LANGUAGE_VERSION 0x8B8C
#define GL_SHORT 0x1402
#define GL_SMOOTH 0x1D01
#define GL_SRC_ALPHA 0x0302
#define GL_SRC_COLOR 0x0300
#define GL_STENCIL_BUFFER_BIT 0x00000400
#define GL_STENCIL_TEST 0x0B90
#define GL_STREAM_DRAW_ARB 0x88E0
#define GL_TEXTURE 0x1702
#define GL_TEXTURE_2D 0x0DE1
#define GL_TEXTURE_BINDING_2D 0x8069
#define GL_TEXTURE_COMPARE_MODE 0x884C
#define GL_TEXTURE_COORD_ARRAY 0x8078
#define GL_TEXTURE_ENV 0x2300
#define GL_TEXTURE_ENV_MODE 0x2200
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_TEXTURE_MAX_LEVEL 0x813D
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_WRAP_S 0x2802
#define GL_TEXTURE_WRAP_T 0x2803
#define GL_TEXTURE0 0x84C0
#define GL_TEXTURE1 0x84C1
#define GL_TRIANGLE_FAN 0x0006
#define GL_TRIANGLE_STRIP 0x0005
#define GL_TRIANGLES 0x0004
#define GL_TRUE 0x1
#define GL_UNPACK_ALIGNMENT 0x0CF5
#define GL_UNSIGNED_BYTE 0x1401
#define GL_UNSIGNED_INT 0x1405
#define GL_UNSIGNED_SHORT 0x1403
#define GL_UNSIGNED_SHORT_5_6_5_REV 0x8364
#define GL_VERSION 0x1F02
#define GL_VERTEX_ARRAY 0x8074
#define GL_VERTEX_SHADER 0x8B31
#define GL_VIEWPORT 0x0BA2
#define GL_ZERO 0x0
GLAPI void GLAPIENTRY glActiveTexture(GLenum texture);
GLAPI void GLAPIENTRY glActiveTextureARB(GLenum texture);
GLAPI void GLAPIENTRY glAlphaFunc(GLenum func, GLclampf ref);
GLAPI void GLAPIENTRY glBegin(GLenum mode);
GLAPI void GLAPIENTRY glBindTexture(GLenum target, GLuint texture);
GLAPI void GLAPIENTRY glBlendFunc(GLenum sfactor, GLenum dfactor);
GLAPI void GLAPIENTRY glClear(GLbitfield mask);
GLAPI void GLAPIENTRY glClearColor(GLclampf red, GLclampf green, GLclampf blue, GLclampf alpha);
GLAPI void GLAPIENTRY glClearStencil(GLint s);
GLAPI void GLAPIENTRY glClientActiveTexture(GLenum texture);
GLAPI void GLAPIENTRY glClientActiveTextureARB(GLenum texture);
GLAPI void GLAPIENTRY glColor3f(GLfloat red, GLfloat green, GLfloat blue);
GLAPI void GLAPIENTRY glColor4f(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha);
GLAPI void GLAPIENTRY glColor4fv(const GLfloat *v);
GLAPI void GLAPIENTRY glColorMask(GLboolean red, GLboolean green, GLboolean blue, GLboolean alpha);
GLAPI void GLAPIENTRY glColorPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *ptr);
GLAPI void GLAPIENTRY glCopyTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset,
                                          GLint x, GLint y, GLsizei width, GLsizei height);
GLAPI void GLAPIENTRY glDeleteTextures(GLsizei n, const GLuint *textures);
GLAPI void GLAPIENTRY glDepthFunc(GLenum func);
GLAPI void GLAPIENTRY glDepthMask(GLboolean flag);
GLAPI void GLAPIENTRY glDisable(GLenum cap);
GLAPI void GLAPIENTRY glDisableClientState(GLenum cap);
GLAPI void GLAPIENTRY glDrawArrays(GLenum mode, GLint first, GLsizei count);
GLAPI void GLAPIENTRY glDrawBuffer(GLenum mode);
GLAPI void GLAPIENTRY glDrawElements(GLenum mode, GLsizei count, GLenum type,
                                     const GLvoid *indices);
GLAPI void GLAPIENTRY glEnable(GLenum cap);
GLAPI void GLAPIENTRY glEnableClientState(GLenum cap);
GLAPI void GLAPIENTRY glEnd(void);
GLAPI void GLAPIENTRY glFinish(void);
GLAPI void GLAPIENTRY glGenTextures(GLsizei n, GLuint *textures);
GLAPI void GLAPIENTRY glGetBooleanv(GLenum pname, GLboolean *params);
GLAPI GLenum GLAPIENTRY glGetError(void);
GLAPI void GLAPIENTRY glGetFloatv(GLenum pname, GLfloat *params);
GLAPI void GLAPIENTRY glGetIntegerv(GLenum pname, GLint *params);
GLAPI const GLubyte *GLAPIENTRY glGetString(GLenum name);
GLAPI void GLAPIENTRY glGetTexEnviv(GLenum target, GLenum pname, GLint *params);
GLAPI GLboolean GLAPIENTRY glIsEnabled(GLenum cap);
GLAPI void GLAPIENTRY glLoadIdentity(void);
GLAPI void GLAPIENTRY glLoadMatrixf(const GLfloat *m);
GLAPI void GLAPIENTRY glMatrixMode(GLenum mode);
GLAPI void GLAPIENTRY glMultiTexCoord2f(GLenum target, GLfloat s, GLfloat t);
GLAPI void GLAPIENTRY glMultiTexCoord2fARB(GLenum target, GLfloat s, GLfloat t);
GLAPI void GLAPIENTRY glMultMatrixf(const GLfloat *m);
GLAPI void GLAPIENTRY glOrtho(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top,
                              GLdouble near_val, GLdouble far_val);
GLAPI void GLAPIENTRY glPixelStorei(GLenum pname, GLint param);
GLAPI void GLAPIENTRY glPolygonMode(GLenum face, GLenum mode);
GLAPI void GLAPIENTRY glPolygonOffset(GLfloat factor, GLfloat units);
GLAPI void GLAPIENTRY glPopAttrib(void);
GLAPI void GLAPIENTRY glPopMatrix(void);
GLAPI void GLAPIENTRY glPushAttrib(GLbitfield mask);
GLAPI void GLAPIENTRY glPushMatrix(void);
GLAPI void GLAPIENTRY glReadBuffer(GLenum mode);
GLAPI void GLAPIENTRY glReadPixels(GLint x, GLint y, GLsizei width, GLsizei height, GLenum format,
                                   GLenum type, GLvoid *pixels);
GLAPI void GLAPIENTRY glScalef(GLfloat x, GLfloat y, GLfloat z);
GLAPI void GLAPIENTRY glScissor(GLint x, GLint y, GLsizei width, GLsizei height);
GLAPI void GLAPIENTRY glShadeModel(GLenum mode);
GLAPI void GLAPIENTRY glStencilFunc(GLenum func, GLint ref, GLuint mask);
GLAPI void GLAPIENTRY glStencilOp(GLenum fail, GLenum zfail, GLenum zpass);
GLAPI void GLAPIENTRY glTexCoord2f(GLfloat s, GLfloat t);
GLAPI void GLAPIENTRY glTexCoord2fv(const GLfloat *v);
GLAPI void GLAPIENTRY glTexCoordPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *ptr);
GLAPI void GLAPIENTRY glTexEnvf(GLenum target, GLenum pname, GLfloat param);
GLAPI void GLAPIENTRY glTexEnvi(GLenum target, GLenum pname, GLint param);
GLAPI void GLAPIENTRY glTexImage2D(GLenum target, GLint level, GLint internalFormat, GLsizei width,
                                   GLsizei height, GLint border, GLenum format, GLenum type,
                                   const GLvoid *pixels);
GLAPI void GLAPIENTRY glTexParameterf(GLenum target, GLenum pname, GLfloat param);
GLAPI void GLAPIENTRY glTexParameteri(GLenum target, GLenum pname, GLint param);
GLAPI void GLAPIENTRY glTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset,
                                      GLsizei width, GLsizei height, GLenum format, GLenum type,
                                      const GLvoid *pixels);
GLAPI void GLAPIENTRY glTranslatef(GLfloat x, GLfloat y, GLfloat z);
GLAPI void GLAPIENTRY glVertex2f(GLfloat x, GLfloat y);
GLAPI void GLAPIENTRY glVertex3f(GLfloat x, GLfloat y, GLfloat z);
GLAPI void GLAPIENTRY glVertex3fv(const GLfloat *v);
GLAPI void GLAPIENTRY glVertexPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *ptr);
GLAPI void GLAPIENTRY glViewport(GLint x, GLint y, GLsizei width, GLsizei height);
#endif
