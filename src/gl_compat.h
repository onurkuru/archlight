/* ARCLIGHT - platform GL header + the small set of GL we allow ourselves to use.
 *
 * Vita:    vitaGL (GL ES 2.0 subset over sceGxm)
 * Desktop: OpenGL 3.3 core (macOS ships gl3.h, no loader needed)
 *
 * Rule: if a call is not valid in BOTH, it does not go in the renderer. */
#ifndef ARC_GL_COMPAT_H
#define ARC_GL_COMPAT_H

#ifdef __vita__
  #include <vitaGL.h>
  #define ARC_GLSL_ES 1
#else
  #define GL_SILENCE_DEPRECATION 1
  #ifdef __APPLE__
    #include <OpenGL/gl3.h>
  #else
    #include <SDL2/SDL_opengl.h>
  #endif
  #define ARC_GLSL_ES 0
#endif

#endif
