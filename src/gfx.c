#include "gfx.h"

#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include "stb_image.h"

arc_stats gfx_stats;
void gfx_stats_reset(void) { gfx_stats.draw_calls = 0; gfx_stats.quads = 0; }

/* ------------------------------------------------------------------ paths */

static char path_buf[512];

const char *gfx_asset_path(const char *rel)
{
#ifdef __vita__
    snprintf(path_buf, sizeof path_buf, "app0:%s", rel);
#else
    static char *base = NULL;
    if (!base) base = SDL_GetBasePath();
    snprintf(path_buf, sizeof path_buf, "%s%s", base ? base : "./", rel);
#endif
    return path_buf;
}

static char *read_file(const char *path, long *out_len)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)n + 1);
    if (!buf) { fclose(f); return NULL; }
    if (fread(buf, 1, (size_t)n, f) != (size_t)n) { free(buf); fclose(f); return NULL; }
    buf[n] = 0;
    fclose(f);
    if (out_len) *out_len = n;
    return buf;
}

/* ---------------------------------------------------------------- shaders */

/* Shader sources are written once, in a neutral dialect, and get a
 * platform preamble prepended here. Rules for shader authors:
 *   - declare inputs with `attribute`, interpolants with `varying`
 *   - sample with TEX2D(), write with FRAGCOLOR
 *   - no #version line in the file */
#if ARC_GLSL_ES
static const char *PRE_VERT =
    "#define TEX2D texture2D\n";
static const char *PRE_FRAG =
    "precision mediump float;\n"
    "#define TEX2D texture2D\n"
    "#define FRAGCOLOR gl_FragColor\n";
#else
static const char *PRE_VERT =
    "#version 330 core\n"
    "#define attribute in\n"
    "#define varying out\n"
    "#define TEX2D texture\n";
static const char *PRE_FRAG =
    "#version 330 core\n"
    "#define varying in\n"
    "#define TEX2D texture\n"
    "#define FRAGCOLOR arc_frag_out\n"
    "out vec4 arc_frag_out;\n";
#endif

#ifdef __vita__
#include <psp2/io/stat.h>

/* Shader binaries: the whole point of this dance is that a released VPK must
 * not require the player to extract libshacccg.suprx onto their own console.
 *
 *   1. If shaders/<name>.<stage>.gxp shipped in the VPK, load it - no compiler.
 *   2. Otherwise compile the GLSL at runtime (dev machine, suprx installed)
 *      and dump the binary to ux0:data/arclight/shaders/.
 *   3. The developer copies those .gxp files into the repo and rebuilds; from
 *      then on every install takes path 1.
 *
 * See docs/TECH_PLAN.md - this is the M0 go/no-go assumption. */
#define GXP_DUMP_DIR "ux0:data/arclight/shaders"

static int load_stage_binary(GLuint sh, const char *name, const char *stage)
{
    char rel[256];
    long len = 0;
    snprintf(rel, sizeof rel, "shaders/%s.%s.gxp", name, stage);
    char *bin = read_file(gfx_asset_path(rel), &len);
    if (!bin) return 0;

    glShaderBinary(1, &sh, 0, bin, (GLsizei)len);
    free(bin);
    SDL_Log("gfx: %s.%s loaded as precompiled binary", name, stage);
    return 1;
}

static void dump_stage_binary(GLuint sh, const char *name, const char *stage)
{
    GLsizei len = 0;
    static uint8_t buf[192 * 1024];

    vglGetShaderBinary(sh, sizeof buf, &len, buf);
    if (len <= 0) { SDL_Log("gfx: no binary produced for %s.%s", name, stage); return; }

    sceIoMkdir("ux0:data/arclight", 0777);
    sceIoMkdir(GXP_DUMP_DIR, 0777);

    char path[256];
    snprintf(path, sizeof path, GXP_DUMP_DIR "/%s.%s.gxp", name, stage);
    FILE *f = fopen(path, "wb");
    if (!f) { SDL_Log("gfx: cannot write %s", path); return; }
    fwrite(buf, 1, (size_t)len, f);
    fclose(f);
    SDL_Log("gfx: dumped %s (%d bytes) - copy into shaders/ and rebuild", path, (int)len);
}
#endif /* __vita__ */

static GLuint compile_stage(GLenum type, const char *pre, const char *src,
                            const char *name, const char *stage)
{
    GLuint sh = glCreateShader(type);

#ifdef __vita__
    if (load_stage_binary(sh, name, stage)) return sh;
#endif

    const char *parts[2] = { pre, src };
    glShaderSource(sh, 2, parts, NULL);
    glCompileShader(sh);

    GLint ok = 0;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048] = {0};
        glGetShaderInfoLog(sh, sizeof log - 1, NULL, log);
        SDL_Log("gfx: %s.%s failed to compile:\n%s", name, stage, log);
#ifdef __vita__
        SDL_Log("gfx: (on hardware this usually means libshacccg.suprx is missing "
                "and no precompiled .gxp shipped)");
#endif
        glDeleteShader(sh);
        return 0;
    }

#ifdef __vita__
    dump_stage_binary(sh, name, stage);
#endif
    return sh;
}

static void cache_uniforms(arc_shader *s)
{
    s->u_proj  = glGetUniformLocation(s->prog, "u_proj");
    s->u_tex   = glGetUniformLocation(s->prog, "u_tex");
    s->u_tex2  = glGetUniformLocation(s->prog, "u_tex2");
    s->u_texel = glGetUniformLocation(s->prog, "u_texel");
    s->u_dir   = glGetUniformLocation(s->prog, "u_dir");
    s->u_param = glGetUniformLocation(s->prog, "u_param");
}

int gfx_shader_load(arc_shader *s, const char *name)
{
    char rel[256];
    memset(s, 0, sizeof *s);

    snprintf(rel, sizeof rel, "shaders/%s.vert", name);
    char *vsrc = read_file(gfx_asset_path(rel), NULL);
    /* Post passes share one trivial vertex stage rather than duplicating it. */
    if (!vsrc) vsrc = read_file(gfx_asset_path("shaders/fullscreen.vert"), NULL);
    snprintf(rel, sizeof rel, "shaders/%s.frag", name);
    char *fsrc = read_file(gfx_asset_path(rel), NULL);
    if (!vsrc || !fsrc) {
        SDL_Log("gfx: shader '%s' source missing (vert:%s frag:%s)",
                name, vsrc ? "ok" : "MISSING", fsrc ? "ok" : "MISSING");
        free(vsrc); free(fsrc);
        return 0;
    }

    GLuint vs = compile_stage(GL_VERTEX_SHADER,   PRE_VERT, vsrc, name, "vert");
    GLuint fs = compile_stage(GL_FRAGMENT_SHADER, PRE_FRAG, fsrc, name, "frag");
    free(vsrc); free(fsrc);
    if (!vs || !fs) return 0;

    s->prog = glCreateProgram();
    glAttachShader(s->prog, vs);
    glAttachShader(s->prog, fs);
    /* Fixed attribute slots so one vertex layout serves every shader. */
    glBindAttribLocation(s->prog, 0, "a_pos");
    glBindAttribLocation(s->prog, 1, "a_uv");
    glBindAttribLocation(s->prog, 2, "a_color");
    glLinkProgram(s->prog);

    GLint ok = 0;
    glGetProgramiv(s->prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[2048] = {0};
        glGetProgramInfoLog(s->prog, sizeof log - 1, NULL, log);
        SDL_Log("gfx: %s failed to link:\n%s", name, log);
        return 0;
    }
    glDeleteShader(vs);
    glDeleteShader(fs);

    cache_uniforms(s);
    return 1;
}

void gfx_shader_use(const arc_shader *s) { glUseProgram(s->prog); }

/* --------------------------------------------------------- render targets */

int gfx_target_create(arc_target *t, int w, int h)
{
    memset(t, 0, sizeof *t);
    t->w = w; t->h = h;

    glGenTextures(1, &t->tex);
    glBindTexture(GL_TEXTURE_2D, t->tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glGenFramebuffers(1, &t->fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, t->fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t->tex, 0);

    GLenum st = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (st != GL_FRAMEBUFFER_COMPLETE) {
        SDL_Log("gfx: incomplete FBO %dx%d (0x%x)", w, h, st);
        return 0;
    }
    return 1;
}

void gfx_target_destroy(arc_target *t)
{
    if (t->fbo) glDeleteFramebuffers(1, &t->fbo);
    if (t->tex) glDeleteTextures(1, &t->tex);
    memset(t, 0, sizeof *t);
}

void gfx_target_bind(const arc_target *t)
{
    glBindFramebuffer(GL_FRAMEBUFFER, t ? t->fbo : 0);
}

/* --------------------------------------------------------------- textures */

int gfx_texture_from_rgba(arc_texture *t, const uint8_t *px, int w, int h, int smooth)
{
    t->w = w; t->h = h;
    glGenTextures(1, &t->id);
    glBindTexture(GL_TEXTURE_2D, t->id);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
    GLint f = smooth ? GL_LINEAR : GL_NEAREST;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, f);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, f);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return t->id != 0;
}

int gfx_texture_load(arc_texture *t, const char *path, int smooth)
{
    int w, h, n;
    stbi_uc *px = stbi_load(gfx_asset_path(path), &w, &h, &n, 4);
    if (!px) { SDL_Log("gfx: stbi failed on %s", path); return 0; }
    int ok = gfx_texture_from_rgba(t, px, w, h, smooth);
    stbi_image_free(px);
    return ok;
}

void gfx_texture_destroy(arc_texture *t)
{
    if (t->id) glDeleteTextures(1, &t->id);
    t->id = 0;
}

/* ---------------------------------------------------------------- batcher */

#define BATCH_MAX_QUADS 2048

typedef struct { float x, y, u, v; uint32_t c; } arc_vert;

static struct {
    arc_vert  verts[BATCH_MAX_QUADS * 6];
    int       count;              /* vertices, not quads */
    GLuint    vbo, vao;
    const arc_shader *shader;
    GLuint    tex;
    int       active;
} B;

static void batch_setup_layout(void)
{
    glBindBuffer(GL_ARRAY_BUFFER, B.vbo);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(arc_vert), (void *)0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(arc_vert), (void *)(2 * sizeof(float)));
    glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(arc_vert), (void *)(4 * sizeof(float)));
}

static void batch_flush(void)
{
    if (!B.count) return;
    glBindBuffer(GL_ARRAY_BUFFER, B.vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(B.count * sizeof(arc_vert)), B.verts, GL_STREAM_DRAW);
#ifndef __vita__
    glBindVertexArray(B.vao);
#endif
    batch_setup_layout();
    glDrawArrays(GL_TRIANGLES, 0, B.count);
    gfx_stats.draw_calls++;
    B.count = 0;
}

void gfx_batch_begin(const arc_shader *s, const arc_texture *tex, int target_w, int target_h)
{
    B.shader = s;
    B.tex    = tex ? tex->id : 0;
    B.count  = 0;
    B.active = 1;

    gfx_shader_use(s);
    /* Orthographic projection, y down, packed as 4 floats: scale.xy + offset.xy. */
    if (s->u_proj >= 0) {
        float p[4] = { 2.0f / (float)target_w, -2.0f / (float)target_h, -1.0f, 1.0f };
        glUniform4fv(s->u_proj, 1, p);
    }
    if (s->u_tex >= 0) glUniform1i(s->u_tex, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, B.tex);
}

void gfx_batch_quad(float x, float y, float w, float h,
                    float u0, float v0, float u1, float v1, uint32_t rgba)
{
    if (B.count + 6 > BATCH_MAX_QUADS * 6) batch_flush();

    arc_vert *v = &B.verts[B.count];
    float x1 = x + w, y1 = y + h;
    v[0] = (arc_vert){ x,  y,  u0, v0, rgba };
    v[1] = (arc_vert){ x1, y,  u1, v0, rgba };
    v[2] = (arc_vert){ x1, y1, u1, v1, rgba };
    v[3] = (arc_vert){ x,  y,  u0, v0, rgba };
    v[4] = (arc_vert){ x1, y1, u1, v1, rgba };
    v[5] = (arc_vert){ x,  y1, u0, v1, rgba };
    B.count += 6;
    gfx_stats.quads++;
}

void gfx_batch_end(void)
{
    batch_flush();
    B.active = 0;
}

/* One screen-filling quad in clip space; shaders using it ignore u_proj. */
void gfx_fullscreen(void)
{
    static const arc_vert q[6] = {
        { -1, -1, 0, 0, 0xFFFFFFFFu }, {  1, -1, 1, 0, 0xFFFFFFFFu }, {  1,  1, 1, 1, 0xFFFFFFFFu },
        { -1, -1, 0, 0, 0xFFFFFFFFu }, {  1,  1, 1, 1, 0xFFFFFFFFu }, { -1,  1, 0, 1, 0xFFFFFFFFu },
    };
    glBindBuffer(GL_ARRAY_BUFFER, B.vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof q, q, GL_STREAM_DRAW);
#ifndef __vita__
    glBindVertexArray(B.vao);
#endif
    batch_setup_layout();
    glDrawArrays(GL_TRIANGLES, 0, 6);
    gfx_stats.draw_calls++;
}

/* -------------------------------------------------------------- lifecycle */

void gfx_clear(float r, float g, float b, float a)
{
    glClearColor(r, g, b, a);
    glClear(GL_COLOR_BUFFER_BIT);
}

int gfx_init(void)
{
    memset(&B, 0, sizeof B);
#ifdef __vita__
    /* Only used on the dev console, when compiling GLSL to dump .gxp binaries. */
    vglSetupRuntimeShaderCompiler(SHARK_OPT_DEFAULT, 1, 0, 1);
#else
    glGenVertexArrays(1, &B.vao);
    glBindVertexArray(B.vao);
#endif
    glGenBuffers(1, &B.vbo);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    return 1;
}

void gfx_shutdown(void)
{
    if (B.vbo) glDeleteBuffers(1, &B.vbo);
#ifndef __vita__
    if (B.vao) glDeleteVertexArrays(1, &B.vao);
#endif
}
