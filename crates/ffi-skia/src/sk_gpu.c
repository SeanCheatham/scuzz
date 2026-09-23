/* Offscreen OpenGL rasterizer. Draw commands run on the GPU.
 * Peek reads the framebuffer. There is no CPU paint buffer. */

#include "sk_gpu.h"
#include "sk_font8.h"
#include "sk_utf8.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

static const char *SOLID_VS =
    "#version 120\n"
    "attribute vec2 a_ndc;\n"
    "void main(){gl_Position=vec4(a_ndc,0.0,1.0);}\n";
static const char *SOLID_FS =
    "#version 120\n"
    "uniform vec4 u_color;\n"
    "void main(){gl_FragColor=u_color;}\n";

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
    if (dpy != EGL_NO_DISPLAY) {
      if (eglInitialize(dpy, NULL, NULL)) {
        g_dpy = dpy;
        return 1;
      }
      eglTerminate(dpy);
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
  if (g_ctx != EGL_NO_CONTEXT)
    return eglMakeCurrent(g_dpy, g_surf, g_surf, g_ctx) == EGL_TRUE;
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

static const char *SOLID_VS =
    "#version 100\n"
    "attribute vec2 a_ndc;\n"
    "void main(){gl_Position=vec4(a_ndc,0.0,1.0);}\n";
static const char *SOLID_FS =
    "#version 100\n"
    "precision highp float;\n"
    "uniform vec4 u_color;\n"
    "void main(){gl_FragColor=u_color;}\n";

#endif

struct SkGpuTarget {
  int w;
  int h;
  GLuint tex;
  GLuint fbo;
  uint8_t *read;
  int fresh;
};

static GLuint g_prog;
static GLint g_ndc = -1;
static GLint g_color = -1;
static int g_ready;

static GLuint compile_shader(GLenum type, const char *src) {
  GLuint shader;
  GLint ok = 0;
  char log[512];
  shader = glCreateShader(type);
  if (!shader)
    return 0;
  glShaderSource(shader, 1, &src, NULL);
  glCompileShader(shader);
  glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
  if (ok)
    return shader;
  glGetShaderInfoLog(shader, (GLsizei)sizeof log, NULL, log);
  fprintf(stderr, "sk_gpu shader: %s\n", log);
  glDeleteShader(shader);
  return 0;
}

static int gpu_init_gl(void) {
  GLuint vs;
  GLuint fs;
  GLint ok = 0;
  char log[512];
  if (g_ready)
    return 1;
  vs = compile_shader(GL_VERTEX_SHADER, SOLID_VS);
  fs = compile_shader(GL_FRAGMENT_SHADER, SOLID_FS);
  if (!vs || !fs) {
    if (vs)
      glDeleteShader(vs);
    if (fs)
      glDeleteShader(fs);
    return 0;
  }
  g_prog = glCreateProgram();
  glAttachShader(g_prog, vs);
  glAttachShader(g_prog, fs);
  glLinkProgram(g_prog);
  glDeleteShader(vs);
  glDeleteShader(fs);
  glGetProgramiv(g_prog, GL_LINK_STATUS, &ok);
  if (!ok) {
    glGetProgramInfoLog(g_prog, (GLsizei)sizeof log, NULL, log);
    fprintf(stderr, "sk_gpu link: %s\n", log);
    glDeleteProgram(g_prog);
    g_prog = 0;
    return 0;
  }
  g_ndc = glGetAttribLocation(g_prog, "a_ndc");
  g_color = glGetUniformLocation(g_prog, "u_color");
  if (g_ndc < 0 || g_color < 0)
    return 0;
  glDisable(GL_DITHER);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);
  g_ready = 1;
  return 1;
}

static int gpu_ready(void) {
  if (!gpu_make_current())
    return 0;
  return gpu_init_gl();
}

int sk_gpu_available(void) { return gpu_ready(); }

static void bind_target(SkGpuTarget *t) {
  glBindFramebuffer(GL_FRAMEBUFFER, t->fbo);
  glViewport(0, 0, t->w, t->h);
  glDisable(GL_DITHER);
  glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
  t->fresh = 0;
}

/* Clip is top-left, exclusive on the far edges. GL scissor is bottom-left. */
static int set_scissor(SkGpuTarget *t, int x0, int y0, int x1, int y1) {
  if (x0 < 0)
    x0 = 0;
  if (y0 < 0)
    y0 = 0;
  if (x1 > t->w)
    x1 = t->w;
  if (y1 > t->h)
    y1 = t->h;
  if (x1 <= x0 || y1 <= y0) {
    glDisable(GL_SCISSOR_TEST);
    return 0;
  }
  glEnable(GL_SCISSOR_TEST);
  glScissor(x0, t->h - y1, x1 - x0, y1 - y0);
  return 1;
}

static void quad_ndc(float *v, const SkGpuTarget *t, int x0, int y0, int x1,
                     int y1) {
  float x0n = (float)x0 / (float)t->w * 2.f - 1.f;
  float x1n = (float)x1 / (float)t->w * 2.f - 1.f;
  float y0n = (float)(t->h - y1) / (float)t->h * 2.f - 1.f;
  float y1n = (float)(t->h - y0) / (float)t->h * 2.f - 1.f;
  v[0] = x0n;
  v[1] = y0n;
  v[2] = x1n;
  v[3] = y0n;
  v[4] = x1n;
  v[5] = y1n;
  v[6] = x0n;
  v[7] = y0n;
  v[8] = x1n;
  v[9] = y1n;
  v[10] = x0n;
  v[11] = y1n;
}

static void premul(uint8_t r, uint8_t g, uint8_t b, uint8_t a, float out[4]) {
  if (a == 255) {
    out[0] = (float)r / 255.f;
    out[1] = (float)g / 255.f;
    out[2] = (float)b / 255.f;
    out[3] = 1.f;
    return;
  }
  out[0] = (float)((r * a) / 255) / 255.f;
  out[1] = (float)((g * a) / 255) / 255.f;
  out[2] = (float)((b * a) / 255) / 255.f;
  out[3] = (float)a / 255.f;
}

static void draw_verts(const float *ndc, int nverts, const float col[4],
                       int opaque) {
  glUseProgram(g_prog);
  glUniform4fv(g_color, 1, col);
  if (opaque)
    glDisable(GL_BLEND);
  else {
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
  }
  glEnableVertexAttribArray((GLuint)g_ndc);
  glVertexAttribPointer((GLuint)g_ndc, 2, GL_FLOAT, GL_FALSE, 0, ndc);
  glDrawArrays(GL_TRIANGLES, 0, nverts);
}

SkGpuTarget *sk_gpu_target_new(int width, int height) {
  SkGpuTarget *t;
  if (width <= 0 || height <= 0)
    return NULL;
  if (!gpu_ready())
    return NULL;
  t = (SkGpuTarget *)calloc(1, sizeof(SkGpuTarget));
  if (!t)
    return NULL;
  t->w = width;
  t->h = height;
  glGenTextures(1, &t->tex);
  glBindTexture(GL_TEXTURE_2D, t->tex);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA,
               GL_UNSIGNED_BYTE, NULL);
  glGenFramebuffers(1, &t->fbo);
  glBindFramebuffer(GL_FRAMEBUFFER, t->fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                         t->tex, 0);
  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
    glDeleteFramebuffers(1, &t->fbo);
    glDeleteTextures(1, &t->tex);
    free(t);
    return NULL;
  }
  glViewport(0, 0, width, height);
  glDisable(GL_SCISSOR_TEST);
  glClearColor(0.f, 0.f, 0.f, 0.f);
  glClear(GL_COLOR_BUFFER_BIT);
  return t;
}

void sk_gpu_target_free(SkGpuTarget *target) {
  if (!target)
    return;
  if (gpu_make_current()) {
    glDeleteFramebuffers(1, &target->fbo);
    glDeleteTextures(1, &target->tex);
  }
  free(target->read);
  free(target);
}

void sk_gpu_clear(SkGpuTarget *target, int x0, int y0, int x1, int y1,
                  uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
  uint8_t pr = r;
  uint8_t pg = g;
  uint8_t pb = b;
  if (!target || !gpu_ready())
    return;
  bind_target(target);
  if (!set_scissor(target, x0, y0, x1, y1))
    return;
  if (a < 255) {
    pr = (uint8_t)((r * a) / 255);
    pg = (uint8_t)((g * a) / 255);
    pb = (uint8_t)((b * a) / 255);
  }
  glDisable(GL_BLEND);
  glClearColor((float)pr / 255.f, (float)pg / 255.f, (float)pb / 255.f,
               (float)a / 255.f);
  glClear(GL_COLOR_BUFFER_BIT);
}

void sk_gpu_fill_rect(SkGpuTarget *target, int x0, int y0, int x1, int y1,
                      int cx0, int cy0, int cx1, int cy1, uint8_t r, uint8_t g,
                      uint8_t b, uint8_t a) {
  float verts[12];
  float col[4];
  if (!target || a == 0 || x1 <= x0 || y1 <= y0 || !gpu_ready())
    return;
  bind_target(target);
  if (!set_scissor(target, cx0, cy0, cx1, cy1))
    return;
  quad_ndc(verts, target, x0, y0, x1, y1);
  premul(r, g, b, a, col);
  draw_verts(verts, 6, col, a == 255);
}

static int gpu_floor(float v) {
  int i = (int)v;
  return (float)i > v ? i - 1 : i;
}

static int gpu_advance(float font_px) {
  int a = (int)(font_px + 0.5f);
  return a < 1 ? 1 : a;
}

static int push_px(float **verts, int *n, int *cap, const SkGpuTarget *t, int x,
                   int y, int cx0, int cy0, int cx1, int cy1) {
  float *nv;
  int need;
  if (x < cx0 || y < cy0 || x >= cx1 || y >= cy1)
    return 1;
  if (x < 0 || y < 0 || x >= t->w || y >= t->h)
    return 1;
  need = *n + 12;
  if (need > *cap) {
    int c = *cap ? *cap * 2 : 256;
    while (c < need)
      c *= 2;
    nv = (float *)realloc(*verts, (size_t)c * sizeof(float));
    if (!nv)
      return 0;
    *verts = nv;
    *cap = c;
  }
  quad_ndc(*verts + *n, t, x, y, x + 1, y + 1);
  *n += 12;
  return 1;
}

void sk_gpu_draw_text(SkGpuTarget *target, const char *text, float x, float y,
                      float size, uint8_t r, uint8_t g, uint8_t b, uint8_t a,
                      int cx0, int cy0, int cx1, int cy1) {
  float px;
  float scale;
  float col[4];
  float *verts = NULL;
  int n = 0;
  int cap = 0;
  int advance;
  int baseline;
  int cx;
  int fast;
  const char *p;
  if (!target || !text || a == 0 || !gpu_ready())
    return;
  bind_target(target);
  if (!set_scissor(target, cx0, cy0, cx1, cy1))
    return;
  px = size > 0.f ? size : 8.f;
  scale = px / 8.f;
  advance = gpu_advance(px);
  baseline = (int)(px - 1.f + 0.5f);
  if (baseline < 0)
    baseline = 0;
  fast = scale >= 0.999f && scale <= 1.001f;
  cx = gpu_floor(x);
  premul(r, g, b, a, col);
  for (p = text; *p;) {
    int clen = sk_utf8_clen(p);
    unsigned char ch = (unsigned char)*p;
    const uint8_t *glyph;
    int gy;
    int row;
    int bit;
    if (clen < 1)
      clen = 1;
    if (ch < 32 || ch > 126)
      ch = '?';
    glyph = sk_font8[ch - 32];
    gy = gpu_floor(y) - baseline;
    if (fast) {
      for (row = 0; row < 8; row++) {
        uint8_t bits = glyph[row];
        for (bit = 0; bit < 8; bit++) {
          if (bits & (uint8_t)(1u << bit)) {
            if (!push_px(&verts, &n, &cap, target, cx + bit, gy + row, cx0, cy0,
                         cx1, cy1))
              goto draw;
          }
        }
      }
    } else {
      int sr;
      int sc;
      for (sr = 0; sr < advance; sr++) {
        row = (int)((float)sr / scale);
        if (row > 7)
          row = 7;
        for (sc = 0; sc < advance; sc++) {
          bit = (int)((float)sc / scale);
          if (bit > 7)
            bit = 7;
          if (glyph[row] & (uint8_t)(1u << bit)) {
            if (!push_px(&verts, &n, &cap, target, cx + sc, gy + sr, cx0, cy0,
                         cx1, cy1))
              goto draw;
          }
        }
      }
    }
    cx += advance;
    p += clen;
  }
draw:
  if (n > 0)
    draw_verts(verts, n / 2, col, a == 255);
  free(verts);
}

const uint8_t *sk_gpu_read(SkGpuTarget *target, size_t *out_size) {
  size_t nbytes;
  size_t stride;
  uint8_t *tmp;
  int y;
  if (!target || !gpu_ready()) {
    if (out_size)
      *out_size = 0;
    return NULL;
  }
  nbytes = (size_t)target->w * (size_t)target->h * 4u;
  if (target->fresh && target->read) {
    if (out_size)
      *out_size = nbytes;
    return target->read;
  }
  bind_target(target);
  /* bind_target clears the cache flag. Restore it after the read. */
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  tmp = (uint8_t *)malloc(nbytes);
  if (!tmp) {
    if (out_size)
      *out_size = 0;
    return NULL;
  }
  glReadPixels(0, 0, target->w, target->h, GL_RGBA, GL_UNSIGNED_BYTE, tmp);
  if (!target->read)
    target->read = (uint8_t *)malloc(nbytes);
  if (!target->read) {
    free(tmp);
    if (out_size)
      *out_size = 0;
    return NULL;
  }
  stride = (size_t)target->w * 4u;
  for (y = 0; y < target->h; y++)
    memcpy(target->read + (size_t)y * stride,
           tmp + (size_t)(target->h - 1 - y) * stride, stride);
  free(tmp);
  target->fresh = 1;
  if (out_size)
    *out_size = nbytes;
  return target->read;
}
