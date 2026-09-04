/* Offscreen OpenGL presenter. CPU paint stays in sk_sw; peek uploads the
 * raster and reads it back so Headless goldens stay on the same bytes. */

#include "sk_gpu.h"

#if defined(__APPLE__)
#define GL_SILENCE_DEPRECATION 1
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>

static CGLContextObj g_ctx;

static int gpu_make_current(void) {
  CGLPixelFormatAttribute attrs_acc[] = {
      kCGLPFAAccelerated, kCGLPFAColorSize, (CGLPixelFormatAttribute)24,
      kCGLPFAAlphaSize, (CGLPixelFormatAttribute)8,
      kCGLPFAAllowOfflineRenderers, (CGLPixelFormatAttribute)0};
  CGLPixelFormatAttribute attrs_any[] = {
      kCGLPFAColorSize, (CGLPixelFormatAttribute)24, kCGLPFAAlphaSize,
      (CGLPixelFormatAttribute)8, kCGLPFAAllowOfflineRenderers,
      (CGLPixelFormatAttribute)0};
  CGLPixelFormatObj pix = NULL;
  GLint npix = 0;
  if (g_ctx) {
    CGLSetCurrentContext(g_ctx);
    return 1;
  }
  if (CGLChoosePixelFormat(attrs_acc, &pix, &npix) != kCGLNoError || !pix)
    (void)CGLChoosePixelFormat(attrs_any, &pix, &npix);
  if (!pix)
    return 0;
  if (CGLCreateContext(pix, NULL, &g_ctx) != kCGLNoError || !g_ctx) {
    CGLDestroyPixelFormat(pix);
    g_ctx = NULL;
    return 0;
  }
  CGLDestroyPixelFormat(pix);
  CGLSetCurrentContext(g_ctx);
  return 1;
}

#else

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>

#ifndef EGL_PLATFORM_SURFACELESS_MESA
#define EGL_PLATFORM_SURFACELESS_MESA 0x31DD
#endif
#ifndef PFNEGLGETPLATFORMDISPLAYEXTPROC
typedef EGLDisplay (*PFNEGLGETPLATFORMDISPLAYEXTPROC)(EGLenum platform,
                                                      void *native_display,
                                                      const EGLint *attrib_list);
#endif

static EGLDisplay g_dpy = EGL_NO_DISPLAY;
static EGLContext g_ctx = EGL_NO_CONTEXT;
static EGLSurface g_surf = EGL_NO_SURFACE;

/* Headless first: surfaceless does not need X11 or Wayland. Fall back to the
 * default display when a window system is present. */
static int gpu_init_display(void) {
  PFNEGLGETPLATFORMDISPLAYEXTPROC get_plat;
  EGLDisplay dpy = EGL_NO_DISPLAY;

  get_plat = (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress(
      "eglGetPlatformDisplayEXT");
  if (get_plat) {
    dpy = get_plat(EGL_PLATFORM_SURFACELESS_MESA, (void *)EGL_DEFAULT_DISPLAY,
                   NULL);
    if (dpy != EGL_NO_DISPLAY && eglInitialize(dpy, NULL, NULL)) {
      g_dpy = dpy;
      return 1;
    }
  }
  dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
  if (dpy == EGL_NO_DISPLAY)
    return 0;
  if (!eglInitialize(dpy, NULL, NULL))
    return 0;
  g_dpy = dpy;
  return 1;
}

static int gpu_make_current(void) {
  EGLConfig cfg;
  EGLint n = 0;
  EGLint cfg_attr[] = {EGL_SURFACE_TYPE,
                       EGL_PBUFFER_BIT,
                       EGL_RED_SIZE,
                       8,
                       EGL_GREEN_SIZE,
                       8,
                       EGL_BLUE_SIZE,
                       8,
                       EGL_ALPHA_SIZE,
                       8,
                       EGL_RENDERABLE_TYPE,
                       EGL_OPENGL_ES2_BIT,
                       EGL_NONE};
  EGLint ctx_attr[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
  EGLint pb_attr[] = {EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE};
  if (g_ctx != EGL_NO_CONTEXT) {
    return eglMakeCurrent(g_dpy, g_surf, g_surf, g_ctx) == EGL_TRUE;
  }
  if (!gpu_init_display())
    return 0;
  if (!eglChooseConfig(g_dpy, cfg_attr, &cfg, 1, &n) || n < 1) {
    eglTerminate(g_dpy);
    g_dpy = EGL_NO_DISPLAY;
    return 0;
  }
  g_surf = eglCreatePbufferSurface(g_dpy, cfg, pb_attr);
  if (g_surf == EGL_NO_SURFACE) {
    eglTerminate(g_dpy);
    g_dpy = EGL_NO_DISPLAY;
    return 0;
  }
  eglBindAPI(EGL_OPENGL_ES_API);
  g_ctx = eglCreateContext(g_dpy, cfg, EGL_NO_CONTEXT, ctx_attr);
  if (g_ctx == EGL_NO_CONTEXT) {
    eglDestroySurface(g_dpy, g_surf);
    g_surf = EGL_NO_SURFACE;
    eglTerminate(g_dpy);
    g_dpy = EGL_NO_DISPLAY;
    return 0;
  }
  if (eglMakeCurrent(g_dpy, g_surf, g_surf, g_ctx) != EGL_TRUE) {
    eglDestroyContext(g_dpy, g_ctx);
    eglDestroySurface(g_dpy, g_surf);
    g_ctx = EGL_NO_CONTEXT;
    g_surf = EGL_NO_SURFACE;
    eglTerminate(g_dpy);
    g_dpy = EGL_NO_DISPLAY;
    return 0;
  }
  return 1;
}

#endif

int sk_gpu_available(void) { return gpu_make_current(); }

int sk_gpu_roundtrip(const uint8_t *src, uint8_t *dst, int w, int h) {
  GLuint tex = 0;
  GLuint fbo = 0;
  if (!src || !dst || w <= 0 || h <= 0)
    return 0;
  if (!gpu_make_current())
    return 0;
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  glGenTextures(1, &tex);
  glBindTexture(GL_TEXTURE_2D, tex);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE,
               src);
  glGenFramebuffers(1, &fbo);
  glBindFramebuffer(GL_FRAMEBUFFER, fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                         tex, 0);
  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteFramebuffers(1, &fbo);
    glDeleteTextures(1, &tex);
    return 0;
  }
  glViewport(0, 0, w, h);
  glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, dst);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glDeleteFramebuffers(1, &fbo);
  glDeleteTextures(1, &tex);
  return 1;
}
