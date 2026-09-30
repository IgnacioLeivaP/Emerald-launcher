#include "gl.h"
#include <SDL2/SDL.h>
#include <stdio.h>

/* Available on both platforms (GLES cores still resolve GL procs through SDL). */
void *gl_get_proc_address(const char *name) {
    void *p = SDL_GL_GetProcAddress(name);
#ifdef _WIN32
    /* wglGetProcAddress returns NULL for GL 1.1 core functions; fall back to
       the opengl32.dll exports so hw-render cores don't get NULL pointers. */
    if (!p) {
        static HMODULE s_gl_dll = NULL;
        if (!s_gl_dll) s_gl_dll = LoadLibraryA("opengl32.dll");
        if (s_gl_dll) p = (void*)GetProcAddress(s_gl_dll, name);
    }
#endif
    return p;
}

/* ── Desktop OpenGL 3.3: runtime function-pointer loading (PC only) ────── */
#ifndef EL_GLES_API

PFN_CreateShader            gl_CreateShader;
PFN_ShaderSource            gl_ShaderSource;
PFN_CompileShader           gl_CompileShader;
PFN_GetShaderiv             gl_GetShaderiv;
PFN_GetShaderInfoLog        gl_GetShaderInfoLog;
PFN_CreateProgram           gl_CreateProgram;
PFN_AttachShader            gl_AttachShader;
PFN_LinkProgram             gl_LinkProgram;
PFN_UseProgram              gl_UseProgram;
PFN_GetProgramiv            gl_GetProgramiv;
PFN_GetProgramInfoLog       gl_GetProgramInfoLog;
PFN_DeleteShader            gl_DeleteShader;
PFN_DeleteProgram           gl_DeleteProgram;
PFN_GetUniformLocation      gl_GetUniformLocation;
PFN_Uniform1f               gl_Uniform1f;
PFN_Uniform2f               gl_Uniform2f;
PFN_Uniform1fv              gl_Uniform1fv;
PFN_Uniform3fv              gl_Uniform3fv;
PFN_Uniform4fv              gl_Uniform4fv;
PFN_Uniform1i               gl_Uniform1i;
PFN_BufferSubData           gl_BufferSubData;
PFN_GenVertexArrays         gl_GenVertexArrays;
PFN_BindVertexArray         gl_BindVertexArray;
PFN_DeleteVertexArrays      gl_DeleteVertexArrays;
PFN_GenBuffers              gl_GenBuffers;
PFN_BindBuffer              gl_BindBuffer;
PFN_BufferData              gl_BufferData;
PFN_DeleteBuffers           gl_DeleteBuffers;
PFN_VertexAttribPointer     gl_VertexAttribPointer;
PFN_EnableVertexAttribArray gl_EnableVertexAttribArray;
PFN_ActiveTexture           gl_ActiveTexture;
PFN_GenFramebuffers         gl_GenFramebuffers;
PFN_BindFramebuffer         gl_BindFramebuffer;
PFN_FramebufferTexture2D    gl_FramebufferTexture2D;
PFN_DeleteFramebuffers      gl_DeleteFramebuffers;
PFN_GenRenderbuffers        gl_GenRenderbuffers;
PFN_BindRenderbuffer        gl_BindRenderbuffer;
PFN_RenderbufferStorage     gl_RenderbufferStorage;
PFN_FramebufferRenderbuffer gl_FramebufferRenderbuffer;
PFN_DeleteRenderbuffers     gl_DeleteRenderbuffers;
PFN_CheckFramebufferStatus  gl_CheckFramebufferStatus;
PFN_BlitFramebuffer         gl_BlitFramebuffer;
PFN_UniformMatrix4fv        gl_UniformMatrix4fv;
PFN_Uniform3f               gl_Uniform3f;
PFN_Uniform4f               gl_Uniform4f;
PFN_GenerateMipmap          gl_GenerateMipmap;
PFN_RenderbufferStorageMultisample gl_RenderbufferStorageMultisample;
PFN_BlendFuncSeparate       gl_BlendFuncSeparate;

#define LOAD(T, var, name)                                                  \
    do {                                                                    \
        void *_p = SDL_GL_GetProcAddress(name);                             \
        if (!_p) { fprintf(stderr, "GL: missing %s\n", name); ok = 0; }    \
        else     { var = (T)_p; }                                           \
    } while (0)

int gl_load(void) {
    int ok = 1;
    LOAD(PFN_CreateShader,            gl_CreateShader,            "glCreateShader");
    LOAD(PFN_ShaderSource,            gl_ShaderSource,            "glShaderSource");
    LOAD(PFN_CompileShader,           gl_CompileShader,           "glCompileShader");
    LOAD(PFN_GetShaderiv,             gl_GetShaderiv,             "glGetShaderiv");
    LOAD(PFN_GetShaderInfoLog,        gl_GetShaderInfoLog,        "glGetShaderInfoLog");
    LOAD(PFN_CreateProgram,           gl_CreateProgram,           "glCreateProgram");
    LOAD(PFN_AttachShader,            gl_AttachShader,            "glAttachShader");
    LOAD(PFN_LinkProgram,             gl_LinkProgram,             "glLinkProgram");
    LOAD(PFN_UseProgram,              gl_UseProgram,              "glUseProgram");
    LOAD(PFN_GetProgramiv,            gl_GetProgramiv,            "glGetProgramiv");
    LOAD(PFN_GetProgramInfoLog,       gl_GetProgramInfoLog,       "glGetProgramInfoLog");
    LOAD(PFN_DeleteShader,            gl_DeleteShader,            "glDeleteShader");
    LOAD(PFN_DeleteProgram,           gl_DeleteProgram,           "glDeleteProgram");
    LOAD(PFN_GetUniformLocation,      gl_GetUniformLocation,      "glGetUniformLocation");
    LOAD(PFN_Uniform1f,               gl_Uniform1f,               "glUniform1f");
    LOAD(PFN_Uniform2f,               gl_Uniform2f,               "glUniform2f");
    LOAD(PFN_Uniform1fv,              gl_Uniform1fv,              "glUniform1fv");
    LOAD(PFN_Uniform3fv,              gl_Uniform3fv,              "glUniform3fv");
    LOAD(PFN_Uniform4fv,              gl_Uniform4fv,              "glUniform4fv");
    LOAD(PFN_Uniform1i,               gl_Uniform1i,               "glUniform1i");
    LOAD(PFN_BufferSubData,           gl_BufferSubData,           "glBufferSubData");
    LOAD(PFN_GenVertexArrays,         gl_GenVertexArrays,         "glGenVertexArrays");
    LOAD(PFN_BindVertexArray,         gl_BindVertexArray,         "glBindVertexArray");
    LOAD(PFN_DeleteVertexArrays,      gl_DeleteVertexArrays,      "glDeleteVertexArrays");
    LOAD(PFN_GenBuffers,              gl_GenBuffers,              "glGenBuffers");
    LOAD(PFN_BindBuffer,              gl_BindBuffer,              "glBindBuffer");
    LOAD(PFN_BufferData,              gl_BufferData,              "glBufferData");
    LOAD(PFN_DeleteBuffers,           gl_DeleteBuffers,           "glDeleteBuffers");
    LOAD(PFN_VertexAttribPointer,     gl_VertexAttribPointer,     "glVertexAttribPointer");
    LOAD(PFN_EnableVertexAttribArray, gl_EnableVertexAttribArray, "glEnableVertexAttribArray");
    LOAD(PFN_ActiveTexture,           gl_ActiveTexture,           "glActiveTexture");
    LOAD(PFN_GenFramebuffers,         gl_GenFramebuffers,         "glGenFramebuffers");
    LOAD(PFN_BindFramebuffer,         gl_BindFramebuffer,         "glBindFramebuffer");
    LOAD(PFN_FramebufferTexture2D,    gl_FramebufferTexture2D,    "glFramebufferTexture2D");
    LOAD(PFN_DeleteFramebuffers,      gl_DeleteFramebuffers,      "glDeleteFramebuffers");
    LOAD(PFN_GenRenderbuffers,        gl_GenRenderbuffers,        "glGenRenderbuffers");
    LOAD(PFN_BindRenderbuffer,        gl_BindRenderbuffer,        "glBindRenderbuffer");
    LOAD(PFN_RenderbufferStorage,     gl_RenderbufferStorage,     "glRenderbufferStorage");
    LOAD(PFN_FramebufferRenderbuffer, gl_FramebufferRenderbuffer, "glFramebufferRenderbuffer");
    LOAD(PFN_DeleteRenderbuffers,     gl_DeleteRenderbuffers,     "glDeleteRenderbuffers");
    LOAD(PFN_CheckFramebufferStatus,  gl_CheckFramebufferStatus,  "glCheckFramebufferStatus");
    LOAD(PFN_BlitFramebuffer,         gl_BlitFramebuffer,         "glBlitFramebuffer");
    LOAD(PFN_UniformMatrix4fv,        gl_UniformMatrix4fv,        "glUniformMatrix4fv");
    LOAD(PFN_Uniform3f,               gl_Uniform3f,               "glUniform3f");
    LOAD(PFN_Uniform4f,               gl_Uniform4f,               "glUniform4f");
    LOAD(PFN_GenerateMipmap,          gl_GenerateMipmap,          "glGenerateMipmap");
    LOAD(PFN_RenderbufferStorageMultisample, gl_RenderbufferStorageMultisample,
                                                                  "glRenderbufferStorageMultisample");
    LOAD(PFN_BlendFuncSeparate,       gl_BlendFuncSeparate,       "glBlendFuncSeparate");
    return ok;
}

#endif /* EL_GLES_API */
