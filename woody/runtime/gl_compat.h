#ifndef WOODY_GL_COMPAT_H
#define WOODY_GL_COMPAT_H
#define GL_GLEXT_PROTOTYPES 1
#include <GL/gl.h>
#include <GL/glext.h>
void glGenBuffersARB(GLsizei n, GLuint *buffers);
void glDeleteBuffersARB(GLsizei n, const GLuint *buffers);
void glBindBufferARB(GLenum target, GLuint buffer);
void glBufferDataARB(GLenum target, GLsizeiptrARB size, const GLvoid *data, GLenum usage);
void woody_gl_enable(GLenum cap);
void woody_gl_disable(GLenum cap);
void woody_gl_polygon_offset(GLfloat factor, GLfloat units);
void woody_gl_depth_func(GLenum func);
void woody_gl_texenvi(GLenum target, GLenum pname, GLint value);
GLboolean woody_gl_is_enabled(GLenum cap);
void woody_gl_get_texenv(GLenum target, GLenum pname, GLint *value);
void woody_gl_texenvf(GLenum target, GLenum pname, GLfloat value);
void woody_gl_texture_max_level(unsigned level);
void woody_gl_world_texture(void);
GLenum woody_read_pixels_rgba(unsigned char *pixels);
#ifndef WOODY_GL_IMPLEMENTATION
#define glEnable woody_gl_enable
#define glDisable woody_gl_disable
#define glPolygonOffset woody_gl_polygon_offset
#define glDepthFunc woody_gl_depth_func
#define glTexEnvi woody_gl_texenvi
#define glIsEnabled woody_gl_is_enabled
#define glGetTexEnviv woody_gl_get_texenv
#define glTexEnvf woody_gl_texenvf
#endif
#endif
void glColorTableEXT(GLenum target, GLenum internal, GLsizei width, GLenum format, GLenum type,
                     const GLvoid *data);
