#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* ===================================================================
   Nintendo Switch — OpenGL ES 3.0 (devkitPro Mesa)
   All entry points are core in GLES 3.0, so gl_* maps directly with no
   runtime loading. Shaders use the GLSL_VERSION macro as their first line.
   =================================================================== */
#ifdef NINTENDO_SWITCH

#include <GLES3/gl3.h>
#include <stddef.h>

#ifndef GL_BGRA
#  define GL_BGRA 0x80E1   /* not in GLES — defined so the CPU-upload path compiles */
#endif

#define GLSL_VERSION "#version 300 es\nprecision highp float;\n"

#define gl_CreateShader             glCreateShader
#define gl_ShaderSource             glShaderSource
#define gl_CompileShader            glCompileShader
#define gl_GetShaderiv              glGetShaderiv
#define gl_GetShaderInfoLog         glGetShaderInfoLog
#define gl_CreateProgram            glCreateProgram
#define gl_AttachShader             glAttachShader
#define gl_LinkProgram              glLinkProgram
#define gl_UseProgram               glUseProgram
#define gl_GetProgramiv             glGetProgramiv
#define gl_GetProgramInfoLog        glGetProgramInfoLog
#define gl_DeleteShader             glDeleteShader
#define gl_DeleteProgram            glDeleteProgram
#define gl_GetUniformLocation       glGetUniformLocation
#define gl_Uniform1f                glUniform1f
#define gl_Uniform2f                glUniform2f
#define gl_Uniform1fv               glUniform1fv
#define gl_Uniform3fv               glUniform3fv
#define gl_Uniform4fv               glUniform4fv
#define gl_Uniform1i                glUniform1i
#define gl_BufferSubData            glBufferSubData
#define gl_GenVertexArrays          glGenVertexArrays
#define gl_BindVertexArray          glBindVertexArray
#define gl_DeleteVertexArrays       glDeleteVertexArrays
#define gl_GenBuffers               glGenBuffers
#define gl_BindBuffer               glBindBuffer
#define gl_BufferData               glBufferData
#define gl_DeleteBuffers            glDeleteBuffers
#define gl_VertexAttribPointer      glVertexAttribPointer
#define gl_EnableVertexAttribArray  glEnableVertexAttribArray
#define gl_ActiveTexture            glActiveTexture
#define gl_GenFramebuffers          glGenFramebuffers
#define gl_BindFramebuffer          glBindFramebuffer
#define gl_FramebufferTexture2D     glFramebufferTexture2D
#define gl_DeleteFramebuffers       glDeleteFramebuffers
#define gl_GenRenderbuffers         glGenRenderbuffers
#define gl_BindRenderbuffer         glBindRenderbuffer
#define gl_RenderbufferStorage      glRenderbufferStorage
#define gl_FramebufferRenderbuffer  glFramebufferRenderbuffer
#define gl_DeleteRenderbuffers      glDeleteRenderbuffers
#define gl_CheckFramebufferStatus   glCheckFramebufferStatus
#define gl_BlitFramebuffer          glBlitFramebuffer

static inline int gl_load(void) { return 1; }   /* nothing to load on GLES */

/* ===================================================================
   PC — desktop OpenGL 3.3 core (functions loaded at runtime)
   =================================================================== */
#else

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#endif
#include <GL/gl.h>
#include <stddef.h>

#define GLSL_VERSION "#version 330 core\n"

/* Types missing from base GL 1.1 */
typedef char      GLchar;
typedef ptrdiff_t GLintptr;
typedef ptrdiff_t GLsizeiptr;

/* Constants not in base GL 1.1 */
#define GL_FRAGMENT_SHADER        0x8B30
#define GL_VERTEX_SHADER          0x8B31
#define GL_COMPILE_STATUS         0x8B81
#define GL_LINK_STATUS            0x8B82
#define GL_INFO_LOG_LENGTH        0x8B84
#define GL_ARRAY_BUFFER           0x8892
#define GL_STATIC_DRAW            0x88B4
#define GL_DYNAMIC_DRAW           0x88B8
#define GL_TEXTURE0               0x84C0
#define GL_TEXTURE1               0x84C1
#define GL_BGRA                   0x80E1
#define GL_CLAMP_TO_EDGE          0x812F
#define GL_R8                     0x8229
#define GL_RED                    0x1903
#define GL_FRAMEBUFFER            0x8D40
#define GL_COLOR_ATTACHMENT0      0x8CE0
#define GL_RGBA32F                0x8814
#define GL_RENDERBUFFER           0x8D41
#define GL_DEPTH_ATTACHMENT       0x8D00
#define GL_DEPTH_STENCIL_ATTACHMENT 0x821A
#define GL_DEPTH24_STENCIL8       0x88F0
#define GL_DEPTH_COMPONENT24      0x81A6
#define GL_FRAMEBUFFER_COMPLETE   0x8CD5
#define GL_READ_FRAMEBUFFER       0x8CA8
#define GL_DRAW_FRAMEBUFFER       0x8CA9

/* ── Function pointer types ──────────────────────────────────────── */
typedef GLuint (*PFN_CreateShader)(GLenum);
typedef void   (*PFN_ShaderSource)(GLuint, GLsizei, const GLchar **, const GLint *);
typedef void   (*PFN_CompileShader)(GLuint);
typedef void   (*PFN_GetShaderiv)(GLuint, GLenum, GLint *);
typedef void   (*PFN_GetShaderInfoLog)(GLuint, GLsizei, GLsizei *, GLchar *);
typedef GLuint (*PFN_CreateProgram)(void);
typedef void   (*PFN_AttachShader)(GLuint, GLuint);
typedef void   (*PFN_LinkProgram)(GLuint);
typedef void   (*PFN_UseProgram)(GLuint);
typedef void   (*PFN_GetProgramiv)(GLuint, GLenum, GLint *);
typedef void   (*PFN_GetProgramInfoLog)(GLuint, GLsizei, GLsizei *, GLchar *);
typedef void   (*PFN_DeleteShader)(GLuint);
typedef void   (*PFN_DeleteProgram)(GLuint);
typedef GLint  (*PFN_GetUniformLocation)(GLuint, const GLchar *);
typedef void   (*PFN_Uniform1f)(GLint, GLfloat);
typedef void   (*PFN_Uniform2f)(GLint, GLfloat, GLfloat);
typedef void   (*PFN_Uniform1fv)(GLint, GLsizei, const GLfloat *);
typedef void   (*PFN_Uniform3fv)(GLint, GLsizei, const GLfloat *);
typedef void   (*PFN_Uniform4fv)(GLint, GLsizei, const GLfloat *);
typedef void   (*PFN_Uniform1i)(GLint, GLint);
typedef void   (*PFN_BufferSubData)(GLenum, GLintptr, GLsizeiptr, const void *);
typedef void   (*PFN_GenVertexArrays)(GLsizei, GLuint *);
typedef void   (*PFN_BindVertexArray)(GLuint);
typedef void   (*PFN_DeleteVertexArrays)(GLsizei, const GLuint *);
typedef void   (*PFN_GenBuffers)(GLsizei, GLuint *);
typedef void   (*PFN_BindBuffer)(GLenum, GLuint);
typedef void   (*PFN_BufferData)(GLenum, GLsizeiptr, const void *, GLenum);
typedef void   (*PFN_DeleteBuffers)(GLsizei, const GLuint *);
typedef void   (*PFN_VertexAttribPointer)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void *);
typedef void   (*PFN_EnableVertexAttribArray)(GLuint);
typedef void   (*PFN_ActiveTexture)(GLenum);
typedef void   (*PFN_GenFramebuffers)(GLsizei, GLuint *);
typedef void   (*PFN_BindFramebuffer)(GLenum, GLuint);
typedef void   (*PFN_FramebufferTexture2D)(GLenum, GLenum, GLenum, GLuint, GLint);
typedef void   (*PFN_DeleteFramebuffers)(GLsizei, const GLuint *);
typedef void   (*PFN_GenRenderbuffers)(GLsizei, GLuint *);
typedef void   (*PFN_BindRenderbuffer)(GLenum, GLuint);
typedef void   (*PFN_RenderbufferStorage)(GLenum, GLenum, GLsizei, GLsizei);
typedef void   (*PFN_FramebufferRenderbuffer)(GLenum, GLenum, GLenum, GLuint);
typedef void   (*PFN_DeleteRenderbuffers)(GLsizei, const GLuint *);
typedef GLenum (*PFN_CheckFramebufferStatus)(GLenum);
typedef void   (*PFN_BlitFramebuffer)(GLint,GLint,GLint,GLint,GLint,GLint,GLint,GLint,GLbitfield,GLenum);

/* ── Global function pointers ────────────────────────────────────── */
extern PFN_CreateShader           gl_CreateShader;
extern PFN_ShaderSource           gl_ShaderSource;
extern PFN_CompileShader          gl_CompileShader;
extern PFN_GetShaderiv            gl_GetShaderiv;
extern PFN_GetShaderInfoLog       gl_GetShaderInfoLog;
extern PFN_CreateProgram          gl_CreateProgram;
extern PFN_AttachShader           gl_AttachShader;
extern PFN_LinkProgram            gl_LinkProgram;
extern PFN_UseProgram             gl_UseProgram;
extern PFN_GetProgramiv           gl_GetProgramiv;
extern PFN_GetProgramInfoLog      gl_GetProgramInfoLog;
extern PFN_DeleteShader           gl_DeleteShader;
extern PFN_DeleteProgram          gl_DeleteProgram;
extern PFN_GetUniformLocation     gl_GetUniformLocation;
extern PFN_Uniform1f              gl_Uniform1f;
extern PFN_Uniform2f              gl_Uniform2f;
extern PFN_Uniform1fv             gl_Uniform1fv;
extern PFN_Uniform3fv             gl_Uniform3fv;
extern PFN_Uniform4fv             gl_Uniform4fv;
extern PFN_Uniform1i              gl_Uniform1i;
extern PFN_BufferSubData          gl_BufferSubData;
extern PFN_GenVertexArrays        gl_GenVertexArrays;
extern PFN_BindVertexArray        gl_BindVertexArray;
extern PFN_DeleteVertexArrays     gl_DeleteVertexArrays;
extern PFN_GenBuffers             gl_GenBuffers;
extern PFN_BindBuffer             gl_BindBuffer;
extern PFN_BufferData             gl_BufferData;
extern PFN_DeleteBuffers          gl_DeleteBuffers;
extern PFN_VertexAttribPointer    gl_VertexAttribPointer;
extern PFN_EnableVertexAttribArray gl_EnableVertexAttribArray;
extern PFN_ActiveTexture          gl_ActiveTexture;
extern PFN_GenFramebuffers        gl_GenFramebuffers;
extern PFN_BindFramebuffer        gl_BindFramebuffer;
extern PFN_FramebufferTexture2D   gl_FramebufferTexture2D;
extern PFN_DeleteFramebuffers     gl_DeleteFramebuffers;
extern PFN_GenRenderbuffers       gl_GenRenderbuffers;
extern PFN_BindRenderbuffer       gl_BindRenderbuffer;
extern PFN_RenderbufferStorage    gl_RenderbufferStorage;
extern PFN_FramebufferRenderbuffer gl_FramebufferRenderbuffer;
extern PFN_DeleteRenderbuffers    gl_DeleteRenderbuffers;
extern PFN_CheckFramebufferStatus gl_CheckFramebufferStatus;
extern PFN_BlitFramebuffer        gl_BlitFramebuffer;

/* Load all function pointers via SDL_GL_GetProcAddress. Returns 0 on failure. */
int gl_load(void);

#endif /* NINTENDO_SWITCH */

/* Wrapper around SDL_GL_GetProcAddress (so non-SDL TUs like core.c can provide
   a get_proc_address to hardware-rendering libretro cores). Both platforms. */
void *gl_get_proc_address(const char *name);

#ifdef __cplusplus
}
#endif
