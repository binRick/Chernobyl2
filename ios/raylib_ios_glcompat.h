// Compatibility shim included via -include when compiling raylib for iOS.
//
// raylib's rlgl.h references several GLES extension constants by their
// _OES / _EXT suffixed names (the GLES2-era extension spellings). Apple's
// OpenGLES.framework only ships the un-suffixed core GLES3 names (and only
// some of the extension equivalents). We map each one back to a core type
// that's a reasonable substitute on iOS - the matrix is small and the lookups
// only matter for HDR/depth-precision texture paths the game doesn't use.

#ifndef CHERNOBYL_IOS_GLCOMPAT_H
#define CHERNOBYL_IOS_GLCOMPAT_H

#include <OpenGLES/ES3/gl.h>
#include <OpenGLES/ES3/glext.h>

// Depth precision. iOS GLES3 has GL_DEPTH_COMPONENT24 / 32F core; the _OES
// variants are GLES2 extension spellings. Map both to the core 24-bit form -
// raylib only ever picks one at runtime based on ExtSupported.maxDepthBits and
// our renderer doesn't depend on the precise choice.
#ifndef GL_DEPTH_COMPONENT32_OES
#define GL_DEPTH_COMPONENT32_OES GL_DEPTH_COMPONENT24
#endif
#ifndef GL_DEPTH_COMPONENT24_OES
#define GL_DEPTH_COMPONENT24_OES GL_DEPTH_COMPONENT24
#endif

// Float-format texture types. Core GLES3 spells these without the _EXT.
#ifndef GL_R32F_EXT
#define GL_R32F_EXT     GL_R32F
#endif
#ifndef GL_RGB32F_EXT
#define GL_RGB32F_EXT   GL_RGB32F
#endif
#ifndef GL_RGBA32F_EXT
#define GL_RGBA32F_EXT  GL_RGBA32F
#endif
#ifndef GL_R16F_EXT
#define GL_R16F_EXT     GL_R16F
#endif
#ifndef GL_RGB16F_EXT
#define GL_RGB16F_EXT   GL_RGB16F
#endif
#ifndef GL_RGBA16F_EXT
#define GL_RGBA16F_EXT  GL_RGBA16F
#endif
#ifndef GL_RED_EXT
#define GL_RED_EXT      GL_RED
#endif
#ifndef GL_RG_EXT
#define GL_RG_EXT       GL_RG
#endif

// raylib's rlgl.h sometimes references GL_LUMINANCE which has been removed
// from core GLES3. Map to GL_RED so the table at least compiles; the game
// doesn't use luminance-only textures.
#ifndef GL_LUMINANCE
#define GL_LUMINANCE       GL_RED
#endif
#ifndef GL_LUMINANCE_ALPHA
#define GL_LUMINANCE_ALPHA GL_RG
#endif

#endif // CHERNOBYL_IOS_GLCOMPAT_H
