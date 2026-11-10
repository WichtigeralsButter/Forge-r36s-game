// FORGE fuer R36S (ArkOS) - Version 3
// 2 Inseln (Stonewake's Cross, Forgotten Kingdom), 38 Erze, Spitzhacken, Rassen,
// 4-stufiges Schmieden, Kampf mit Ausdauer/Dash, Quests, Runen, Portal.
// SDL2 + OpenGL ES 2.0
#include <SDL2/SDL.h>
#include <SDL2/SDL_opengles2.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdarg>
#include <cctype>
#include <string>
#include <vector>
#include <algorithm>

static const float PI = 3.14159265f;

// ---------------------------------------------------------------
// Steuerung (aenderbar ueber controls.cfg)
// ---------------------------------------------------------------
static int BTN_A = 0, BTN_B = 1, BTN_X = 2, BTN_Y = 3, BTN_L = 4, BTN_R = 5, BTN_SELECT = 6, BTN_START = 7;
static int BTN_UP = -1, BTN_DOWN = -1, BTN_LEFT = -1, BTN_RIGHT = -1;
static int AX_LX = 0, AX_LY = 1, AX_RX = 2, AX_RY = 3;

struct CfgEntry { const char* name; int* val; };
static void loadControls() {
    CfgEntry t[] = {{"A", &BTN_A}, {"B", &BTN_B}, {"X", &BTN_X}, {"Y", &BTN_Y}, {"L", &BTN_L}, {"R", &BTN_R},
                    {"SELECT", &BTN_SELECT}, {"START", &BTN_START},
                    {"UP", &BTN_UP}, {"DOWN", &BTN_DOWN}, {"LEFT", &BTN_LEFT}, {"RIGHT", &BTN_RIGHT},
                    {"AXIS_LX", &AX_LX}, {"AXIS_LY", &AX_LY}, {"AXIS_RX", &AX_RX}, {"AXIS_RY", &AX_RY}};
    FILE* f = fopen("controls.cfg", "r");
    if (!f) return;
    char k[32];
    int v;
    while (fscanf(f, " %31[^=\n]=%d", k, &v) == 2) {
        for (auto& e : t)
            if (!strcmp(e.name, k)) *e.val = v;
    }
    fclose(f);
}

// ---------------------------------------------------------------
// Mathe
// ---------------------------------------------------------------
struct V3 { float x, y, z; };
static V3 sub(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
static float dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static V3 cross(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
static V3 norm(V3 a) { float l = sqrtf(dot(a, a)); if (l < 1e-6f) l = 1; return {a.x / l, a.y / l, a.z / l}; }

struct M { float m[16]; };
static M I() { M r; memset(&r, 0, sizeof r); r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1; return r; }
static M mul(const M& a, const M& b) {
    M r;
    for (int c = 0; c < 4; c++)
        for (int row = 0; row < 4; row++) {
            float s = 0;
            for (int k = 0; k < 4; k++) s += a.m[k * 4 + row] * b.m[c * 4 + k];
            r.m[c * 4 + row] = s;
        }
    return r;
}
static M T(float x, float y, float z) { M r = I(); r.m[12] = x; r.m[13] = y; r.m[14] = z; return r; }
static M S(float x, float y, float z) { M r = I(); r.m[0] = x; r.m[5] = y; r.m[10] = z; return r; }
static M RY(float a) { M r = I(); float c = cosf(a), s = sinf(a); r.m[0] = c; r.m[8] = s; r.m[2] = -s; r.m[10] = c; return r; }
static M RX(float a) { M r = I(); float c = cosf(a), s = sinf(a); r.m[5] = c; r.m[9] = -s; r.m[6] = s; r.m[10] = c; return r; }
static M persp(float fovy, float asp, float n, float f) {
    M r; memset(&r, 0, sizeof r);
    float t = 1.0f / tanf(fovy / 2);
    r.m[0] = t / asp; r.m[5] = t; r.m[10] = (f + n) / (n - f); r.m[11] = -1; r.m[14] = 2 * f * n / (n - f);
    return r;
}
static M ortho(float l, float r_, float b, float t, float n, float f) {
    M r = I();
    r.m[0] = 2 / (r_ - l); r.m[5] = 2 / (t - b); r.m[10] = -2 / (f - n);
    r.m[12] = -(r_ + l) / (r_ - l); r.m[13] = -(t + b) / (t - b); r.m[14] = -(f + n) / (f - n);
    return r;
}
static M lookAt(V3 e, V3 c, V3 u) {
    V3 f = norm(sub(c, e));
    V3 s = norm(cross(f, u));
    V3 uu = cross(s, f);
    M r = I();
    r.m[0] = s.x; r.m[4] = s.y; r.m[8] = s.z;
    r.m[1] = uu.x; r.m[5] = uu.y; r.m[9] = uu.z;
    r.m[2] = -f.x; r.m[6] = -f.y; r.m[10] = -f.z;
    r.m[12] = -dot(s, e); r.m[13] = -dot(uu, e); r.m[14] = dot(f, e);
    return r;
}
static float wrapAngle(float a) {
    while (a > PI) a -= 2 * PI;
    while (a < -PI) a += 2 * PI;
    return a;
}
static float tri(float x) { return fabsf(fmodf(x, 2.0f) - 1.0f); }
static float clamp01(float x) { return x < 0 ? 0 : (x > 1 ? 1 : x); }

// ---------------------------------------------------------------
// OpenGL
// ---------------------------------------------------------------
static GLuint prog = 0, cubeVbo = 0, discVbo = 0;
static GLint uVP, uModel, uRot, uColor, uFog, uLit, uFogOn, uTex, aPos, aNrm;
static int SW = 640, SH = 480;

static const char* VS =
    "attribute vec3 aPos;attribute vec3 aNrm;\n"
    "uniform mat4 uVP;uniform mat4 uModel;uniform mat3 uRot;\n"
    "varying float vLight;varying float vDist;varying vec3 vPos;\n"
    "void main(){vec4 w=uModel*vec4(aPos,1.0);vPos=w.xyz;gl_Position=uVP*w;\n"
    "vec3 n=normalize(uRot*aNrm);\n"
    "float l=max(dot(n,normalize(vec3(0.4,0.8,0.3))),0.0);\n"
    "vLight=0.38+0.45*l+0.17*(0.5+0.5*n.y);vDist=gl_Position.w;}\n";
static const char* FS =
    "precision mediump float;\n"
    "uniform vec4 uColor;uniform vec3 uFog;uniform float uLit;uniform float uFogOn;uniform float uTex;\n"
    "varying float vLight;varying float vDist;varying vec3 vPos;\n"
    "void main(){vec3 c=uColor.rgb;\n"
    "float n=fract(sin(dot(floor(vPos.xz*1.3+vPos.y*0.7),vec2(12.9898,78.233)))*43758.5453);\n"
    "c*=1.0-uTex*0.14*n;\n"
    "c*=mix(1.0,vLight,uLit);\n"
    "float f=clamp((vDist-30.0)/48.0,0.0,1.0)*uFogOn;\n"
    "gl_FragColor=vec4(mix(c,uFog,f),uColor.a);}\n";

static GLuint compileShader(GLenum t, const char* s) {
    GLuint h = glCreateShader(t);
    glShaderSource(h, 1, &s, 0);
    glCompileShader(h);
    GLint ok = 0;
    glGetShaderiv(h, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char b[512];
        glGetShaderInfoLog(h, 512, 0, b);
        fprintf(stderr, "Shader-Fehler: %s\n", b);
    }
    return h;
}

static void buildMeshes() {
    static const float N[6][3] = {{0, 0, 1}, {0, 0, -1}, {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}};
    std::vector<float> v;
    for (int f = 0; f < 6; f++) {
        float nx = N[f][0], ny = N[f][1], nz = N[f][2];
        float ux, uy, uz, vx, vy, vz;
        if (ny != 0) { ux = 1; uy = 0; uz = 0; vx = 0; vy = 0; vz = 1; }
        else if (nx != 0) { ux = 0; uy = 1; uz = 0; vx = 0; vy = 0; vz = 1; }
        else { ux = 1; uy = 0; uz = 0; vx = 0; vy = 1; vz = 0; }
        static const float c[4][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
        static const int idx[6] = {0, 1, 2, 0, 2, 3};
        for (int i = 0; i < 6; i++) {
            float a = c[idx[i]][0] * 0.5f, b = c[idx[i]][1] * 0.5f;
            v.push_back(nx * 0.5f + ux * a + vx * b);
            v.push_back(ny * 0.5f + uy * a + vy * b);
            v.push_back(nz * 0.5f + uz * a + vz * b);
            v.push_back(nx); v.push_back(ny); v.push_back(nz);
        }
    }
    glGenBuffers(1, &cubeVbo);
    glBindBuffer(GL_ARRAY_BUFFER, cubeVbo);
    glBufferData(GL_ARRAY_BUFFER, v.size() * sizeof(float), v.data(), GL_STATIC_DRAW);

    std::vector<float> d;
    float c0[6] = {0, 0, 0, 0, 1, 0};
    d.insert(d.end(), c0, c0 + 6);
    for (int i = 0; i <= 32; i++) {
        float a = i * 2 * PI / 32;
        float p[6] = {cosf(a), 0, sinf(a), 0, 1, 0};
        d.insert(d.end(), p, p + 6);
    }
    glGenBuffers(1, &discVbo);
    glBindBuffer(GL_ARRAY_BUFFER, discVbo);
    glBufferData(GL_ARRAY_BUFFER, d.size() * sizeof(float), d.data(), GL_STATIC_DRAW);
}

static void useMesh(GLuint vbo) {
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glVertexAttribPointer(aPos, 3, GL_FLOAT, GL_FALSE, 24, (void*)0);
    glVertexAttribPointer(aNrm, 3, GL_FLOAT, GL_FALSE, 24, (void*)12);
}

static void setObj(const M& model, float r, float g, float b, float a) {
    glUniformMatrix4fv(uModel, 1, GL_FALSE, model.m);
    float rot[9];
    for (int c = 0; c < 3; c++) {
        float x = model.m[c * 4], y = model.m[c * 4 + 1], z = model.m[c * 4 + 2];
        float l = sqrtf(x * x + y * y + z * z);
        if (l < 1e-6f) l = 1;
        rot[c * 3] = x / l; rot[c * 3 + 1] = y / l; rot[c * 3 + 2] = z / l;
    }
    glUniformMatrix3fv(uRot, 1, GL_FALSE, rot);
    glUniform4f(uColor, r, g, b, a);
}
static void drawBox(const M& model, const float* c, float a = 1) {
    setObj(model, c[0], c[1], c[2], a);
    glDrawArrays(GL_TRIANGLES, 0, 36);
}
static void box(const M& p, float x, float y, float z, float sx, float sy, float sz, const float* c, float a = 1) {
    drawBox(mul(p, mul(T(x, y, z), S(sx, sy, sz))), c, a);
}
static void drawDisc(float y, float radius, const float* c) {
    useMesh(discVbo);
    setObj(mul(T(0, y, 0), S(radius, 1, radius)), c[0], c[1], c[2], 1);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 34);
    useMesh(cubeVbo);
}

// ---------------------------------------------------------------
// Mini-Schrift 3x5
// ---------------------------------------------------------------
struct Glyph { char c; const char* b; };
static const Glyph FONT[] = {
    {'A', "010101111101101"}, {'B', "110101110101110"}, {'C', "011100100100011"}, {'D', "110101101101110"},
    {'E', "111100110100111"}, {'F', "111100110100100"}, {'G', "011100101101011"}, {'H', "101101111101101"},
    {'I', "111010010010111"}, {'J', "001001001101010"}, {'K', "101101110101101"}, {'L', "100100100100111"},
    {'M', "101111111101101"}, {'N', "110101101101101"}, {'O', "010101101101010"}, {'P', "110101110100100"},
    {'Q', "010101101111011"}, {'R', "110101110101101"}, {'S', "011100010001110"}, {'T', "111010010010010"},
    {'U', "101101101101111"}, {'V', "101101101101010"}, {'W', "101101111111101"}, {'X', "101101010101101"},
    {'Y', "101101010010010"}, {'Z', "111001010100111"},
    {'0', "111101101101111"}, {'1', "010110010010111"}, {'2', "110001010100111"}, {'3', "110001010001110"},
    {'4', "101101111001001"}, {'5', "111100110001110"}, {'6', "011100111101111"}, {'7', "111001010100100"},
    {'8', "111101111101111"}, {'9', "111101111001110"},
    {':', "000010000010000"}, {'+', "000010111010000"}, {'-', "000000111000000"}, {'/', "001001010100100"},
    {'.', "000000000000010"}, {'>', "100010001010100"}, {'!', "010010010000010"},
    {'(', "001010010010001"}, {')', "100010010010100"}, {'\'', "010010000000000"}, {'%', "101001010100101"},
    {'[', "110100100100110"}, {']', "011001001001011"}, {'$', "011110010011110"}, {',', "000000000010100"},
    {'=', "000111000111000"}, {'x', "101101010101101"}};
static const char* glyphOf[128];
static void initFont() {
    memset(glyphOf, 0, sizeof glyphOf);
    for (auto& g : FONT) glyphOf[(int)g.c] = g.b;
}
static int textW(const char* s, int sc) { return (int)strlen(s) * 4 * sc; }

static void quadV(std::vector<float>& v, float x, float y, float w, float h) {
    float p[6][2] = {{x, y}, {x + w, y}, {x + w, y + h}, {x, y}, {x + w, y + h}, {x, y + h}};
    for (auto& q : p) {
        v.push_back(q[0]); v.push_back(q[1]); v.push_back(0);
        v.push_back(0); v.push_back(0); v.push_back(1);
    }
}
static void flushUI(std::vector<float>& v, float r, float g, float b, float a) {
    if (v.empty()) return;
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glVertexAttribPointer(aPos, 3, GL_FLOAT, GL_FALSE, 24, v.data());
    glVertexAttribPointer(aNrm, 3, GL_FLOAT, GL_FALSE, 24, v.data() + 3);
    glUniform4f(uColor, r, g, b, a);
    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)(v.size() / 6));
    v.clear();
}
static void drawRect(float x, float y, float w, float h, float r, float g, float b, float a) {
    std::vector<float> v;
    quadV(v, x, y, w, h);
    flushUI(v, r, g, b, a);
}
static void drawText(const char* s, float x, float y, int sc, const float* c) {
    for (int pass = 0; pass < 2; pass++) {
        std::vector<float> v;
        float ox = pass == 0 ? (float)sc : 0, oy = pass == 0 ? (float)sc : 0;
        float cx = x;
        for (const char* p = s; *p; p++) {
            int ch = *p;
            if (ch != 'x') ch = toupper((unsigned char)ch);
            const char* gl = (ch > 0 && ch < 128) ? glyphOf[ch] : 0;
            if (gl) {
                for (int i = 0; i < 15; i++)
                    if (gl[i] == '1') quadV(v, cx + ox + (i % 3) * sc, y + oy + (i / 3) * sc, (float)sc, (float)sc);
            }
            cx += 4 * sc;
        }
        if (pass == 0) flushUI(v, 0, 0, 0, 0.8f);
        else flushUI(v, c[0], c[1], c[2], 1);
    }
}
static const float WHITE3[3] = {1, 1, 1}, GRAY3[3] = {0.6f, 0.6f, 0.6f}, GOLD3[3] = {1.0f, 0.85f, 0.3f};
static const float GREEN3[3] = {0.5f, 1.0f, 0.5f}, RED3[3] = {1.0f, 0.45f, 0.4f}, LGRAY3[3] = {0.8f, 0.8f, 0.8f};
static void drawTextC(const char* s, float y, int sc, const float* c) {
    drawText(s, (SW - textW(s, sc)) / 2.0f, y, sc, c);
}
static void drawBar(float x, float y, float w, float h, float frac, float r, float g, float b) {
    drawRect(x, y, w, h, 0, 0, 0, 0.65f);
    drawRect(x + 1, y + 1, (w - 2) * clamp01(frac), h - 2, r, g, b, 1);
}
static void panel(float x, float y, float w, float h) {
    drawRect(x - 3, y - 3, w + 6, h + 6, 0.85f, 0.65f, 0.25f, 0.9f);
    drawRect(x, y, w, h, 0.07f, 0.06f, 0.08f, 0.93f);
}
static void selRow(float x, float y, float w, float h) { drawRect(x, y, w, h, 0.35f, 0.28f, 0.1f, 0.95f); }
static void outline(float x, float y, float w, float h, float t, const float* c) {
    drawRect(x, y, w, t, c[0], c[1], c[2], 1);
    drawRect(x, y + h - t, w, t, c[0], c[1], c[2], 1);
    drawRect(x, y, t, h, c[0], c[1], c[2], 1);
    drawRect(x + w - t, y, t, h, c[0], c[1], c[2], 1);
}

// ---------------------------------------------------------------
// Spieldaten
// ---------------------------------------------------------------
enum { R_PEB = 1, R_ROCK = 2, R_BOUL = 4, R_BAS = 8, R_CORE = 16, R_VEIN = 32, R_VOLC = 64 };
static const int NORE = 38;
struct OreDef { const char* name; int rar; int chance; float mult; float price; int trait; int rocks; float col[3]; };
static const OreDef ORES[NORE] = {
    {"STONE", 0, 1, 0.2f, 3, 0, R_PEB, {0.62f, 0.62f, 0.64f}},
    {"SAND STONE", 0, 2, 0.25f, 3.75f, 0, R_PEB | R_ROCK, {0.85f, 0.78f, 0.55f}},
    {"COPPER", 0, 3, 0.3f, 4.5f, 0, R_PEB | R_ROCK | R_BOUL, {0.85f, 0.45f, 0.2f}},
    {"IRON", 0, 5, 0.35f, 5.25f, 0, R_PEB | R_ROCK | R_BOUL, {0.55f, 0.6f, 0.72f}},
    {"TIN", 1, 7, 0.425f, 6.38f, 0, R_ROCK | R_BOUL, {0.75f, 0.78f, 0.8f}},
    {"SILVER", 1, 12, 0.5f, 7.5f, 0, R_ROCK | R_BOUL, {0.92f, 0.94f, 0.98f}},
    {"GOLD", 1, 16, 0.65f, 19.5f, 0, R_BOUL, {1.0f, 0.85f, 0.2f}},
    {"MUSHROOMITE", 2, 22, 0.8f, 12, 0, R_ROCK | R_BOUL, {0.8f, 0.3f, 0.35f}},
    {"PLATINUM", 2, 28, 0.8f, 12, 0, R_BOUL, {0.8f, 0.85f, 0.9f}},
    {"BANANITE", 1, 30, 0.85f, 12.75f, 0, R_ROCK | R_BOUL, {0.95f, 0.9f, 0.3f}},
    {"CARDBOARDITE", 0, 31, 0.7f, 10.5f, 0, R_ROCK | R_BOUL, {0.7f, 0.55f, 0.35f}},
    {"AITE", 3, 44, 1.1f, 16.5f, 0, R_BOUL, {0.4f, 0.6f, 0.9f}},
    {"POOPITE", 3, 131, 1.2f, 18, 1, R_PEB | R_ROCK | R_BOUL, {0.45f, 0.3f, 0.15f}},
    {"COBALT", 1, 37, 1.0f, 15, 0, R_BAS | R_CORE, {0.2f, 0.35f, 0.8f}},
    {"TITANIUM", 1, 50, 1.15f, 17.25f, 0, R_BAS | R_CORE, {0.6f, 0.62f, 0.68f}},
    {"VOLCANIC ROCK", 2, 55, 1.55f, 23.25f, 0, R_VOLC, {0.5f, 0.2f, 0.15f}},
    {"LAPIS LAZULI", 1, 73, 1.3f, 22.5f, 0, R_BAS | R_CORE, {0.15f, 0.25f, 0.65f}},
    {"QUARTZ", 2, 90, 1.5f, 22.5f, 0, R_CORE | R_VEIN, {0.9f, 0.85f, 0.9f}},
    {"AMETHYST", 2, 115, 1.65f, 24.75f, 0, R_CORE | R_VEIN, {0.6f, 0.35f, 0.8f}},
    {"TOPAZ", 2, 143, 1.75f, 26.25f, 0, R_CORE | R_VEIN, {0.95f, 0.7f, 0.2f}},
    {"DIAMOND", 2, 192, 2.0f, 30, 0, R_CORE | R_VEIN, {0.5f, 0.95f, 0.95f}},
    {"SAPPHIRE", 2, 247, 2.25f, 33.75f, 0, R_CORE | R_VEIN, {0.2f, 0.4f, 0.9f}},
    {"CUPRITE", 3, 303, 2.43f, 36.45f, 0, R_CORE | R_VEIN, {0.75f, 0.2f, 0.2f}},
    {"OBSIDIAN", 3, 333, 2.35f, 35.25f, 2, R_VOLC, {0.25f, 0.1f, 0.35f}},
    {"EMERALD", 3, 363, 2.55f, 38.25f, 0, R_CORE | R_VEIN, {0.2f, 0.8f, 0.4f}},
    {"RUBY", 3, 487, 2.95f, 44.25f, 0, R_VEIN, {0.9f, 0.15f, 0.25f}},
    {"RIVALITE", 3, 569, 3.33f, 49.95f, 3, R_VEIN, {0.9f, 0.5f, 0.2f}},
    {"URANIUM", 4, 777, 3.0f, 66, 4, R_VEIN, {0.4f, 0.95f, 0.3f}},
    {"MYTHRIL", 4, 813, 3.5f, 52.5f, 5, R_VEIN, {0.6f, 0.9f, 0.95f}},
    {"EYE ORE", 4, 1333, 4.0f, 37.5f, 6, R_BAS | R_CORE | R_VEIN, {0.95f, 0.9f, 0.9f}},
    {"FIREITE", 4, 2187, 4.5f, 67.5f, 7, R_VOLC, {1.0f, 0.45f, 0.1f}},
    {"MAGMAITE", 4, 3003, 5.0f, 75, 8, R_VOLC, {1.0f, 0.3f, 0.05f}},
    {"LIGHTITE", 4, 3333, 4.6f, 69, 9, R_VEIN, {1.0f, 1.0f, 0.7f}},
    {"DEMONITE", 5, 3666, 5.5f, 82.5f, 10, R_VOLC, {0.7f, 0.1f, 0.3f}},
    {"DARKRYTE", 5, 6655, 6.3f, 94.5f, 11, R_VOLC, {0.25f, 0.18f, 0.35f}},
    {"BONEITE", 2, 222, 1.2f, 18, 0, 0, {0.9f, 0.9f, 0.8f}},
    {"DARK BONEITE", 2, 555, 2.25f, 33.75f, 0, 0, {0.5f, 0.5f, 0.55f}},
    {"SLIMITE", 3, 247, 2.25f, 37.55f, 0, 0, {0.4f, 0.9f, 0.5f}}};
static const char* TRAITN[12] = {"-", "GIFTWOLKE", "+30% LEBEN (RUESTUNG)", "+20% KRIT (WAFFE)", "AURA SCHADEN (RUESTUNG)",
                                 "+15% LEBEN (RUESTUNG)", "+15% SCHADEN, -10% LEBEN", "+20% SCHADEN, BRENNEN (WAFFE)",
                                 "+50% SCHADEN, EXPLOSION (WAFFE)", "+15% TEMPO (RUESTUNG)", "BRENNEN / DORNEN", "+15% AUSWEICHEN (RUESTUNG)"};
static const char* RARN[8] = {"COMMON", "UNCOMMON", "RARE", "EPIC", "LEGENDARY", "MYTHICAL", "RELIC", "DIVINE"};
static const float RARC[8][3] = {{0.75f, 0.75f, 0.75f}, {0.4f, 0.9f, 0.4f}, {0.35f, 0.6f, 1.0f}, {0.75f, 0.4f, 1.0f},
                                 {1.0f, 0.65f, 0.15f}, {1.0f, 0.3f, 0.3f}, {0.3f, 1.0f, 0.95f}, {1.0f, 0.95f, 0.6f}};

struct RockDef { const char* name; float hp; float sz; int world; float col[3]; };
static const RockDef ROCKS[7] = {{"PEBBLE", 10, 0.9f, 1, {0.62f, 0.62f, 0.64f}}, {"ROCK", 40, 1.3f, 1, {0.5f, 0.5f, 0.53f}},
                                 {"BOULDER", 120, 1.9f, 1, {0.42f, 0.42f, 0.46f}}, {"BASALT ROCK", 400, 1.5f, 2, {0.25f, 0.25f, 0.28f}},
                                 {"BASALT CORE", 900, 2.0f, 2, {0.2f, 0.2f, 0.25f}}, {"BASALT VEIN", 1800, 2.4f, 2, {0.17f, 0.15f, 0.22f}},
                                 {"VOLCANIC ROCK", 4500, 2.6f, 2, {0.3f, 0.12f, 0.1f}}};

static const int ROLLS[7] = {1, 1, 2, 40, 40, 80, 150};
struct Pick { const char* name; int power; float luck; int slots; int cost; int speed; int col; };
static const int NPICK = 14;
static const Pick PICKS[NPICK] = {
    {"STONE", 4, 0, 0, 0, 0, 0},          {"BRONZE", 7, 0, 0, 150, 0, 2},        {"IRON", 10, 5, 0, 500, 0, 3},
    {"GOLDEN", 16, 15, 1, 1500, 0, 6},    {"STONEWAKE'S", 33, 10, 1, 3333, -30, 0}, {"PLATINUM", 24, 25, 2, 4400, 0, 8},
    {"ARCANE", 115, 50, 3, 125000, 0, 18}, {"COBALT", 40, 35, 2, 10000, 0, 13}, {"TITANIUM", 55, 43, 2, 22500, 0, 14},
    {"URANIUM", 67, 41, 2, 37500, 0, 27}, {"MYTHRIL", 80, 55, 2, 67500, 0, 28}, {"LIGHTITE", 100, 45, 1, 98500, 0, 32},
    {"MAGMA", 135, 46, 2, 150000, 0, 31}, {"DEMONIC", 175, 47, 3, 500000, 0, 33}};

struct RaceDef { const char* name; int tier; float chance; float luck, xp, hp, spd, dmg, atk, mine, forge, life, shop, stam, dodge, reflect; const char* desc; };
static const int NRACE = 16;
static const RaceDef RACES[NRACE] = {
    {"HUMAN", 0, 25, 3, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, "+3% GLUECK, +3% XP"},
    {"ELF", 1, 15, 7, 0, 0, 0, 0, 0, 0, 0, 0, 0, 15, 0, 0, "+7% GLUECK, +15% AUSDAUER"},
    {"ZOMBIE", 1, 14, 0, 0, -25, 0, 0, 0, 0, 0, 5, 0, 0, 0, 0, "WENIGER LEBEN, ABSORB"},
    {"GOBLIN", 2, 10.5f, 0, 0, 0, 0, 0, 0, 0, 0, 0, 12, 0, 0, 0, "RABATT IM SHOP"},
    {"UNDEAD", 2, 9, 0, 0, -12, 0, 0, 0, 0, 0, 0, 0, 0, 0, 10, "-12% LEBEN, 10% REFLEKTION"},
    {"ORC", 2, 8, 0, 0, 15, 0, 10, 0, 0, 0, 0, 0, 0, 0, 0, "+15% LEBEN, +10% SCHADEN"},
    {"DWARF", 3, 7, 0, 0, 0, 0, 0, 0, 15, 5, 0, 0, 0, 0, 0, "+15% ABBAU, +5% SCHMIEDE, KRIT-ABBAU"},
    {"SHADOW", 3, 6, 0, 0, -10, 15, 5, 0, 0, 0, 0, 0, 0, 15, 0, "+15% TEMPO, 15% AUSWEICHEN"},
    {"VAMPIRE", 3, 4, 0, 0, 0, 0, 10, 0, 0, 0, 15, 0, 0, 0, 0, "+10% SCHADEN, LEBENSRAUB"},
    {"MINOTAUR", 4, 1.75f, 0, 0, 20, 0, 5, 0, 0, 0, 0, 0, 0, 0, 0, "+20% LEBEN, WUT UNTER 50%"},
    {"DRAGONBORN", 4, 1.5f, 0, 0, 20, 0, 12, 0, 0, 0, 0, 0, 0, 0, 0, "+20% LEBEN, +12% SCHADEN"},
    {"GOLEM", 4, 1.25f, 0, 0, 30, -10, 0, 0, 0, 0, 0, 0, 0, 0, 5, "+30% LEBEN, -10% TEMPO"},
    {"FELYNX", 4, 1.25f, 0, 0, 0, 20, 0, 0, 0, 0, 0, 0, 25, 20, 0, "+20% TEMPO, 20% AUSWEICHEN"},
    {"ANGEL", 5, 0.5f, 25, 0, 0, 20, 0, 0, 0, 0, 0, 0, 0, 0, 0, "+25% GLUECK, +20% TEMPO"},
    {"DEMON", 5, 0.5f, 0, 0, 0, 20, 20, 20, 0, 0, 0, 0, 0, 0, 0, "+20% TEMPO, SCHADEN, ANGRIFFSTEMPO"},
    {"ARCHANGEL", 6, 0.1f, 30, 10, 20, 10, 20, 0, 10, 10, 5, 10, 10, 10, 0, "ALLES BESSER"}};

struct EType { const char* name; float hpLo, hpHi, dmgLo, dmgHi; int lvlMin, lvlMax; float spd, goldLo, goldHi, xpLo, xpHi, sz, aggro; int flag; float col[3]; };
// flag: 1 Bomber, 2 Slime, 4 Reaper
static const int NETY = 12;
static const EType ETY[NETY] = {
    {"ZOMBIE", 20, 52, 6, 11.2f, 1, 5, 2.6f, 5, 10, 10, 20, 1.0f, 11, 0, {0.4f, 0.6f, 0.4f}},
    {"DELVER ZOMBIE", 40, 175, 12, 30.4f, 1, 10, 2.8f, 10, 20, 20, 40, 1.05f, 11, 0, {0.45f, 0.5f, 0.3f}},
    {"ELITE ZOMBIE", 70, 295, 20, 20, 1, 10, 2.8f, 15, 30, 25, 50, 1.35f, 12, 0, {0.3f, 0.45f, 0.3f}},
    {"BRUTE ZOMBIE", 220, 370, 28, 47.5f, 5, 10, 2.5f, 32.5f, 45, 43, 60, 1.5f, 12, 0, {0.35f, 0.35f, 0.3f}},
    {"BOMBER", 60, 120, 70, 70, 1, 5, 4.0f, 50, 100, 20, 40, 1.0f, 12, 1, {0.5f, 0.3f, 0.3f}},
    {"SKELETON ROGUE", 75, 195, 7, 76, 1, 10, 4.6f, 14.5f, 29, 37.5f, 75, 1.0f, 13, 0, {0.9f, 0.9f, 0.85f}},
    {"AXE SKELETON", 125, 215, 15, 38, 1, 10, 3.6f, 19, 38, 48.5f, 97, 1.15f, 13, 0, {0.85f, 0.85f, 0.8f}},
    {"DEATHAXE SKELETON", 150, 420, 35, 91, 1, 10, 3.8f, 35, 80, 58.5f, 117, 1.25f, 13, 0, {0.7f, 0.7f, 0.7f}},
    {"ELITE ROGUE SKELETON", 250, 295, 45, 114, 1, 10, 4.4f, 45, 90, 67.5f, 135, 1.15f, 13, 0, {0.9f, 0.6f, 0.3f}},
    {"ELITE DEATHAXE", 300, 570, 50, 123.5f, 1, 10, 3.6f, 60, 120, 90, 180, 1.5f, 13, 0, {0.55f, 0.5f, 0.55f}},
    {"BLAZING SLIME", 600, 870, 35, 35, 1, 10, 2.4f, 65, 130, 55, 110, 1.3f, 11, 2, {0.95f, 0.45f, 0.2f}},
    {"REAPER", 2500, 3200, 85, 85, 1, 1, 3.2f, 300, 500, 600, 900, 1.8f, 14, 4, {0.15f, 0.12f, 0.2f}}};
struct EDrop { int enemy, ore, chance; };
static const EDrop EDROPS[] = {{5, 35, 5}, {6, 35, 5}, {7, 35, 5}, {8, 36, 6}, {9, 36, 6}, {10, 37, 5}, {11, 36, 4}};
struct RDrop { int enemy, rune, chance; };
static const RDrop RDROPS[] = {{1, 0, 35}, {4, 0, 15}, {4, 2, 10}, {7, 3, 35}, {7, 5, 50}, {9, 3, 25}, {9, 5, 40}, {8, 4, 25}, {11, 3, 5}, {11, 1, 12}, {10, 3, 20}};
static const char* RUNEN[6] = {"MINER SHARD", "DRAIN EDGE", "BLAST CHIP", "FLAME SPARK", "RAGE MARK", "BRIAR NOTCH"};
static const char* RUNED[6] = {"PICKE: +10% GLUECK, 5% EXTRA ERZ", "WAFFE: +15% SCHADEN, 12% LEBENSRAUB", "WAFFE: EXPLOSION (AOE)",
                               "WAFFE: BRENNEN", "WAFFE: +30% SCHADEN UNTER 35% LEBEN", "RUESTUNG: DORNEN"};

struct WeapDef { const char* name; float base, cd, rng; int minOre, optOre; };
static const WeapDef WEAP[7] = {{"DAGGER", 4.3f, 0.35f, 3.0f, 3, 3},          {"STRAIGHT SWORD", 8.5f, 0.55f, 4.0f, 4, 8},
                                {"GAUNTLETS", 7.0f, 0.4f, 2.6f, 7, 11},        {"KATANA", 8.925f, 0.63f, 4.5f, 9, 15},
                                {"GREAT SWORD", 12.0f, 1.0f, 4.5f, 12, 20},    {"GREAT AXE", 18.75f, 0.91f, 4.5f, 16, 37},
                                {"COLOSSAL SWORD", 20.0f, 1.12f, 5.0f, 21, 46}};
struct ArmDef { const char* name; int tier; int slot; int minOre; int optOre; float hp; float def; };
static const int NARM = 9;
static const ArmDef ARM[NARM] = {{"LIGHT HELMET", 0, 1, 3, 3, 0.0375f, 5},      {"LIGHT LEGGINGS", 0, 3, 7, 10, 0.04375f, 7},
                                 {"LIGHT CHESTPLATE", 0, 2, 10, 12, 0.05f, 10},  {"MEDIUM HELMET", 1, 1, 13, 13, 0.0625f, 14},
                                 {"MEDIUM LEGGINGS", 1, 3, 17, 21, 0.075f, 18},  {"MEDIUM CHESTPLATE", 1, 2, 21, 24, 0.0875f, 25},
                                 {"HEAVY HELMET", 2, 1, 26, 26, 0.125f, 30},     {"HEAVY LEGGINGS", 2, 3, 30, 31, 0.1375f, 38},
                                 {"HEAVY CHESTPLATE", 2, 2, 37, 46, 0.1625f, 50}};
static const char* VARN[3][3] = {{"LIGHT", "LIGHT", "LIGHT"}, {"MEDIUM", "SAMURAI", "VIKING"}, {"KNIGHT", "DARK KNIGHT", "WOLF"}};
static const float VARM[3][3] = {{1, 1, 1}, {1, 1.28f, 1.68f}, {1, 1.5f, 2.0f}};
static const char* ARMN[3] = {"HELMET", "CHESTPLATE", "LEGGINGS"};
static const char* SLOTN[4] = {"WAFFE", "HELM", "BRUST", "HOSE"};

struct Quest { const char* text; int type; int target; int reward; };
// type: 0 mined, 1 forgedW, 2 forgedA, 3 kills, 4 sold, 5 level, 6 pickaxes owned
static const int NQUEST = 12;
static const Quest QUESTS[NQUEST] = {
    {"BAUE 3 ERZE AB", 0, 3, 30},           {"SCHMIEDE EINE WAFFE", 1, 1, 80},     {"BESIEGE 3 ZOMBIES", 3, 3, 120},
    {"VERKAUFE FUER 100 GOLD", 4, 100, 150}, {"SCHMIEDE EINE RUESTUNG", 2, 1, 250}, {"ERREICHE LEVEL 5", 5, 5, 400},
    {"KAUFE EINE NEUE SPITZHACKE", 6, 2, 500}, {"BESIEGE 15 GEGNER", 3, 15, 800},    {"ENDE DES ANFANGS: LEVEL 10", 5, 10, 1500},
    {"BAUE 100 ERZE AB", 0, 100, 2000},     {"BESIEGE 40 GEGNER", 3, 40, 3000},    {"VERKAUFE FUER 5000 GOLD", 4, 5000, 6000}};

struct Item {
    int kind, tier, n, ore, rar, var;
    float q, dmg, def, hpP, spd, crit, dodge, burn, expl, aura, thorns;
    int poison, val;
};
struct SaveData {
    int magic, coins, pickEq, race, spins, xp, level, questIdx, portal, world;
    int cMined, cForgedW, cForgedA, cKills, cSold;
    int ore[NORE];
    int pickOwned[NPICK];
    int runeOwn[6];
    int runeInst[6];
    int eqValid[4];
    Item eq[4];
    int nItems;
    float luckPot, minePot;
};
static SaveData G;
static std::vector<Item> items;
static const int SAVE_MAGIC = 0x46524735;

static void resetGame() {
    memset(&G, 0, sizeof G);
    G.magic = SAVE_MAGIC;
    G.race = 0;
    G.spins = 5;
    G.level = 1;
    G.world = 1;
    G.pickOwned[0] = 1;
    items.clear();
}
static void saveGame() {
    FILE* f = fopen("forge_save.dat", "wb");
    if (!f) return;
    G.nItems = (int)items.size();
    fwrite(&G, sizeof G, 1, f);
    if (G.nItems) fwrite(items.data(), sizeof(Item), G.nItems, f);
    fclose(f);
}
static void loadGame() {
    resetGame();
    FILE* f = fopen("forge_save.dat", "rb");
    if (!f) return;
    SaveData t;
    if (fread(&t, sizeof t, 1, f) == 1 && t.magic == SAVE_MAGIC && t.nItems >= 0 && t.nItems <= 300) {
        std::vector<Item> it(t.nItems);
        if (t.nItems == 0 || fread(it.data(), sizeof(Item), t.nItems, f) == (size_t)t.nItems) {
            G = t;
            items = it;
        }
    }
    fclose(f);
}

static char msgBuf[110];
static float msgT = 0;
static float msgCol[3] = {1, 0.6f, 0.2f};
static void setMsgC(const float* c, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msgBuf, sizeof msgBuf, fmt, ap);
    va_end(ap);
    msgT = 3.0f;
    msgCol[0] = c[0]; msgCol[1] = c[1]; msgCol[2] = c[2];
}
#define setMsg(...) setMsgC(GOLD3, __VA_ARGS__)

// ---- abgeleitete Werte ----
static const RaceDef& RC() { return RACES[G.race]; }
static bool runeOn(int r);
static float luckTotal() { return PICKS[G.pickEq].luck / 100.0f + RC().luck / 100.0f + (G.luckPot > 0 ? 0.2f : 0.0f) + (runeOn(0) ? 0.1f : 0.0f); }
static bool runeOn(int r) { return G.runeInst[r] > 0; }
static float mineMult() { return (1 + RC().mine / 100.0f) * (G.minePot > 0 ? 1.25f : 1.0f); }
static float mineCd() { return 0.45f * 100.0f / (100.0f + PICKS[G.pickEq].speed); }
static int pickSlots() { return PICKS[G.pickEq].slots; }
static float maxHP() {
    float p = 1 + RC().hp / 100.0f;
    for (int i = 1; i < 4; i++)
        if (G.eqValid[i]) p += G.eq[i].hpP;
    if (G.eqValid[0]) p += G.eq[0].hpP;
    if (p < 0.3f) p = 0.3f;
    return (100.0f + (G.level - 1) * 10.0f) * p;
}
static float defTotal() {
    float d = 0;
    for (int i = 1; i < 4; i++)
        if (G.eqValid[i]) d += G.eq[i].def;
    return d;
}
static float sumArm(float Item::*f) {
    float s = 0;
    for (int i = 1; i < 4; i++)
        if (G.eqValid[i]) s += G.eq[i].*f;
    return s;
}
static float weaponDmg() {
    float d = G.eqValid[0] ? G.eq[0].dmg : 1.0f;
    return d * (1 + RC().dmg / 100.0f) * (runeOn(1) ? 1.15f : 1.0f);
}
static float weaponCd() { return (G.eqValid[0] ? WEAP[G.eq[0].kind].cd : 0.4f) * 100.0f / (100.0f + RC().atk); }
static float weaponRng() { return G.eqValid[0] ? WEAP[G.eq[0].kind].rng : 2.4f; }
static float moveSpeed() { return 6.5f * (1 + RC().spd / 100.0f + sumArm(&Item::spd)); }
static float dodgeTotal() { return RC().dodge / 100.0f + sumArm(&Item::dodge); }
static float maxStam() { return 100.0f * (1 + RC().stam / 100.0f); }
static float lifesteal() { return RC().life / 100.0f + (runeOn(1) ? 0.12f : 0.0f); }
static float xpNeed() { return 40.0f + 30.0f * G.level; }
static int questProgress(int q) {
    switch (QUESTS[q].type) {
    case 0: return G.cMined;
    case 1: return G.cForgedW;
    case 2: return G.cForgedA;
    case 3: return G.cKills;
    case 4: return G.cSold;
    case 5: return G.level;
    default: { int c = 0; for (int i = 0; i < NPICK; i++) c += G.pickOwned[i] ? 1 : 0; return c; }
    }
}
static bool questDone() { return G.questIdx < NQUEST && questProgress(G.questIdx) >= QUESTS[G.questIdx].target; }
static void gainXP(int n) {
    G.xp += (int)(n * (1 + RC().xp / 100.0f));
    while (G.xp >= (int)xpNeed()) {
        G.xp -= (int)xpNeed();
        G.level++;
        if (G.level % 2 == 0) G.spins++;
        setMsg("LEVEL %d ERREICHT!", G.level);
    }
}
static int buyPrice(int p) { return (int)(p * (1 - RC().shop / 100.0f)); }

static int weaponKind(int n) {
    float w[7], tot = 0;
    for (int i = 0; i < 7; i++) {
        w[i] = n >= WEAP[i].minOre ? expf(-fabsf((float)(n - WEAP[i].optOre)) / 4.0f) : 0.0f;
        tot += w[i];
    }
    float r = ((float)rand() / RAND_MAX) * tot, acc = 0;
    for (int i = 0; i < 7; i++) { acc += w[i]; if (r <= acc && w[i] > 0) return i; }
    return 0;
}
static int armorPiece(int n) {
    float w[NARM], tot = 0;
    for (int i = 0; i < NARM; i++) {
        w[i] = n >= ARM[i].minOre ? expf(-fabsf((float)(n - ARM[i].optOre)) / 4.0f) : 0.0f;
        tot += w[i];
    }
    float r = ((float)rand() / RAND_MAX) * tot, acc = 0;
    for (int i = 0; i < NARM; i++) { acc += w[i]; if (r <= acc && w[i] > 0) return i; }
    return 0;
}
static int likelyWeapon(int n) {
    int b = 0; float bw = -1;
    for (int i = 0; i < 7; i++) { float w = n >= WEAP[i].minOre ? expf(-fabsf((float)(n - WEAP[i].optOre)) / 4.0f) : 0; if (w > bw) { bw = w; b = i; } }
    return b;
}
static int likelyArmor(int n) {
    int b = 0; float bw = -1;
    for (int i = 0; i < NARM; i++) { float w = n >= ARM[i].minOre ? expf(-fabsf((float)(n - ARM[i].optOre)) / 4.0f) : 0; if (w > bw) { bw = w; b = i; } }
    return b;
}
static int slotOf(const Item& it) { return it.kind < 7 ? 0 : ARM[it.kind - 7].slot; }
static Item makeItem(int cat, const int* sel, float q) {
    Item it;
    memset(&it, 0, sizeof it);
    int n = 0, best = 0;
    float msum = 0, val = 0;
    for (int i = 0; i < NORE; i++) {
        n += sel[i]; msum += sel[i] * ORES[i].mult; val += sel[i] * ORES[i].price;
        if (sel[i] > sel[best]) best = i;
    }
    if (n < 1) n = 1;
    float M_ = msum / n;
    it.n = n; it.ore = best; it.q = q;
    it.rar = M_ < 0.6f ? 0 : (M_ < 1.2f ? 1 : (M_ < 2.2f ? 2 : (M_ < 3.5f ? 3 : (M_ < 5 ? 4 : (M_ < 7 ? 5 : (M_ < 12 ? 6 : 7))))));
    float qf = 0.8f + 0.4f * q;
    if (cat == 0) {
        int k = weaponKind(n);
        it.kind = k;
        it.dmg = WEAP[k].base * M_ * qf;
    } else {
        int pc = armorPiece(n);
        it.kind = 7 + pc;
        it.tier = ARM[pc].tier;
        float vr = (float)rand() / RAND_MAX;
        it.var = it.tier == 0 ? 0 : (vr < 0.25f ? 2 : (vr < 0.6f ? 1 : 0));
        it.def = ARM[pc].def * M_ * qf;
        it.hpP = ARM[pc].hp * VARM[it.tier][it.var];
    }
    for (int i = 0; i < NORE; i++) {
        if (!sel[i] || sel[i] * 10 < n) continue;
        switch (ORES[i].trait) {
        case 1: it.poison = 1; break;
        case 2: if (cat == 1) it.hpP += 0.30f; break;
        case 3: if (cat == 0) it.crit += 0.2f; break;
        case 4: if (cat == 1) it.aura += 0.05f; break;
        case 5: if (cat == 1) it.hpP += 0.15f; break;
        case 6: it.dmg *= 1.15f; it.hpP -= 0.10f; break;
        case 7: if (cat == 0) { it.dmg *= 1.2f; it.burn += 0.3f; } break;
        case 8: if (cat == 0) { it.dmg *= 1.5f; it.expl += 0.35f; } break;
        case 9: if (cat == 1) it.spd += 0.15f; break;
        case 10: if (cat == 0) it.burn += 0.25f; else it.thorns += 0.25f; break;
        case 11: if (cat == 1) it.dodge += 0.15f; break;
        default: break;
        }
    }
    it.val = (int)(val * (1.2f + q) + 0.5f);
    return it;
}
static void itemName(const Item& it, char* out, int n) {
    if (it.kind < 7) snprintf(out, n, "%s %s", ORES[it.ore].name, WEAP[it.kind].name);
    else snprintf(out, n, "%s %s %s", VARN[it.tier][it.var], ORES[it.ore].name, ARMN[ARM[it.kind - 7].slot - 1]);
}

// ---------------------------------------------------------------
// Welt
// ---------------------------------------------------------------
struct Node { float x, z, sz, rot; int type; float hp, maxhp, respawn, shake; };
struct Solid { float x, z, r; };
struct Tree { float x, z, sz; };
struct Enemy { float x, z, hx, hz, hp, yaw, atk, flash, dead, burn, burnDps, maxhp, dmg, gold, xp; int type, lvl; };
struct Cliff { float x, z, w, h, d; float c; };
struct Npc { float x, z; int kind; float yaw; };  // 0 Moro, 1 Fred, 2 Maria, 3 Runemaker, 4 Wizard, 5 Cey, 6 Portal
struct Part { float x, y, z, vx, vy, vz, life; float c[3]; };
static std::vector<Node> nodes;
static std::vector<Solid> solids;
static std::vector<Tree> trees;
static std::vector<Enemy> enemies;
static std::vector<Cliff> cliffs;
static std::vector<Npc> npcs;
static std::vector<Part> parts;
static const float ANV_X = 0, ANV_Z = -8;
static int curWorld = 1;
static float px = 0, py = 0, pz = 3, hp = 100, stam = 100;

static unsigned rs = 12345;
static float rnd() { rs = rs * 1664525u + 1013904223u; return ((rs >> 8) & 0xFFFFFF) / 16777216.0f; }
static bool freeSpot(float x, float z, float minD) {
    for (auto& s : solids) {
        float dx = x - s.x, dz = z - s.z, d = minD + s.r;
        if (dx * dx + dz * dz < d * d) return false;
    }
    return true;
}
static void genWorld(int w) {
    nodes.clear(); solids.clear(); trees.clear(); enemies.clear(); cliffs.clear(); npcs.clear(); parts.clear();
    rs = 12345 + w * 777;
    solids.push_back({ANV_X, ANV_Z, 1.0f});
    solids.push_back({0, -15, 3.2f});
    solids.push_back({-7, -5, 1.5f});
    if (w == 1) {
        npcs.push_back({-4, 1, 0, 1.57f}); npcs.push_back({-9, -2, 1, 1.2f}); npcs.push_back({9, -2, 2, -1.2f});
        npcs.push_back({7, -8, 3, -0.5f}); npcs.push_back({-8, -8, 4, 0.5f}); npcs.push_back({3, 8, 5, 3.14f});
    } else {
        npcs.push_back({-4, 1, 0, 1.57f}); npcs.push_back({-9, -2, 1, 1.2f}); npcs.push_back({4, 8, 5, 3.14f});
        npcs.push_back({8, -4, 6, 0});
    }
    for (auto& n : npcs) solids.push_back({n.x, n.z, 0.9f});
    // Felsen
    struct Spec { int type; int count; float lo, hi; };
    Spec rk1[3] = {{0, 26, 11, 36}, {1, 20, 20, 50}, {2, 14, 32, 60}};
    Spec rk2[4] = {{3, 16, 14, 38}, {4, 14, 26, 48}, {5, 12, 36, 56}, {6, 10, 48, 62}};
    int nSpec = w == 1 ? 3 : 4;
    for (int s = 0; s < nSpec; s++) {
        Spec sp = w == 1 ? rk1[s] : rk2[s];
        int placed = 0, tries = 0;
        while (placed < sp.count && tries < 700) {
            tries++;
            float a = rnd() * 2 * PI, r = sp.lo + rnd() * (sp.hi - sp.lo);
            float x = cosf(a) * r, z = sinf(a) * r;
            if (!freeSpot(x, z, 2.2f)) continue;
            Node n;
            n.x = x; n.z = z; n.sz = ROCKS[sp.type].sz; n.rot = rnd() * PI; n.type = sp.type;
            n.maxhp = ROCKS[sp.type].hp; n.hp = n.maxhp; n.respawn = 0; n.shake = 0;
            nodes.push_back(n);
            solids.push_back({x, z, n.sz * 0.9f});
            placed++;
        }
    }
    if (w == 1) {
        int placed = 0, tries = 0;
        while (placed < 40 && tries < 1000) {
            tries++;
            float a = rnd() * 2 * PI, r = 9 + rnd() * 54;
            float x = cosf(a) * r, z = sinf(a) * r;
            if (!freeSpot(x, z, 1.5f)) continue;
            trees.push_back({x, z, 0.8f + rnd() * 0.6f});
            solids.push_back({x, z, 0.6f});
            placed++;
        }
    }
    // Gegner
    struct ESpec { int type; int count; float lo, hi; };
    ESpec e1[4] = {{0, 10, 14, 36}, {1, 8, 24, 46}, {2, 5, 36, 52}, {3, 5, 42, 58}};
    ESpec e2[8] = {{4, 6, 14, 30}, {5, 6, 16, 34}, {6, 6, 20, 38}, {10, 5, 26, 44}, {7, 6, 34, 50}, {8, 4, 38, 52}, {9, 4, 46, 58}, {11, 2, 54, 62}};
    int ne = w == 1 ? 4 : 8;
    for (int s = 0; s < ne; s++) {
        ESpec sp = w == 1 ? e1[s] : e2[s];
        int placed = 0, tries = 0;
        while (placed < sp.count && tries < 600) {
            tries++;
            float a = rnd() * 2 * PI, r = sp.lo + rnd() * (sp.hi - sp.lo);
            float x = cosf(a) * r, z = sinf(a) * r;
            if (!freeSpot(x, z, 1.0f)) continue;
            Enemy e;
            memset(&e, 0, sizeof e);
            const EType& et = ETY[sp.type];
            e.type = sp.type;
            e.lvl = et.lvlMin + (int)(rnd() * (et.lvlMax - et.lvlMin + 1));
            if (e.lvl > et.lvlMax) e.lvl = et.lvlMax;
            float tl = et.lvlMax > et.lvlMin ? (float)(e.lvl - et.lvlMin) / (et.lvlMax - et.lvlMin) : 0.0f;
            e.maxhp = et.hpLo + (et.hpHi - et.hpLo) * tl;
            e.hp = e.maxhp;
            e.dmg = et.dmgLo + (et.dmgHi - et.dmgLo) * tl;
            e.gold = et.goldLo + (et.goldHi - et.goldLo) * tl;
            e.xp = et.xpLo + (et.xpHi - et.xpLo) * tl;
            e.x = e.hx = x; e.z = e.hz = z; e.yaw = rnd() * 6;
            enemies.push_back(e);
            placed++;
        }
    }
    // Klippen als Bergwand
    for (int i = 0; i < 40; i++) {
        float a = i * (2 * PI / 40) + rnd() * 0.1f, r = 72 + rnd() * 5;
        Cliff c;
        c.x = cosf(a) * r; c.z = sinf(a) * r; c.w = 8 + rnd() * 6; c.h = 10 + rnd() * 18; c.d = 8 + rnd() * 6; c.c = 0.8f + rnd() * 0.4f;
        cliffs.push_back(c);
    }
}
static int areaOf(int w, float x, float z) {
    float r = sqrtf(x * x + z * z);
    if (r < 13) return 0;
    if (w == 1) return r < 32 ? 1 : (r < 48 ? 2 : 3);
    return r < 32 ? 1 : (r < 48 ? 2 : 3);
}
static const char* AREAN[2][4] = {{"STONEWAKE'S CROSS", "THE ENTRANCE", "DEEP MINE", "GOBLIN DEPTHS"},
                                  {"FORGOTTEN KINGDOM", "RUINED COURTYARD", "BASALT CAVES", "VOLCANIC DEPTHS"}};

static void spawnParts(float x, float y, float z, const float* c, int n) {
    for (int i = 0; i < n && parts.size() < 80; i++) {
        Part p;
        p.x = x; p.y = y; p.z = z;
        p.vx = (rnd() - 0.5f) * 6; p.vy = 2 + rnd() * 4; p.vz = (rnd() - 0.5f) * 6;
        p.life = 0.5f + rnd() * 0.4f;
        p.c[0] = c[0]; p.c[1] = c[1]; p.c[2] = c[2];
        parts.push_back(p);
    }
}

// ---------------------------------------------------------------
// Figuren
// ---------------------------------------------------------------
static const float SKINC[3] = {0.97f, 0.82f, 0.35f};
static const float HAIR[3] = {0.25f, 0.15f, 0.08f};
static const float PANTS[3] = {0.3f, 0.6f, 0.25f};
static const float BLUE[3] = {0.2f, 0.4f, 0.85f};
static const float WOOD[3] = {0.55f, 0.4f, 0.2f};
static const float EYEC[3] = {0.08f, 0.08f, 0.1f};

struct Look { const float* shirt; const float* pants; const float* helm; const float* blade; const float* pick; };
static void person(const M& base, const Look& lk, float ph, float amp, float armR, float armL) {
    float s = sinf(ph) * 0.7f * amp;
    for (int i = 0; i < 2; i++) {
        float sx = i ? 0.2f : -0.2f;
        float a = i ? -s : s;
        M leg = mul(base, mul(T(sx, 0.8f, 0), mul(RX(a), mul(T(0, -0.4f, 0), S(0.35f, 0.8f, 0.35f)))));
        drawBox(leg, lk.pants);
    }
    box(base, 0, 1.25f, 0, 0.85f, 0.9f, 0.45f, lk.shirt);
    box(base, 0, 2.0f, 0, 0.55f, 0.55f, 0.55f, SKINC);
    box(base, -0.12f, 2.05f, 0.28f, 0.09f, 0.09f, 0.04f, EYEC);
    box(base, 0.12f, 2.05f, 0.28f, 0.09f, 0.09f, 0.04f, EYEC);
    if (lk.helm) box(base, 0, 2.22f, 0, 0.68f, 0.36f, 0.68f, lk.helm);
    else box(base, 0, 2.3f, -0.02f, 0.6f, 0.14f, 0.6f, HAIR);
    M la = mul(base, mul(T(-0.55f, 1.65f, 0), RX(armL - s)));
    drawBox(mul(la, mul(T(0, -0.4f, 0), S(0.25f, 0.8f, 0.25f))), lk.shirt);
    if (lk.blade) drawBox(mul(la, mul(T(0, -0.8f, 0.55f), S(0.09f, 0.14f, 1.1f))), lk.blade);
    M ra = mul(base, mul(T(0.55f, 1.65f, 0), RX(armR)));
    drawBox(mul(ra, mul(T(0, -0.4f, 0), S(0.25f, 0.8f, 0.25f))), lk.shirt);
    if (lk.pick) {
        drawBox(mul(ra, mul(T(0, -0.8f, 0.35f), S(0.1f, 0.1f, 1.0f))), WOOD);
        drawBox(mul(ra, mul(T(0, -0.8f, 0.8f), S(0.12f, 0.9f, 0.14f))), lk.pick);
    }
}

// ---------------------------------------------------------------
// Menues
// ---------------------------------------------------------------
enum { ST_PLAY, ST_MENU, ST_QUEST, ST_ORES, ST_CAT, ST_FORGE, ST_RESULT, ST_INV };
enum { M_NONE, M_FRED, M_MARIA, M_RUNE, M_WIZARD, M_CEY, M_PORTAL };
struct MenuLine { char t[90]; bool ok; };
static std::vector<MenuLine> menuLines;
static char menuTitle[40];
static int menuType = M_NONE;

static void addLine(bool ok, const char* fmt, ...) {
    MenuLine l;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(l.t, sizeof l.t, fmt, ap);
    va_end(ap);
    l.ok = ok;
    menuLines.push_back(l);
}
static void sellValues(int& oreV, int& itemV) {
    float o = 0;
    for (int i = 0; i < NORE; i++) o += G.ore[i] * ORES[i].price;
    oreV = (int)o;
    itemV = 0;
    for (auto& it : items) itemV += it.val;
}
static void buildMenu() {
    menuLines.clear();
    if (menuType == M_FRED) {
        snprintf(menuTitle, sizeof menuTitle, "MINER FRED");
        int lo = curWorld == 1 ? 1 : 7, hi = curWorld == 1 ? 6 : 13;
        for (int i = lo; i <= hi; i++) {
            const Pick& p = PICKS[i];
            if (G.pickEq == i) addLine(true, "%s: POW %d  LUCK %.0f%%  SLOTS %d  [AUSGERUESTET]", p.name, p.power, p.luck, p.slots);
            else if (G.pickOwned[i]) addLine(true, "%s: POW %d  LUCK %.0f%%  SLOTS %d  [BESITZ]", p.name, p.power, p.luck, p.slots);
            else addLine(G.coins >= buyPrice(p.cost), "%s: POW %d  LUCK %.0f%%  SLOTS %d  $%d", p.name, p.power, p.luck, p.slots, buyPrice(p.cost));
        }
    } else if (menuType == M_MARIA) {
        snprintf(menuTitle, sizeof menuTitle, "MARIA - TRAENKE");
        addLine(G.coins >= buyPrice(100), "HEILTRANK: VOLLE LEBEN  $%d", buyPrice(100));
        addLine(G.coins >= buyPrice(350), "GLUECKSTRANK +20%% (5 MIN)  $%d", buyPrice(350));
        addLine(G.coins >= buyPrice(300), "MINER-TRANK +25%% ABBAU (5 MIN)  $%d", buyPrice(300));
    } else if (menuType == M_RUNE) {
        snprintf(menuTitle, sizeof menuTitle, "RUNEMAKER");
        for (int i = 0; i < 6; i++)
            addLine(G.runeOwn[i] > 0 || G.runeInst[i] > 0, "%s x%d %s - %s", RUNEN[i], G.runeOwn[i], G.runeInst[i] ? "[INSTALLIERT]" : "", RUNED[i]);
    } else if (menuType == M_WIZARD) {
        snprintf(menuTitle, sizeof menuTitle, "WIZARD");
        addLine(G.spins > 0, "RASSE WUERFELN (SPINS: %d)  JETZT: %s", G.spins, RC().name);
        addLine(false, "PORTAL-WERKZEUG: %s", G.portal ? "BESITZ - NUTZE DAS PORTAL" : "ERST ENDE DES ANFANGS");
        if (G.portal) addLine(true, "ZUM PORTAL (WELTEN)");
    } else if (menuType == M_CEY) {
        snprintf(menuTitle, sizeof menuTitle, "GREEDY CEY - ANKAUF");
        int oV, iV;
        sellValues(oV, iV);
        addLine(oV > 0, "ALLE ERZE VERKAUFEN (+$%d)", oV);
        addLine(iV > 0, "ALLE GESCHMIEDETEN STUECKE (+$%d) [%d]", iV, (int)items.size());
    } else if (menuType == M_PORTAL) {
        snprintf(menuTitle, sizeof menuTitle, "PORTAL");
        addLine(true, "1. STONEWAKE'S CROSS (AB LEVEL 1)");
        addLine(G.level >= 10, "2. FORGOTTEN KINGDOM (AB LEVEL 10)");
        addLine(false, "3. FROSTSPIRE EXPANSE (AB LEVEL 70) - BALD");
        addLine(false, "4. CRIMSON SAKURA ISLE (AB LEVEL 100) - BALD");
    }
}

// ---------------------------------------------------------------
// Hauptprogramm
// ---------------------------------------------------------------
struct Target { int kind; int idx; float d; };  // 1 Erz, 2 NPC, 3 Amboss

static float ax(SDL_Joystick* joy, int i) {
    if (!joy || i < 0 || i >= SDL_JoystickNumAxes(joy)) return 0;
    float v = SDL_JoystickGetAxis(joy, i) / 32767.0f;
    return fabsf(v) < 0.25f ? 0.0f : v;
}
static const char* ratingOf(float s) { return s > 0.9f ? "PERFEKT!" : (s > 0.6f ? "GUT!" : (s > 0.3f ? "OK" : "SCHLECHT")); }

static void enterWorld(int w) {
    curWorld = w;
    G.world = w;
    genWorld(w);
    px = 0; pz = 3; py = 0;
}

int main(int argc, char** argv) {
    bool windowed = false;
    for (int i = 1; i < argc; i++)
        if (!strcmp(argv[i], "--window")) windowed = true;

    loadControls();
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    Uint32 wf = SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | (windowed ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
    SDL_Window* win = 0;
    int depths[2] = {24, 16};
    for (int d = 0; d < 2 && !win; d++) {
        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, depths[d]);
        win = SDL_CreateWindow("Forge", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 640, 480, wf);
    }
    if (!win) { fprintf(stderr, "Fenster: %s\n", SDL_GetError()); return 1; }
    SDL_GLContext ctx = SDL_GL_CreateContext(win);
    if (!ctx) { fprintf(stderr, "GL-Kontext: %s\n", SDL_GetError()); return 1; }
    SDL_GL_SetSwapInterval(1);
    SDL_ShowCursor(SDL_DISABLE);
    SDL_GL_GetDrawableSize(win, &SW, &SH);

    GLuint vs = compileShader(GL_VERTEX_SHADER, VS), fs = compileShader(GL_FRAGMENT_SHADER, FS);
    prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);
    glUseProgram(prog);
    uVP = glGetUniformLocation(prog, "uVP");
    uModel = glGetUniformLocation(prog, "uModel");
    uRot = glGetUniformLocation(prog, "uRot");
    uColor = glGetUniformLocation(prog, "uColor");
    uFog = glGetUniformLocation(prog, "uFog");
    uLit = glGetUniformLocation(prog, "uLit");
    uFogOn = glGetUniformLocation(prog, "uFogOn");
    uTex = glGetUniformLocation(prog, "uTex");
    aPos = glGetAttribLocation(prog, "aPos");
    aNrm = glGetAttribLocation(prog, "aNrm");
    glEnableVertexAttribArray(aPos);
    glEnableVertexAttribArray(aNrm);
    buildMeshes();
    initFont();
    srand(12345);
    loadGame();
    enterWorld(G.world == 2 ? 2 : 1);
    hp = maxHP();
    stam = maxStam();

    SDL_Joystick* joy = 0;
    if (SDL_NumJoysticks() > 0) joy = SDL_JoystickOpen(0);

    const float id3[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    // Welt-Farbschemata
    const float SKYTOP[2][3] = {{0.28f, 0.5f, 0.9f}, {0.3f, 0.26f, 0.32f}};
    const float SKYHOR[2][3] = {{0.72f, 0.86f, 0.98f}, {0.62f, 0.45f, 0.38f}};
    const float DISC[2][7][3] = {
        {{0.85f, 0.8f, 0.55f}, {0.33f, 0.62f, 0.28f}, {0.42f, 0.4f, 0.36f}, {0.5f, 0.47f, 0.4f}, {0.46f, 0.46f, 0.48f}, {0.4f, 0.55f, 0.3f}, {0.42f, 0.68f, 0.32f}},
        {{0.95f, 0.38f, 0.06f}, {0.3f, 0.28f, 0.28f}, {0.3f, 0.2f, 0.18f}, {0.32f, 0.3f, 0.3f}, {0.37f, 0.34f, 0.32f}, {0.4f, 0.37f, 0.34f}, {0.45f, 0.42f, 0.38f}}};
    const float DRAD[7] = {73, 70, 64, 50, 38, 26, 13};

    float cyaw = PI, cpitch = 0.5f;
    float hurtT = 0, lastHurt = 0, deathFlash = 0, stunT = 0, dashT = 0, yaw = PI, walkPh = 0, vy = 0;
    float swingT = 0, atkT = 0, atkCd = 0, auraT = 0;
    bool hitDone = true, atkHit = true, swingCrit = false;
    int swingNode = -1;
    int state = ST_PLAY, menuCur = 0, oreCur = 0, catCur = 0, invCur = 0;
    int sel[NORE] = {0};
    // Schmieden
    int forgeCat = 0, forgeStage = 0;
    float smeltHeat = 0, smeltTime = 0, pourY = 0.5f, pourProg = 0, pourTime = 0, pourT = 0;
    int ringIdx = 0;
    float ringT = 0, ringX = 0.5f, ringY = 0.5f, ringDelay = 0, ringSc[5] = {0}, quenchT = 0;
    bool ringDone = false;
    float s0 = 0, s1 = 0, s2 = 0;
    Item resultItem;
    memset(&resultItem, 0, sizeof resultItem);
    float timeAcc = 0, bannerT = 0;
    int curArea = -1;
    char lastIn[24] = "";
    bool selHeld = false, startHeld = false, questNotified = false;
    int prevSd = 0;
    bool running = true;
    Uint32 last = SDL_GetTicks();
    setMsg("A: ABBAUEN/BENUTZEN  X: ANGRIFF  Y: TASCHE");
    msgT = 6.0f;

    auto killEnemy = [&](Enemy& en) {
        const EType& t = ETY[en.type];
        en.dead = 40;
        G.coins += (int)en.gold;
        G.cKills++;
        gainXP((int)en.xp);
        setMsg("%s BESIEGT! +$%d", t.name, (int)en.gold);
        for (auto& d : EDROPS)
            if (d.enemy == en.type && rnd() < (1.0f + luckTotal()) / d.chance) {
                G.ore[d.ore]++;
                setMsgC(RARC[ORES[d.ore].rar], "DROP: %s!", ORES[d.ore].name);
            }
        for (auto& d : RDROPS)
            if (d.enemy == en.type && rnd() < 1.0f / d.chance) {
                G.runeOwn[d.rune]++;
                setMsgC(RARC[3], "RUNE: %s!", RUNEN[d.rune]);
            }
        spawnParts(en.x, 1.0f, en.z, t.col, 10);
        saveGame();
    };
    auto hurtEnemy = [&](Enemy& en, float d, bool flash) {
        en.hp -= d;
        if (flash) en.flash = 0.15f;
        if (en.hp <= 0 && en.dead <= 0) killEnemy(en);
    };

    while (running) {
        Uint32 now = SDL_GetTicks();
        float dt = (now - last) / 1000.0f;
        last = now;
        if (dt > 0.05f) dt = 0.05f;
        timeAcc += dt;

        bool aP = false, bP = false, xP = false, yP = false, rP = false, nU = false, nD = false;
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            switch (e.type) {
            case SDL_QUIT: running = false; break;
            case SDL_KEYDOWN:
                if (e.key.repeat) break;
                switch (e.key.keysym.sym) {
                case SDLK_ESCAPE: running = false; break;
                case SDLK_e: case SDLK_RETURN: aP = true; break;
                case SDLK_SPACE: case SDLK_q: bP = true; break;
                case SDLK_f: xP = true; break;
                case SDLK_i: yP = true; break;
                case SDLK_c: rP = true; break;
                case SDLK_w: case SDLK_UP: nU = true; break;
                case SDLK_s: case SDLK_DOWN: nD = true; break;
                default: break;
                }
                break;
            case SDL_JOYBUTTONDOWN: {
                int b = e.jbutton.button;
                snprintf(lastIn, sizeof lastIn, "BTN %d", b);
                if (b == BTN_A) aP = true;
                if (b == BTN_B) bP = true;
                if (b == BTN_X) xP = true;
                if (b == BTN_Y) yP = true;
                if (b == BTN_R) rP = true;
                if (b == BTN_UP) nU = true;
                if (b == BTN_DOWN) nD = true;
                if (b == BTN_START) startHeld = true;
                if (b == BTN_SELECT) selHeld = true;
                if (startHeld && selHeld) running = false;
                break;
            }
            case SDL_JOYBUTTONUP: {
                int b = e.jbutton.button;
                if (b == BTN_START) startHeld = false;
                if (b == BTN_SELECT) selHeld = false;
                break;
            }
            case SDL_JOYHATMOTION:
                snprintf(lastIn, sizeof lastIn, "HAT %d", e.jhat.value);
                if (e.jhat.value & SDL_HAT_UP) nU = true;
                if (e.jhat.value & SDL_HAT_DOWN) nD = true;
                break;
            case SDL_JOYAXISMOTION:
                if (abs(e.jaxis.value) > 20000) snprintf(lastIn, sizeof lastIn, "AXIS %d", e.jaxis.axis);
                break;
            default: break;
            }
        }

        // ---- Eingaben ----
        float mx = ax(joy, AX_LX), my = ax(joy, AX_LY);
        float cx = ax(joy, AX_RX), cy = ax(joy, AX_RY);
        bool blocking = false, aHeld = false;
        if (joy) {
            if (SDL_JoystickNumHats(joy) > 0) {
                int h = SDL_JoystickGetHat(joy, 0);
                if (h & SDL_HAT_LEFT) mx -= 1;
                if (h & SDL_HAT_RIGHT) mx += 1;
                if (h & SDL_HAT_UP) my -= 1;
                if (h & SDL_HAT_DOWN) my += 1;
            }
            if (BTN_LEFT >= 0 && SDL_JoystickGetButton(joy, BTN_LEFT)) mx -= 1;
            if (BTN_RIGHT >= 0 && SDL_JoystickGetButton(joy, BTN_RIGHT)) mx += 1;
            if (BTN_UP >= 0 && SDL_JoystickGetButton(joy, BTN_UP)) my -= 1;
            if (BTN_DOWN >= 0 && SDL_JoystickGetButton(joy, BTN_DOWN)) my += 1;
            if (BTN_L >= 0 && SDL_JoystickGetButton(joy, BTN_L)) blocking = true;
            if (BTN_A >= 0 && SDL_JoystickGetButton(joy, BTN_A)) aHeld = true;
            if (BTN_START >= 0 && BTN_SELECT >= 0 && SDL_JoystickGetButton(joy, BTN_START) && SDL_JoystickGetButton(joy, BTN_SELECT)) running = false;
        }
        const Uint8* ks = SDL_GetKeyboardState(0);
        if (ks[SDL_SCANCODE_A]) mx -= 1;
        if (ks[SDL_SCANCODE_D]) mx += 1;
        if (ks[SDL_SCANCODE_W]) my -= 1;
        if (ks[SDL_SCANCODE_S]) my += 1;
        if (ks[SDL_SCANCODE_LEFT]) cx -= 1;
        if (ks[SDL_SCANCODE_RIGHT]) cx += 1;
        if (ks[SDL_SCANCODE_UP]) cy -= 1;
        if (ks[SDL_SCANCODE_DOWN]) cy += 1;
        if (ks[SDL_SCANCODE_LSHIFT]) blocking = true;
        {
            float ly = ax(joy, AX_LY);
            int sd = ly < -0.6f ? -1 : (ly > 0.6f ? 1 : 0);
            if (sd != prevSd) {
                if (sd < 0) nU = true;
                if (sd > 0) nD = true;
                prevSd = sd;
            }
        }

        // ---- Update (immer) ----
        if (msgT > 0) msgT -= dt;
        if (bannerT > 0) bannerT -= dt;
        if (hurtT > 0) hurtT -= dt;
        if (deathFlash > 0) deathFlash -= dt;
        if (atkCd > 0) atkCd -= dt;
        if (G.luckPot > 0) G.luckPot -= dt;
        if (G.minePot > 0) G.minePot -= dt;
        for (auto& n : nodes) {
            if (n.respawn > 0) {
                n.respawn -= dt;
                if (n.respawn <= 0) n.hp = n.maxhp;
            }
            if (n.shake > 0) n.shake -= dt;
        }
        for (size_t i = 0; i < parts.size();) {
            Part& p = parts[i];
            p.life -= dt; p.vy -= 12 * dt;
            p.x += p.vx * dt; p.y += p.vy * dt; p.z += p.vz * dt;
            if (p.y < 0.05f) { p.y = 0.05f; p.vy = 0; p.vx *= 0.5f; p.vz *= 0.5f; }
            if (p.life <= 0) parts.erase(parts.begin() + i); else i++;
        }
        float mhp = maxHP();
        if (hp > mhp) hp = mhp;
        float msm = maxStam();
        if (stam > msm) stam = msm;

        auto takeHit = [&](float dmg, Enemy* src) {
            if (dashT > 0) return;
            if (rnd() < dodgeTotal()) { setMsg("AUSGEWICHEN!"); return; }
            dmg *= 60.0f / (60.0f + defTotal());
            if (blocking && stunT <= 0 && stam > 0) {
                stam -= dmg * 1.5f;
                dmg *= 0.25f;
                if (stam <= 0) { stam = 0; stunT = 1.2f; setMsgC(RED3, "BLOCK GEBROCHEN!"); }
            }
            hp -= dmg; hurtT = 0.25f; lastHurt = 0;
            if (src) {
                float th = sumArm(&Item::thorns) + (runeOn(5) ? 0.25f : 0.0f);
                if (th > 0) { src->burn = 3; src->burnDps = std::max(src->burnDps, 0.05f * src->maxhp); }
                if (RC().reflect > 0) hurtEnemy(*src, dmg * RC().reflect / 100.0f, false);
            }
        };

        Target tg = {0, -1, 1e9f};
        if (state == ST_PLAY) {
            if (stunT > 0) stunT -= dt;
            float len = sqrtf(mx * mx + my * my);
            if (len > 1) { mx /= len; my /= len; len = 1; }
            cyaw -= cx * dt * 2.6f;
            cpitch += cy * dt * 1.5f;
            cpitch = std::max(0.12f, std::min(1.25f, cpitch));
            float fx = sinf(cyaw), fz = cosf(cyaw);
            float rx = -fz, rz = fx;
            float dx = fx * (-my) + rx * mx, dz = fz * (-my) + rz * mx;
            if (stunT > 0) { dx = dz = 0; len = 0; }
            float sp = (swingT > 0 || atkT > 0) ? 2.0f : (blocking ? 3.0f : moveSpeed());
            if (dashT > 0) {
                dashT -= dt;
                px += sinf(yaw) * 14.0f * dt;
                pz += cosf(yaw) * 14.0f * dt;
            } else {
                px += dx * sp * dt;
                pz += dz * sp * dt;
            }
            if (len > 0.1f && dashT <= 0) {
                float ty = atan2f(dx, dz);
                yaw += wrapAngle(ty - yaw) * std::min(1.0f, 12 * dt);
                walkPh += dt * 10;
            }
            if (rP && stam >= 25 && dashT <= 0 && stunT <= 0) {
                stam -= 25; dashT = 0.25f;
                if (len > 0.1f) yaw = atan2f(dx, dz);
            }
            if (bP && py <= 0.001f && stunT <= 0) vy = 7.5f;
            vy -= 20 * dt;
            py += vy * dt;
            if (py < 0) { py = 0; vy = 0; }
            for (auto& s : solids) {
                float ddx = px - s.x, ddz = pz - s.z;
                float d = sqrtf(ddx * ddx + ddz * ddz), md = s.r + 0.45f;
                if (d < md && d > 1e-4f) { px = s.x + ddx / d * md; pz = s.z + ddz / d * md; }
            }
            float pr = sqrtf(px * px + pz * pz);
            if (pr > 66) { px *= 66 / pr; pz *= 66 / pr; }

            int ar = areaOf(curWorld, px, pz);
            if (ar != curArea) { curArea = ar; bannerT = 3.0f; }

            lastHurt += dt;
            if (lastHurt > 5 && hp < mhp) hp = std::min(mhp, hp + mhp * 0.02f * dt);
            if (!blocking && dashT <= 0) stam = std::min(msm, stam + 20 * dt);

            // Ziel suchen
            for (size_t i = 0; i < nodes.size(); i++) {
                if (nodes[i].respawn > 0) continue;
                float d = hypotf(nodes[i].x - px, nodes[i].z - pz);
                if (d < 3.2f + nodes[i].sz * 0.5f && d < tg.d) tg = {1, (int)i, d};
            }
            for (size_t i = 0; i < npcs.size(); i++) {
                float d = hypotf(npcs[i].x - px, npcs[i].z - pz);
                if (d < 3.4f && d < tg.d) tg = {2, (int)i, d};
            }
            float d = hypotf(ANV_X - px, ANV_Z - pz);
            if (d < 3.0f && d < tg.d) tg = {3, 0, d};

            if (aP && tg.kind && stunT <= 0) {
                if (tg.kind == 1) {
                    Node& n = nodes[tg.idx];
                    if (swingT <= 0 && atkT <= 0) {
                        swingT = mineCd(); hitDone = false; swingNode = tg.idx;
                        swingCrit = fabsf(tri(timeAcc * 1.4f) - 0.5f) < 0.12f || (RC().mine >= 15 && rnd() < 0.2f);
                        yaw = atan2f(n.x - px, n.z - pz);
                    }
                } else if (tg.kind == 2) {
                    int k = npcs[tg.idx].kind;
                    menuCur = 0;
                    if (k == 0) state = ST_QUEST;
                    else {
                        menuType = k == 1 ? M_FRED : (k == 2 ? M_MARIA : (k == 3 ? M_RUNE : (k == 4 ? M_WIZARD : (k == 5 ? M_CEY : M_PORTAL))));
                        if (k == 6 && !G.portal) setMsg("DU BRAUCHST DAS PORTAL-WERKZEUG");
                        else state = ST_MENU;
                    }
                } else if (tg.kind == 3) {
                    memset(sel, 0, sizeof sel);
                    oreCur = 0;
                    state = ST_ORES;
                }
            }
            // Angriff
            if (xP && atkCd <= 0 && swingT <= 0 && stunT <= 0) {
                atkT = 0.35f; atkCd = weaponCd(); atkHit = false;
                float bd = 6.0f;
                for (auto& en : enemies) {
                    if (en.dead > 0) continue;
                    float dd = hypotf(en.x - px, en.z - pz);
                    if (dd < bd) { bd = dd; yaw = atan2f(en.x - px, en.z - pz); }
                }
            }
            if (swingT > 0) {
                float cd = mineCd();
                swingT -= dt;
                float p = 1 - swingT / cd;
                if (!hitDone && p >= 0.7f) {
                    hitDone = true;
                    if (swingNode >= 0) {
                        Node& n = nodes[swingNode];
                        if (n.respawn <= 0 && hypotf(n.x - px, n.z - pz) < 4.5f) {
                            float dmg = PICKS[G.pickEq].power * mineMult() * (swingCrit ? 2.5f : 1.0f);
                            n.hp -= dmg;
                            n.shake = 0.3f;
                            spawnParts(n.x, n.sz * 0.7f, n.z, ROCKS[n.type].col, 4);
                            if (swingCrit) setMsg("CRITICAL MINING!");
                            if (n.hp <= 0) {
                                int mask = 1 << n.type;
                                int got[NORE] = {0};
                                int total = 0, bestRar = -1, bestOre = 0;
                                for (int rep = 0; rep < ROLLS[n.type] * (swingCrit ? 2 : 1); rep++) {
                                    for (int i = 0; i < NORE; i++) {
                                        if (!(ORES[i].rocks & mask)) continue;
                                        float pr_ = std::min(1.0f, (1.0f + luckTotal()) / ORES[i].chance);
                                        if (rnd() < pr_) { got[i]++; total++; }
                                    }
                                }
                                if (!total) { for (int i = 0; i < NORE; i++) if (ORES[i].rocks & mask) { got[i] = 1; total = 1; break; } }
                                if (runeOn(0) && rnd() < 0.05f) { for (int i = 0; i < NORE; i++) if (got[i]) { got[i]++; total++; break; } }
                                for (int i = 0; i < NORE; i++) {
                                    if (!got[i]) continue;
                                    G.ore[i] += got[i];
                                    if (ORES[i].rar > bestRar) { bestRar = ORES[i].rar; bestOre = i; }
                                }
                                G.cMined += total;
                                n.respawn = 30;
                                gainXP((int)(ROCKS[n.type].hp / 8) + 3);
                                spawnParts(n.x, n.sz, n.z, ORES[bestOre].col, 14);
                                if (bestRar >= 3) setMsgC(RARC[bestRar], "SELTEN: %s! (+%d ERZE)", ORES[bestOre].name, total);
                                else setMsg("+%d ERZE (%s)", total, ORES[bestOre].name);
                                saveGame();
                            }
                        }
                    }
                }
            }
            if (atkT > 0) {
                atkT -= dt;
                float p = 1 - atkT / 0.35f;
                if (!atkHit && p >= 0.5f) {
                    atkHit = true;
                    float rng = weaponRng();
                    float baseDmg = weaponDmg();
                    float critCh = 0.05f + (G.eqValid[0] ? G.eq[0].crit : 0.0f);
                    float rageMul = ((G.race == 9 && hp < mhp * 0.5f) ? 1.3f : 1.0f) * (runeOn(4) && hp < mhp * 0.35f ? 1.3f : 1.0f);
                    for (auto& en : enemies) {
                        if (en.dead > 0) continue;
                        const EType& t = ETY[en.type];
                        float ddx = en.x - px, ddz = en.z - pz, dd = hypotf(ddx, ddz);
                        if (dd > rng + t.sz * 0.6f) continue;
                        if (fabsf(wrapAngle(atan2f(ddx, ddz) - yaw)) > 1.4f) continue;
                        float dmg = baseDmg * rageMul;
                        if (rnd() < critCh) { dmg *= 1.5f; setMsg("KRITISCH!"); }
                        if (dd > 0.1f) { en.x += ddx / dd * 0.5f; en.z += ddz / dd * 0.5f; }
                        if ((G.eqValid[0] && rnd() < G.eq[0].burn) || (runeOn(3) && rnd() < 0.5f)) { en.burn = 3; en.burnDps = dmg * 0.15f; }
                        float ls = lifesteal();
                        if (ls > 0) hp = std::min(mhp, hp + dmg * ls);
                        bool expl = (G.eqValid[0] && rnd() < G.eq[0].expl) || (runeOn(2) && rnd() < 0.3f);
                        hurtEnemy(en, dmg, true);
                        if (expl) {
                            const float EXPC[3] = {1.0f, 0.5f, 0.1f};
                            spawnParts(en.x, 1.0f, en.z, EXPC, 12);
                            for (auto& e2 : enemies)
                                if (&e2 != &en && e2.dead <= 0 && hypotf(e2.x - en.x, e2.z - en.z) < 3.0f) hurtEnemy(e2, dmg * 0.5f, true);
                        }
                    }
                }
            }
            // Aura / Giftwolke
            auraT += dt;
            if (auraT >= 1.0f) {
                auraT = 0;
                float au = sumArm(&Item::aura);
                bool poison = hp < mhp * 0.35f;
                if (poison) {
                    bool has = false;
                    for (int i = 0; i < 4; i++) if (G.eqValid[i] && G.eq[i].poison) has = true;
                    poison = has;
                }
                float tot = au + (poison ? 0.03f : 0.0f);
                if (tot > 0)
                    for (auto& en : enemies)
                        if (en.dead <= 0 && hypotf(en.x - px, en.z - pz) < 4.5f) hurtEnemy(en, en.maxhp * tot, false);
            }
            // Gegner
            for (auto& en : enemies) {
                const EType& t = ETY[en.type];
                if (en.dead > 0) {
                    en.dead -= dt;
                    if (en.dead <= 0) { en.hp = en.maxhp; en.x = en.hx; en.z = en.hz; en.burn = 0; }
                    continue;
                }
                if (en.flash > 0) en.flash -= dt;
                if (en.atk > 0) en.atk -= dt;
                if (en.burn > 0) { en.burn -= dt; hurtEnemy(en, en.burnDps * dt, false); if (en.dead > 0) continue; }
                float ex = px - en.x, ez = pz - en.z, ed = hypotf(ex, ez);
                if (ed < t.aggro && ed > 1e-3f) {
                    en.yaw = atan2f(ex, ez);
                    if ((t.flag & 1) && ed < 2.4f) {
                        const float EXPC[3] = {1.0f, 0.5f, 0.1f};
                        spawnParts(en.x, 1.0f, en.z, EXPC, 16);
                        takeHit(en.dmg, 0);
                        en.dead = 40; en.hp = 0;
                        continue;
                    }
                    if (ed > 1.4f * t.sz + 0.5f) { en.x += ex / ed * t.spd * dt; en.z += ez / ed * t.spd * dt; }
                    else if (en.atk <= 0) {
                        en.atk = 1.2f;
                        takeHit(en.dmg, &en);
                        if (t.flag & 2) { /* Schleim hat keine Extras */ }
                    }
                } else {
                    float hx = en.hx - en.x, hz = en.hz - en.z, hd = hypotf(hx, hz);
                    if (hd > 0.5f) { en.x += hx / hd * t.spd * 0.6f * dt; en.z += hz / hd * t.spd * 0.6f * dt; }
                }
            }
            if (hp <= 0) {
                int lost = G.coins / 10;
                G.coins -= lost;
                hp = maxHP(); px = 0; pz = 3; deathFlash = 1.0f; stam = msm;
                setMsgC(RED3, "BESIEGT! -$%d", lost);
                saveGame();
            }
            if (questDone() && !questNotified) { setMsg("QUEST FERTIG! SPRICH MIT MORO"); questNotified = true; }
            if (!questDone()) questNotified = false;
            if (yP) { state = ST_INV; invCur = 0; }
        } else if (state == ST_MENU) {
            buildMenu();
            int n = (int)menuLines.size();
            if (menuCur >= n) menuCur = n - 1;
            if (menuCur < 0) menuCur = 0;
            if (nU && n) menuCur = (menuCur + n - 1) % n;
            if (nD && n) menuCur = (menuCur + 1) % n;
            if (bP) { state = ST_PLAY; saveGame(); }
            else if (aP && n && menuLines[menuCur].ok) {
                int c = menuCur;
                if (menuType == M_FRED) {
                    int idx = (curWorld == 1 ? 1 : 7) + c;
                    if (G.pickOwned[idx]) { G.pickEq = idx; setMsg("AUSGERUESTET: %s", PICKS[idx].name); }
                    else { G.coins -= buyPrice(PICKS[idx].cost); G.pickOwned[idx] = 1; G.pickEq = idx; setMsg("GEKAUFT: %s-SPITZHACKE!", PICKS[idx].name); }
                    if (PICKS[G.pickEq].slots < 1) G.runeInst[0] = 0;
                } else if (menuType == M_MARIA) {
                    if (c == 0) { G.coins -= buyPrice(100); hp = maxHP(); setMsg("GEHEILT!"); }
                    else if (c == 1) { G.coins -= buyPrice(350); G.luckPot = 300; setMsg("GLUECK +20%% FUER 5 MIN"); }
                    else { G.coins -= buyPrice(300); G.minePot = 300; setMsg("ABBAU +25%% FUER 5 MIN"); }
                } else if (menuType == M_RUNE) {
                    if (G.runeInst[c]) { G.runeInst[c] = 0; setMsg("RUNE ENTFERNT"); }
                    else {
                        int wr = G.runeInst[1] + G.runeInst[2] + G.runeInst[3] + G.runeInst[4];
                        bool okSlot = true;
                        if (c == 0) okSlot = pickSlots() > 0;
                        else if (c == 5) okSlot = true;
                        else okSlot = wr < 2;
                        if (okSlot) { G.runeInst[c] = 1; setMsg("RUNE INSTALLIERT: %s", RUNEN[c]); }
                        else setMsg("KEIN SLOT FREI");
                    }
                } else if (menuType == M_WIZARD) {
                    if (c == 0) {
                        G.spins--;
                        float tot = 0;
                        for (int i = 0; i < NRACE; i++) tot += RACES[i].chance;
                        float r = rnd() * tot, acc = 0;
                        int pick = 0;
                        for (int i = 0; i < NRACE; i++) { acc += RACES[i].chance; if (r <= acc) { pick = i; break; } }
                        G.race = pick;
                        setMsgC(RARC[RACES[pick].tier], "NEUE RASSE: %s! %s", RACES[pick].name, RACES[pick].desc);
                        msgT = 5.0f;
                        hp = std::min(hp, maxHP());
                    } else if (c == 2) { menuType = M_PORTAL; menuCur = 0; }
                } else if (menuType == M_CEY) {
                    int oV, iV;
                    sellValues(oV, iV);
                    if (c == 0) { G.coins += oV; G.cSold += oV; memset(G.ore, 0, sizeof G.ore); setMsg("+$%d", oV); }
                    else { G.coins += iV; G.cSold += iV; items.clear(); setMsg("+$%d", iV); }
                } else if (menuType == M_PORTAL) {
                    int target = c + 1;
                    if (target != curWorld) {
                        enterWorld(target);
                        state = ST_PLAY;
                        curArea = -1;
                        setMsg("WILLKOMMEN: %s", AREAN[target - 1][0]);
                    } else setMsg("DU BIST SCHON HIER");
                }
                saveGame();
            }
        } else if (state == ST_QUEST) {
            if (bP) state = ST_PLAY;
            else if (aP && G.questIdx < NQUEST && questDone()) {
                G.coins += QUESTS[G.questIdx].reward;
                gainXP(QUESTS[G.questIdx].reward / 2);
                setMsg("QUEST ERLEDIGT! +$%d", QUESTS[G.questIdx].reward);
                if (G.questIdx == 8) { G.portal = 1; setMsgC(GREEN3, "PORTAL-WERKZEUG ERHALTEN!"); }
                G.questIdx++;
                questNotified = false;
                saveGame();
            }
        } else if (state == ST_ORES) {
            std::vector<int> av;
            for (int i = 0; i < NORE; i++) if (G.ore[i] > 0) av.push_back(i);
            int na = (int)av.size();
            int total = 0;
            for (int i = 0; i < NORE; i++) total += sel[i];
            if (oreCur >= na) oreCur = na - 1;
            if (oreCur < 0) oreCur = 0;
            if (nU && na) oreCur = (oreCur + na - 1) % na;
            if (nD && na) oreCur = (oreCur + 1) % na;
            if (na) {
                int o = av[oreCur];
                if (aP && sel[o] < G.ore[o] && total < 60) sel[o]++;
                if (rP) sel[o] = std::min(G.ore[o], sel[o] + std::min(5, 60 - total));
                if (xP && sel[o] > 0) sel[o]--;
            }
            if (yP) {
                if (total >= 3) { state = ST_CAT; catCur = 0; }
                else setMsg("MINDESTENS 3 ERZE WAEHLEN");
            }
            if (bP) { state = ST_PLAY; saveGame(); }
        } else if (state == ST_CAT) {
            if (nU || nD) catCur = 1 - catCur;
            if (bP) state = ST_ORES;
            else if (aP) {
                for (int i = 0; i < NORE; i++) G.ore[i] -= sel[i];
                forgeCat = catCur;
                forgeStage = 0; smeltHeat = 0; smeltTime = 0;
                state = ST_FORGE;
                saveGame();
            }
        } else if (state == ST_FORGE) {
            if (forgeStage == 0) {
                smeltTime += dt;
                if (aP) smeltHeat += 0.09f;
                smeltHeat = std::max(0.0f, smeltHeat - 0.28f * dt);
                if (smeltHeat >= 1) { s0 = 0.5f + 0.5f * clamp01(1 - smeltTime / 8.0f); forgeStage = 1; pourY = 0.5f; pourProg = 0; pourTime = 0; pourT = 0; setMsg("SCHMELZEN: %s", ratingOf(s0)); }
                else if (smeltTime > 8) { s0 = 0.5f * smeltHeat; forgeStage = 1; pourY = 0.5f; pourProg = 0; pourTime = 0; pourT = 0; setMsg("ZU LANGSAM!"); }
            } else if (forgeStage == 1) {
                pourTime += dt; pourT += dt;
                float zc = 0.5f + 0.28f * sinf(pourT * 1.4f);
                pourY += (aHeld ? 0.85f : -0.85f) * dt;
                pourY = clamp01(pourY);
                bool inside = fabsf(pourY - zc) < 0.13f;
                pourProg += (inside ? 0.25f : -0.06f) * dt;
                pourProg = std::max(0.0f, pourProg);
                if (pourProg >= 1) { s1 = 0.5f + 0.5f * clamp01(1 - pourTime / 10.0f); forgeStage = 2; ringIdx = 0; ringT = 0; ringDone = false; ringX = 0.3f + rnd() * 0.4f; ringY = 0.3f + rnd() * 0.4f; setMsg("GIESSEN: %s", ratingOf(s1)); }
                else if (pourTime > 10) { s1 = 0.5f * pourProg; forgeStage = 2; ringIdx = 0; ringT = 0; ringDone = false; ringX = 0.3f + rnd() * 0.4f; ringY = 0.3f + rnd() * 0.4f; }
            } else if (forgeStage == 2) {
                float dur = 1.2f - 0.05f * ringIdx;
                if (!ringDone) {
                    ringT += dt;
                    float r = 1 - ringT / dur;
                    if (aP) { ringSc[ringIdx] = clamp01(1 - fabsf(r - 0.12f) / 0.45f); ringDone = true; ringDelay = 0.35f; setMsg("HAMMER: %s", ratingOf(ringSc[ringIdx])); }
                    else if (r <= -0.05f) { ringSc[ringIdx] = 0; ringDone = true; ringDelay = 0.35f; setMsgC(RED3, "VERFEHLT!"); }
                } else {
                    ringDelay -= dt;
                    if (ringDelay <= 0) {
                        ringIdx++;
                        if (ringIdx >= 5) { s2 = (ringSc[0] + ringSc[1] + ringSc[2] + ringSc[3] + ringSc[4]) / 5.0f; forgeStage = 3; quenchT = 0; }
                        else { ringT = 0; ringDone = false; ringX = 0.25f + rnd() * 0.5f; ringY = 0.3f + rnd() * 0.4f; }
                    }
                }
            } else {
                quenchT += dt;
                if (quenchT > 1.3f) {
                    float q = clamp01(s0 * 0.25f + s1 * 0.35f + s2 * 0.4f + RC().forge / 100.0f);
                    resultItem = makeItem(forgeCat, sel, q);
                    items.push_back(resultItem);
                    if (forgeCat == 0) G.cForgedW++; else G.cForgedA++;
                    gainXP(20 + resultItem.n * 3);
                    state = ST_RESULT;
                    saveGame();
                }
            }
        } else if (state == ST_RESULT) {
            if (aP || bP) { memset(sel, 0, sizeof sel); state = ST_ORES; oreCur = 0; }
        } else if (state == ST_INV) {
            int n = (int)items.size();
            if (nU && n) invCur = (invCur + n - 1) % n;
            if (nD && n) invCur = (invCur + 1) % n;
            if (bP || yP) { state = ST_PLAY; saveGame(); }
            else if (n && aP) {
                Item it = items[invCur];
                int slot = slotOf(it);
                items.erase(items.begin() + invCur);
                if (G.eqValid[slot]) items.push_back(G.eq[slot]);
                G.eq[slot] = it; G.eqValid[slot] = 1;
                invCur = 0;
                hp = std::min(hp, maxHP());
                setMsg("AUSGERUESTET!");
                saveGame();
            } else if (n && xP) {
                Item it = items[invCur];
                int back = std::max(1, it.n / 2);
                G.ore[it.ore] += back;
                items.erase(items.begin() + invCur);
                if (invCur >= (int)items.size()) invCur = std::max(0, (int)items.size() - 1);
                setMsg("RECYCELT: +%d %s", back, ORES[it.ore].name);
                saveGame();
            }
        }

        // ---- Zeichnen: Himmel ----
        glViewport(0, 0, SW, SH);
        const float* sT = SKYTOP[curWorld - 1];
        const float* sH = SKYHOR[curWorld - 1];
        glClearColor(sH[0], sH[1], sH[2], 1);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glDisable(GL_DEPTH_TEST);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        {
            M ui0 = ortho(0, (float)SW, (float)SH, 0, -1, 1);
            glUniformMatrix4fv(uVP, 1, GL_FALSE, ui0.m);
            M idm0 = I();
            glUniformMatrix4fv(uModel, 1, GL_FALSE, idm0.m);
            glUniformMatrix3fv(uRot, 1, GL_FALSE, id3);
            glUniform1f(uLit, 0);
            glUniform1f(uFogOn, 0);
            glUniform1f(uTex, 0);
            float horizon = SH * (0.5f - (cpitch - 0.5f) * 0.45f);
            for (int i = 0; i < 16; i++) {
                float t = i / 15.0f;
                float y0 = horizon * i / 16.0f, y1 = horizon * (i + 1) / 16.0f;
                float tt = 1 - t;
                drawRect(0, y0, (float)SW, y1 - y0 + 1, sT[0] + (sH[0] - sT[0]) * (1 - tt * tt), sT[1] + (sH[1] - sT[1]) * (1 - tt * tt), sT[2] + (sH[2] - sT[2]) * (1 - tt * tt), 1);
            }
            drawRect(0, horizon, (float)SW, SH - horizon + 1, sH[0] * 0.8f, sH[1] * 0.8f, sH[2] * 0.8f, 1);
        }

        // ---- Zeichnen: 3D ----
        glEnable(GL_DEPTH_TEST);
        V3 tgt = {px, py + 1.4f, pz};
        float dist = 8.0f;
        V3 eye = {tgt.x - sinf(cyaw) * dist * cosf(cpitch), tgt.y + dist * sinf(cpitch), tgt.z - cosf(cyaw) * dist * cosf(cpitch)};
        M vp = mul(persp(60.0f * PI / 180.0f, (float)SW / SH, 0.5f, 200.0f), lookAt(eye, tgt, {0, 1, 0}));
        glUniformMatrix4fv(uVP, 1, GL_FALSE, vp.m);
        glUniform3f(uFog, sH[0], sH[1], sH[2]);
        glUniform1f(uLit, 1);
        glUniform1f(uFogOn, 1);

        {
            glUniform1f(uTex, 1);
            const float (*dc)[3] = DISC[curWorld - 1];
            glUniform1f(uLit, curWorld == 2 ? 0.0f : 1.0f);
            drawDisc(-0.1f, DRAD[0], dc[0]);
            glUniform1f(uLit, 1);
            for (int i = 1; i < 7; i++) drawDisc(0.0f + 0.02f * i, DRAD[i], dc[i]);
            useMesh(cubeVbo);
            glUniform1f(uTex, 0);
            const float WATER[3] = {0.2f, 0.45f, 0.75f}, LAVA[3] = {0.9f, 0.3f, 0.05f};
            glUniform1f(uLit, curWorld == 2 ? 0.0f : 1.0f);
            drawBox(mul(T(0, -0.5f, 0), S(400, 0.4f, 400)), curWorld == 2 ? LAVA : WATER);
            glUniform1f(uLit, 1);
        }
        auto vis = [&](float x, float z) { float dx = x - px, dz = z - pz; return dx * dx + dz * dz < 70.0f * 70.0f; };

        // Klippen
        for (auto& c : cliffs) {
            if (!vis(c.x, c.z)) continue;
            float k = c.c;
            const float cc[3] = {(curWorld == 1 ? 0.45f : 0.28f) * k, (curWorld == 1 ? 0.42f : 0.22f) * k, (curWorld == 1 ? 0.4f : 0.22f) * k};
            box(T(c.x, 0, c.z), 0, c.h / 2, 0, c.w, c.h, c.d, cc);
            const float top[3] = {cc[0] * 1.15f, cc[1] * 1.15f, cc[2] * 1.15f};
            box(T(c.x, 0, c.z), 0, c.h + 0.5f, 0, c.w * 0.6f, 1.4f, c.d * 0.6f, top);
        }

        // Dorf
        {
            const float STONE[3] = {0.45f, 0.45f, 0.48f}, DARK[3] = {0.3f, 0.3f, 0.33f};
            M f = T(-7, 0, -5);
            box(f, 0, 0.8f, 0, 2.2f, 1.6f, 2.2f, STONE);
            box(f, 0, 2.0f, 0, 1.4f, 0.8f, 1.4f, DARK);
            box(f, 0.5f, 2.9f, 0.3f, 0.6f, 1.2f, 0.6f, DARK);
            const float GLOW[3] = {1.0f, 0.55f + 0.2f * sinf(timeAcc * 6), 0.1f};
            glUniform1f(uLit, 0);
            box(f, 0, 0.7f, 1.12f, 1.0f, 0.7f, 0.1f, GLOW);
            glUniform1f(uLit, 1);

            M a = T(ANV_X, 0, ANV_Z);
            const float AN[3] = {0.22f, 0.22f, 0.25f}, AT[3] = {0.35f, 0.35f, 0.4f};
            box(a, 0, 0.35f, 0, 0.9f, 0.7f, 0.9f, AN);
            box(a, 0, 0.85f, 0, 0.6f, 0.3f, 0.6f, AN);
            box(a, 0, 1.2f, 0, 1.8f, 0.35f, 0.7f, AT);
            box(a, 1.1f, 1.2f, 0, 0.5f, 0.2f, 0.5f, AT);

            const float HW[3] = {0.55f, 0.4f, 0.28f}, HR[3] = {0.45f, 0.2f, 0.18f}, HD[3] = {0.25f, 0.15f, 0.08f};
            M h = T(0, 0, -15);
            box(h, 0, 1.5f, 0, 6, 3, 4, HW);
            box(h, 0, 3.3f, 0, 7, 0.6f, 5, HR);
            box(h, 0, 1.0f, 2.05f, 1.2f, 2.0f, 0.1f, HD);

            // Laternen
            const float LP[3] = {0.25f, 0.2f, 0.15f}, LG[3] = {1.0f, 0.85f, 0.4f};
            static const float LX[4] = {-5, 5, -5, 5}, LZ[4] = {-3, -3, 5, 5};
            for (int i = 0; i < 4; i++) {
                box(T(LX[i], 0, LZ[i]), 0, 1.3f, 0, 0.15f, 2.6f, 0.15f, LP);
                glUniform1f(uLit, 0);
                box(T(LX[i], 0, LZ[i]), 0, 2.8f, 0, 0.4f, 0.4f, 0.4f, LG);
                glUniform1f(uLit, 1);
            }
        }
        for (auto& n : npcs) {
            if (n.kind == 6) {
                const float PB[3] = {0.35f, 0.45f, 1.0f}, PD[3] = {0.2f, 0.2f, 0.3f};
                M p = T(n.x, 0, n.z);
                box(p, 0, 0.1f, 0, 3.4f, 0.2f, 3.4f, PD);
                glUniform1f(uLit, 0);
                box(p, 0, 0.25f, 0, 2.6f, 0.1f, 2.6f, PB);
                box(p, 0, 1.8f + sinf(timeAcc * 3) * 0.2f, 0, 0.5f, 0.5f, 0.5f, PB);
                glUniform1f(uLit, 1);
                continue;
            }
            static const float NC[6][3] = {{0.9f, 0.9f, 0.9f}, {0.55f, 0.35f, 0.15f}, {0.6f, 0.2f, 0.6f}, {0.2f, 0.45f, 0.7f}, {0.35f, 0.15f, 0.55f}, {0.8f, 0.65f, 0.15f}};
            static const float NP[6][3] = {{0.2f, 0.2f, 0.2f}, {0.3f, 0.25f, 0.2f}, {0.35f, 0.2f, 0.35f}, {0.2f, 0.2f, 0.3f}, {0.2f, 0.1f, 0.35f}, {0.3f, 0.5f, 0.2f}};
            static const float NH[6][3] = {{0.9f, 0.9f, 0.9f}, {0.95f, 0.8f, 0.2f}, {0.7f, 0.3f, 0.7f}, {0.3f, 0.5f, 0.8f}, {0.4f, 0.2f, 0.7f}, {0.9f, 0.8f, 0.2f}};
            Look lk = {NC[n.kind], NP[n.kind], (n.kind == 0) ? 0 : NH[n.kind], 0, 0};
            person(mul(T(n.x, 0, n.z), RY(n.yaw)), lk, 0, 0, sinf(timeAcc * 2 + n.x) * 0.1f, 0);
            if (n.kind == 0 && questDone()) {
                const float QM[3] = {1.0f, 0.9f, 0.2f};
                box(T(n.x, 3.3f + sinf(timeAcc * 5) * 0.15f, n.z), 0, 0, 0, 0.3f, 0.7f, 0.3f, QM);
            }
        }

        // Felsen
        for (auto& n : nodes) {
            if (!vis(n.x, n.z)) continue;
            if (n.respawn > 0) {
                box(T(n.x, 0, n.z), 0, 0.15f, 0, 0.9f, 0.3f, 0.9f, ROCKS[n.type].col);
                continue;
            }
            float sh = sinf(timeAcc * 60) * (n.shake > 0 ? n.shake : 0) * 0.15f;
            M b = mul(T(n.x + sh, 0, n.z), RY(n.rot));
            float z = n.sz;
            const float* rc = ROCKS[n.type].col;
            box(b, 0, z * 0.5f, 0, z * 1.5f, z, z * 1.3f, rc);
            box(b, z * 0.2f, z * 1.05f, 0, z * 0.9f, z * 0.45f, z * 0.8f, rc);
            // sichtbare Erzkristalle: haeufigste Erze dieses Felsens
            int shown = 0;
            int mask = 1 << n.type;
            for (int i = NORE - 1; i >= 0 && shown < 3; i--) {
                if (!(ORES[i].rocks & mask)) continue;
                if (shown == 0 || (i + n.type) % 3 == 0 || shown < 2) {
                    static const float ox[3] = {0.35f, -0.4f, 0.5f}, oy[3] = {1.0f, 0.65f, 0.35f}, oz[3] = {0.1f, 0.5f, -0.5f};
                    glUniform1f(uLit, ORES[i].rar >= 3 ? 0.0f : 1.0f);
                    box(b, z * ox[shown], z * oy[shown], z * oz[shown], z * 0.42f, z * 0.42f, z * 0.42f, ORES[i].col);
                    glUniform1f(uLit, 1);
                    shown++;
                }
            }
        }
        // Baeume (Insel 1)
        {
            const float TR[3] = {0.4f, 0.28f, 0.15f}, LV[3] = {0.18f, 0.5f, 0.2f}, LV2[3] = {0.25f, 0.6f, 0.25f};
            for (auto& t : trees) {
                if (!vis(t.x, t.z)) continue;
                M b = mul(T(t.x, 0, t.z), S(t.sz, t.sz, t.sz));
                box(b, 0, 1.0f, 0, 0.5f, 2.0f, 0.5f, TR);
                box(b, 0, 2.5f, 0, 2.4f, 1.2f, 2.4f, LV);
                box(b, 0, 3.5f, 0, 1.5f, 1.0f, 1.5f, LV2);
            }
        }
        // Gegner
        {
            const float WH[3] = {1, 1, 1}, HPC[3] = {0.9f, 0.15f, 0.15f}, AXC[3] = {0.5f, 0.5f, 0.55f}, BAR[3] = {0.5f, 0.15f, 0.1f}, RED_[3] = {1.0f, 0.2f, 0.2f};
            for (auto& en : enemies) {
                if (en.dead > 0 || !vis(en.x, en.z)) continue;
                const EType& t = ETY[en.type];
                float z = t.sz;
                const float* col = en.flash > 0 ? WH : t.col;
                M b = mul(T(en.x, 0, en.z), RY(en.yaw));
                if (t.flag & 2) {
                    float bounce = 0.1f * fabsf(sinf(timeAcc * 4 + en.x));
                    box(b, 0, 0.5f * z + bounce, 0, 1.4f * z, 0.9f * z, 1.4f * z, col, 0.9f);
                    box(b, 0, 1.1f * z + bounce, 0, 1.0f * z, 0.5f * z, 1.0f * z, col, 0.9f);
                    box(b, -0.25f * z, 0.8f * z + bounce, 0.72f * z, 0.18f * z, 0.22f * z, 0.08f, EYEC);
                    box(b, 0.25f * z, 0.8f * z + bounce, 0.72f * z, 0.18f * z, 0.22f * z, 0.08f, EYEC);
                } else if (t.flag & 4) {
                    float fl = 0.5f + 0.2f * sinf(timeAcc * 2);
                    box(b, 0, 1.3f * z + fl, 0, 1.0f * z, 2.0f * z, 0.7f * z, col);
                    box(b, 0, 2.5f * z + fl, 0, 0.8f * z, 0.7f * z, 0.8f * z, col);
                    glUniform1f(uLit, 0);
                    box(b, -0.15f * z, 2.5f * z + fl, 0.42f * z, 0.12f * z, 0.12f * z, 0.05f, RED_);
                    box(b, 0.15f * z, 2.5f * z + fl, 0.42f * z, 0.12f * z, 0.12f * z, 0.05f, RED_);
                    glUniform1f(uLit, 1);
                    float sw = en.atk > 0.9f ? -1.0f : 0.2f;
                    M sc = mul(b, mul(T(0.9f * z, 1.8f * z + fl, 0.2f), RX(sw)));
                    drawBox(mul(sc, mul(T(0, 0.2f, 0), S(0.1f, 3.0f * z, 0.1f))), WOOD);
                    drawBox(mul(sc, mul(T(0, 1.7f * z, 0.5f), S(0.08f, 0.2f, 1.2f * z))), AXC);
                } else {
                    float sw = en.atk > 0.9f ? -1.4f : sinf(timeAcc * 6 + en.x) * 0.3f;
                    box(b, -0.18f * z, 0.45f * z, 0, 0.3f * z, 0.9f * z, 0.3f * z, col);
                    box(b, 0.18f * z, 0.45f * z, 0, 0.3f * z, 0.9f * z, 0.3f * z, col);
                    box(b, 0, 1.3f * z, 0, 0.8f * z, 0.9f * z, 0.45f * z, col);
                    box(b, 0, 2.0f * z, 0, 0.6f * z, 0.6f * z, 0.6f * z, col);
                    box(b, -0.15f * z, 2.05f * z, 0.31f * z, 0.12f * z, 0.12f * z, 0.05f, EYEC);
                    box(b, 0.15f * z, 2.05f * z, 0.31f * z, 0.12f * z, 0.12f * z, 0.05f, EYEC);
                    M arm = mul(b, mul(T(0.6f * z, 1.6f * z, 0), RX(sw)));
                    drawBox(mul(arm, mul(T(0, -0.4f * z, 0), S(0.25f * z, 0.8f * z, 0.25f * z))), col);
                    if (en.type >= 5 && en.type <= 9) {
                        drawBox(mul(arm, mul(T(0, -0.8f * z, 0.3f * z), S(0.08f, 0.1f, 0.9f * z))), WOOD);
                        drawBox(mul(arm, mul(T(0, -0.8f * z, 0.75f * z), S(0.1f, 0.5f * z, 0.3f * z))), AXC);
                    }
                    if (t.flag & 1) box(b, 0, 1.4f * z, -0.4f * z, 0.5f * z, 0.6f * z, 0.4f * z, BAR);
                }
                if (en.hp < en.maxhp) {
                    float fr = en.hp / en.maxhp;
                    box(T(en.x, 2.9f * z + 0.3f, en.z), 0, 0, 0, 1.2f * z * std::max(0.02f, fr), 0.15f, 0.15f, HPC);
                }
            }
        }
        // Teilchen
        for (auto& p : parts) box(T(p.x, p.y, p.z), 0, 0, 0, 0.18f, 0.18f, 0.18f, p.c);
        if (tg.kind) {
            float tx = 0, ty = 0, tz = 0;
            if (tg.kind == 1) { tx = nodes[tg.idx].x; tz = nodes[tg.idx].z; ty = nodes[tg.idx].sz + 1.2f; }
            else if (tg.kind == 2) { tx = npcs[tg.idx].x; tz = npcs[tg.idx].z; ty = 3.8f; }
            else { tx = ANV_X; tz = ANV_Z; ty = 2.4f; }
            const float MK[3] = {1.0f, 0.9f, 0.2f};
            glUniform1f(uLit, 0);
            box(mul(T(tx, ty + sinf(timeAcc * 5) * 0.15f, tz), RY(timeAcc * 3)), 0, 0, 0, 0.35f, 0.35f, 0.35f, MK);
            glUniform1f(uLit, 1);
        }
        // Spieler
        {
            const float SHADOW[3] = {0, 0, 0};
            box(T(px, 0.2f, pz), 0, 0, 0, 1.0f, 0.02f, 0.8f, SHADOW, 0.35f);
            Look lk = {G.eqValid[2] ? ORES[G.eq[2].ore].col : BLUE, G.eqValid[3] ? ORES[G.eq[3].ore].col : PANTS,
                       G.eqValid[1] ? ORES[G.eq[1].ore].col : 0, G.eqValid[0] ? ORES[G.eq[0].ore].col : 0, ORES[PICKS[G.pickEq].col].col};
            float amp = (state == ST_PLAY && stunT <= 0) ? std::min(1.0f, sqrtf(mx * mx + my * my)) : 0.0f;
            float armR, armL = 0;
            if (swingT > 0) {
                float p = 1 - swingT / mineCd();
                armR = p < 0.45f ? (-0.4f + (-2.5f + 0.4f) * (p / 0.45f))
                                 : (-2.5f + (-0.2f + 2.5f) * ((p - 0.45f) / 0.55f));
            } else armR = sinf(walkPh) * 0.7f * amp * 0.7f - 0.3f;
            if (atkT > 0) {
                float p = 1 - atkT / 0.35f;
                armL = -2.0f + 1.8f * p;
            } else if (blocking) armL = -1.4f;
            M pb = mul(T(px, py, pz), RY(yaw));
            if (dashT > 0) pb = mul(pb, RX(0.4f));
            person(pb, lk, walkPh, amp, armR, armL);
        }

        // ---- Zeichnen: UI ----
        glDisable(GL_DEPTH_TEST);
        M ui = ortho(0, (float)SW, (float)SH, 0, -1, 1);
        glUniformMatrix4fv(uVP, 1, GL_FALSE, ui.m);
        M idm = I();
        glUniformMatrix4fv(uModel, 1, GL_FALSE, idm.m);
        glUniformMatrix3fv(uRot, 1, GL_FALSE, id3);
        glUniform1f(uLit, 0);
        glUniform1f(uFogOn, 0);

        char line[110];
        if (hurtT > 0 || deathFlash > 0) drawRect(0, 0, (float)SW, (float)SH, 0.8f, 0, 0, std::max(hurtT * 0.8f, deathFlash * 0.5f));

        if (state == ST_PLAY) {
            // Leben / Ausdauer unten links, Level oben links
            drawRect(6, 6, 190, 40, 0, 0, 0, 0.45f);
            snprintf(line, sizeof line, "LV %d  %s", G.level, RC().name);
            drawText(line, 12, 10, 2, WHITE3);
            drawBar(12, 28, 178, 10, (float)G.xp / xpNeed(), 0.3f, 0.7f, 1.0f);
            drawRect(6, SH - 52, 210, 46, 0, 0, 0, 0.45f);
            snprintf(line, sizeof line, "%d/%d", (int)ceilf(hp), (int)mhp);
            drawText(line, 12, SH - 48, 2, RED3);
            drawBar(12, SH - 32, 198, 12, hp / mhp, 0.85f, 0.15f, 0.15f);
            drawBar(12, SH - 16, 198, 8, stam / msm, 0.9f, 0.8f, 0.2f);
            snprintf(line, sizeof line, "$%d", G.coins);
            drawRect((float)(SW - textW(line, 3) - 18), 6, (float)(textW(line, 3) + 12), 26, 0, 0, 0, 0.45f);
            drawText(line, (float)(SW - textW(line, 3) - 12), 10, 3, GOLD3);
            // Questanzeige rechts
            if (G.questIdx < NQUEST) {
                const Quest& q = QUESTS[G.questIdx];
                drawRect((float)(SW - 224), 44, 218, 50, 0, 0, 0, 0.4f);
                drawText("QUEST", (float)(SW - 218), 48, 1, GOLD3);
                drawText(q.text, (float)(SW - 218), 60, 1, WHITE3);
                snprintf(line, sizeof line, "%d/%d", std::min(questProgress(G.questIdx), q.target), q.target);
                drawText(line, (float)(SW - 218), 74, 2, GREEN3);
            }
            if (G.luckPot > 0 || G.minePot > 0) {
                snprintf(line, sizeof line, "%s%s", G.luckPot > 0 ? "GLUECK " : "", G.minePot > 0 ? "ABBAU" : "");
                drawText(line, (float)(SW - 218), 100, 1, GREEN3);
            }
            if (bannerT > 0 && curArea >= 0) drawTextC(AREAN[curWorld - 1][curArea], 70, 3, WHITE3);
            // Hotbar
            {
                float bx = SW / 2.0f - 70, by = SH - 66;
                for (int i = 0; i < 2; i++) {
                    float x0 = bx + i * 76;
                    drawRect(x0, by, 64, 56, 0, 0, 0, 0.55f);
                    drawRect(x0, by, 64, 2, 0.85f, 0.65f, 0.25f, 0.9f);
                    const char* nm;
                    const float* cc;
                    if (i == 0) { nm = PICKS[G.pickEq].name; cc = ORES[PICKS[G.pickEq].col].col; }
                    else if (G.eqValid[0]) { nm = WEAP[G.eq[0].kind].name; cc = ORES[G.eq[0].ore].col; }
                    else { nm = "FAUST"; cc = LGRAY3; }
                    drawRect(x0 + 22, by + 10, 20, 20, cc[0], cc[1], cc[2], 1);
                    snprintf(line, sizeof line, "%d", i + 1);
                    drawText(line, x0 + 4, by + 4, 2, WHITE3);
                    char nb[12];
                    snprintf(nb, sizeof nb, "%.7s", nm);
                    drawText(nb, x0 + 3, by + 38, 1, WHITE3);
                }
            }
            if (tg.kind) {
                if (tg.kind == 1) {
                    Node& n = nodes[tg.idx];
                    drawBar(SW / 2.0f - 100, SH - 100, 200, 10, n.hp / n.maxhp, 0.9f, 0.6f, 0.2f);
                    float m = tri(timeAcc * 1.4f);
                    drawRect(SW / 2.0f - 100, SH - 86, 200, 8, 0.3f, 0.1f, 0.1f, 0.9f);
                    drawRect(SW / 2.0f - 12, SH - 86, 24, 8, 0.3f, 0.9f, 0.3f, 1);
                    drawRect(SW / 2.0f - 100 + 200 * m - 2, SH - 90, 4, 16, 1, 1, 1, 1);
                    snprintf(line, sizeof line, "A: ABBAUEN (%s)", ROCKS[n.type].name);
                } else if (tg.kind == 2) {
                    static const char* NN[7] = {"SENSEI MORO", "MINER FRED", "MARIA", "RUNEMAKER", "WIZARD", "GREEDY CEY", "PORTAL"};
                    snprintf(line, sizeof line, "A: %s", NN[npcs[tg.idx].kind]);
                } else snprintf(line, sizeof line, "A: SCHMIEDEN");
                drawTextC(line, SH - 126, 3, WHITE3);
            }
            drawText("X:ANGRIFF Y:TASCHE L:BLOCK R:DASH START+SELECT:ENDE", 8, 52, 1, LGRAY3);
        } else if (state == ST_MENU) {
            panel(30, 40, SW - 60, 400);
            drawText(menuTitle, 50, 52, 4, GOLD3);
            snprintf(line, sizeof line, "$%d", G.coins);
            drawText(line, SW - 50 - textW(line, 3), 56, 3, GOLD3);
            for (size_t i = 0; i < menuLines.size() && i < 12; i++) {
                float y = 110 + i * 28;
                if ((int)i == menuCur) selRow(40, y - 4, SW - 80, 24);
                drawText(menuLines[i].t, 52, y, 2, menuLines[i].ok ? WHITE3 : GRAY3);
            }
            if (menuType == M_WIZARD && !menuLines.empty()) {
                drawText(RC().desc, 52, 400, 2, GREEN3);
            }
            drawText("A: WAEHLEN   B: ZURUECK", 50, 418, 2, LGRAY3);
        } else if (state == ST_QUEST) {
            panel(30, 100, SW - 60, 280);
            drawText("SENSEI MORO", 50, 112, 4, GOLD3);
            if (G.questIdx >= NQUEST) drawText("DU HAST ALLE AUFGABEN GEMEISTERT!", 50, 190, 2, WHITE3);
            else {
                const Quest& q = QUESTS[G.questIdx];
                snprintf(line, sizeof line, "AUFGABE %d/%d", G.questIdx + 1, NQUEST);
                drawText(line, 50, 160, 2, LGRAY3);
                drawText(q.text, 50, 190, 3, WHITE3);
                int pr = std::min(questProgress(G.questIdx), q.target);
                snprintf(line, sizeof line, "FORTSCHRITT: %d/%d", pr, q.target);
                drawText(line, 50, 240, 3, GREEN3);
                drawBar(50, 280, SW - 100, 14, (float)pr / q.target, 0.3f, 0.8f, 0.3f);
                snprintf(line, sizeof line, "BELOHNUNG: $%d", q.reward);
                drawText(line, 50, 310, 2, GOLD3);
                if (questDone()) drawText("A: BELOHNUNG ABHOLEN", 50, 345, 2, GOLD3);
            }
            drawText("B: ZURUECK", 440, 345, 2, LGRAY3);
        } else if (state == ST_ORES) {
            std::vector<int> av;
            for (int i = 0; i < NORE; i++) if (G.ore[i] > 0) av.push_back(i);
            int na = (int)av.size(), total = 0;
            float msum = 0;
            for (int i = 0; i < NORE; i++) { total += sel[i]; msum += sel[i] * ORES[i].mult; }
            panel(14, 14, SW - 28, 452);
            drawText("SCHMIEDE: ERZE WAEHLEN", 30, 22, 3, GOLD3);
            snprintf(line, sizeof line, "GEWAEHLT: %d/60 (MIN 3)  MULT: %.2f", total, total ? msum / total : 0.0f);
            drawText(line, 30, 52, 2, WHITE3);
            const char* cls = total < 3 ? "-" : WEAP[likelyWeapon(total)].name;
            const char* acl = total < 3 ? "-" : ARM[likelyArmor(total)].name;
            snprintf(line, sizeof line, "WAHRSCHEINLICH: %s / %s", cls, acl);
            drawText(line, 30, 72, 2, GREEN3);
            if (!na) drawText("KEINE ERZE IN DER TASCHE", 30, 120, 2, GRAY3);
            int start = std::max(0, std::min(oreCur - 4, std::max(0, na - 9)));
            for (int k = start; k < na && k < start + 9; k++) {
                int i = av[k];
                float y = 98 + (k - start) * 32;
                if (k == oreCur) selRow(22, y - 3, SW - 44, 28);
                drawRect(30, y + 1, 18, 18, ORES[i].col[0], ORES[i].col[1], ORES[i].col[2], 1);
                snprintf(line, sizeof line, "%-12s", ORES[i].name);
                drawText(line, 56, y + 3, 2, RARC[ORES[i].rar]);
                snprintf(line, sizeof line, "BESITZ %d", G.ore[i]);
                drawText(line, 210, y + 3, 2, LGRAY3);
                snprintf(line, sizeof line, "GEWAEHLT %d", sel[i]);
                drawText(line, 330, y + 3, 2, sel[i] ? GOLD3 : GRAY3);
                snprintf(line, sizeof line, "x%.2f", ORES[i].mult);
                drawText(line, 480, y + 3, 2, GREEN3);
            }
            if (na) {
                int i = av[oreCur];
                snprintf(line, sizeof line, "%s [%s] 1/%d - TRAIT: %s", ORES[i].name, RARN[ORES[i].rar], ORES[i].chance, TRAITN[ORES[i].trait]);
                drawText(line, 30, 394, 1, RARC[ORES[i].rar]);
            }
            drawText("TRAITS WIRKEN AB 10% ANTEIL IN DER MISCHUNG", 30, 408, 1, LGRAY3);
            drawText("A:+1  R:+5  X:-1  Y:WEITER  B:ZURUECK", 30, 430, 2, LGRAY3);
        } else if (state == ST_CAT) {
            int total = 0;
            for (int i = 0; i < NORE; i++) total += sel[i];
            panel(60, 100, SW - 120, 260);
            drawText("WAS SCHMIEDEN?", 80, 112, 4, GOLD3);
            const char* opts[2] = {"WAFFE", "RUESTUNG"};
            for (int i = 0; i < 2; i++) {
                float y = 170 + i * 60;
                if (i == catCur) selRow(70, y - 6, SW - 140, 44);
                drawText(opts[i], 90, y + 4, 3, WHITE3);
            }
            snprintf(line, sizeof line, "%d ERZE: DIE ART WIRD DURCH DIE MENGE BESTIMMT", total);
            drawText(line, 80, 300, 1, LGRAY3);
            drawText("A: STARTEN   B: ZURUECK", 80, 330, 2, LGRAY3);
        } else if (state == ST_FORGE) {
            panel(30, 70, SW - 60, 340);
            const char* stg[4] = {"1/4 SCHMELZEN", "2/4 GIESSEN", "3/4 HAMMERN", "4/4 ABSCHRECKEN"};
            drawTextC(stg[forgeStage], 82, 3, GOLD3);
            if (forgeStage == 0) {
                drawTextC("BLASEBALG: A SCHNELL DRUECKEN!", 125, 2, WHITE3);
                drawRect(SW / 2.0f - 25, 150, 50, 220, 0.15f, 0.15f, 0.15f, 1);
                float h = 220 * clamp01(smeltHeat);
                drawRect(SW / 2.0f - 25, 370 - h, 50, h, 1.0f, 0.4f + 0.4f * smeltHeat, 0.1f, 1);
                snprintf(line, sizeof line, "ZEIT: %.1f", std::max(0.0f, 8 - smeltTime));
                drawTextC(line, 380, 2, LGRAY3);
            } else if (forgeStage == 1) {
                drawTextC("A GEDRUECKT HALTEN: ANZEIGE IN DER ZONE LASSEN", 125, 2, WHITE3);
                float bx = SW / 2.0f - 25, by = 150, bh = 210;
                drawRect(bx, by, 50, bh, 0.15f, 0.15f, 0.2f, 1);
                float zc = 0.5f + 0.28f * sinf(pourT * 1.4f);
                drawRect(bx, by + bh * (1 - (zc + 0.13f)), 50, bh * 0.26f, 0.3f, 0.75f, 0.3f, 1);
                drawRect(bx - 6, by + bh * (1 - pourY) - 3, 62, 6, 1, 1, 1, 1);
                drawBar(120, 380, SW - 240, 14, pourProg, 1.0f, 0.5f, 0.1f);
            } else if (forgeStage == 2) {
                snprintf(line, sizeof line, "SCHLAG %d/5 - A WENN DER RING DAS ZIEL TRIFFT", std::min(ringIdx + 1, 5));
                drawTextC(line, 125, 2, WHITE3);
                float cx0 = 60 + (SW - 120) * ringX, cy0 = 150 + 200 * ringY;
                outline(cx0 - 20, cy0 - 20, 40, 40, 3, GOLD3);
                if (!ringDone) {
                    float dur = 1.2f - 0.05f * ringIdx;
                    float r = std::max(0.0f, 1 - ringT / dur);
                    float sz = 40 + r * 140;
                    outline(cx0 - sz / 2, cy0 - sz / 2, sz, sz, 3, WHITE3);
                }
            } else {
                drawTextC("DAS METALL KUEHLT AB...", 200, 3, WHITE3);
                drawBar(160, 260, SW - 320, 16, quenchT / 1.3f, 0.3f, 0.6f, 1.0f);
            }
        } else if (state == ST_RESULT) {
            panel(60, 60, SW - 120, 360);
            const Item& it = resultItem;
            char nm[60];
            itemName(it, nm, sizeof nm);
            drawTextC("GESCHMIEDET!", 76, 4, GOLD3);
            drawTextC(nm, 126, 3, RARC[it.rar]);
            snprintf(line, sizeof line, "%s  QUALITAET: %d%%", RARN[it.rar], (int)(it.q * 100));
            drawTextC(line, 166, 2, RARC[it.rar]);
            float y = 200;
            if (it.kind < 7) { snprintf(line, sizeof line, "SCHADEN: %.0f", it.dmg); drawTextC(line, y, 2, WHITE3); y += 24; }
            else { snprintf(line, sizeof line, "RUESTUNG: %.0f   LEBEN: %+.0f%%", it.def, it.hpP * 100); drawTextC(line, y, 2, WHITE3); y += 24; }
            snprintf(line, sizeof line, "ERZE: %d   HAUPT-ERZ: %s   WERT: $%d", it.n, ORES[it.ore].name, it.val);
            drawTextC(line, y, 2, LGRAY3); y += 24;
            if (it.crit > 0) { snprintf(line, sizeof line, "+%.0f%% KRITISCH", it.crit * 100); drawTextC(line, y, 2, GREEN3); y += 22; }
            if (it.burn > 0) { snprintf(line, sizeof line, "%.0f%% BRENNEN", it.burn * 100); drawTextC(line, y, 2, GREEN3); y += 22; }
            if (it.expl > 0) { snprintf(line, sizeof line, "%.0f%% EXPLOSION", it.expl * 100); drawTextC(line, y, 2, GREEN3); y += 22; }
            if (it.spd > 0) { snprintf(line, sizeof line, "+%.0f%% TEMPO", it.spd * 100); drawTextC(line, y, 2, GREEN3); y += 22; }
            if (it.dodge > 0) { snprintf(line, sizeof line, "+%.0f%% AUSWEICHEN", it.dodge * 100); drawTextC(line, y, 2, GREEN3); y += 22; }
            if (it.aura > 0) { drawTextC("AURA SCHADEN", y, 2, GREEN3); y += 22; }
            if (it.thorns > 0) { drawTextC("DORNEN (BRENNEN)", y, 2, GREEN3); y += 22; }
            if (it.poison) { drawTextC("GIFTWOLKE BEI NIEDRIGEM LEBEN", y, 2, GREEN3); y += 22; }
            drawTextC("A: WEITER", 390, 2, LGRAY3);
        } else if (state == ST_INV) {
            panel(14, 14, SW - 28, 452);
            drawText("TASCHE", 30, 22, 4, GOLD3);
            snprintf(line, sizeof line, "RUESTUNG %.0f  LEBEN %d  RASSE %s", defTotal(), (int)mhp, RC().name);
            drawText(line, 190, 28, 2, WHITE3);
            for (int i = 0; i < 4; i++) {
                if (G.eqValid[i]) {
                    char nm[60];
                    itemName(G.eq[i], nm, sizeof nm);
                    if (i == 0) snprintf(line, sizeof line, "%s: %s  SCH %.0f", SLOTN[i], nm, G.eq[i].dmg);
                    else snprintf(line, sizeof line, "%s: %s  RUE %.0f", SLOTN[i], nm, G.eq[i].def);
                    drawText(line, 30, 62 + i * 18, 2, RARC[G.eq[i].rar]);
                } else { snprintf(line, sizeof line, "%s: -", SLOTN[i]); drawText(line, 30, 62 + i * 18, 2, GRAY3); }
            }
            snprintf(line, sizeof line, "PICKE: %s (POW %d)  RUNEN: %d INSTALLIERT", PICKS[G.pickEq].name, PICKS[G.pickEq].power, G.runeInst[0] + G.runeInst[1] + G.runeInst[2] + G.runeInst[3] + G.runeInst[4] + G.runeInst[5]);
            drawText(line, 30, 136, 2, LGRAY3);
            int n = (int)items.size();
            if (!n) drawText("KEINE GESCHMIEDETEN STUECKE", 30, 180, 2, GRAY3);
            int start = std::max(0, std::min(invCur - 3, std::max(0, n - 7)));
            for (int i = start; i < n && i < start + 7; i++) {
                float y = 164 + (i - start) * 34;
                if (i == invCur) selRow(22, y - 4, SW - 44, 30);
                char nm[60];
                itemName(items[i], nm, sizeof nm);
                drawText(nm, 30, y + 2, 2, RARC[items[i].rar]);
                if (items[i].kind < 7) snprintf(line, sizeof line, "SCH %.0f", items[i].dmg);
                else snprintf(line, sizeof line, "RUE %.0f", items[i].def);
                drawText(line, 400, y + 2, 2, WHITE3);
                snprintf(line, sizeof line, "$%d", items[i].val);
                drawText(line, 520, y + 2, 2, GOLD3);
            }
            drawText("A:ANLEGEN  X:RECYCELN  B:ZURUECK", 30, 440, 2, LGRAY3);
        }
        if (msgT > 0) drawTextC(msgBuf, 20, 3, msgCol);
        if (lastIn[0]) drawText(lastIn, (float)(SW - textW(lastIn, 1) - 6), (float)(SH - 12), 1, GRAY3);

        SDL_GL_SwapWindow(win);
    }

    saveGame();
    if (joy) SDL_JoystickClose(joy);
    SDL_GL_DeleteContext(ctx);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
