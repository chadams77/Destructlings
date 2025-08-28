#include <iostream>
#include <thread>
#include <chrono>
#include <future>
#include <string>

using namespace std;

#include <emscripten/emscripten.h>

#include "vec.h"

#ifdef __cplusplus
#define EXTERN extern "C" EMSCRIPTEN_KEEPALIVE
#else
#define EXTERN EMSCRIPTEN_KEEPALIVE
#endif

#define PI 3.141592
#define MAX_PRT 8192
#define LEVEL_SIZE 256
#define GRAVITY 15.
#define MAX_RIVER 16
#define MAX_DLING 64
#define MAX_ITEM 12

uint32_t gfxBfr[64*64];
uint8_t ground[LEVEL_SIZE * LEVEL_SIZE];
uint8_t items[MAX_ITEM];
uint8_t itemCount[4];

#define ITEM_BOMB   1
#define ITEM_ROCKET 2
#define ITEM_DRILL  3

vec2 camera, cameraTarget;

int mouseX=-10, mouseY=-10, mouseDownLeft=0, mouseDownRight=0, mousePressedLeft=0, mousePressedRight=0;
string lastKey;
int ssWidth, ssHeight;
unsigned char * ssBfr = NULL;
int emode = 0;
int itemSel = -1;
double gtime, introT;
double wlT = 0.;

vec2 startPos = vec2(16., 32.);
vec2 endPos = vec2(48., 32.);

const char * glyphOrder = "0123456789-:abcdefghijklmnopqrstuvwxyz.,'";
int glyphX[256] = {0};

bool inEditor = false, isTesting = false;
bool exitLevel = false;
double exitT = 0.;
bool inTitle = false, inIntro = false;
bool goingToLevel = false;
int levelToPlay = 0;
bool wasInIntro = false;

EXTERN bool editorMode() {
    return inEditor || isTesting;
}

class Dling {
public:
    Dling() {
        alive = false;
    }

    void init(vec2 pos) {
        p = pos;
        v = vec2(0., 0.);
        life = 1.;
        next_jump_t = 0.;
        jump_t = 0.;
        alive = true;
        action_t = 0.;
        ascend = bomb = rocket = drill = going = false;
    }

    bool alive, ascend, bomb, rocket, drill, going;
    vec2 p, v;
    double life, jump_t, next_jump_t, action_t;
};

class River {
public:
    River() {
        active = false;
    }

    void init(vec2 pos, int _type) {
        p = pos;
        type = _type;
        active = true;
    }

    bool active;
    vec2 p;
    int type;
};

class Particle {
public:
    Particle(int t = 1) {
        if (t > 0) {
            setType(t);
            active = true;
        }
        else {
            type = 0;
            active = false;
        }
        next = -1;
    }

    bool active;
    vec2 p, v, f;
    float mass;
    int type; // 1-water, 2-lava
    uint32_t clr, deadClr, sClr;
    int next;
    float life;
    bool dying;

    void reset() {
        p = v = f = vec2(0., 0.);
        mass = 1.;
        clr = 0;
        next = -1;
        active = false;
        dying = false;
        life = 1.;
    }

    void setType(int t) {
        type = t;
        if (t == 1) {
            clr = 0xBBF06030;
            mass = 1.;
            deadClr = 0x00FFFFFF;
            sClr = 0xFFFFDDBB;
        }
        else if (t == 2) {
            clr = 0xF81090F0;
            mass = 10.;
            deadClr = 0xFF505560;
            sClr = 0xFF608888;
        }
        else if (t == 3) {
            clr = 0xFF20B0FF;
            mass = 0.1;
            deadClr = 0x0050FFFF;
            sClr = 0xFFFFFFFF;
            dying = true;
            life = 1.0;
        }
    }
};

Particle prt[MAX_PRT];
int phash[LEVEL_SIZE*LEVEL_SIZE];
int prtLastIdx = 0;

Dling dls[MAX_DLING];
River rivers[MAX_RIVER];
int dlingSel = -1;

int dlToSpawn = 20, dlSafe = 0, dlDead = 0;
double dlSpawnTimer = 1.5;

EM_JS(int, getGamePos, (), {
    return _getGamePos();
});

EM_JS(void, setLevelRes, (int level, bool win), {
    _setLevelRes(level, win);
});

EM_JS(void, playSound, (int a), {
    _playSound(a);
});

void giveItem(uint8_t itype) {
    for (int i=0; i<MAX_ITEM; i++) {
        if (!items[i]) {
            items[i] = itype;
            return;
        }
    }
}

void removeItem(uint8_t type) {
    for (int i=MAX_ITEM-1; i>=0; i--) {
        if (items[i] == type) {
            items[i] = 0;
            return;
        }
    }
}

void addRiver(vec2 p, int t) {
    for (int i=0; i<MAX_RIVER; i++) {
        if (!rivers[i].active) {
            rivers[i].init(p, t);
            return;
        }
    }
}

bool deleteRiver(vec2 p) {
    for (int i=0; i<MAX_RIVER; i++) {
        if (rivers[i].active && (rivers[i].p - p).length() <= 6.) {
            rivers[i].active = false;
            return true;
        }
    }
    return false;
}

void updatePHash() {
    for (int i=0; i<LEVEL_SIZE*LEVEL_SIZE; i++) {
        phash[i] = -1;
    }
    for (int i=0; i<MAX_PRT; i++) {
        if (!prt[i].active) {
            continue;
        }
        int x = (int)floor(prt[i].p.x), y = (int)floor(prt[i].p.y);
        if (x>=0 && y>=0 && x<LEVEL_SIZE && y<LEVEL_SIZE) {
            int off = x+y*LEVEL_SIZE; 
            prt[i].next = phash[off];
            phash[off] = i;
        }
        else {
            prt[i].active = false;
        }
    }
}

void updateParticles(float dt) {

    double ft = dt / (1. / 60.);

    for (int i=0; i<MAX_PRT; i++) {
        if (!prt[i].active) {
            continue;
        }
        prt[i].v += prt[i].f * dt;
        if (prt[i].v.length() > 10.) {
            prt[i].v = prt[i].v / prt[i].v.length() * 10.;
        }
        prt[i].f = vec2(0., 0.);
        prt[i].v -= prt[i].v * dt * 0.5;
        prt[i].v.y += GRAVITY * dt;
        if (prt[i].type == 1) {
            prt[i].v.y -= GRAVITY * dt * 10. * (1. - prt[i].life);
        }
        prt[i].p += prt[i].v * dt;
        if (prt[i].life < 0.5 || prt[i].dying) {
            prt[i].dying = true;
            prt[i].life -= dt / 3.;
            if (prt[i].type == 3) {
                prt[i].life -= dt * 2. / 3.;
            }
        }
        if (!(prt[i].p.x >= 0. && prt[i].p.y >= 0. && prt[i].p.x < (double)LEVEL_SIZE && prt[i].p.y < (double)LEVEL_SIZE)) {
            prt[i].active = false;
            continue;
        }
        if (prt[i].life <= 0.) {
            prt[i].active = false;
            if (prt[i].type == 2) {
                int hx = (int)floor(prt[i].p.x), hy = (int)floor(prt[i].p.y);   
                if (hx >= 1 && hy >= 1 && hx < (LEVEL_SIZE-1) && hy < (LEVEL_SIZE-1)) {
                    if (ground[hx-1+hy*LEVEL_SIZE] || ground[hx+1+hy*LEVEL_SIZE] || ground[hx+(hy-1)*LEVEL_SIZE] || ground[hx+(hy+1)*LEVEL_SIZE]) {
                        ground[hx+hy*LEVEL_SIZE] = 2;
                    }
                    else {
                        prt[i].active = true;
                        prt[i].life = 0.01;
                    }
                }
                else {
                    prt[i].active = true;
                    prt[i].life = 0.01;
                }
            }
            continue;
        }
    }

    updatePHash();

    for (int i=0; i<MAX_PRT; i++) {
        if (!prt[i].active) {
            continue;
        }
        int hx = (int)floor(prt[i].p.x), hy = (int)floor(prt[i].p.y);
        for (int ox=-1; ox<=1; ox++) {
            for (int oy=-1; oy<=1; oy++) {
                int x = hx + ox, y = hy + oy;
                if (x >= 0 && y >= 0 && x < LEVEL_SIZE && y < LEVEL_SIZE) {
                    int idx = phash[x+y*LEVEL_SIZE];
                    while (idx >= 0) {
                        if (idx != i && prt[idx].active) {
                            double dist = prt[i].p.distance(prt[idx].p);
                            if (dist < 1.) {
                                if (prt[i].type == 1 && prt[idx].type == 2 && prt[idx].p.y > prt[i].p.y) {
                                    prt[i].dying = true;
                                }
                                else if (prt[i].type == 2 && prt[idx].type == 1 && prt[idx].p.y > prt[i].p.y) {
                                    prt[i].life -= dt;
                                }
                                else if (prt[i].type == 2 && prt[idx].type == 2 && prt[idx].dying) {
                                    prt[i].life -= dt * 2.;
                                }
                                vec2 n = (prt[i].p - prt[idx].p) / dist;
                                double h = 1. - dist;
                                //h = h * h;
                                double f1 = prt[idx].mass / (prt[i].mass + prt[idx].mass);
                                double f2 = prt[i].mass / (prt[i].mass + prt[idx].mass);
                                prt[i].f += n * h * ft * 2000. * f1;
                                prt[idx].f -= n * h * ft * 2000. * f2;
                            }
                        }
                        idx = prt[idx].next;
                    }
                    int g = ground[x+y*LEVEL_SIZE];
                    if (g > 0) {
                        vec2 p2 = vec2((double)x + 0.5, (double)y + 0.5);
                        double dist = prt[i].p.distance(p2);
                        if (dist < 1.1) {
                            vec2 n = (prt[i].p - p2) / dist;
                            double h = (1.1 - dist) / 1.1;
                            //h = h;
                            prt[i].f += n * h * ft * 10000.;
                        }
                    }
                }
            }
        }
    }
}

uint32_t blendPixel(uint32_t bg, uint32_t clr, float alpha) {
    uint32_t rs = bg & 0xFFu;
    uint32_t gs = (bg >> 8u) & 0xFFu;
    uint32_t bs = (bg >> 16u) & 0xFFu;
    uint32_t as = (bg >> 24u) & 0xFFu;
    uint32_t rc = clr & 0xFFu;
    uint32_t gc = (clr >> 8u) & 0xFFu;
    uint32_t bc = (clr >> 16u) & 0xFFu;
    uint32_t ac = (clr >> 24u) & 0xFFu;
    uint32_t t = min(255, max(0, (uint32_t)(alpha * 255. * (float)ac / 255.)));
    uint32_t r = (rs * (256u - t) + rc * t) >> 8u;
    uint32_t g = (gs * (256u - t) + gc * t) >> 8u;
    uint32_t b = (bs * (256u - t) + bc * t) >> 8u;
    uint32_t a = (as * (256u - t) + ac * t) >> 8u;
    return r | (g << 8u) | (b << 16u) | (a << 24u);
}

void drawSpr(int x, int y, int w, int h, int sx, int sy, float alpha = 1.) {
    uint32_t * ptr = (uint32_t*)ssBfr;
    for (int i=0; i<w; i++) {
        for (int j=0; j<h; j++) {
            uint32_t clr = ptr[i+sx+(j+sy)*ssWidth];
            int x2 = i+x, y2 = j+y;
            if (x2 >= 0 && y2 >= 0 && x2 < 64 && y2 < 64) {
                int off = x2+(y2<<6);
                gfxBfr[off] = blendPixel(gfxBfr[off], clr, alpha);
            }
        }
    }
}

void drawBox(int x, int y, int w, int h, uint32_t clr, float alpha = 1., bool inv = false) {
    for (int xx=0; xx<64; xx++) {
        for (int yy=0; yy<64; yy++) {
            bool flag = xx >= x && xx < (x+w) && yy >= y && yy <= (y+h);
            if ((!inv && flag) || (inv && !flag)) {
                int off = xx+(yy<<6);
                gfxBfr[off] = blendPixel(gfxBfr[off], clr, alpha);
            }
        }
    }
}

void drawPixel(int x2, int y2, uint32_t clr, float alpha=1.) {
    if (x2 >= 0 && y2 >= 0 && x2 < 64 && y2 < 64) {
        int off = x2+(y2<<6);
        gfxBfr[off] = blendPixel(gfxBfr[off], clr, alpha);
    }
}

void drawText(string str, int x, int y, int align=-1) {
    int width = 5*str.size();
    if (align == 0) {
        x -= width/2;
    }
    else if (align == 1) {
        x -= width;
    }
    for (int i=0; i<str.size(); i++) {
        char ch = str[i];
        if (ch >= 65 && ch < (65+26)) {
            ch = ch - 65 + 97;
        }
        int gx = glyphX[ch];
        if (gx >= 0) {
            drawSpr(x, y, 6, 7, gx, 0);
        }
        x += 5;
    }
}

int getNewPrtIndex() {
    int k=256;
    while (prt[prtLastIdx%MAX_PRT].active) {
        k--;
        if (k<0) {
            return -1;
        }
        prtLastIdx += 1;
    }
    return prtLastIdx % MAX_PRT;
}

inline double randf() {
    return (double)(rand()%100000)/100000.;
}

bool addParticle(vec2 p, int type) {
    int idx = getNewPrtIndex();
    if (idx < 0) {
        return false;
    }
    prt[idx].reset();
    prt[idx].active = true;
    prt[idx].setType(type);
    prt[idx].p = p + vec2(randf() * 3. - 1.5, randf() * 3. - 1.5);
    prtLastIdx = (idx+1) % MAX_PRT;
    return true;
}

void explosion(vec2 p, int r, bool isDrill = false) {
    int x1 = (int)floor(p.x), y1 = (int)floor(p.y);
    for (int x=-r; x<=r; x++) {
        for (int y=-r; y<=r; y++) {
            if ((x*x+y*y) <= (r*r)) {
                int x2 = x + x1, y2 = y + y1;
                if (x2 >= 0 && y2 >= 0 && x2 < LEVEL_SIZE && y2 < LEVEL_SIZE) {
                    ground[x2+y2*LEVEL_SIZE] = 0;
                }
                if (!isDrill || (rand()%4)==0) {
                    addParticle(vec2((double)x2+0.5, (double)y2+0.5), 3);
                }
            }
        }
    }
    for (int i=0; i<MAX_DLING; i++) {
        if (dls[i].alive && dls[i].p.distance(p) < (float)r) {
            if (!dls[i].going) {
                dls[i].alive = false;
                dlDead += 1;
                if (dlDead >= 11) {
                    playSound(5);
                }
            }
        }
    }
    if (isDrill) {
        playSound(3);
    }
    else {
        playSound(4);
    }
}

void updateDlings(float dt) {

    if (inEditor || introT < 5.) {
        return;
    }

    if (dlToSpawn > 0) {
        dlSpawnTimer -= dt;
        if (dlSpawnTimer <= 0.) {
            for (int i=0; i<MAX_DLING; i++) {
                if (!dls[i].alive) {
                    dls[i].init(startPos + vec2(0., -1.));
                    break;
                }
            }
            dlSpawnTimer = 1.75;
            dlToSpawn -= 1;
        }
    }

    dlingSel = -1;
    double mdist = 1e9;
    vec2 mouseWorld = vec2((double)(mouseX-32), (double)(mouseY-32)) + camera;    

    for (int i=0; i<MAX_DLING; i++) {
        if (dls[i].alive) {
            float md = (mouseWorld - dls[i].p).length();
            if (itemSel >= 0 && md < mdist && md < 5. && !dls[i].going) {
                dlingSel = i;
                mdist = md;
            }
            dls[i].v -= dls[i].v * dt * 0.5;
            dls[i].v.y += GRAVITY * dt;
            dls[i].p += dls[i].v * dt;
            int x = (int)(dls[i].p.x), y = (int)(dls[i].p.y);
            if (x >= 0 && y >= 0 && x < LEVEL_SIZE && y < LEVEL_SIZE) {
                bool grounded = y < (LEVEL_SIZE-1) && ground[x+(y+1)*LEVEL_SIZE] > 0;
                bool toRight = x < (LEVEL_SIZE-1) && ground[x+1+y*LEVEL_SIZE] > 0;
                bool toLeft = x >= 1 && ground[x-1+y*LEVEL_SIZE] > 0;
                int pidx = phash[x+y*LEVEL_SIZE];
                bool water = false, lava = false;
                vec2 waterV = vec2(0., 0.);
                while (pidx >= 0) {
                    if (prt[pidx].active) {
                        if (prt[pidx].type == 1) {
                            water = true;
                            waterV += prt[pidx].v;
                        }
                        else if (prt[pidx].type == 2) {
                            lava = true;
                        }
                    }
                    pidx = prt[pidx].next;
                }
                if (water) {
                    dls[i].v -= dls[i].v * dt * 3.;
                    dls[i].v.y -= 0.25 * GRAVITY * dt;
                    dls[i].v += (waterV -= dls[i].v) * dt * 4.;
                }
                if (lava) {
                    explosion(dls[i].p, 1, false);
                    dls[i].alive = false;
                    dlDead += 1;
                    if (dlDead >= 11) {
                       playSound(5);
                    }
                }
                if (!dls[i].going) {
                    if (dls[i].v.y > 0. && grounded) {
                        dls[i].p.y = floor(dls[i].p.y);
                        dls[i].v.y = 0;
                        dls[i].v.x -= dls[i].v.x * dt * 8.;
                    }
                    if ((dls[i].v.x > 0. && toRight) || (dls[i].v.x < 0. && toLeft)) {
                        dls[i].v.x = -dls[i].v.x * 0.5;
                        dls[i].v.y -= dls[i].v.y * dt * 8.;
                    }
                    if (grounded) {
                        dls[i].jump_t += dt;
                        if (dls[i].jump_t > 0.8) {
                            dls[i].v += vec2(1., -1.5) * 7.5;
                            dls[i].jump_t = 0.;
                        }
                    }
                    else if (water) {
                        dls[i].jump_t += dt;
                        if (dls[i].jump_t > 0.8) {
                            dls[i].v += vec2(1., -1.5) * 9.5;
                            dls[i].jump_t = 0.;
                        }
                    }
                    if (dls[i].p.y <= endPos.y && (dls[i].p - endPos).length() <= 4.) {
                        dls[i].action_t = 0.;
                        dls[i].ascend = true;
                        dls[i].going = true;
                    }
                }
                else {
                    if (dls[i].ascend) {
                        dls[i].action_t += dt;
                        dls[i].v.y -= 6. * GRAVITY * dt;
                    }
                    else if (dls[i].rocket) {
                        dls[i].action_t += dt;
                        dls[i].v.y -= 9.5 * GRAVITY * dt;
                        if (ground[x+y*LEVEL_SIZE] > 0) {
                            dls[i].action_t = 1.01;
                        }
                        addParticle(dls[i].p, 3);
                    }
                    else if (dls[i].bomb) {
                        dls[i].action_t += dt * 2.;
                        if (dls[i].v.y > 0. && grounded) {
                            dls[i].p.y = floor(dls[i].p.y);
                            dls[i].v.y = 0;
                            dls[i].v.x -= dls[i].v.x * dt * 8.;
                        }
                        if ((dls[i].v.x > 0. && toRight) || (dls[i].v.x < 0. && toLeft)) {
                            dls[i].v.x = -dls[i].v.x * 0.5;
                            dls[i].v.y -= dls[i].v.y * dt * 8.;
                        }
                    }
                    else if (dls[i].drill) {
                        dls[i].action_t += dt * 1.5;
                        dls[i].v.x -= dls[i].v.x * dt * 4.;
                        dls[i].v.y = (0.25 + dls[i].action_t * 0.75) * 60.;
                        explosion(dls[i].p, 2, true);
                    }
                    if (dls[i].action_t >= 1. && dls[i].ascend) {
                        dlSafe += 1;
                        if (dlSafe >= 11) {
                            playSound(9);
                        }
                        playSound(7);
                        dls[i].alive = false;
                    }
                    else if (dls[i].action_t >= 1.) {
                        playSound(2);
                        dlDead += 1;
                        if (dlDead >= 11) {
                            playSound(5);
                        }
                        dls[i].alive = false;
                        explosion(dls[i].p, 5);
                    }
                }
            }
            else {
                if (dls[i].ascend) {
                    dlSafe += 1;
                    if (dlSafe >= 11) {
                        playSound(9);
                    }
                    dls[i].alive = false;
                    playSound(7);
                }
                else {
                    dls[i].alive = false;
                    dlDead += 1;
                    if (dlDead >= 11) {
                        playSound(5);
                    }
                    playSound(2);
                }
            }
        }
    }

    for (int i=0; i<MAX_DLING; i++) {
        if (dls[i].alive) {
            vec2 dpos = dls[i].p - vec2(floor(camera.x)-32., floor(camera.y)-32.);
            int x = (int)(dpos.x), y = (int)(dpos.y);
            uint32_t clr = 0xFF30FFFF;
            float alpha = 1.;
            if (dls[i].ascend) {
                clr = blendPixel(clr, 0xFF30FF30, (float)(dls[i].action_t));
                alpha = 1. - dls[i].action_t;
            }
            if (dls[i].bomb) {
                clr = blendPixel(clr, 0xFF0000FF, powf(fmodf(powf(dls[i].action_t, 2.f)*20.f, 1.f), 0.25f));
            }
            if (x >= 0 && y >= 0 && x < 64 && y < 64) {
                uint32_t bg = gfxBfr[x+y*64];
                gfxBfr[x+y*64] = blendPixel(bg, clr, alpha);
            }
            if (x >= 0 && y >= 1 && x < 64 && y < 64) {
                uint32_t bg = gfxBfr[x+y*64-64];
                gfxBfr[x+y*64-64] = blendPixel(bg, clr, alpha);
            }
        }
    }

    if (dlingSel >= 0 && dls[dlingSel].alive) {
        vec2 pos = dls[dlingSel].p - vec2(floor(camera.x)-32., floor(camera.y)-32.);
        drawSpr((int)floor(pos.x) - 3, (int)floor(pos.y) - 4, 16, 16, 0, 176);
    }
}

EM_JS(void, editorSaveLevel, (int level), {
    _editorSaveLevel(level);
});

EM_JS(void, editorLoadLevel, (int level), {
    _editorLoadLevel(level);
});

EM_JS(void, loadLevel, (int level), {
    _loadLevel(level);
});

EXTERN unsigned char * allocate(int size) {
    return (unsigned char *)malloc(size);
}

EXTERN void setSpriteSheet(unsigned char * ptr, int w, int h) {
    ssWidth = w;
    ssHeight = h;
    ssBfr = ptr;
}

EXTERN void setMouse(int x, int y, int dl, int dr, int pl, int pr, char * lk) {
    mouseX = x; mouseY = y;
    mouseDownLeft = dl; mousePressedLeft = pl;
    mouseDownRight = dr; mousePressedRight = pr;
    lastKey = string(lk);
}

EXTERN int getGfxPtr() {
    return (int)((char *)gfxBfr);
}

#define SAVE_BFR_SIZE (sizeof(uint8_t)*LEVEL_SIZE*LEVEL_SIZE+sizeof(Particle)*MAX_PRT+sizeof(vec2)*2+sizeof(River)*MAX_RIVER+sizeof(uint8_t)*MAX_ITEM)

uint8_t saveBfr[SAVE_BFR_SIZE], testTmpBfr[SAVE_BFR_SIZE];

EXTERN int getSaveBfr() {
    return (int)((char *)saveBfr);
}

EXTERN int getSaveBfrSz() {
    return SAVE_BFR_SIZE;
}

void serialize () {

    size_t ptr = 0, sz = 0;
    sz = sizeof(uint8_t)*LEVEL_SIZE*LEVEL_SIZE; memcpy((void*)(saveBfr + ptr), (void*)ground, sz); ptr += sz;
    sz = sizeof(Particle)*MAX_PRT; memcpy((void*)(saveBfr + ptr), (void*)prt, sz); ptr += sz;
    sz = sizeof(vec2)*1; memcpy((void*)(saveBfr + ptr), (void*)&startPos, sz); ptr += sz;
    sz = sizeof(vec2)*1; memcpy((void*)(saveBfr + ptr), (void*)&endPos, sz); ptr += sz;
    sz = sizeof(River)*MAX_RIVER; memcpy((void*)(saveBfr + ptr), (void*)rivers, sz); ptr += sz;
    vector<uint8_t> itmp;
    sz = sizeof(uint8_t)*MAX_ITEM; memcpy((void*)(saveBfr + ptr), (void*)items, sz); ptr += sz;

}

void load() {

    size_t ptr = 0, sz = 0;
    sz = sizeof(uint8_t)*LEVEL_SIZE*LEVEL_SIZE; memcpy((void*)ground, (void*)(saveBfr + ptr), sz); ptr += sz;
    sz = sizeof(Particle)*MAX_PRT; memcpy((void*)prt, (void*)(saveBfr + ptr), sz); ptr += sz;
    sz = sizeof(vec2)*1; memcpy((void*)&startPos, (void*)(saveBfr + ptr), sz); ptr += sz;
    sz = sizeof(vec2)*1; memcpy((void*)&endPos, (void*)(saveBfr + ptr), sz); ptr += sz;
    sz = sizeof(River)*MAX_RIVER; memcpy((void*)rivers, (void*)(saveBfr + ptr), sz); ptr += sz;
    sz = sizeof(uint8_t)*MAX_ITEM; memcpy((void*)items, (void*)(saveBfr + ptr), sz); ptr += sz;
    memset(itemCount, 0, 4);
    for (int i=0; i<MAX_ITEM; i++) {
        if (items[i] > 0) {
            itemCount[items[i]] += 1;
        }
    }
    for (int i=0; i<MAX_DLING; i++) {
        dls[i].alive = false;
    }
    dlToSpawn = 20; dlSafe = 0; dlDead = 0;
    itemSel = -1;
    dlingSel = -1;

}

EXTERN void initTitle(bool intro) {
    inTitle = true;
    inIntro = intro;
    load();
}

bool goingToLS = false;

void loadLevelSelect() {
    goingToLS = true;
}

EXTERN void loadLevelEditor() {
    load();
}

void initLevel(bool testing) {
    isTesting = testing;
    inEditor = false;
    dlToSpawn = 20; dlSafe = 0; dlDead = 0;
    dlSpawnTimer = 1.5;
    emode = 0;
    gtime = 0.0;
    camera = endPos;
    cameraTarget = startPos;
    introT = 0.;
    wlT = 0.;
}

EXTERN void playLevel() {
    load();
    initLevel(false);
}

void initEditor() {
    isTesting = false;
    inEditor = true;
    dlToSpawn = 20; dlSafe = 0; dlDead = 0;
    dlSpawnTimer = 1.5;
    emode = 0;
    gtime = 0.0;
    cameraTarget = camera = vec2(32., 32.);
    introT = 0.;
}

void testStart() {
    serialize();
    memcpy(testTmpBfr, saveBfr, SAVE_BFR_SIZE);
    initLevel(true);
}

void testEnd() {
    exitLevel = true;
    exitT = 0.;
}

EXTERN void init() {

    cameraTarget = camera = vec2(32., 32.);
    emode = 0;
    gtime = 0.0;

    for (int i=0; i<256; i++) {
        glyphX[i] = -1;
    }
    for (int i=0; i<strlen(glyphOrder); i++) {
        glyphX[glyphOrder[i]] = (i*6) + 16;
    }
    for (int i=0; i<MAX_PRT; i++) {
        prt[i].reset();
    }
    for (int i=0; i<LEVEL_SIZE*LEVEL_SIZE; i++) {
        phash[i] = -1;
    }
    memset(ground, 0, LEVEL_SIZE*LEVEL_SIZE);
    for (int i=0; i<MAX_DLING; i++) {
        dls[i].alive = false;
    }
    for (int i=0; i<MAX_RIVER; i++) {
        rivers[i].active = false;
    }
    memset(items, 0, MAX_ITEM);
    memset(itemCount, 0, 4);

    dlToSpawn = 20; dlSafe = 0; dlDead = 0;
    dlSpawnTimer = 1.5;

}

EXTERN void drawFrame(float dt) {

    if (inEditor) {
        if (lastKey == "C") {
            init();
        }
        else if (lastKey == "t") {
            testStart();
        }
        if (lastKey != "") {
            char ch = lastKey[0];
            if (ch >= '0' && ch <= '9') {
                editorLoadLevel(ch == '0' ? 10 : (ch-'0'));
            }
            else if (ch == '!') {
                serialize();
                editorSaveLevel(1);
            }
            else if (ch == '@') {
                serialize();
                editorSaveLevel(2);
            }
            else if (ch == '#') {
                serialize();
                editorSaveLevel(3);
            }
            else if (ch == '$') {
                serialize();
                editorSaveLevel(4);
            }
            else if (ch == '%') {
                serialize();
                editorSaveLevel(5);
            }
            else if (ch == '^') {
                serialize();
                editorSaveLevel(6);
            }
            else if (ch == '&') {
                serialize();
                editorSaveLevel(7);
            }
            else if (ch == '*') {
                serialize();
                editorSaveLevel(8);
            }
            else if (ch == '(') {
                serialize();
                editorSaveLevel(9);
            }
            else if (ch == ')') {
                serialize();
                editorSaveLevel(10);
            }
        }
    }
    else if (isTesting) {
        if (lastKey == "t") {
            testEnd();
        }
    }
    else if (!inTitle && !inIntro && lastKey == "Escape") {
        exitLevel = true;
        exitT = 0.;
        playSound(5);
    }

    if (exitLevel) {
        exitT += dt;
        if (exitT > 1.) {
            if (isTesting) {
                memcpy(saveBfr, testTmpBfr, SAVE_BFR_SIZE);
                load();
                initEditor();
            }
            //else if (isRestarting) {}
            else {
                if (dlSafe >= 11) {
                    setLevelRes(levelToPlay, true);
                }
                wasInIntro = false;
                inTitle = true;
                inIntro = false;
                loadLevel(10);
            }
            exitLevel = false;
            exitT = 0.;
        }
    }

    gtime += dt;

    for (int i=0; i<10; i++) {
        updateParticles(dt/10.);
    }

    if (inEditor || introT > 5.) {
        camera += (cameraTarget - camera) * dt * 8.;
    }
    else if (introT > 1. && introT <= 5.) {
        double t = powf(sin((introT - 1.) / 4. * PI * 0.5), 2.);
        cameraTarget = (startPos - endPos) * t + endPos;
        camera = cameraTarget;
    }
    vec2 mouseWorld = vec2((double)(mouseX-32), (double)(mouseY-32)) + camera;    

    bool hoverPLeft = false, hoverPRight = false, hoverPUp = false, hoverPDown = false;

    if (inEditor) {
        if (lastKey == "s") {
            startPos = mouseWorld;
        }
        else if (lastKey == "e") {
            endPos = mouseWorld;
        }
        else if (lastKey == "Delete") {
            deleteRiver(mouseWorld);
        }
        else if (lastKey == "r") {
            addRiver(mouseWorld, 1);
        }
        else if (lastKey == "l") {
            addRiver(mouseWorld, 2);
        }
    }

    if ((inEditor || introT >= 5.) && !inTitle) {
        if (mouseX >= (64-5)) {
            cameraTarget.x += 32. * dt;
            hoverPRight = true;
        }
        if (mouseY >= (64-5)) {
            cameraTarget.y += 32. * dt;
            hoverPDown = true;
        }
        if (mouseX <= (4) && mouseY > 12) {
            cameraTarget.x -= 32. * dt;
            hoverPLeft = true;
        }
        if (mouseY <= (4) && mouseX > 16) {
            cameraTarget.y -= 32. * dt;
            hoverPUp = true;
        }
    }

    bool showPLeft = true, showPRight = true, showPUp = true, showPDown = true;

    if (inTitle) {
        camera = cameraTarget = vec2(32., 32.);
    }

    if (cameraTarget.x <= 32.) {
        cameraTarget.x = 32.;
        showPLeft = false;
    }
    else if (cameraTarget.x >= (double)(LEVEL_SIZE - 32)) {
        cameraTarget.x = (double)(LEVEL_SIZE - 32);
        showPRight = false;
    }

    if (cameraTarget.y <= 32.) {
        cameraTarget.y = 32.;
        showPUp = false;
    }
    else if (cameraTarget.y > (double)(LEVEL_SIZE - 32)) {
        cameraTarget.y = (double)(LEVEL_SIZE - 32);
        showPDown = false;
    }

    bool doPaint = false;
    uint8_t paintColor = 0;

    if (emode == 0 && mouseY > 6 && inEditor) {
        if (mouseDownLeft) {
            paintColor = 1; doPaint = true;
        }

        if (mouseDownRight) {
            paintColor = 0; doPaint = true;
        }
    }

    if (doPaint && inEditor) {
        int mx = (int)mouseWorld.x, my = (int)mouseWorld.y;
        for (int x=-1; x<=1; x++) {
            for (int y=-1; y<=1; y++) {
                if (abs(x)+abs(y) < 2) {
                    int x2 = x + mx, y2 = y + my;
                    if (x2 >= 0 && y2 >= 0 && x2 < LEVEL_SIZE && y2 < LEVEL_SIZE) {
                        ground[x2+y2*LEVEL_SIZE] = paintColor;
                    }
                }
            }
        }
    }

    if (emode == 1 && mouseY > 6 && inEditor) {
        if (mouseDownLeft) {
            addParticle(mouseWorld, 1);
        }
    }

    if (emode == 2 && mouseY > 6 && inEditor) {
        if (mouseDownLeft) {
            addParticle(mouseWorld, 2);
        }
    }

    memset((char*)gfxBfr, 0, 4*64*64);

    drawSpr(0, 0, 64, 64, 0, 80);

    if (!inTitle) {
        vec2 spos = startPos - vec2(floor(camera.x)-32., floor(camera.y)-32.);
        drawSpr((int)floor(spos.x) - 8, (int)floor(spos.y) - 16, 16, 16, 16, 16);

        vec2 epos = endPos - vec2(floor(camera.x)-32., floor(camera.y)-32.);
        drawSpr((int)floor(epos.x) - 8, (int)floor(epos.y) - 16, 16, 16, 32, 16);
    }

    for (int i=0; i<MAX_RIVER; i++) {
        if (rivers[i].active) {
            vec2 rpos = rivers[i].p - vec2(floor(camera.x)-32., floor(camera.y)-32.);
            drawSpr((int)floor(rpos.x) - 8, (int)floor(rpos.y) - 8, 16, 16, 32 + rivers[i].type * 16, 16);
            int gx = (int)floor(rivers[i].p.x), gy = (int)floor(rivers[i].p.y);
            if (gx >= 0 && gy >= 0 && gx < LEVEL_SIZE && gy < LEVEL_SIZE && ground[gx+gy*LEVEL_SIZE] == 0) {
                addParticle(rivers[i].p, rivers[i].type);
            }
        }
    }

    if (!inTitle) {
        updateDlings(dt);
    }

    for (int x=0; x<64; x++) {
        for (int y=0; y<64; y++) {
            vec2 wpos = vec2(floor((double)(x-32)), floor((double)(y-32))) + vec2(floor(camera.x), floor(camera.y));
            int wx = (int)floor(wpos.x), wy = (int)floor(wpos.y);
            uint32_t bg = gfxBfr[x+y*64];
            if (wx >= 0 && wy >= 0 && wx < LEVEL_SIZE && wy < LEVEL_SIZE) {
                uint8_t above = 0, below = 0;
                if (wy >= 1) {
                    above = ground[wx+(wy-1)*LEVEL_SIZE];
                }
                if (wy < (LEVEL_SIZE-1)) {
                    below = ground[wx+(wy+1)*LEVEL_SIZE];
                }
                bool wAbove = wy > 0 ? phash[wx+(wy-1)*LEVEL_SIZE] >= 0 : false;
                bool wBelow = wy < (LEVEL_SIZE-1) ? phash[wx+(wy+1)*LEVEL_SIZE] >= 0 : false;
                bool wLeft = wx > 0 ? phash[wx-1+wy*LEVEL_SIZE] >= 0 : false;
                bool wRight = wx < (LEVEL_SIZE-1) ? phash[wx+1+wy*LEVEL_SIZE] >= 0 : false;
                vec2 p2 = wpos;
                for (int ox=-1; ox<=1; ox++) {
                    for (int oy=-1; oy<=1; oy++) {
                        int hx = wx+ox, hy = wy+oy;
                        if (hx >= 0 && hy >= 0 && hx < LEVEL_SIZE && hy < LEVEL_SIZE) {
                            int off = hx+hy*LEVEL_SIZE;
                            int idx = phash[off];
                            while (idx > -1) {
                                uint32_t cc = prt[idx].clr;
                                if (!wAbove && wBelow && (wLeft || wRight)) {
                                    cc = prt[idx].sClr;
                                }
                                bg = blendPixel(bg, blendPixel(cc, prt[idx].deadClr, 1. - prt[idx].life), 0.5 * powf(max(1. - (prt[idx].p.distance(p2)), 0.), 0.5) * powf(prt[idx].life, 0.5f));
                                idx = prt[idx].next;
                            }
                        }
                    }
                }
                if (ground[wx+wy*LEVEL_SIZE] == 1) {
                    uint32_t clr = 0xFF203025;
                    if (!above) {
                        clr = 0xFF108030;
                    }
                    if (!below) {
                        clr = blendPixel(clr, 0xFF101010, 0.5);
                    }
                    uint64_t r1 = ((((wx+63ul)*37ul)*(wy+63ul)*17ul)*331ul)%7ul;
                    if (r1 == 1) {
                        clr = blendPixel(clr, 0xFF101010, 0.1);
                    }
                    else if (r1 == 2) {
                        clr = blendPixel(clr, 0xFFE0E0E0, 0.1);
                    }
                    bg = blendPixel(bg, clr, 1.0);
                }
                else if (ground[wx+wy*LEVEL_SIZE] == 2) {
                    uint32_t clr = 0xFF393838;
                    if (!above) {
                        clr = 0xFF808080;
                    }
                    if (!below) {
                        clr = blendPixel(clr, 0xFF202020, 0.5);
                    }
                    uint64_t r1 = ((((wx+63ul)*37ul)*(wy+63ul)*17ul)*331ul)%7ul;
                    if (r1 == 1) {
                        clr = blendPixel(clr, 0xFF202020, 0.1);
                    }
                    else if (r1 == 2) {
                        clr = blendPixel(clr, 0xFFF0F0F0, 0.1);
                    }
                    bg = blendPixel(bg, clr, 1.0);
                }
                gfxBfr[x+y*64] = bg;
            }
        }
    }

    if (inTitle) {
        drawBox(0, 0, 64, 64, 0x88000000);
    }

    if (inEditor && !inTitle) {
        drawSpr(0, 0, 12, 4, 0, 0);
        drawSpr(13, 0, 12, 4, 33, 48);

        drawSpr(0 + emode*4, 0, 5, 5, 0, 5 + (sin(gtime*PI*8.) > 0. ? 0 : 5), sin(gtime*PI*2.)*0.2+0.6);
    }

    if (!inEditor && !inTitle) {
        drawSpr(0, 0, 64, 4, 0, 144);
        int x = 42, y = 2;
        for (int i=0; i<dlSafe; i++) {
            drawSpr(x, y, 1, 1, 0, 160);
            x += 1;
        }
        x = 42 + 19;
        for (int i=0; i<dlDead; i++) {
            drawSpr(x, y, 1, 1, 1, 160);
            x -= 1;
        }
    }

    int yy = 1;
    if (inEditor) {
        yy += 4;
    }
        
    int xx = 1;
    int kk = 0;
    int selI = -1;
    int sxx=0,syy=0;
    int ixx=0,iyy=0;
    for (int i=0; i<MAX_ITEM; i++) {
        if (items[i]) {
            drawSpr(xx, yy, 3, 4, 33+4*(-1 + (int)items[i]), 48);
            if (mouseX >= xx && mouseX <= (xx+3) && mouseY >= yy && mouseY <= (yy+4)) {
                selI = i;
                sxx = xx; syy = yy;
            }
            if (itemSel == i) {
                ixx = xx; iyy = yy;
            }
            xx += 4;
            kk += 1;
            if (!(kk % 4)) {
                xx = 1;
                yy += 5;
            }
        }
    }

    if (selI >= 0) {
        drawSpr(sxx-1, syy, 5, 5, 0, 5 + (sin(gtime*PI*8.) > 0. ? 0 : 5), sin(gtime*PI*2.)*0.1+0.3);
    }
    if (itemSel >= 0 && !inEditor) {
        drawSpr(ixx-1, iyy, 5, 5, 0, 5 + (sin(gtime*PI*8.) > 0. ? 0 : 5), sin(gtime*PI*2.)*0.2+0.6);
    }

    if (mousePressedLeft && selI >= 0) {
        itemSel = selI;
        playSound(8);
    }
    else if (itemSel >= 0) {
        if (mousePressedLeft && dlingSel >= 0 && dls[dlingSel].alive && !dls[dlingSel].going) {
            playSound(8);
            if (items[itemSel] == ITEM_BOMB) {
                dls[dlingSel].bomb = true;
                dls[dlingSel].action_t = 0.;
                dls[dlingSel].going = true;
            }
            else if (items[itemSel] == ITEM_ROCKET) {
                dls[dlingSel].rocket = true;
                dls[dlingSel].action_t = 0.;
                dls[dlingSel].going = true;
                playSound(6);
            }
            else if (items[itemSel] == ITEM_DRILL) {
                dls[dlingSel].drill = true;
                dls[dlingSel].action_t = 0.;
                dls[dlingSel].going = true;
                playSound(3);
            }
            items[itemSel] = 0;
            itemSel = -1;
            dlingSel = -1;
        }
    }

    if (!inTitle) {
        drawSpr(0, 32-8, 16, 16, 0, 16, showPLeft ? (hoverPLeft ? 0.75 : 0.5) : 0.2);
        drawSpr(64-16, 32-8, 16, 16, 0, 16*2, showPRight ? (hoverPRight ? 0.75 : 0.5) : 0.2);
        drawSpr(32-8, 0, 16, 16, 0, 16*3, showPUp ? (hoverPUp ? 0.75 : 0.5) : 0.2);
        drawSpr(32-8, 64-16, 16, 16, 0, 16*4, showPDown ? (hoverPDown ? 0.75 : 0.5) : 0.2);
    }

    if (inTitle && inIntro) {
        float t1 = min(introT*2., 1.);
        float t2 = max(0., min((introT-0.5)*2., 1.));
        int yb = (int)(-fabs(cos(t1*PI*1.*sqrtf(t1))) * 32. * ((1. - t1)));
        drawSpr(0, yb+8, 64, 32, 64, 80);
        int yb2 = (int)(-fabs(cos(t2*PI*1.*sqrtf(t2))) * 32. * ((1. - t2)));
        if (introT > 2. && mouseX > 10 && mouseX < 54 && mouseY > 41 && mouseY < 60 && !goingToLS) {
            drawSpr(0, 35-yb2, 64, 32, 64+64, 80+32);
            if (mousePressedLeft) {
                loadLevelSelect();
                introT = min(introT, 1.5);
                playSound(1);
            }
        }
        else {
            drawSpr(0, 35-yb2, 64, 32, 64, 80+32);
        }
    }
    else if (inTitle) {
        drawText("Select Level", 32, 3, 0);
        int maxLevel = getGamePos();
        for (int x=0; x<3; x++) {
            for (int y=0; y<3; y++) {
                int level = (x+1)+y*3;
                if (level > 7) {
                    continue;
                }
                int sx = 32 - 3*16/2 + x*16 + (level == 7 ? 16 : 0);
                int sy = 32 - 3*16/2 + y*16 + 4;
                if (mouseX >= sx && mouseY >= sy && mouseX < (sx+16) && mouseY < (sy+16) && level <= (maxLevel+1)) {
                    sy -= 1;
                    if (mousePressedLeft) {
                        levelToPlay = level;
                        goingToLevel = true;
                        introT = min(introT, 1.5);
                        playSound(1);
                    }
                }
                if ((level-1) == maxLevel) {
                    drawSpr(sx, sy, 16, 16, 224, 80);
                }
                else if (level > maxLevel) {
                    drawSpr(sx, sy, 16, 16, 224-16, 80);
                }
                else {
                    drawSpr(sx, sy, 16, 16, 224+16, 80);
                }
            }
        }
    }

    if (goingToLS) {
        introT -= dt * 2.;
        if (introT <= 0.) {
            introT = 0.;
            goingToLS = false;
            inIntro = false;
        }
    }
    else if (goingToLevel) {
        introT -= dt * 2.;
        if (introT <= 0.) {
            introT = 0.;
            goingToLevel = false;
            wasInIntro = true;
            loadLevel(levelToPlay);
            playLevel();
            inTitle = false;
            inIntro = false;
        }
    }

    drawSpr(mouseX, mouseY, 5, 6, 7, 5 + (dlingSel >= 0 ? 7 : 0));

    if (inEditor) {
        if (mouseY < 6 && mouseX < 12) {
            if (mousePressedLeft) {
                emode = mouseX / 4;
            }
        }
        else if (mouseY < 6 && mouseX >= 13 && mouseX < 24) {
            int type = 1 + (mouseX - 13) / 4;
            if (type >= 1 && type <= 3) {
                if (mousePressedLeft) {
                    giveItem((uint8_t)type);
                }
                else if (mousePressedRight) {
                    removeItem((uint8_t)type);
                }
                else {
                    drawSpr(12 + (type-1)*4, 0, 5, 5, 0, 5 + (sin(gtime*PI*8.) > 0. ? 0 : 5), sin(gtime*PI*2.)*0.1+0.3);
                }
            }
        }
    }

    if (dlDead >= 11 || dlSafe >= 11) {
        int yb = (int)(-fabs(cos(wlT*PI*2.*sqrtf(wlT))) * 32. * ((2. - wlT)/2.));
        if (dlSafe >= dlDead) {
            drawSpr(16, 16 + yb, 32, 32, 80, 48);
        }
        else {
            drawSpr(16, 16 + yb, 32, 32, 80+32, 48);
        }
        wlT += dt;
        if (wlT > 2.) {
            if (!exitLevel) {
                exitLevel = true;
                exitT = 0.;
            }
            wlT = 2.;
        }
    }

    if (!inEditor && introT < 1.) {
        int l = 31 - (int)floor(31. * introT);
        drawBox(l, l, 64-l*2, 64-l*2, 0xFF000000, 1., true);
    }
    else if (exitLevel && exitT > 0.) {
        int l = (int)floor(31. * exitT);
        drawBox(l, l, 64-l*2, 64-l*2, 0xFF000000, 1., true);
    }
    if (!inEditor) {
        introT += dt;
    }

    //drawPixel(mouseX, mouseY, 0xFFFFFFFF, 0.5);
}

/*int addThread(int a, int b) {
    this_thread::sleep_for(std::chrono::seconds(5));
    return a + b;
}

EXTERN int addTwo(int a, int b) {
    future<int> ret1 = async(&addThread, a, b);
    future<int> ret2 = async(&addThread, a+1, b+1);
    return ret1.get() + ret2.get();
}*/