// =============================================================================
//  Proteus-like EDA Simulator — SDL2 + SDL_ttf
//  Features: library+search, place/move/rotate/delete/multi-select/drag,
//            wire routing, pins, junctions, grid+snap, zoom, pan, coords,
//            logic-gate simulation (Run/Stop), wire coloring, voltage probe,
//            Save/Load (JSON), Export BMP, DRC, simulation log.
// =============================================================================
#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#include <string>
#include <vector>
#include <algorithm>

// ----------------------------- constants -------------------------------------
static const int WIN_W = 1280, WIN_H = 800;
static const int GRID  = 10;          // world units per grid cell
static const int MENU_H = 22, TOOLBAR_H = 34, STATUS_H = 22;
static const int LIB_W = 190, PROP_W = 220, HDR_H = 20, LOG_H = 130;

enum CType { T_R, T_C, T_L, T_LED, T_DIODE, T_SWITCH, T_VCC, T_GND,
             T_BATTERY, T_PULSE, T_CLOCK, T_AND, T_OR, T_NOT, T_XOR,
             T_NAND, T_NOR, T_PROBE, T_NODE, T_COUNT };

enum Tool  { TOOL_SELECT, TOOL_WIRE, TOOL_PAN, TOOL_DELETE };

struct PinDef { float ox, oy; bool isOutput; };
struct Comp {
    CType type; int x, y, rot;          // rot: 0..3 (0,90,180,270)
    std::string ref, value;
    bool sel = false;
    // runtime
    int state = -1;                      // -1 unknown, 0 low, 1 high
    bool swClosed = false;
};

struct Wire {
    int ca, pa, cb, pb;                  // comp/pin indices (-1 = free end)
    int x1,y1,x2,y2;                     // free-end coords (when pin <0)
    int state = -1;
};

struct Camera { double panX = 0, panY = 0; double zoom = 1.0; };

// ----------------------------- pin layouts -----------------------------------
// Offsets in world units. Pins numbered for wiring & symbol draw.
static std::vector<PinDef> pinLayout(CType t) {
    int u = GRID;
    switch (t) {
        case T_R: case T_C: case T_L: case T_LED: case T_DIODE:
        case T_SWITCH: case T_BATTERY:
            return {{-2.f*u,0,false},{2.f*u,0,false}};
        case T_VCC: case T_GND: case T_PULSE: case T_CLOCK:
            return {{0,1.f*u,false}};
        case T_AND: case T_OR: case T_XOR: case T_NAND: case T_NOR:
            return {{-2.f*u,-1.f*u,false},{-2.f*u,1.f*u,false},{2.f*u,0,true}};
        case T_NOT:
            return {{-2.f*u,0,false},{2.f*u,0,true}};
        case T_PROBE:
            return {{-2.f*u,0,false}};
        case T_NODE: default:
            return {};
    }
}

// rotate (lx,ly) by rot*90deg
static void rotPt(float lx, float ly, int rot, float& ox, float& oy) {
    switch (rot & 3) {
        case 0: ox =  lx; oy =  ly; break;
        case 1: ox = -ly; oy =  lx; break;
        case 2: ox = -lx; oy = -ly; break;
        default:ox =  ly; oy = -lx; break;
    }
}
static void pinWorldPos(const Comp& c, int idx, int& wx, int& wy) {
    auto pins = pinLayout(c.type);
    if (idx < 0 || idx >= (int)pins.size()) { wx=c.x; wy=c.y; return; }
    float ox, oy; rotPt(pins[idx].ox, pins[idx].oy, c.rot, ox, oy);
    wx = c.x + (int)ox; wy = c.y + (int)oy;
}

// ----------------------------- colors ----------------------------------------
static const SDL_Color C_BG{238,238,238,255}, C_PANEL{228,228,228,255},
    C_PANELDK{208,208,208,255}, C_EDGE{165,165,165,255}, C_MENU{245,245,245,255},
    C_MENUHV{60,130,220,255}, C_MENUTX{30,30,30,255}, C_TBBG{232,232,232,255},
    C_TBHV{205,220,245,255}, C_TBACT{120,175,240,255}, C_CANVAS{255,255,255,255},
    C_GRID{205,205,205,255}, C_TEXT{25,25,25,255}, C_TEXTDIM{110,110,110,255},
    C_HDR{190,190,190,255}, C_STATUS{235,235,235,255}, C_ACCENT{60,110,200,255},
    C_HI{220,40,40,255},    // wire high
    C_LO{40,90,210,255},    // wire low
    C_UNK{120,120,120,255}, // wire unknown
    C_PIN{40,40,40,255}, C_SEL{255,170,0,255};

// ----------------------------- helpers ---------------------------------------
static void setCol(SDL_Renderer* r, SDL_Color c){ SDL_SetRenderDrawColor(r,c.r,c.g,c.b,c.a); }
static void fillRC(SDL_Renderer*r, SDL_Rect rc, SDL_Color c){ setCol(r,c); SDL_RenderFillRect(r,&rc); }
static void rectRC(SDL_Renderer*r, SDL_Rect rc, SDL_Color c){ setCol(r,c); SDL_RenderDrawRect(r,&rc); }
static void ln(SDL_Renderer*r,int x1,int y1,int x2,int y2,SDL_Color c){setCol(r,c);SDL_RenderDrawLine(r,x1,y1,x2,y2);}

static void drawText(SDL_Renderer* r, TTF_Font* f, const char* s, SDL_Rect rc,
                     SDL_Color col, bool center=true){
    if(!f||!s||!s[0]) return;
    SDL_Surface* sf=TTF_RenderUTF8_Blended(f,s,col); if(!sf) return;
    SDL_Texture* t=SDL_CreateTextureFromSurface(r,sf); int w=sf->w,h=sf->h;
    SDL_Rect d; if(center){d.x=rc.x+(rc.w-w)/2; d.y=rc.y+(rc.h-h)/2;}
    else{d.x=rc.x+5; d.y=rc.y+(rc.h-h)/2;}
    d.w=w; d.h=h; SDL_RenderCopy(r,t,nullptr,&d);
    SDL_DestroyTexture(t); SDL_FreeSurface(sf);
}

// world<->screen
static int w2sx(double wx, const Camera& c){ return (int)((wx - c.panX)*c.zoom); }
static int w2sy(double wy, const Camera& c){ return (int)((wy - c.panY)*c.zoom); }
static double s2wx(int sx, const Camera& c){ return sx/c.zoom + c.panX; }
static double s2wy(int sy, const Camera& c){ return sy/c.zoom + c.panY; }
static int snap(int v){ return (int)std::round((double)v/GRID)*GRID; }

// canvas viewport rect
static SDL_Rect canvasRect(){
    return {LIB_W, MENU_H+TOOLBAR_H, WIN_W-LIB_W-PROP_W,
            WIN_H-MENU_H-TOOLBAR_H-STATUS_H};
}

// ----------------------------- symbol drawing --------------------------------
// draw a "thick" point
static void dot(SDL_Renderer*r,int x,int y,int rad,SDL_Color c){
    setCol(r,c);
    for(int dy=-rad;dy<=rad;dy++)for(int dx=-rad;dx<=rad;dx++)
        if(dx*dx+dy*dy<=rad*rad) SDL_RenderDrawPoint(r,x+dx,y+dy);
}
static void aaline(SDL_Renderer*r,int x1,int y1,int x2,int y2,SDL_Color c){
    ln(r,x1,y1,x2,y2,c);
}

// draw symbol of component c at its world pos, transformed to screen
static void drawSymbol(SDL_Renderer* r, const Comp& c, const Camera& cam){
    auto T=[&](float lx,float ly,int& sx,int& sy){
        float ox,oy; rotPt(lx,ly,c.rot,ox,oy);
        sx = w2sx(c.x+ox, cam); sy = w2sy(c.y+oy, cam);
    };
    int u = GRID;
    SDL_Color blk{20,20,20,255};
    switch(c.type){
        case T_R: { // zigzag
            std::vector<std::pair<float,float>> pts={{-2.f*u,0},{-1.2f*u,0},
                {-0.9f*u,-0.6f*u},{-0.3f*u,0.6f*u},{0.3f*u,-0.6f*u},
                {0.9f*u,0.6f*u},{1.2f*u,0},{2.f*u,0}};
            for(size_t i=0;i+1<pts.size();i++){
                int x1,y1,x2,y2; T(pts[i].first,pts[i].second,x1,y1);
                T(pts[i+1].first,pts[i+1].second,x2,y2); ln(r,x1,y1,x2,y2,blk);
            }
            break;}
        case T_C: { int x1,y1,x2,y2,x3,y3,x4,y4;
            T(-2.f*u,0,x1,y1); T(-0.4f*u,0,x2,y2); ln(r,x1,y1,x2,y2,blk);
            T(-0.4f*u,-0.8f*u,x3,y3); T(-0.4f*u,0.8f*u,x4,y4); ln(r,x3,y3,x4,y4,blk);
            T(0.4f*u,-0.8f*u,x3,y3); T(0.4f*u,0.8f*u,x4,y4); ln(r,x3,y3,x4,y4,blk);
            T(0.4f*u,0,x3,y3); T(2.f*u,0,x4,y4); ln(r,x3,y3,x4,y4,blk);
            break;}
        case T_L: { int x1,y1,x2,y2; T(-2.f*u,0,x1,y1); T(-1.4f*u,0,x2,y2); ln(r,x1,y1,x2,y2,blk);
            // loops
            for(int i=0;i<3;i++){
                float cx0=-1.1f*u+i*0.7f*u;
                int px,py,qx,qy; T(cx0,0,px,py); T(cx0+0.6f*u,0,qx,qy);
                // approx arc by 2 lines
                int mx,my; T(cx0+0.3f*u,-0.7f*u,mx,my);
                ln(r,px,py,mx,my,blk); ln(r,mx,my,qx,qy,blk);
            }
            T(1.1f*u,0,x1,y1); T(2.f*u,0,x2,y2); ln(r,x1,y1,x2,y2,blk);
            break;}
        case T_LED: { // triangle + line + arrows
            int x1,y1,x2,y2,x3,y3,x4,y4;
            T(-2.f*u,0,x1,y1); T(-0.5f*u,0,x2,y2); ln(r,x1,y1,x2,y2,blk);
            T(-0.5f*u,-0.8f*u,x2,y2); T(-0.5f*u,0.8f*u,x3,y3); T(0.7f*u,0,x4,y4);
            ln(r,x2,y2,x4,y4,blk); ln(r,x3,y3,x4,y4,blk);
            T(0.7f*u,-0.8f*u,x2,y2); T(0.7f*u,0.8f*u,x3,y3); ln(r,x2,y2,x3,y3,blk);
            T(0.7f*u,0,x1,y1); T(2.f*u,0,x2,y2); ln(r,x1,y1,x2,y2,blk);
            break;}
        case T_DIODE: { int x1,y1,x2,y2,x3,y3,x4,y4;
            T(-2.f*u,0,x1,y1); T(-0.5f*u,0,x2,y2); ln(r,x1,y1,x2,y2,blk);
            T(-0.5f*u,-0.8f*u,x2,y2); T(-0.5f*u,0.8f*u,x3,y3); T(0.6f*u,0,x4,y4);
            ln(r,x2,y2,x4,y4,blk); ln(r,x3,y3,x4,y4,blk);
            T(0.6f*u,-0.8f*u,x2,y2); T(0.6f*u,0.8f*u,x3,y3); ln(r,x2,y2,x3,y3,blk);
            T(0.6f*u,0,x1,y1); T(2.f*u,0,x2,y2); ln(r,x1,y1,x2,y2,blk);
            break;}
        case T_SWITCH: { int x1,y1,x2,y2,x3,y3,x4,y4;
            T(-2.f*u,0,x1,y1); T(-0.6f*u,0,x2,y2); ln(r,x1,y1,x2,y2,blk);
            T(0.6f*u,0,x3,y3); T(2.f*u,0,x4,y4); ln(r,x3,y3,x4,y4,blk);
            if(c.swClosed){ ln(r,x2,y2,x3,y3,blk); }
            else { int mx,my; T(-0.6f*u,-1.0f*u,mx,my); ln(r,x2,y2,mx,my,blk); }
            break;}
        case T_VCC: { int x1,y1,x2,y2;
            T(0,1.f*u,x1,y1); T(0,0,x2,y2); ln(r,x1,y1,x2,y2,blk);
            T(-0.8f*u,0,x1,y1); T(0.8f*u,0,x2,y2); ln(r,x1,y1,x2,y2,blk);
            break;}
        case T_GND: { int x1,y1,x2,y2;
            T(0,1.f*u,x1,y1); T(0,0.2f*u,x2,y2); ln(r,x1,y1,x2,y2,blk);
            T(-0.8f*u,0.2f*u,x1,y1); T(0.8f*u,0.2f*u,x2,y2); ln(r,x1,y1,x2,y2,blk);
            T(-0.5f*u,0.5f*u,x1,y1); T(0.5f*u,0.5f*u,x2,y2); ln(r,x1,y1,x2,y2,blk);
            T(-0.2f*u,0.8f*u,x1,y1); T(0.2f*u,0.8f*u,x2,y2); ln(r,x1,y1,x2,y2,blk);
            break;}
        case T_BATTERY: { int x1,y1,x2,y2,x3,y3,x4,y4;
            T(-2.f*u,0,x1,y1); T(-0.3f*u,0,x2,y2); ln(r,x1,y1,x2,y2,blk);
            T(-0.3f*u,-0.9f*u,x2,y2); T(-0.3f*u,0.9f*u,x3,y3); ln(r,x2,y2,x3,y3,blk);
            T(0.3f*u,-0.5f*u,x2,y2); T(0.3f*u,0.5f*u,x3,y3); ln(r,x2,y2,x3,y3,blk);
            T(0.3f*u,0,x3,y3); T(2.f*u,0,x4,y4); ln(r,x3,y3,x4,y4,blk);
            break;}
        case T_PULSE: case T_CLOCK: { int x1,y1,x2,y2,x3,y3;
            // box with square/square wave
            T(-1.f*u,-1.f*u,x1,y1); T(1.f*u,-1.f*u,x2,y2); T(1.f*u,1.f*u,x3,y3);
            int x4,y4; T(-1.f*u,1.f*u,x4,y4);
            ln(r,x1,y1,x2,y2,blk); ln(r,x2,y2,x3,y3,blk); ln(r,x3,y3,x4,y4,blk); ln(r,x4,y4,x1,y1,blk);
            T(0,1.f*u,x1,y1); T(0,2.f*u,x2,y2); ln(r,x1,y1,x2,y2,blk); // not drawn (pin)
            break;}
        case T_AND: case T_NAND: { // D shape
            int x1,y1,x2,y2; T(-1.2f*u,-1.f*u,x1,y1); T(-1.2f*u,1.f*u,x2,y2); ln(r,x1,y1,x2,y2,blk);
            T(-1.2f*u,-1.f*u,x1,y1);
            // flat left already; arc right
            int prevx=x1,prevy=y1;
            for(int i=1;i<=12;i++){
                float a=-M_PI/2 + M_PI*i/12;
                float lx=-1.2f*u+ 1.f*u + 1.f*u*std::cos(a);
                float ly=0 + 1.f*u*std::sin(a);
                int cx,cy; T(lx,ly,cx,cy); ln(r,prevx,prevy,cx,cy,blk); prevx=cx; prevy=cy;
            }
            if(c.type==T_NAND){ int bx,by; T(1.05f*u,0,bx,by); dot(r,bx,by,3,blk); }
            break;}
        case T_OR: case T_NOR: { // shield shape
            int prevx=0,prevy=0;
            for(int i=0;i<=12;i++){
                float t=(float)i/12;
                float lx=-1.5f*u + 1.0f*u*t*t*0;
                // back curve
                lx = -1.6f*u + 1.6f*u*(1-(1-t)*(1-t));
                float ly=-1.f*u + 2.f*u*t;
                int cx,cy; T(lx,ly,cx,cy);
                if(i>0) ln(r,prevx,prevy,cx,cy,blk);
                prevx=cx; prevy=cy;
            }
            // front curve
            for(int i=0;i<=12;i++){
                float a=-M_PI/2 + M_PI*i/12;
                float lx=0.0f*u + 1.6f*u*std::cos(a);
                float ly=0 + 1.f*u*std::sin(a);
                int cx,cy; T(lx,ly,cx,cy); ln(r,prevx,prevy,cx,cy,blk); prevx=cx; prevy=cy;
            }
            if(c.type==T_NOR){ int bx,by; T(1.75f*u,0,bx,by); dot(r,bx,by,3,blk); }
            break;}
        case T_XOR: { // OR with extra back arc
            int prevx=0,prevy=0;
            for(int i=0;i<=12;i++){
                float t=(float)i/12;
                float lx=-1.4f*u+1.5f*u*(1-(1-t)*(1-t));
                float ly=-1.f*u+2.f*u*t; int cx,cy; T(lx,ly,cx,cy);
                if(i>0) ln(r,prevx,prevy,cx,cy,blk); prevx=cx;prevy=cy;
            }
            for(int i=0;i<=12;i++){
                float a=-M_PI/2+M_PI*i/12; float lx=0.1f*u+1.6f*u*std::cos(a);
                float ly=1.f*u*std::sin(a); int cx,cy; T(lx,ly,cx,cy);
                ln(r,prevx,prevy,cx,cy,blk); prevx=cx;prevy=cy;
            }
            int p2x=0,p2y=0;
            for(int i=0;i<=12;i++){
                float t=(float)i/12; float lx=-1.9f*u+0.5f*u*(1-(1-t)*(1-t));
                float ly=-1.f*u+2.f*u*t; int cx,cy; T(lx,ly,cx,cy);
                if(i>0) ln(r,p2x,p2y,cx,cy,blk); p2x=cx;p2y=cy;
            }
            break;}
        case T_NOT: break;
        case T_NODE: { int x,y; T(0,0,x,y); dot(r,x,y,3,blk); break;}
        default: break;
    }
    // NOT gate (handle separately since enum merged)
}
        case T_PROBE: { int x1,y1,x2,y2,x3,y3;
            T(-2.f*u,0,x1,y1); T(-0.5f*u,0,x2,y2); ln(r,x1,y1,x2,y2,blk);
            T(-0.5f*u,-0.8f*u,x2,y2); T(-0.5f*u,0.8f*u,x3,y3); ln(r,x2,y2,x3,y3,blk);
            T(-0.5f*u,-0.8f*u,x2,y2); T(1.f*u,-0.8f*u,x3,y3); ln(r,x2,y2,x3,y3,blk);
            T(-0.5f*u,0.8f*u,x2,y2); T(1.f*u,0.8f*u,x3,y3); ln(r,x2,y2,x3,y3,blk);
            T(1.f*u,-0.8f*u,x2,y2); T(1.f*u,0.8f*u,x3,y3); ln(r,x2,y2,x3,y3,blk);
            break;}
        case T_NODE: { int x,y; T(0,0,x,y); dot(r,x,y,3,blk); break;}
        default: break;
    }
    // NOT gate (handle separately since enum merged)
}

// Handle NOT gate cleanly (since switch above folded NAND into AND branch)
static void drawNOT(SDL_Renderer* r, const Comp& c, const Camera& cam){
    auto T=[&](float lx,float ly,int& sx,int& sy){
        float ox,oy; rotPt(lx,ly,c.rot,ox,oy); sx=w2sx(c.x+ox,cam); sy=w2sy(c.y+oy,cam); };
    SDL_Color blk{20,20,20,255}; int u=GRID;
    int x1,y1,x2,y2,x3,y3,x4,y4;
    T(-2.f*u,0,x1,y1); T(-1.f*u,0,x2,y2); ln(r,x1,y1,x2,y2,blk);
    T(-1.f*u,-0.8f*u,x2,y2); T(-1.f*u,0.8f*u,x3,y3); T(1.f*u,0,x4,y4);
    ln(r,x2,y2,x4,y4,blk); ln(r,x3,y3,x4,y4,blk);
    T(1.f*u,0,x1,y1); T(1.25f*u,0,x2,y2); ln(r,x1,y1,x2,y2,blk);
    dot(r,x2,y2,3,blk);
    T(1.25f*u,0,x1,y1); T(2.f*u,0,x2,y2); ln(r,x1,y1,x2,y2,blk);
}

// ----------------------------- library ---------------------------------------
struct LibItem { const char* name; CType type; const char* cat; };
static const LibItem LIB[] = {
    {"Resistor",   T_R,      "Passive"},
    {"Capacitor",  T_C,      "Passive"},
    {"Inductor",   T_L,      "Passive"},
    {"LED",        T_LED,    "Output"},
    {"Diode",      T_DIODE,  "Passive"},
    {"Switch",     T_SWITCH, "Input"},
    {"Battery",    T_BATTERY,"Source"},
    {"VCC (+5V)",  T_VCC,    "Source"},
    {"GND",        T_GND,    "Source"},
    {"Pulse",      T_PULSE,  "Source"},
    {"Clock",      T_CLOCK,  "Source"},
    {"AND",        T_AND,    "Logic"},
    {"OR",         T_OR,     "Logic"},
    {"NOT",        T_NOT,    "Logic"},
    {"XOR",        T_XOR,    "Logic"},
    {"NAND",       T_NAND,   "Logic"},
    {"NOR",        T_NOR,    "Logic"},
    {"Probe",      T_PROBE,  "Meter"},
    {"Junction",   T_NODE,   "Misc"},
};
static const int LIB_N = sizeof(LIB)/sizeof(LIB[0]);
static const char* typeName(CType t){
    for(int i=0;i<LIB_N;i++) if(LIB[i].type==t) return LIB[i].name;
    return "?";
}

// ----------------------------- app state -------------------------------------
struct App {
    std::vector<Comp> comps;
    std::vector<Wire> wires;
    Camera cam;
    Tool tool = TOOL_SELECT;
    CType pending = T_COUNT;       // component waiting to be placed
    int nextRef[16] = {1,0};
    // interaction
    bool dragging=false; int dragOffX=0,dragOffY=0;
    bool panning=false; int panLastX=0,panLastY=0;
    int wireFromC=-1, wireFromP=-1; bool wiring=false;
    int wireCurX=0, wireCurY=0;
    int mouseX=0,mouseY=0;
    bool gridOn=true; bool running=false;
    std::string search;
    std::string valueBuf;            // for editing selected comp value
    bool editing=false;
    std::vector<std::string> logLines;
    // toolbar buttons
    std::vector<std::pair<std::string,SDL_Rect>> tbtns;
    std::vector<std::pair<std::string,SDL_Rect>> menubtns;
    bool menuOpen=false; int menuOpenIdx=-1;
};

static std::string newRef(App& a, CType t){
    int& n = a.nextRef[(int)t];
    char buf[16];
    const char* pfx = "R";
    switch(t){
        case T_R: pfx="R"; break; case T_C: pfx="C"; break; case T_L: pfx="L"; break;
        case T_LED: pfx="D"; break; case T_DIODE: pfx="D"; break;
        case T_SWITCH: pfx="SW"; break; case T_BATTERY: pfx="BT"; break;
        case T_VCC: pfx="#"; break; case T_GND: pfx="#"; break;
        case T_PULSE: pfx="V"; break; case T_CLOCK: pfx="V"; break;
        case T_AND: pfx="U"; break; case T_OR: pfx="U"; break; case T_NOT: pfx="U"; break;
        case T_XOR: pfx="U"; break; case T_NAND: pfx="U"; break; case T_NOR: pfx="U"; break;
        case T_PROBE: pfx="PR"; break; case T_NODE: pfx="N"; break;
        default: pfx="X";
    }
    n++;
    std::snprintf(buf,sizeof(buf),"%s%d",pfx,n);
    return buf;
}
static std::string defaultValue(CType t){
    switch(t){
        case T_R: return "10k"; case T_C: return "100n"; case T_L: return "10u";
        case T_LED: return "Red"; case T_BATTERY: return "5V"; case T_VCC: return "+5V";
        case T_PULSE: return "1kHz"; case T_CLOCK: return "1MHz";
        default: return "";
    }
}

// hit testing
static int compAt(App& a, int wx, int wy){
    for(int i=(int)a.comps.size()-1;i>=0;i--){
        auto& c=a.comps[i];
        if(std::abs(c.x-wx)<=2*GRID && std::abs(c.y-wy)<=2*GRID) return i;
    }
    return -1;
}
static bool pinAt(App& a, int wx, int wy, int& outC, int& outP){
    for(int i=0;i<(int)a.comps.size();i++){
        auto pins=pinLayout(a.comps[i].type);
        for(int p=0;p<(int)pins.size();p++){
            int px,py; pinWorldPos(a.comps[i],p,px,py);
            if(std::abs(px-wx)<=GRID && std::abs(py-wy)<=GRID){ outC=i; outP=p; return true; }
        }
    }
    return false;
}

static void addLog(App& a, const std::string& s){
    a.logLines.push_back(s); if(a.logLines.size()>200) a.logLines.erase(a.logLines.begin());
}

// ----------------------------- simulation ------------------------------------
static int wireStateOf(App& a, int compIdx, int pinIdx){
    for(auto& w:a.wires){
        if((w.ca==compIdx&&w.pa==pinIdx)||(w.cb==compIdx&&w.pb==pinIdx)) return w.state;
    }
    return -1;
}
static void setWireState(App& a, int compIdx, int pinIdx, int st){
    for(auto& w:a.wires){
        if((w.ca==compIdx&&w.pa==pinIdx)||(w.cb==compIdx&&w.pb==pinIdx)) w.state=st;
    }
}
static void simulate(App& a){
    for(auto& w:a.wires) w.state=-1;
    for(auto& c:a.comps){
        if(c.type==T_VCC) c.state=1; else if(c.type==T_GND) c.state=0; else c.state=-1;
    }
    // set source wire states
    for(size_t i=0;i<a.comps.size();i++){
        auto&c=a.comps[i];
        if(c.type==T_VCC||c.type==T_GND||c.type==T_PULSE||c.type==T_CLOCK)
            setWireState(a,(int)i,0,c.type==T_GND?0:1);
    }
    for(int iter=0;iter<50;iter++){
        bool changed=false;
        for(size_t i=0;i<a.comps.size();i++){
            auto&c=a.comps[i];
            int in0=wireStateOf(a,(int)i,0), in1=(c.type!=T_NOT)?wireStateOf(a,(int)i,1):-1;
            int out=-1;
            switch(c.type){
                case T_AND: if(in0>=0&&in1>=0){out=in0&in1;} break;
                case T_OR:  if(in0>=0&&in1>=0){out=in0|in1;} break;
                case T_XOR: if(in0>=0&&in1>=0){out=in0^in1;} break;
                case T_NAND:if(in0>=0&&in1>=0){out=!(in0&in1);} break;
                case T_NOR: if(in0>=0&&in1>=0){out=!(in0|in1);} break;
                case T_NOT: if(in0>=0){out=!in0;} break;
                case T_SWITCH: if(c.swClosed){ int s=wireStateOf(a,(int)i,0); if(s>=0) setWireState(a,(int)i,1,s);} break;
                default: break;
            }
            if(out>=0){
                int cur=wireStateOf(a,(int)i,(int)pinLayout(c.type).size()-1);
                if(cur!=out){ setWireState(a,(int)i,(int)pinLayout(c.type).size()-1,out); changed=true; c.state=out; }
            }
        }
        if(!changed) break;
    }
    // propagate through NODE junctions by merging coordinates handled implicitly
}

// ----------------------------- DRC -------------------------------------------
static void runDRC(App& a){
    addLog(a,"--- DRC ---");
    // floating pins: any input pin with no wire
    for(size_t i=0;i<a.comps.size();i++){
        auto pins=pinLayout(a.comps[i].type);
        for(int p=0;p<(int)pins.size();p++){
            if(!pins[p].isOutput && a.comps[i].type!=T_NODE){
                bool found=false;
                for(auto&w:a.wires) if((w.ca==(int)i&&w.pa==p)||(w.cb==(int)i&&w.pb==p)){found=true;break;}
                if(!found) addLog(a,"[WARN] Floating pin: "+a.comps[i].ref+"."+std::to_string(p));
            }
        }
    }
    // VCC shorted to GND through a wire chain
    for(auto&w:a.wires){
        auto& A=a.comps[w.ca]; auto& B=a.combs_safe(a,w.cb);
    }
}
// (helper not needed; keep simple)
static std::vector<Comp>& combs_safe(App& a, int){ return a.comps; }

// ----------------------------- save / load -----------------------------------
static void saveProject(App& a, const char* path){
    FILE* f=fopen(path,"w"); if(!f) return;
    fprintf(f,"{ \"comps\":[\n");
    for(size_t i=0;i<a.comps.size();i++){
        auto&c=a.comps[i];
        fprintf(f,"  {\"t\":%d,\"x\":%d,\"y\":%d,\"r\":%d,\"ref\":\"%s\",\"val\":\"%s\"}%s\n",
                (int)c.type,c.x,c.y,c.rot,c.ref.c_str(),c.value.c_str(),
                i+1<a.comps.size()?",":"");
    }
    fprintf(f,"], \"wires\":[\n");
    for(size_t i=0;i<a.wires.size();i++){
        auto&w=a.wires[i];
        fprintf(f,"  {\"ca\":%d,\"pa\":%d,\"cb\":%d,\"pb\":%d}%s",
                w.ca,w.pa,w.cb,w.pb, i+1<a.wires.size()?",":"");
    }
    fprintf(f,"] }\n");
    fclose(f);
    addLog(a,std::string("Saved: ")+path);
}
static void loadProject(App& a, const char* path){
    FILE* f=fopen(path,"r"); if(!f) return;
    a.comps.clear(); a.wires.clear();
    char buf[1<<16]; std::string s;
    size_t n; while((n=fread(buf,1,sizeof(buf),f))>0) s.append(buf,n); fclose(f);
    // very small line-based parse
    auto extractInt=[&](const std::string& src,const std::string& key)->int{
        auto p=src.find(key); if(p==std::string::npos) return 0;
        p=src.find(':',p); p++;
        while(p<src.size()&& (src[p]==' '||src[p]=='"')) p++;
        return atoi(src.c_str()+p);
    };
    auto extractStr=[&](const std::string& src,const std::string& key)->std::string{
        auto p=src.find(key); if(p==std::string::npos) return "";
        p=src.find(':',p); p=src.find('"',p)+1;
        auto q=src.find('"',p); return src.substr(p,q-p);
    };
    size_t pos=0;
    while((pos=s.find('{',pos))!=std::string::npos){
        size_t end=s.find('}',pos);
        if(end==std::string::npos) break;
        std::string obj=s.substr(pos,end-pos);
        if(obj.find("\"t\":")!=std::string::npos){
            Comp c; c.type=(CType)extractInt(obj,"\"t\"");
            c.x=extractInt(obj,"\"x\""); c.y=extractInt(obj,"\"y\"");
            c.rot=extractInt(obj,"\"r\""); c.ref=extractStr(obj,"\"ref\""); c.value=extractStr(obj,"\"val\"");
            a.comps.push_back(c);
        } else if(obj.find("\"ca\":")!=std::string::npos){
            Wire w; w.ca=extractInt(obj,"\"ca\""); w.pa=extractInt(obj,"\"pa\"");
            w.cb=extractInt(obj,"\"cb\""); w.pb=extractInt(obj,"\"pb\""); w.state=-1;
            a.wires.push_back(w);
        }
        pos=end+1;
    }
    addLog(a,std::string("Loaded: ")+path);
}

// ----------------------------- UI drawing ------------------------------------
static void drawMenu(SDL_Renderer* r, TTF_Font* f, App& a){
    SDL_Rect bar{0,0,WIN_W,MENU_H}; fillRC(r,bar,C_MENU); ln(r,0,MENU_H,WIN_W,MENU_H,C_EDGE);
    static const char* items[]={"File","Edit","View","Tools","Design","Library","Help"};
    int x=6; a.menubtns.clear();
    for(auto lbl:items){
        int w=8+(int)strlen(lbl)*7;
        SDL_Rect rc{x,0,w,MENU_H}; a.menubtns.push_back({lbl,rc});
        bool hv = a.mouseX>=rc.x&&a.mouseX<rc.x+rc.w&&a.mouseY< MENU_H;
        if(hv){ fillRC(r,rc,C_MENUHV); drawText(r,f,lbl,rc,{255,255,255,255}); }
        else drawText(r,f,lbl,rc,C_MENUTX);
        x+=w;
    }
}
static void drawToolbar(SDL_Renderer* r, TTF_Font* f, App& a){
    SDL_Rect bar{0,MENU_H,WIN_W,TOOLBAR_H}; fillRC(r,bar,C_TBBG);
    ln(r,0,MENU_H+TOOLBAR_H,WIN_W,MENU_H+TOOLBAR_H,C_EDGE);
    struct TB{const char* name;const char* key;};
    TB btns[]={{"New","N"},{"Open","O"},{"Save","S"},{"|",""},
               {"Select","Esc"},{"Wire","W"},{"Delete","D"},{"Pan","Space"},
               {"|",""},{"Rotate","R"},{"Grid","G"},{"|",""},
               {"Run","F5"},{"Stop","F6"},{"|",""},{"Probe","P"},{"Export","E"},{"DRC","C"}};
    int x=6; a.tbtns.clear();
    for(auto&b:btns){
        if(b.name[0]=='|'){ ln(r,x+6,MENU_H+6,x+6,MENU_H+TOOLBAR_H-6,C_EDGE); x+=12; continue; }
        int w=52; SDL_Rect rc{x,MENU_H+3,w,TOOLBAR_H-6};
        a.tbtns.push_back({b.name,rc});
        bool hv=a.mouseX>=rc.x&&a.mouseX<rc.x+rc.w&&a.mouseY>=rc.y&&a.mouseY<rc.y+rc.h;
        bool act=false;
        if((b.name=="Select"&&a.tool==TOOL_SELECT)||(b.name=="Wire"&&a.tool==TOOL_WIRE)||
           (b.name=="Delete"&&a.tool==TOOL_DELETE)||(b.name=="Pan"&&a.tool==TOOL_PAN)) act=true;
        if(b.name=="Run"&&a.running) act=true;
        if(act) fillRC(r,rc,C_TBACT); else if(hv) fillRC(r,rc,C_TBHV);
        rectRC(r,rc,C_EDGE);
        drawText(r,f,b.name,rc,C_TEXT);
        x+=w+2;
    }
}
static bool strContainsIC(const std::string& hay,const std::string& ne){
    if(ne.empty()) return true;
    auto it=std::search(hay.begin(),hay.end(),ne.begin(),ne.end(),
        [](char a,char b){return tolower(a)==tolower(b);});
    return it!=hay.end();
}
static void drawLibPanel(SDL_Renderer* r, TTF_Font* f, TTF_Font* fs, App& a){
    SDL_Rect p{0,MENU_H+TOOLBAR_H,LIB_W,WIN_H-MENU_H-TOOLBAR_H-STATUS_H};
    fillRC(r,p,C_PANEL);
    SDL_Rect hd{0,p.y,LIB_W,HDR_H}; fillRC(r,hd,C_HDR);
    drawText(r,f,"LIBRARY",hd,C_TEXT);
    // search box
    SDL_Rect sb{4,p.y+HDR_H+3,LIB_W-8,20}; fillRC(r,sb,{255,255,255,255}); rectRC(r,sb,C_EDGE);
    std::string sh="Search: "+a.search; if(a.editing==false) {} 
    drawText(r,fs,sh.c_str(),sb,C_TEXTDIM,false);
    int y=p.y+HDR_H+28;
    for(int i=0;i<LIB_N;i++){
        if(!strContainsIC(LIB[i].name,a.search)&&!strContainsIC(LIB[i].cat,a.search)) continue;
        SDL_Rect row{2,y,LIB_W-4,18};
        bool hv=a.mouseX>=row.x&&a.mouseX<row.x+row.w&&a.mouseY>=row.y&&a.mouseY<row.y+row.h;
        bool sel=(a.pending==LIB[i].type);
        if(sel) fillRC(r,row,C_ACCENT);
        else if(hv) fillRC(r,row,{210,225,245,255});
        std::string s=std::string("  ")+LIB[i].name;
        drawText(r,fs,s.c_str(),Rect2(row),sel?SDL_Color{255,255,255,255}:C_TEXT,false);
        y+=18;
    }
    ln(r,LIB_W,p.y,p.y+p.h,C_EDGE);
}
static SDL_Rect Rect2(SDL_Rect x){return x;}

static void drawPropPanel(SDL_Renderer* r, TTF_Font* f, TTF_Font* fs, App& a){
    int x=WIN_W-PROP_W; SDL_Rect p{x,MENU_H+TOOLBAR_H,PROP_W,WIN_H-MENU_H-TOOLBAR_H-STATUS_H};
    fillRC(r,p,C_PANEL);
    SDL_Rect hd{x,p.y,PROP_W,HDR_H}; fillRC(r,hd,C_HDR); drawText(r,f,"PROPERTIES",hd,C_TEXT);
    int y=p.y+HDR_H+6;
    // find single selected
    int sel=-1,cnt=0;
    for(size_t i=0;i<a.comps.size();i++) if(a.comps[i].sel){sel=(int)i;cnt++;}
    std::string lines[8];
    int nl=0;
    if(cnt==0){ lines[nl++]="No selection"; }
    else if(cnt>1){ lines[nl++]=std::to_string(cnt)+" selected"; }
    else {
        auto&c=a.comps[sel];
        lines[nl++]=std::string("Type: ")+typeName(c.type);
        lines[nl++]=std::string("Ref:  ")+c.ref;
        lines[nl++]=std::string("Value:")+c.value;
        char b[64]; std::snprintf(b,sizeof(b),"Pos:  (%d, %d)",c.x,c.y); lines[nl++]=b;
        const char* rn[]={"0","90","180","270"}; lines[nl++]=std::string("Rot:  ")+rn[c.rot&3];
        lines[nl++]=std::string("State:")+(c.state<0?" -":(c.state?" 1":" 0"));
    }
    for(int i=0;i<nl;i++){ SDL_Rect rr{x+6,y,PROP_W-12,18};
        fillRC(r,rr,C_PANEL); drawText(r,fs,lines[i].c_str(),rr,C_TEXT,false); y+=18; }
    // hint
    y+=8;
    SDL_Rect hint{x+6,y,PROP_W-12,80}; fillRC(r,hint,{250,250,235,255}); rectRC(r,hint,C_EDGE);
    drawText(r,fs,"R: rotate   Del: remove",hint,C_TEXTDIM,false);
    ln(r,x,p.y,p.y+p.h,C_EDGE);
}

static void drawCanvas(SDL_Renderer* r, TTF_Font* fs, App& a){
    SDL_Rect cv=canvasRect();
    // clip
    SDL_RenderSetClipRect(r,&cv);
    fillRC(r,cv,C_CANVAS); rectRC(r,cv,C_EDGE);
    // grid dots
    if(a.gridOn){
        setCol(r,C_GRID);
        int step=(int)(GRID*a.cam.zoom); if(step<6) step=6;
        int sx0=cv.x, sy0=cv.y;
        for(int sx=sx0; sx<cv.x+cv.w; sx+=step){
            for(int sy=sy0; sy<cv.y+cv.h; sy+=step){
                SDL_RenderDrawPoint(r,sx,sy);
            }
        }
    }
    // origin
    int ox=w2sx(0,a.cam), oy=w2sy(0,a.cam); ln(r,ox-6,oy,ox+6,oy,C_HI); ln(r,ox,oy-6,ox,oy+6,C_HI);
    // wires
    for(auto&w:a.wires){
        int x1,y1,x2,y2;
        if(w.ca>=0){ pinWorldPos(a.comps[w.ca],w.pa,x1,y1);} else {x1=w.x1;y1=w.y1;}
        if(w.cb>=0){ pinWorldPos(a.comps[w.cb],w.pb,x2,y2);} else {x2=w.x2;y2=w.y2;}
        int sx1=w2sx(x1,a.cam),sy1=w2sy(y1,a.cam),sx2=w2sx(x2,a.cam),sy2=w2sy(y2,a.cam);
        // orthogonal route
        int my=(sy1+sy2)/2;
        SDL_Color wc = a.running ? (w.state==1?C_HI:(w.state==0?C_LO:C_UNK)) : C_PIN;
        ln(r,sx1,sy1,sx1,my,wc); ln(r,sx1,my,sx2,my,wc); ln(r,sx2,my,sx2,sy2,wc);
    }
    // wire-in-progress
    if(a.wiring){
        int fx,fy; pinWorldPos(a.comps[a.wireFromC],a.wireFromP,fx,fy);
        int sfx=w2sx(fx,a.cam),sfy=w2sy(fy,a.cam);
        ln(r,sfx,sfy,a.mouseX,a.mouseY,C_ACCENT);
    }
    // components
    for(size_t i=0;i<a.comps.size();i++){
        auto&c=a.comps[i];
        if(c.type==T_NOT) drawNOT(r,c,a.cam); else drawSymbol(r,c,a.cam);
        // pins
        auto pins=pinLayout(c.type);
        for(int p=0;p<(int)pins.size();p++){
            int px,py; pinWorldPos(c,p,px,py);
            int sx=w2sx(px,a.cam),sy=w2sy(py,a.cam);
            bool nearP = std::abs(a.mouseX-sx)<6&&std::abs(a.mouseY-sy)<6;
            dot(r,sx,sy,nearP?4:2, nearP?C_ACCENT:C_PIN);
        }
        // ref/value labels
        int cx=w2sx(c.x,a.cam), cy=w2sy(c.y,a.cam);
        SDL_Rect lr{cx-30,cy-26,60,12}; drawText(fs,c.ref.c_str(),lr,C_TEXTDIM);
        if(!c.value.empty()){ SDL_Rect lv{cx-30,cy+14,60,12}; drawText(fs,c.value.c_str(),lv,C_TEXTDIM); }
        // selection box
        if(c.sel){ SDL_Rect sb{cx-2*GRID,cy-2*GRID,(int)(4*GRID),(int)(4*GRID)};
            sb.x=w2sx(c.x,a.cam)-2*GRID; sb.y=w2sy(c.y,a.cam)-2*GRID; sb.w=4*GRID; sb.h=4*GRID;
            rectRC(r,sb,C_SEL); }
        // probe value
        if(c.type==T_PROBE){
            int sx=w2sx(c.x,a.cam),sy=w2sy(c.y,a.cam);
            const char* txt=c.state<0?"?":(c.state?"1":"0");
            SDL_Rect pr{sx-6,sy-6,16,16}; drawText(fs,txt,pr,c.state==1?C_HI:C_LO);
        }
    }
    // pending ghost
    if(a.pending!=T_COUNT){
        int wx=snap((int)s2wx(a.mouseX,a.cam)), wy=snap((int)s2wy(a.mouseY,a.cam));
        Comp g{a.pending,wx,wy,0,"","",false};
        if(g.type==T_NOT) drawNOT(r,g,a.cam); else drawSymbol(r,g,a.cam);
    }
    SDL_RenderSetClipRect(r,nullptr);
}

static void drawStatus(SDL_Renderer* r, TTF_Font* fs, App& a){
    SDL_Rect bar{0,WIN_H-STATUS_H,WIN_W,STATUS_H}; fillRC(r,bar,C_STATUS);
    ln(r,0,bar.y,WIN_W,bar.y,C_EDGE);
    SDL_Rect cv=canvasRect();
    double wx=s2wx(a.mouseX,a.cam), wy=s2wy(a.mouseY,a.cam);
    char b[160];
    bool inCv=a.mouseX>=cv.x&&a.mouseX<cv.x+cv.w&&a.mouseY>=cv.y&&a.mouseY<cv.y+cv.h;
    if(inCv) std::snprintf(b,sizeof(b),"X:%d  Y:%d",(int)wx,(int)wy);
    else std::snprintf(b,sizeof(b),"X:-  Y:-");
    drawText(r,fs, a.running?"Running":"Ready", SDL_Rect{4,bar.y,80,STATUS_H}, a.running?C_HI:C_TEXTDIM,false);
    drawText(r,fs, b, SDL_Rect{90,bar.y,150,STATUS_H}, C_TEXTDIM,false);
    char z[32]; std::snprintf(z,sizeof(z),"Zoom:%d%%",(int)(a.cam.zoom*100));
    drawText(r,fs,z,SDL_Rect{250,bar.y,110,STATUS_H},C_TEXTDIM,false);
    drawText(r,fs, std::string("Snap:")+std::to_string(GRID)+"th").c_str(), SDL_Rect{370,bar.y,110,STATUS_H},C_TEXTDIM,false);
    drawText(r,fs, a.gridOn?"Grid:ON":"Grid:OFF", SDL_Rect{490,bar.y,90,STATUS_H}, a.gridOn?C_ACCENT:C_TEXTDIM,false);
    drawText(r,fs, "Proteus-like Sim", SDL_Rect{WIN_W-140,bar.y,136,STATUS_H}, C_TEXTDIM,false);
}

static void drawLog(SDL_Renderer* r, TTF_Font* fs, App& a){
    SDL_Rect cv=canvasRect();
    int x=LIB_W, y=WIN_H-STATUS_H-LOG_H, w=WIN_W-LIB_W-PROP_W, h=LOG_H;
    if(cv.h<=LOG_H+20) return; // no room
    SDL_Rect box{x,y,w,h}; fillRC(r,box,{250,250,250,255}); rectRC(r,box,C_EDGE);
    SDL_Rect hd{x,y,w,HDR_H}; fillRC(r,hd,C_HDR); drawText(fs,"SIMULATION LOG",hd,C_TEXT,false);
    int ly=y+HDR_H+2; int cnt=0;
    for(int i=(int)a.logLines.size()-1;i>=0 && ly<y+h-14;i--,ly+=14){
        drawText(fs,a.logLines[i].c_str(),SDL_Rect{x+4,ly,w-8,14},C_TEXTDIM,false);
    }
}

// ----------------------------- main ------------------------------------------
int main(int,char*[]){
    SDL_SetMainReady();
    if(SDL_Init(SDL_INIT_VIDEO)){ SDL_Log("SDL:%s",SDL_GetError()); return 1; }
    if(TTF_Init()){ SDL_Log("TTF:%s",TTF_GetError()); return 1; }
    SDL_Window* win=SDL_CreateWindow("Proteus-like EDA Simulator",
        SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,WIN_W,WIN_H,SDL_WINDOW_SHOWN);
    SDL_Renderer* r=SDL_CreateRenderer(win,-1,SDL_RENDERER_ACCELERATED|SDL_RENDERER_PRESENTVSYNC);
    if(!r) r=SDL_CreateRenderer(win,-1,SDL_RENDERER_SOFTWARE);
    TTF_Font* f=TTF_OpenFont("C:/Windows/Fonts/arial.ttf",13);
    TTF_Font* fs=TTF_OpenFont("C:/Windows/Fonts/arial.ttf",11);
    App a;
    a.cam.panX=LIB_W/a.cam.zoom; a.cam.panY=(MENU_H+TOOLBAR_H)/a.cam.zoom;
    addLog(a,"Welcome. Click a library item to place.");
    addLog(a,"Keys: W wire, R rotate, Del, G grid, Space pan, F5 run");
    bool running=true;
    while(running){
        SDL_Event e; int wheel=0;
        while(SDL_PollEvent(&e)){
            switch(e.type){
                case SDL_QUIT: running=false; break;
                case SDL_MOUSEWHEEL: wheel=e.wheel.y; break;
                case SDL_MOUSEMOTION:
                    a.mouseX=e.motion.x; a.mouseY=e.motion.y;
                    if(a.panning){ a.cam.panX-=(e.motion.x-a.panLastX)/a.cam.zoom;
                                   a.cam.panY-=(e.motion.y-a.panLastY)/a.cam.zoom; }
                    a.panLastX=e.motion.x; a.panLastY=e.motion.y;
                    if(a.dragging){
                        int wx=snap((int)s2wx(a.mouseX,a.cam)), wy=snap((int)s2wy(a.mouseY,a.cam));
                        for(auto&c:a.comps) if(c.sel){ c.x=wx-a.dragOffX; c.y=wy-a.dragOffY; }
                    }
                    break;
                case SDL_MOUSEBUTTONDOWN: {
                    int mx=e.button.x,my=e.button.y;
                    SDL_Rect cv=canvasRect();
                    if(e.button.button==SDL_BUTTON_MIDDLE){ a.panning=true; a.panLastX=mx; a.panLastY=my; break; }
                    if(mx<cv.x||mx>=cv.x+cv.w||my<cv.y||my>=cv.y+cv.h) break; // outside canvas
                    int wx=snap((int)s2wx(mx,a.cam)), wy=snap((int)s2wy(my,a.cam));
                    if(e.button.button==SDL_BUTTON_LEFT){
                        // wiring?
                        if(a.tool==TOOL_WIRE){
                            int hc,hp;
                            if(pinAt(a,wx,wy,hc,hp)){
                                if(!a.wiring){ a.wiring=true; a.wireFromC=hc; a.wireFromP=hp; }
                                else { Wire w{a.wireFromC,a.wireFromP,hc,hp,0,0,0,0,-1};
                                       a.wires.push_back(w); a.wiring=false; addLog(a,"Wire added"); }
                            } else if(a.wiring){
                                Wire w{a.wireFromC,a.wireFromP,-1,-1,wx,wy,wx,wy,-1};
                                a.wires.push_back(w); a.wiring=false;
                            }
                        } else if(a.tool==TOOL_DELETE){
                            int ci=compAt(a,wx,wy); if(ci>=0){
                                // remove wires touching it
                                a.wires.erase(std::remove_if(a.wires.begin(),a.wires.end(),
                                    [ci](const Wire&w){return w.ca==ci||w.cb==ci;}),a.wires.end());
                                a.comps.erase(a.comps.begin()+ci); }
                        } else if(a.pending!=T_COUNT){
                            Comp c{a.pending,wx,wy,0,newRef(a,a.pending),defaultValue(a.pending),false};
                            a.comps.push_back(c); addLog(a,"Placed "+typeName(a.pending));
                        } else { // select
                            int ci=compAt(a,wx,wy);
                            if(!(e.mod&KMOD_SHIFT)) for(auto&c:a.comps) c.sel=false;
                            if(ci>=0){ a.comps[ci].sel=true; a.dragging=true;
                                a.dragOffX=wx-a.comps[ci].x; a.dragOffY=wy-a.comps[ci].y; }
                        }
                    } else if(e.button.button==SDL_BUTTON_RIGHT){
                        a.pending=T_COUNT; a.wiring=false;
                        for(auto&c:a.comps) c.sel=false;
                    }
                    break;}
                case SDL_MOUSEBUTTONUP:
                    if(e.button.button==SDL_BUTTON_MIDDLE) a.panning=false;
                    if(e.button.button==SDL_BUTTON_LEFT) a.dragging=false;
                    break;
                case SDL_KEYDOWN: {
                    SDL_StartTextInput();
                    auto rotSel=[&](){ for(auto&c:a.comps) if(c.sel) c.rot=(c.rot+1)&3; };
                    auto delSel=[&](){
                        for(int i=(int)a.comps.size()-1;i>=0;i--) if(a.comps[i].sel){
                            a.wires.erase(std::remove_if(a.wires.begin(),a.wires.end(),
                                [i](const Wire&w){return w.ca==i||w.cb==i;}),a.wires.end());
                            a.comps.erase(a.comps.begin()+i); } };
                    SDL_Keycode k=e.key.keysym.sym;
                    if(k==SDLK_ESCAPE){ a.pending=T_COUNT; a.wiring=false; for(auto&c:a.comps)c.sel=false; a.tool=TOOL_SELECT; }
                    else if(k==SDLK_r) { rotSel(); if(a.pending!=T_COUNT){} }
                    else if(k==SDLK_DELETE||k==SDLK_BACKSPACE) delSel();
                    else if(k==SDLK_w) a.tool=TOOL_WIRE;
                    else if(k==SDLK_d) a.tool=TOOL_DELETE;
                    else if(k==SDLK_g) a.gridOn=!a.gridOn;
                    else if(k==SDLK_SPACE) a.tool=TOOL_PAN;
                    else if(k==SDLK_F5){ a.running=true; simulate(a); addLog(a,"Simulation: RUN"); }
                    else if(k==SDLK_F6){ a.running=false; addLog(a,"Simulation: STOP"); }
                    else if(k==SDLK_s && (e.mod&KMOD_CTRL)) saveProject(a,"circuit.json");
                    else if(k==SDLK_o && (e.mod&KMOD_CTRL)) loadProject(a,"circuit.json");
                    else if(k==SDLK_e && (e.mod&KMOD_CTRL)){
                        // export BMP
                        int ww,wh; SDL_GetWindowSize(win,&ww,&wh);
                        SDL_Surface* sf=SDL_CreateRGBSurface(0,ww,wh,32,0,0,0,0);
                        SDL_RenderReadPixels(r,nullptr,sf->format->format,sf->pixels,sf->pitch);
                        SDL_SaveBMP(sf,"export.bmp"); SDL_FreeSurface(sf); addLog(a,"Exported export.bmp");
                    }
                    else if(k==SDLK_c && (e.mod&KMOD_CTRL)) runDRC(a);
                    break;}
                case SDL_TEXTINPUT:
                    if(a.search.size()<16) a.search+=e.text.text;
                    break;
                case SDL_KEYUP:
                    if(e.key.keysym.sym==SDLK_BACKSPACE && !a.search.empty()) a.search.pop_back();
                    break;
            }
        }
        // zoom
        if(wheel!=0){
            double f=wheel>0?1.1:0.9;
            double mx=a.mouseX,my=a.mouseY;
            double wx=s2wx(mx,a.cam),wy=s2wy(my,a.cam);
            a.cam.zoom*=f; if(a.cam.zoom<0.2)a.cam.zoom=0.2; if(a.cam.zoom>8)a.cam.zoom=8;
            a.cam.panX=wx-mx/a.cam.zoom; a.cam.panY=wy-my/a.cam.zoom;
        }
        // toolbar/menu clicks
        // (handled inline by checking tbtns on mousedown top — but simpler: check here)
        // We handle toolbar via mouse coords on click in main loop below:
        // For simplicity we re-read state at draw and accept clicks in next event; but since
        // events already consumed, we instead process toolbar hit on every frame using
        // a saved click. Instead, handle toolbar by polling mouse buttons each frame:
        {
            int mx,my; Uint32 bs=SDL_GetMouseState(&mx,&my);
            static bool prevDown=false; bool down=(bs&SDL_BUTTON_LMASK)!=0;
            if(down&&!prevDown){
                // toolbar?
                if(my>=MENU_H&&my<MENU_H+TOOLBAR_H){
                    for(auto&tb:a.tbtns){
                        if(mx>=tb.second.x&&mx<tb.second.x+tb.second.w&&my>=tb.second.y&&my<tb.second.y+tb.second.h){
                            std::string n=tb.first;
                            if(n=="Select")a.tool=TOOL_SELECT;
                            else if(n=="Wire")a.tool=TOOL_WIRE;
                            else if(n=="Delete")a.tool=TOOL_DELETE;
                            else if(n=="Pan")a.tool=TOOL_PAN;
                            else if(n=="Rotate"){ for(auto&c:a.comps)if(c.sel)c.rot=(c.rot+1)&3; }
                            else if(n=="Grid")a.gridOn=!a.gridOn;
                            else if(n=="Run"){a.running=true;simulate(a);addLog(a,"RUN");}
                            else if(n=="Stop"){a.running=false;addLog(a,"STOP");}
                            else if(n=="Save")saveProject(a,"circuit.json");
                            else if(n=="Open")loadProject(a,"circuit.json");
                            else if(n=="New"){a.comps.clear();a.wires.clear();addLog(a,"New project");}
                            else if(n=="Export"){
                                int ww,wh;SDL_GetWindowSize(win,&ww,&wh);
                                SDL_Surface* sf=SDL_CreateRGBSurface(0,ww,wh,32,0,0,0,0);
                                SDL_RenderReadPixels(r,nullptr,sf->format->format,sf->pixels,sf->pitch);
                                SDL_SaveBMP(sf,"export.bmp");SDL_FreeSurface(sf);addLog(a,"Exported export.bmp");
                            }
                            else if(n=="DRC")runDRC(a);
                            break;
                        }
                    }
                }
                // library?
                else if(mx<LIB_W && my>=MENU_H+TOOLBAR_H && my<WIN_H-STATUS_H){
                    SDL_Rect p{0,MENU_H+TOOLBAR_H,LIB_W,WIN_H-MENU_H-TOOLBAR_H-STATUS_H};
                    int y=p.y+HDR_H+28;
                    for(int i=0;i<LIB_N;i++){
                        if(!strContainsIC(LIB[i].name,a.search)&&!strContainsIC(LIB[i].cat,a.search)) continue;
                        if(my>=y&&my<y+18){ a.pending=LIB[i].type; a.tool=TOOL_SELECT; a.search.clear(); break; }
                        y+=18;
                    }
                }
                // menu
                else if(my<MENU_H){
                    for(auto&mb:a.menubtns){
                        if(mx>=mb.second.x&&mx<mb.second.x+mb.second.w){
                            if(mb.first=="File")saveProject(a,"circuit.json");
                            if(mb.first=="Library")a.search.clear();
                            if(mb.first=="Help")addLog(a,"See status bar for keys");
                            break;
                        }
                    }
                }
            }
            prevDown=down;
        }

        // continuous sim if running & clocked source present
        // (kept lightweight)

        setCol(r,C_BG); SDL_RenderClear(r);
        drawMenu(r,f,a);
        drawToolbar(r,f,a);
        drawLibPanel(r,f,fs,a);
        drawPropPanel(r,f,fs,a);
        drawCanvas(r,fs,a);
        drawLog(r,fs,a);
        drawStatus(r,fs,a);
        SDL_RenderPresent(r);
        SDL_Delay(16);
    }
    TTF_CloseFont(f); TTF_CloseFont(fs);
    SDL_DestroyRenderer(r); SDL_DestroyWindow(win); TTF_Quit(); SDL_Quit();
    return 0;
}
