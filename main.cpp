// FORGE fuer R36S (ArkOS) - Stufe 1
// Abbauen, Schmelzen, Schmieden, Verkaufen. SDL2 + OpenGL ES 2.0
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
// Steuerung (kann ueber controls.cfg geaendert werden)
// ---------------------------------------------------------------
static int BTN_A = 0, BTN_B = 1, BTN_SELECT = 6, BTN_START = 7;
static int BTN_UP = -1, BTN_DOWN = -1, BTN_LEFT = -1, BTN_RIGHT = -1;
static int AX_LX = 0, AX_LY = 1, AX_RX = 2, AX_RY = 3;

struct CfgEntry { const char* name; int* val; };
static void loadControls() {
    CfgEntry t[] = {{"A", &BTN_A}, {"B", &BTN_B}, {"SELECT", &BTN_SELECT}, {"START", &BTN_START},
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

// ---------------------------------------------------------------
// OpenGL
// ---------------------------------------------------------------
static GLuint prog = 0, cubeVbo = 0, discVbo = 0;
static GLint uVP, uModel, uRot, uColor, uFog, uLit, uFogOn, aPos, aNrm;
static int SW = 640, SH = 480;

static const char* VS =
    "attribute vec3 aPos;attribute vec3 aNrm;\n"
    "uniform mat4 uVP;uniform mat4 uModel;uniform mat3 uRot;\n"
    "varying float vLight;varying float vDist;\n"
    "void main(){vec4 w=uModel*vec4(aPos,1.0);gl_Position=uVP*w;\n"
    "vec3 n=normalize(uRot*aNrm);\n"
    "float l=max(dot(n,normalize(vec3(0.4,0.8,0.3))),0.0);\n"
    "vLight=0.5+0.5*l;vDist=gl_Position.w;}\n";
static const char* FS =
    "precision mediump float;\n"
    "uniform vec4 uColor;uniform vec3 uFog;uniform float uLit;uniform float uFogOn;\n"
    "varying float vLight;varying float vDist;\n"
    "void main(){vec3 c=uColor.rgb*mix(1.0,vLight,uLit);\n"
    "float f=clamp((vDist-30.0)/45.0,0.0,1.0)*uFogOn;\n"
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
    {'(', "001010010010001"}, {')', "100010010010100"}};
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
static void drawText(const char* s, float x, float y, int sc, float r, float g, float b) {
    for (int pass = 0; pass < 2; pass++) {
        std::vector<float> v;
        float ox = pass == 0 ? (float)sc : 0, oy = pass == 0 ? (float)sc : 0;
        float cx = x;
        for (const char* p = s; *p; p++) {
            int ch = toupper((unsigned char)*p);
            const char* gl = (ch > 0 && ch < 128) ? glyphOf[ch] : 0;
            if (gl) {
                for (int i = 0; i < 15; i++)
                    if (gl[i] == '1') quadV(v, cx + ox + (i % 3) * sc, y + oy + (i / 3) * sc, (float)sc, (float)sc);
            }
            cx += 4 * sc;
        }
        if (pass == 0) flushUI(v, 0, 0, 0, 0.8f);
        else flushUI(v, r, g, b, 1);
    }
}
static void drawTextC(const char* s, float y, int sc, float r, float g, float b) {
    drawText(s, (SW - textW(s, sc)) / 2.0f, y, sc, r, g, b);
}

// ---------------------------------------------------------------
// Spieldaten
// ---------------------------------------------------------------
static const char* OREN[4] = {"KUPFER", "EISEN", "GOLD", "KRISTALL"};
static const float OREC[4][3] = {{0.85f, 0.45f, 0.2f}, {0.78f, 0.8f, 0.88f}, {1.0f, 0.85f, 0.2f}, {0.65f, 0.4f, 1.0f}};
static const int ORE_HP[4] = {4, 8, 14, 24};
static const int ORE_PRICE[4] = {3, 6, 15, 40};
static const int BAR_PRICE[4] = {7, 14, 35, 90};

struct Recipe { const char* name; int ore; int need; int value; bool armor; };
static const Recipe REC[8] = {
    {"DOLCH", 0, 2, 25, false},          {"KUPFERSCHWERT", 0, 4, 60, false},
    {"EISENSCHWERT", 1, 4, 120, false},  {"EISENRUESTUNG", 1, 7, 220, true},
    {"GOLDSCHWERT", 2, 4, 300, false},   {"GOLDRUESTUNG", 2, 7, 520, true},
    {"KRISTALLKLINGE", 3, 4, 800, false}, {"KRISTALLRUESTUNG", 3, 7, 1300, true}};

static const char* PICKN[5] = {"HOLZ", "KUPFER", "EISEN", "GOLD", "KRISTALL"};
static const int PICKDMG[5] = {1, 2, 3, 5, 8};
static const int PICKCOST[5] = {0, 80, 300, 900, 2500};

struct Item { int rec; float mult; };
static int coins = 0, pickLvl = 0;
static int ore[4] = {0, 0, 0, 0}, bar[4] = {0, 0, 0, 0};
static std::vector<Item> items;
static float smeltT = 0;
static int smeltOre[4] = {0, 0, 0, 0};

static int itemVal(const Item& it) { return (int)(REC[it.rec].value * it.mult + 0.5f); }
static int armorTier() {
    int t = -1;
    for (auto& it : items)
        if (REC[it.rec].armor) t = std::max(t, REC[it.rec].ore);
    return t;
}

static const int SAVE_MAGIC = 0x46524732;
static void saveGame() {
    FILE* f = fopen("forge_save.dat", "wb");
    if (!f) return;
    int magic = SAVE_MAGIC;
    fwrite(&magic, 4, 1, f);
    fwrite(&coins, 4, 1, f);
    fwrite(&pickLvl, 4, 1, f);
    fwrite(ore, 4, 4, f);
    fwrite(bar, 4, 4, f);
    int n = (int)items.size();
    fwrite(&n, 4, 1, f);
    if (n) fwrite(items.data(), sizeof(Item), n, f);
    fclose(f);
}
static void loadGame() {
    FILE* f = fopen("forge_save.dat", "rb");
    if (!f) return;
    int magic = 0, n = 0;
    bool ok = fread(&magic, 4, 1, f) == 1 && magic == SAVE_MAGIC;
    ok = ok && fread(&coins, 4, 1, f) == 1 && fread(&pickLvl, 4, 1, f) == 1;
    ok = ok && fread(ore, 4, 4, f) == 4 && fread(bar, 4, 4, f) == 4 && fread(&n, 4, 1, f) == 1;
    if (ok && n >= 0 && n <= 200) {
        items.resize(n);
        if (n && fread(items.data(), sizeof(Item), n, f) != (size_t)n) items.clear();
    }
    fclose(f);
    if (pickLvl < 0 || pickLvl > 4) pickLvl = 0;
}

static char msgBuf[96];
static float msgT = 0;
static void setMsg(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msgBuf, sizeof msgBuf, fmt, ap);
    va_end(ap);
    msgT = 3.0f;
}

// ---------------------------------------------------------------
// Welt
// ---------------------------------------------------------------
struct Node { float x, z, sz, rot; int type; float hp, respawn, shake; };
struct Solid { float x, z, r; };
struct Tree { float x, z, sz; };
static std::vector<Node> nodes;
static std::vector<Solid> solids;
static std::vector<Tree> trees;

static const float FURN_X = -6, FURN_Z = -5, ANV_X = 0, ANV_Z = -8, SHOP_X = 6, SHOP_Z = -5;
static unsigned rs = 12345;
static float rnd() { rs = rs * 1664525u + 1013904223u; return ((rs >> 8) & 0xFFFFFF) / 16777216.0f; }
static bool freeSpot(float x, float z, float minD) {
    for (auto& s : solids) {
        float dx = x - s.x, dz = z - s.z, d = minD + s.r;
        if (dx * dx + dz * dz < d * d) return false;
    }
    return true;
}
static void genWorld() {
    solids.push_back({FURN_X, FURN_Z, 1.5f});
    solids.push_back({ANV_X, ANV_Z, 1.0f});
    solids.push_back({SHOP_X, SHOP_Z, 1.8f});
    solids.push_back({0, -15, 3.2f});
    float lo[4] = {14, 26, 38, 50}, hi[4] = {26, 38, 50, 60};
    int cnt[4] = {14, 14, 12, 10};
    for (int t = 0; t < 4; t++) {
        int placed = 0, tries = 0;
        while (placed < cnt[t] && tries < 500) {
            tries++;
            float a = rnd() * 2 * PI, r = lo[t] + rnd() * (hi[t] - lo[t]);
            float x = cosf(a) * r, z = sinf(a) * r;
            if (!freeSpot(x, z, 2.5f)) continue;
            Node n;
            n.x = x; n.z = z; n.sz = 1.3f + t * 0.25f; n.rot = rnd() * PI; n.type = t;
            n.hp = (float)ORE_HP[t]; n.respawn = 0; n.shake = 0;
            nodes.push_back(n);
            solids.push_back({x, z, n.sz * 0.9f});
            placed++;
        }
    }
    int placed = 0, tries = 0;
    while (placed < 60 && tries < 1000) {
        tries++;
        float a = rnd() * 2 * PI, r = 9 + rnd() * 54;
        float x = cosf(a) * r, z = sinf(a) * r;
        if (!freeSpot(x, z, 1.5f)) continue;
        trees.push_back({x, z, 0.8f + rnd() * 0.6f});
        solids.push_back({x, z, 0.6f});
        placed++;
    }
}

// ---------------------------------------------------------------
// Figuren
// ---------------------------------------------------------------
static const float SKIN[3] = {0.95f, 0.76f, 0.6f};
static const float HAIR[3] = {0.25f, 0.15f, 0.08f};
static const float PANTS[3] = {0.25f, 0.25f, 0.35f};
static const float WOOD[3] = {0.55f, 0.4f, 0.2f};
static const float ROCK[3] = {0.42f, 0.42f, 0.45f};

static void person(const M& base, const float* shirt, float ph, float amp, float armR, bool pick) {
    float s = sinf(ph) * 0.7f * amp;
    for (int i = 0; i < 2; i++) {
        float sx = i ? 0.2f : -0.2f;
        float a = i ? -s : s;
        M leg = mul(base, mul(T(sx, 0.8f, 0), mul(RX(a), mul(T(0, -0.4f, 0), S(0.35f, 0.8f, 0.35f)))));
        drawBox(leg, PANTS);
    }
    box(base, 0, 1.25f, 0, 0.85f, 0.9f, 0.45f, shirt);
    box(base, 0, 2.0f, 0, 0.55f, 0.55f, 0.55f, SKIN);
    box(base, 0, 2.3f, -0.02f, 0.6f, 0.14f, 0.6f, HAIR);
    M la = mul(base, mul(T(-0.55f, 1.65f, 0), RX(-s)));
    drawBox(mul(la, mul(T(0, -0.4f, 0), S(0.25f, 0.8f, 0.25f))), shirt);
    M ra = mul(base, mul(T(0.55f, 1.65f, 0), RX(armR)));
    drawBox(mul(ra, mul(T(0, -0.4f, 0), S(0.25f, 0.8f, 0.25f))), shirt);
    if (pick) {
        const float* hc = pickLvl == 0 ? WOOD : OREC[pickLvl - 1];
        drawBox(mul(ra, mul(T(0, -0.8f, 0.35f), S(0.1f, 0.1f, 1.0f))), WOOD);
        drawBox(mul(ra, mul(T(0, -0.8f, 0.8f), S(0.12f, 0.9f, 0.14f))), hc);
    }
}

// ---------------------------------------------------------------
// Hauptprogramm
// ---------------------------------------------------------------
enum { ST_PLAY, ST_ANVIL, ST_FORGE, ST_SHOP };
struct Target { int kind; int idx; float d; };  // 0 nichts, 1 Erz, 2 Ofen, 3 Amboss, 4 Haendler

static float ax(SDL_Joystick* joy, int i) {
    if (!joy || i < 0 || i >= SDL_JoystickNumAxes(joy)) return 0;
    float v = SDL_JoystickGetAxis(joy, i) / 32767.0f;
    return fabsf(v) < 0.25f ? 0.0f : v;
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
    aPos = glGetAttribLocation(prog, "aPos");
    aNrm = glGetAttribLocation(prog, "aNrm");
    glEnableVertexAttribArray(aPos);
    glEnableVertexAttribArray(aNrm);
    buildMeshes();
    initFont();
    genWorld();
    loadGame();

    SDL_Joystick* joy = 0;
    if (SDL_NumJoysticks() > 0) joy = SDL_JoystickOpen(0);

    const float SKY[3] = {0.55f, 0.75f, 0.95f};
    const float id3[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    const float GRASS[3] = {0.33f, 0.62f, 0.28f};

    float px = 0, py = 0, pz = 2, vy = 0, yaw = PI, walkPh = 0;
    float cyaw = PI, cpitch = 0.5f;
    float swingT = 0;
    bool hitDone = true;
    int swingNode = -1;
    int state = ST_PLAY, ai = 0, si = 0, forgeRec = 0;
    float forgeT = 0, forgeRes = 0, forgeMult = 1;
    char forgeLabel[24] = "";
    float timeAcc = 0;
    char lastIn[24] = "";
    bool selHeld = false, startHeld = false;
    int prevSd = 0;
    bool running = true;
    Uint32 last = SDL_GetTicks();

    setMsg("A: ABBAUEN / BENUTZEN   B: SPRINGEN");
    msgT = 6.0f;

    while (running) {
        Uint32 now = SDL_GetTicks();
        float dt = (now - last) / 1000.0f;
        last = now;
        if (dt > 0.05f) dt = 0.05f;
        timeAcc += dt;

        bool aP = false, bP = false, nU = false, nD = false;
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
        {
            float ly = ax(joy, AX_LY);
            int sd = ly < -0.6f ? -1 : (ly > 0.6f ? 1 : 0);
            if (sd != prevSd) {
                if (sd < 0) nU = true;
                if (sd > 0) nD = true;
                prevSd = sd;
            }
        }

        // ---- Update ----
        if (msgT > 0) msgT -= dt;
        for (auto& n : nodes) {
            if (n.respawn > 0) {
                n.respawn -= dt;
                if (n.respawn <= 0) n.hp = (float)ORE_HP[n.type];
            }
            if (n.shake > 0) n.shake -= dt;
        }
        if (smeltT > 0) {
            smeltT -= dt;
            if (smeltT <= 0) {
                for (int i = 0; i < 4; i++) { bar[i] += smeltOre[i]; smeltOre[i] = 0; }
                setMsg("BARREN FERTIG!");
                saveGame();
            }
        }

        Target tg = {0, -1, 1e9f};
        if (state == ST_PLAY) {
            float len = sqrtf(mx * mx + my * my);
            if (len > 1) { mx /= len; my /= len; len = 1; }
            cyaw -= cx * dt * 2.6f;
            cpitch += cy * dt * 1.5f;
            cpitch = std::max(0.12f, std::min(1.25f, cpitch));
            float fx = sinf(cyaw), fz = cosf(cyaw);
            float rx = -fz, rz = fx;
            float dx = fx * (-my) + rx * mx, dz = fz * (-my) + rz * mx;
            float sp = swingT > 0 ? 1.5f : 6.5f;
            px += dx * sp * dt;
            pz += dz * sp * dt;
            if (len > 0.1f) {
                float ty = atan2f(dx, dz);
                yaw += wrapAngle(ty - yaw) * std::min(1.0f, 12 * dt);
                walkPh += dt * 10;
            }
            if (bP && py <= 0.001f) vy = 7.5f;
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

            // Ziel suchen
            for (size_t i = 0; i < nodes.size(); i++) {
                if (nodes[i].respawn > 0) continue;
                float d = hypotf(nodes[i].x - px, nodes[i].z - pz);
                if (d < 3.2f + nodes[i].sz * 0.5f && d < tg.d) tg = {1, (int)i, d};
            }
            float d;
            d = hypotf(FURN_X - px, FURN_Z - pz); if (d < 3.6f && d < tg.d) tg = {2, 0, d};
            d = hypotf(ANV_X - px, ANV_Z - pz);   if (d < 3.0f && d < tg.d) tg = {3, 0, d};
            d = hypotf(SHOP_X - px, SHOP_Z - pz); if (d < 3.8f && d < tg.d) tg = {4, 0, d};

            if (aP && tg.kind) {
                if (tg.kind == 1) {
                    if (swingT <= 0) {
                        swingT = 0.45f; hitDone = false; swingNode = tg.idx;
                        yaw = atan2f(nodes[tg.idx].x - px, nodes[tg.idx].z - pz);
                    }
                } else if (tg.kind == 2) {
                    if (smeltT > 0) setMsg("OFEN LAEUFT SCHON");
                    else {
                        int total = ore[0] + ore[1] + ore[2] + ore[3];
                        if (!total) setMsg("KEINE ERZE ZUM SCHMELZEN");
                        else {
                            for (int i = 0; i < 4; i++) { smeltOre[i] = ore[i]; ore[i] = 0; }
                            smeltT = std::min(4.0f, 0.8f + 0.25f * total);
                            setMsg("SCHMELZE %d ERZE...", total);
                        }
                    }
                } else if (tg.kind == 3) {
                    state = ST_ANVIL;
                } else if (tg.kind == 4) {
                    state = ST_SHOP; si = 0;
                }
            }
            if (swingT > 0) {
                swingT -= dt;
                float p = 1 - swingT / 0.45f;
                if (!hitDone && p >= 0.7f) {
                    hitDone = true;
                    if (swingNode >= 0) {
                        Node& n = nodes[swingNode];
                        if (n.respawn <= 0 && hypotf(n.x - px, n.z - pz) < 4.5f) {
                            n.hp -= PICKDMG[pickLvl];
                            n.shake = 0.3f;
                            if (n.hp <= 0) {
                                int c = 1 + (rnd() < 0.5f ? 1 : 0) + (pickLvl >= 3 ? 1 : 0);
                                ore[n.type] += c;
                                n.respawn = 30;
                                setMsg("+%d %s", c, OREN[n.type]);
                                saveGame();
                            }
                        }
                    }
                }
            }
        } else if (state == ST_ANVIL) {
            if (nU) ai = (ai + 7) % 8;
            if (nD) ai = (ai + 1) % 8;
            if (bP) { state = ST_PLAY; saveGame(); }
            else if (aP) {
                const Recipe& r = REC[ai];
                if (bar[r.ore] < r.need) setMsg("ZU WENIG %s-BARREN", OREN[r.ore]);
                else { bar[r.ore] -= r.need; forgeRec = ai; forgeT = 0; forgeRes = 0; state = ST_FORGE; }
            }
        } else if (state == ST_FORGE) {
            if (forgeRes > 0) {
                forgeRes -= dt;
                if (forgeRes <= 0) state = ST_ANVIL;
            } else {
                forgeT += dt;
                float sp = 1.0f + 0.3f * REC[forgeRec].ore;
                float m = fabsf(fmodf(forgeT * sp, 2.0f) - 1.0f);
                if (aP) {
                    float q = 1 - fabsf(m - 0.5f) * 2;
                    if (q > 0.9f) { forgeMult = 1.6f; strcpy(forgeLabel, "PERFEKT!"); }
                    else if (q > 0.65f) { forgeMult = 1.2f; strcpy(forgeLabel, "GUT!"); }
                    else if (q > 0.35f) { forgeMult = 0.9f; strcpy(forgeLabel, "OK"); }
                    else { forgeMult = 0.6f; strcpy(forgeLabel, "SCHLECHT"); }
                    items.push_back({forgeRec, forgeMult});
                    forgeRes = 1.3f;
                    saveGame();
                }
            }
        } else if (state == ST_SHOP) {
            if (nU) si = (si + 2) % 3;
            if (nD) si = (si + 1) % 3;
            if (bP) { state = ST_PLAY; saveGame(); }
            else if (aP) {
                int oreVal = 0, itemV = 0;
                for (int i = 0; i < 4; i++) oreVal += ore[i] * ORE_PRICE[i] + bar[i] * BAR_PRICE[i];
                for (auto& it : items) itemV += itemVal(it);
                if (si == 0) {
                    if (oreVal) { coins += oreVal; for (int i = 0; i < 4; i++) { ore[i] = 0; bar[i] = 0; } setMsg("+%d MUENZEN", oreVal); }
                    else setMsg("NICHTS ZU VERKAUFEN");
                } else if (si == 1) {
                    if (itemV) { coins += itemV; items.clear(); setMsg("+%d MUENZEN", itemV); }
                    else setMsg("KEINE WAFFEN ZUM VERKAUFEN");
                } else {
                    if (pickLvl >= 4) setMsg("SPITZHACKE IST MAXIMAL");
                    else if (coins >= PICKCOST[pickLvl + 1]) { coins -= PICKCOST[pickLvl + 1]; pickLvl++; setMsg("NEUE SPITZHACKE: %s!", PICKN[pickLvl]); }
                    else setMsg("ZU WENIG MUENZEN");
                }
                saveGame();
            }
        }

        // ---- Zeichnen: 3D ----
        glViewport(0, 0, SW, SH);
        glClearColor(SKY[0], SKY[1], SKY[2], 1);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glEnable(GL_DEPTH_TEST);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        V3 tgt = {px, py + 1.4f, pz};
        float dist = 8.0f;
        V3 eye = {tgt.x - sinf(cyaw) * dist * cosf(cpitch), tgt.y + dist * sinf(cpitch), tgt.z - cosf(cyaw) * dist * cosf(cpitch)};
        M vp = mul(persp(60.0f * PI / 180.0f, (float)SW / SH, 0.5f, 200.0f), lookAt(eye, tgt, {0, 1, 0}));
        glUniformMatrix4fv(uVP, 1, GL_FALSE, vp.m);
        glUniform3f(uFog, SKY[0], SKY[1], SKY[2]);
        glUniform1f(uLit, 1);
        glUniform1f(uFogOn, 1);

        // Boden
        {
            const float SAND[3] = {0.85f, 0.8f, 0.55f};
            const float Z5[3] = {0.45f, 0.4f, 0.52f}, Z4[3] = {0.55f, 0.5f, 0.36f}, Z3[3] = {0.48f, 0.48f, 0.5f};
            const float Z2[3] = {0.5f, 0.4f, 0.3f}, VIL[3] = {0.4f, 0.68f, 0.32f};
            drawDisc(-0.1f, 73, SAND);
            drawDisc(0.0f, 70, GRASS);
            drawDisc(0.04f, 64, Z5);
            drawDisc(0.06f, 50, Z4);
            drawDisc(0.08f, 38, Z3);
            drawDisc(0.10f, 26, Z2);
            drawDisc(0.12f, 13, VIL);
            useMesh(cubeVbo);
            const float WATER[3] = {0.2f, 0.45f, 0.75f};
            drawBox(mul(T(0, -0.5f, 0), S(400, 0.4f, 400)), WATER);
        }
        auto vis = [&](float x, float z) { float dx = x - px, dz = z - pz; return dx * dx + dz * dz < 78.0f * 78.0f; };

        // Dorf
        {
            const float STONE[3] = {0.45f, 0.45f, 0.48f}, DARK[3] = {0.3f, 0.3f, 0.33f};
            M f = T(FURN_X, 0, FURN_Z);
            box(f, 0, 0.8f, 0, 2.2f, 1.6f, 2.2f, STONE);
            box(f, 0, 2.0f, 0, 1.4f, 0.8f, 1.4f, DARK);
            box(f, 0.5f, 2.9f, 0.3f, 0.6f, 1.2f, 0.6f, DARK);
            float gl = smeltT > 0 ? 0.55f + 0.3f * sinf(timeAcc * 12) : 0.0f;
            const float GLOW[3] = {1.0f, 0.45f + gl * 0.5f, 0.1f}, EMBER[3] = {0.45f, 0.18f, 0.1f};
            box(f, 0, 0.7f, 1.12f, 1.0f, 0.7f, 0.1f, smeltT > 0 ? GLOW : EMBER);

            M a = T(ANV_X, 0, ANV_Z);
            const float AN[3] = {0.22f, 0.22f, 0.25f}, AT[3] = {0.35f, 0.35f, 0.4f};
            box(a, 0, 0.35f, 0, 0.9f, 0.7f, 0.9f, AN);
            box(a, 0, 0.85f, 0, 0.6f, 0.3f, 0.6f, AN);
            box(a, 0, 1.2f, 0, 1.8f, 0.35f, 0.7f, AT);
            box(a, 1.1f, 1.2f, 0, 0.5f, 0.2f, 0.5f, AT);
            box(a, 1.9f, 0.25f, 0.6f, 0.8f, 0.5f, 0.8f, WOOD);

            // Haendler-Stand
            const float RED[3] = {0.8f, 0.2f, 0.2f};
            M s = T(SHOP_X, 0, SHOP_Z);
            box(s, 0, 0.5f, 0, 3.0f, 1.0f, 1.0f, WOOD);
            box(s, -1.6f, 1.3f, -1.0f, 0.15f, 2.6f, 0.15f, WOOD);
            box(s, 1.6f, 1.3f, -1.0f, 0.15f, 2.6f, 0.15f, WOOD);
            box(s, -1.6f, 1.3f, 1.0f, 0.15f, 2.6f, 0.15f, WOOD);
            box(s, 1.6f, 1.3f, 1.0f, 0.15f, 2.6f, 0.15f, WOOD);
            box(s, 0, 2.7f, 0, 3.6f, 0.2f, 2.6f, RED);
            const float NPC[3] = {0.7f, 0.55f, 0.2f};
            person(T(SHOP_X, 0, SHOP_Z - 1.3f), NPC, 0, 0, sinf(timeAcc * 2) * 0.1f, false);

            // Huette
            const float HW[3] = {0.55f, 0.4f, 0.28f}, HR[3] = {0.45f, 0.2f, 0.18f}, HD[3] = {0.25f, 0.15f, 0.08f};
            M h = T(0, 0, -15);
            box(h, 0, 1.5f, 0, 6, 3, 4, HW);
            box(h, 0, 3.3f, 0, 7, 0.6f, 5, HR);
            box(h, 0, 1.0f, 2.05f, 1.2f, 2.0f, 0.1f, HD);
        }

        // Erzadern
        for (auto& n : nodes) {
            if (!vis(n.x, n.z)) continue;
            if (n.respawn > 0) {
                box(T(n.x, 0, n.z), 0, 0.15f, 0, 0.9f, 0.3f, 0.9f, ROCK);
                continue;
            }
            float sh = sinf(timeAcc * 60) * (n.shake > 0 ? n.shake : 0) * 0.15f;
            M b = mul(T(n.x + sh, 0, n.z), RY(n.rot));
            float z = n.sz;
            const float* oc = OREC[n.type];
            box(b, 0, z * 0.5f, 0, z * 1.5f, z, z * 1.3f, ROCK);
            box(b, z * 0.3f, z * 1.0f, 0, z * 0.5f, z * 0.5f, z * 0.5f, oc);
            box(b, -z * 0.4f, z * 0.6f, z * 0.5f, z * 0.4f, z * 0.4f, z * 0.4f, oc);
            box(b, z * 0.5f, z * 0.3f, -z * 0.5f, z * 0.4f, z * 0.4f, z * 0.4f, oc);
        }
        // Baeume
        {
            const float TR[3] = {0.4f, 0.28f, 0.15f}, LV[3] = {0.18f, 0.5f, 0.2f};
            for (auto& t : trees) {
                if (!vis(t.x, t.z)) continue;
                M b = mul(T(t.x, 0, t.z), S(t.sz, t.sz, t.sz));
                box(b, 0, 1.0f, 0, 0.5f, 2.0f, 0.5f, TR);
                box(b, 0, 2.3f, 0, 2.4f, 0.9f, 2.4f, LV);
                box(b, 0, 3.1f, 0, 1.7f, 0.9f, 1.7f, LV);
                box(b, 0, 3.8f, 0, 1.0f, 0.8f, 1.0f, LV);
            }
        }
        // Markierung am Ziel
        if (tg.kind) {
            float tx = 0, ty = 0, tz = 0;
            if (tg.kind == 1) { tx = nodes[tg.idx].x; tz = nodes[tg.idx].z; ty = nodes[tg.idx].sz + 1.2f; }
            else if (tg.kind == 2) { tx = FURN_X; tz = FURN_Z; ty = 4.2f; }
            else if (tg.kind == 3) { tx = ANV_X; tz = ANV_Z; ty = 2.4f; }
            else { tx = SHOP_X; tz = SHOP_Z; ty = 4.2f; }
            const float MK[3] = {1.0f, 0.9f, 0.2f};
            box(mul(T(tx, ty + sinf(timeAcc * 5) * 0.15f, tz), RY(timeAcc * 3)), 0, 0, 0, 0.35f, 0.35f, 0.35f, MK);
        }
        // Spieler
        {
            const float SHADOW[3] = {0, 0, 0};
            box(T(px, 0.16f, pz), 0, 0, 0, 1.0f, 0.02f, 0.8f, SHADOW, 0.35f);
            int at = armorTier();
            const float BLUE[3] = {0.25f, 0.45f, 0.8f};
            const float* shirt = at >= 0 ? OREC[at] : BLUE;
            float amp = std::min(1.0f, sqrtf(mx * mx + my * my));
            float armR;
            if (swingT > 0) {
                float p = 1 - swingT / 0.45f;
                armR = p < 0.45f ? (-0.4f + (-2.5f + 0.4f) * (p / 0.45f))
                                 : (-2.5f + (-0.2f + 2.5f) * ((p - 0.45f) / 0.55f));
            } else {
                armR = sinf(walkPh) * 0.7f * amp * 0.7f - 0.3f;
            }
            person(mul(T(px, py, pz), RY(yaw)), shirt, walkPh, amp, armR, true);
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

        char line[96];
        if (state == ST_PLAY) {
            drawRect(6, 6, 330, 118, 0, 0, 0, 0.45f);
            snprintf(line, sizeof line, "MUENZEN: %d", coins);
            drawText(line, 12, 12, 2, 1, 0.9f, 0.3f);
            snprintf(line, sizeof line, "SPITZHACKE: %s", PICKN[pickLvl]);
            drawText(line, 12, 28, 2, 0.9f, 0.9f, 0.9f);
            for (int i = 0; i < 4; i++) {
                snprintf(line, sizeof line, "%-8s ERZ %d  BARREN %d", OREN[i], ore[i], bar[i]);
                drawText(line, 12, 48 + i * 18, 2, OREC[i][0], OREC[i][1], OREC[i][2]);
            }
            if (smeltT > 0) {
                snprintf(line, sizeof line, "OFEN: SCHMELZE... %d", (int)smeltT + 1);
                drawText(line, 12, 124, 2, 1, 0.6f, 0.2f);
            }
            if (tg.kind) {
                if (tg.kind == 1) {
                    Node& n = nodes[tg.idx];
                    float fr = n.hp / ORE_HP[n.type];
                    drawRect(SW / 2 - 100, SH - 70, 200, 10, 0, 0, 0, 0.6f);
                    drawRect(SW / 2 - 100, SH - 70, 200 * std::max(0.0f, fr), 10, OREC[n.type][0], OREC[n.type][1], OREC[n.type][2], 1);
                    snprintf(line, sizeof line, "A: ABBAUEN (%s)", OREN[n.type]);
                } else if (tg.kind == 2) snprintf(line, sizeof line, "A: OFEN BENUTZEN");
                else if (tg.kind == 3) snprintf(line, sizeof line, "A: SCHMIEDEN");
                else snprintf(line, sizeof line, "A: HAENDLER");
                drawTextC(line, SH - 52, 3, 1, 1, 1);
            }
        } else if (state == ST_ANVIL) {
            drawRect(30, 40, SW - 60, 420, 0, 0, 0, 0.8f);
            drawText("AMBOSS", 50, 50, 4, 1, 0.8f, 0.3f);
            for (int i = 0; i < 8; i++) {
                float y = 106 + i * 36;
                const Recipe& r = REC[i];
                bool have = bar[r.ore] >= r.need;
                if (i == ai) drawRect(40, y - 6, SW - 80, 32, 0.35f, 0.3f, 0.1f, 0.9f);
                snprintf(line, sizeof line, "%s  %d %s (%d)", r.name, r.need, OREN[r.ore], bar[r.ore]);
                if (have) drawText(line, 52, y, 3, 0.4f, 1, 0.4f);
                else drawText(line, 52, y, 3, 0.6f, 0.6f, 0.6f);
            }
            drawText("A: SCHMIEDEN   B: ZURUECK", 50, 436, 2, 0.8f, 0.8f, 0.8f);
        } else if (state == ST_FORGE) {
            drawRect(30, 110, SW - 60, 250, 0, 0, 0, 0.85f);
            snprintf(line, sizeof line, "SCHMIEDE: %s", REC[forgeRec].name);
            drawTextC(line, 124, 3, 1, 0.8f, 0.3f);
            float bx = 120, bw = SW - 240, by = 200, bh = 50;
            drawRect(bx, by, bw, bh, 0.55f, 0.15f, 0.15f, 1);
            drawRect(bx + bw * 0.175f, by, bw * 0.65f, bh, 0.8f, 0.7f, 0.15f, 1);
            drawRect(bx + bw * 0.325f, by, bw * 0.35f, bh, 0.3f, 0.75f, 0.3f, 1);
            drawRect(bx + bw * 0.45f, by, bw * 0.1f, bh, 0.9f, 1.0f, 0.9f, 1);
            if (forgeRes > 0) {
                drawTextC(forgeLabel, 280, 4, 1, 1, 0.4f);
            } else {
                float sp = 1.0f + 0.3f * REC[forgeRec].ore;
                float m = fabsf(fmodf(forgeT * sp, 2.0f) - 1.0f);
                drawRect(bx + bw * m - 4, by - 10, 8, bh + 20, 1, 1, 1, 1);
                drawTextC("A: HAMMERN - TRIFF DIE MITTE!", 290, 2, 0.9f, 0.9f, 0.9f);
            }
        } else if (state == ST_SHOP) {
            int oreVal = 0, itemV = 0;
            for (int i = 0; i < 4; i++) oreVal += ore[i] * ORE_PRICE[i] + bar[i] * BAR_PRICE[i];
            for (auto& it : items) itemV += itemVal(it);
            drawRect(30, 60, SW - 60, 360, 0, 0, 0, 0.8f);
            drawText("HAENDLER", 50, 70, 4, 1, 0.8f, 0.3f);
            snprintf(line, sizeof line, "MUENZEN: %d", coins);
            drawText(line, 50, 110, 3, 1, 0.9f, 0.3f);
            const char* names[3];
            char l0[64], l1[64], l2[64];
            snprintf(l0, sizeof l0, "ERZE+BARREN VERKAUFEN (+%d)", oreVal);
            snprintf(l1, sizeof l1, "WAFFEN VERKAUFEN (+%d) [%d]", itemV, (int)items.size());
            if (pickLvl >= 4) snprintf(l2, sizeof l2, "SPITZHACKE: MAXIMAL");
            else snprintf(l2, sizeof l2, "SPITZHACKE %s: %d", PICKN[pickLvl + 1], PICKCOST[pickLvl + 1]);
            names[0] = l0; names[1] = l1; names[2] = l2;
            for (int i = 0; i < 3; i++) {
                float y = 170 + i * 50;
                if (i == si) drawRect(40, y - 8, SW - 80, 36, 0.35f, 0.3f, 0.1f, 0.9f);
                drawText(names[i], 52, y, 3, 1, 1, 1);
            }
            drawText("A: WAEHLEN   B: ZURUECK", 50, 390, 2, 0.8f, 0.8f, 0.8f);
        }
        if (msgT > 0) drawTextC(msgBuf, 20, 3, 1, 1, 0.6f);
        if (lastIn[0]) drawText(lastIn, (float)(SW - textW(lastIn, 2) - 6), (float)(SH - 18), 2, 0.7f, 0.7f, 0.7f);

        SDL_GL_SwapWindow(win);
    }

    saveGame();
    if (joy) SDL_JoystickClose(joy);
    SDL_GL_DeleteContext(ctx);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
