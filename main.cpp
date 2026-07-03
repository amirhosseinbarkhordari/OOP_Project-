// =============================================================================
//  Proteus-like Schematic Editor  (clean, compilable rewrite)
//  SDL2 + SDL2_ttf, single file, MinGW.
// =============================================================================
#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <cmath>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ---------------- layout ----------------
static const int WIN_W=1280, WIN_H=800;
static const int GRID=10;
static const int MENU_H=22, TOOL_H=34, STATUS_H=22;
static const int LIB_W=190, PROP_W=220, HDR_H=20, LOG_H=96;

// ---------------- colors ----------------
// Proteus-like classic palette (Windows 98-style beveled UI)
static const SDL_Color C_BG{236,233,216,255};     // tan/cream (Windows classic)
static const SDL_Color C_PANEL{236,233,216,255};  // tan panels
static const SDL_Color C_PANELHI{255,255,255,255};// bevel highlight (top-left)
static const SDL_Color C_PANELLO{172,168,153,255};// bevel shadow (bottom-right)
static const SDL_Color C_EDGE{113,111,100,255};   // dark edge
static const SDL_Color C_MENU{236,233,216,255};   // tan menu bar
static const SDL_Color C_MENUHV{10,36,106,255};   // dark blue hover (Win98)
static const SDL_Color C_MENUTX{0,0,0,255};       // black menu text
static const SDL_Color C_TBBG{236,233,216,255};   // tan toolbar
static const SDL_Color C_TBHV{182,210,248,255};   // light blue hover
static const SDL_Color C_TBACT{10,36,106,255};    // dark blue active
static const SDL_Color C_CANVAS{250,249,241,255}; // cream canvas (not pure white)
static const SDL_Color C_GRID{210,205,180,255};   // tan-tinted grid dots
static const SDL_Color C_TEXT{0,0,0,255};
static const SDL_Color C_DIM{120,110,90,255};
static const SDL_Color C_HDR{218,215,198,255};    // tan header
static const SDL_Color C_STATUS{236,233,216,255};
static const SDL_Color C_ACCENT{10,36,106,255};   // Proteus blue
static const SDL_Color C_HI{200,0,0,255};         // wire high (red)
static const SDL_Color C_LO{0,0,160,255};         // wire low (blue)
static const SDL_Color C_UNK{128,128,128,255};
static const SDL_Color C_WIRE{0,0,0,255};
static const SDL_Color C_PIN{0,0,0,255};
static const SDL_Color C_SEL{200,100,0,255};      // orange selection
static const SDL_Color C_BLK{0,0,0,255};

// ---------------- component types ----------------
enum CType { T_R, T_C, T_L, T_LED, T_DIODE, T_SWITCH, T_VCC, T_GND,
             T_BATT, T_CLK, T_AND, T_OR, T_NOT, T_XOR, T_NAND, T_NOR,
             T_PROBE, T_COUNT };

static const char* typeName(CType t){
    switch(t){
        case T_R:return "Resistor"; case T_C:return "Capacitor";
        case T_L:return "Inductor"; case T_LED:return "LED";
        case T_DIODE:return "Diode"; case T_SWITCH:return "Switch";
        case T_VCC:return "VCC"; case T_GND:return "GND";
        case T_BATT:return "Battery"; case T_CLK:return "Clock";
        case T_AND:return "AND"; case T_OR:return "OR";
        case T_NOT:return "NOT"; case T_XOR:return "XOR";
        case T_NAND:return "NAND"; case T_NOR:return "NOR";
        case T_PROBE:return "Probe"; default:return "?";
    }
}
static const char* typeCat(CType t){
    switch(t){
        case T_R:case T_C:case T_L:case T_DIODE:return "Passive";
        case T_LED:return "Output";
        case T_SWITCH:return "Input";
        case T_VCC:case T_GND:case T_BATT:case T_CLK:return "Source";
        case T_AND:case T_OR:case T_NOT:case T_XOR:case T_NAND:case T_NOR:return "Logic";
        case T_PROBE:return "Meter";
        default:return "";
    }
}

struct PinDef{ float ox,oy; };
struct Comp{
    CType type; int x,y,rot; std::string ref,value;
    bool sel=false; int state=-1; bool swClosed=false;
};

static std::vector<PinDef> pins(CType t){
    int u=GRID;
    switch(t){
        case T_R:case T_C:case T_L:case T_LED:case T_DIODE:case T_SWITCH:case T_BATT:
            return {{-2.f*u,0.f},{2.f*u,0.f}};
        case T_VCC:case T_GND:case T_CLK:
            return {{0.f,(float)u}};
        case T_AND:case T_OR:case T_XOR:case T_NAND:case T_NOR:
            return {{-2.f*u,-1.f*u},{-2.f*u,1.f*u},{2.f*u,0.f}};
        case T_NOT:
            return {{-2.f*u,0.f},{2.f*u,0.f}};
        case T_PROBE:
            return {{-2.f*u,0.f}};
        default:return {};
    }
}
static void rotPt(float lx,float ly,int rot,float&ox,float&oy){
    rot&=3;
    if(rot==0){ox=lx;oy=ly;}else if(rot==1){ox=-ly;oy=lx;}
    else if(rot==2){ox=-lx;oy=-ly;}else{ox=ly;oy=-lx;}
}
static void pinPos(const Comp&c,int i,int&wx,int&wy){
    auto P=pins(c.type);
    if(i<0||i>=(int)P.size()){wx=c.x;wy=c.y;return;}
    float ox,oy; rotPt(P[i].ox,P[i].oy,c.rot,ox,oy);
    wx=c.x+(int)ox; wy=c.y+(int)oy;
}

// ---------------- wire ----------------
struct Wire{ int ca,pa,cb,pb; int state=-1; };

// ---------------- camera ----------------
struct Cam{ double px=0,py=0,zoom=1; };

// ---------------- helpers ----------------
static void sc(SDL_Renderer*r,SDL_Color c){SDL_SetRenderDrawColor(r,c.r,c.g,c.b,c.a);}
static void fr(SDL_Renderer*r,SDL_Rect rc,SDL_Color c){sc(r,c);SDL_RenderFillRect(r,&rc);}
static void rct(SDL_Renderer*r,SDL_Rect rc,SDL_Color c){sc(r,c);SDL_RenderDrawRect(r,&rc);}
static void ln(SDL_Renderer*r,int x1,int y1,int x2,int y2,SDL_Color c){
    sc(r,c);SDL_RenderDrawLine(r,x1,y1,x2,y2);}
static int snap(int v){return (int)std::round((double)v/GRID)*GRID;}
static int sx(double wx,Cam c){return (int)((wx-c.px)*c.zoom);}
static int sy(double wy,Cam c){return (int)((wy-c.py)*c.zoom);}
static double wx(int s,Cam c){return s/c.zoom+c.px;}
static double wy(int s,Cam c){return s/c.zoom+c.py;}

static void txt(SDL_Renderer*r,TTF_Font*f,const char*s,SDL_Rect rc,SDL_Color col,bool ctr=true){
    if(!f||!s||!s[0])return;
    SDL_Surface*srf=TTF_RenderUTF8_Blended(f,s,col); if(!srf)return;
    SDL_Texture*t=SDL_CreateTextureFromSurface(r,srf);
    int w=srf->w,h=srf->h; SDL_Rect d;
    if(ctr){d.x=rc.x+(rc.w-w)/2;d.y=rc.y+(rc.h-h)/2;}
    else{d.x=rc.x+5;d.y=rc.y+(rc.h-h)/2;}
    d.w=w;d.h=h;SDL_RenderCopy(r,t,nullptr,&d);
    SDL_DestroyTexture(t);SDL_FreeSurface(srf);
}
static void dot(SDL_Renderer*r,int x,int y,int rad,SDL_Color c){
    sc(r,c);
    for(int dy=-rad;dy<=rad;dy++)for(int dx=-rad;dx<=rad;dx++)
        if(dx*dx+dy*dy<=rad*rad)SDL_RenderDrawPoint(r,x+dx,y+dy);
}
static bool containsIC(const std::string&h,const std::string&n){
    if(n.empty())return true;
    return std::search(h.begin(),h.end(),n.begin(),n.end(),
        [](char a,char b){return tolower(a)==tolower(b);})!=h.end();
}

// ---------------- symbol drawing ----------------
static void drawComp(SDL_Renderer*r,const Comp&c,const Cam&cam){
    auto T=[&](float lx,float ly,int&X,int&Y){
        float ox,oy;rotPt(lx,ly,c.rot,ox,oy);X=sx(c.x+ox,cam);Y=sy(c.y+oy,cam);};
    int u=GRID; SDL_Color b=C_BLK;
    auto L=[&](float x0,float y0,float x1,float y1){
        int a,bb,cc,dd;T(x0,y0,a,bb);T(x1,y1,cc,dd);ln(r,a,bb,cc,dd,b);};
    switch(c.type){
        case T_R:
            L(-2.f*u,0,-1.2f*u,0);
            L(-1.2f*u,0,-0.9f*u,-0.7f*u);
            L(-0.9f*u,-0.7f*u,-0.3f*u,0.7f*u);
            L(-0.3f*u,0.7f*u,0.3f*u,-0.7f*u);
            L(0.3f*u,-0.7f*u,0.9f*u,0.7f*u);
            L(0.9f*u,0.7f*u,1.2f*u,0);
            L(1.2f*u,0,2.f*u,0);
            break;
        case T_C:
            L(-2.f*u,0,-0.4f*u,0);
            L(-0.4f*u,-1.f*u,-0.4f*u,1.f*u);
            L(0.4f*u,-1.f*u,0.4f*u,1.f*u);
            L(0.4f*u,0,2.f*u,0);
            break;
        case T_L:
            L(-2.f*u,0,-1.4f*u,0);
            for(int i=0;i<4;i++){
                float x0=-1.4f*u+i*0.7f*u;
                int a,bb,cc,dd,e,f;
                T(x0,0,a,bb);T(x0+0.35f*u,-0.6f*u,cc,dd);T(x0+0.7f*u,0,e,f);
                ln(r,a,bb,cc,dd,b);ln(r,cc,dd,e,f,b);
            }
            L(1.4f*u,0,2.f*u,0);
            break;
        case T_LED:{
            L(-2.f*u,0,-0.5f*u,0);
            int x1,y1,x2,y2,x3,y3;
            T(-0.5f*u,-0.9f*u,x1,y1);T(-0.5f*u,0.9f*u,x2,y2);T(0.6f*u,0,x3,y3);
            ln(r,x1,y1,x3,y3,b);ln(r,x2,y2,x3,y3,b);
            T(0.6f*u,-0.9f*u,x1,y1);T(0.6f*u,0.9f*u,x2,y2);ln(r,x1,y1,x2,y2,b);
            L(0.6f*u,0,2.f*u,0);
            int a,bb;T(0.2f*u,-0.9f*u,a,bb);
            ln(r,a,bb,a+6,bb-6,b);ln(r,a,bb,a+8,bb,b);
            break;}
        case T_DIODE:
            L(-2.f*u,0,-0.5f*u,0);
            {int x1,y1,x2,y2,x3,y3;
            T(-0.5f*u,-0.8f*u,x1,y1);T(-0.5f*u,0.8f*u,x2,y2);T(0.6f*u,0,x3,y3);
            ln(r,x1,y1,x3,y3,b);ln(r,x2,y2,x3,y3,b);
            T(0.6f*u,-0.8f*u,x1,y1);T(0.6f*u,0.8f*u,x2,y2);ln(r,x1,y1,x2,y2,b);
            L(0.6f*u,0,2.f*u,0);}
            break;
        case T_SWITCH:
            L(-2.f*u,0,-0.7f*u,0);
            L(0.7f*u,0,2.f*u,0);
            if(c.swClosed){L(-0.7f*u,0,0.7f*u,0);}
            else{int a,bb,cc,dd;T(-0.7f*u,0,a,bb);T(0.7f*u,-1.2f*u,cc,dd);ln(r,a,bb,cc,dd,b);}
            break;
        case T_BATT:
            L(-2.f*u,0,-0.4f*u,0);
            L(-0.4f*u,-1.f*u,-0.4f*u,1.f*u);
            L(0.2f*u,-0.5f*u,0.2f*u,0.5f*u);
            L(0.2f*u,0,2.f*u,0);
            break;
        case T_VCC:
            L(0,1.f*u,0,0);
            L(-1.f*u,0,1.f*u,0);
            break;
        case T_GND:
            L(0,1.f*u,0,0.3f*u);
            L(-1.f*u,0.3f*u,1.f*u,0.3f*u);
            L(-0.6f*u,0.6f*u,0.6f*u,0.6f*u);
            L(-0.3f*u,0.9f*u,0.3f*u,0.9f*u);
            break;
        case T_CLK:{
            int a,bb,cc,dd;
            T(-1.f*u,-1.f*u,a,bb);T(1.f*u,-1.f*u,cc,dd);ln(r,a,bb,cc,dd,b);
            T(1.f*u,-1.f*u,a,bb);T(1.f*u,1.f*u,cc,dd);ln(r,a,bb,cc,dd,b);
            T(1.f*u,1.f*u,a,bb);T(-1.f*u,1.f*u,cc,dd);ln(r,a,bb,cc,dd,b);
            T(-1.f*u,1.f*u,a,bb);T(-1.f*u,-1.f*u,cc,dd);ln(r,a,bb,cc,dd,b);
            // pulse waveform inside
            T(-0.5f*u,-0.4f*u,a,bb);T(-0.5f*u,0.4f*u,cc,dd);ln(r,a,bb,cc,dd,b);
            T(-0.5f*u,0.4f*u,a,bb);T(0,0.4f*u,cc,dd);ln(r,a,bb,cc,dd,b);
            T(0,0.4f*u,a,bb);T(0,-0.4f*u,cc,dd);ln(r,a,bb,cc,dd,b);
            T(0,-0.4f*u,a,bb);T(0.5f*u,-0.4f*u,cc,dd);ln(r,a,bb,cc,dd,b);
            break;}
        case T_AND: case T_NAND:{
            int p0x,p0y,p1x,p1y;
            T(-1.2f*u,-1.f*u,p0x,p0y);T(-1.2f*u,1.f*u,p1x,p1y);
            ln(r,p0x,p0y,p1x,p1y,b);
            int px=p0x,py=p0y;
            for(int i=1;i<=14;i++){
                float a2=-M_PI/2+M_PI*i/14;
                float lx=-0.2f*u+1.2f*u*(float)std::cos(a2),ly=1.f*u*(float)std::sin(a2);
                int cx,cy;T(lx,ly,cx,cy);ln(r,px,py,cx,cy,b);px=cx;py=cy;
            }
            if(c.type==T_NAND){int bx,by;T(1.1f*u,0,bx,by);dot(r,bx,by,3,b);}
            break;}
        case T_OR: case T_NOR:{
            int px=0,py=0;
            for(int i=0;i<=14;i++){
                float t=(float)i/14;
                float lx=-1.6f*u+1.6f*u*(1-(1-t)*(1-t)),ly=-1.f*u+2.f*u*t;
                int cx,cy;T(lx,ly,cx,cy);if(i>0)ln(r,px,py,cx,cy,b);px=cx;py=cy;
            }
            for(int i=0;i<=14;i++){
                float a2=-M_PI/2+M_PI*i/14;
                float lx=1.6f*u*(float)std::cos(a2),ly=1.f*u*(float)std::sin(a2);
                int cx,cy;T(lx,ly,cx,cy);ln(r,px,py,cx,cy,b);px=cx;py=cy;
            }
            if(c.type==T_NOR){int bx,by;T(1.75f*u,0,bx,by);dot(r,bx,by,3,b);}
            break;}
        case T_XOR:{
            int px=0,py=0;
            for(int i=0;i<=14;i++){
                float t=(float)i/14;
                float lx=-1.4f*u+1.6f*u*(1-(1-t)*(1-t)),ly=-1.f*u+2.f*u*t;
                int cx,cy;T(lx,ly,cx,cy);if(i>0)ln(r,px,py,cx,cy,b);px=cx;py=cy;
            }
            for(int i=0;i<=14;i++){
                float a2=-M_PI/2+M_PI*i/14;
                float lx=0.2f*u+1.6f*u*(float)std::cos(a2),ly=1.f*u*(float)std::sin(a2);
                int cx,cy;T(lx,ly,cx,cy);ln(r,px,py,cx,cy,b);px=cx;py=cy;
            }
            int qx=0,qy=0;
            for(int i=0;i<=14;i++){
                float t=(float)i/14;
                float lx=-1.9f*u+0.5f*u*(1-(1-t)*(1-t)),ly=-1.f*u+2.f*u*t;
                int cx,cy;T(lx,ly,cx,cy);if(i>0)ln(r,qx,qy,cx,cy,b);qx=cx;qy=cy;
            }
            break;}
        case T_NOT:
            L(-2.f*u,0,-1.f*u,0);
            {int x1,y1,x2,y2,x3,y3;
            T(-1.f*u,-0.8f*u,x1,y1);T(-1.f*u,0.8f*u,x2,y2);T(0.8f*u,0,x3,y3);
            ln(r,x1,y1,x3,y3,b);ln(r,x2,y2,x3,y3,b);
            int bx,by;T(1.05f*u,0,bx,by);dot(r,bx,by,3,b);}
            L(1.25f*u,0,2.f*u,0);
            break;
        case T_PROBE:
            L(-2.f*u,0,-0.5f*u,0);
            {int x1,y1,x2,y2;
            T(-0.5f*u,-0.7f*u,x1,y1);T(-0.5f*u,0.7f*u,x2,y2);ln(r,x1,y1,x2,y2,b);
            T(-0.5f*u,-0.7f*u,x1,y1);T(0.9f*u,-0.7f*u,x2,y2);ln(r,x1,y1,x2,y2,b);
            T(-0.5f*u,0.7f*u,x1,y1);T(0.9f*u,0.7f*u,x2,y2);ln(r,x1,y1,x2,y2,b);
            T(0.9f*u,-0.7f*u,x1,y1);T(0.9f*u,0.7f*u,x2,y2);ln(r,x1,y1,x2,y2,b);}
            break;
        default: break;
    }
    // pin dots
    auto P=pins(c.type);
    for(int i=0;i<(int)P.size();i++){
        int wx,wy;pinPos(c,i,wx,wy);
        dot(r,sx(wx,cam),sy(wy,cam),2,C_PIN);
    }
}

// ---------------- app state ----------------
struct App{
    std::vector<Comp> comps;
    std::vector<Wire> wires;
    Cam cam;
    CType pending=T_COUNT;
    bool wiring=false; int wc=-1,wp=-1;
    bool dragging=false; int dox=0,doy=0;
    bool panning=false; int plx=0,ply=0;
    int mx=0,my=0; bool gridOn=true; bool running=false;
    std::string search;
    std::vector<std::string> log;
    int nextId[20]={0};
    // undo stacks (snapshot-based, simple)
    std::vector<std::string> undoStack; std::vector<std::string> redoStack;
    std::vector<std::pair<std::string,SDL_Rect>> tb;
    std::vector<std::pair<std::string,SDL_Rect>> mb;
};

static std::string newRef(App&a,CType t){
    int n=++a.nextId[(int)t]; const char*p="X";
    switch(t){
        case T_R:p="R";break;case T_C:p="C";break;case T_L:p="L";break;
        case T_LED:p="D";break;case T_DIODE:p="D";break;
        case T_SWITCH:p="SW";break;case T_BATT:p="BT";break;
        case T_VCC:p="VCC";break;case T_GND:p="GND";break;case T_CLK:p="CLK";break;
        case T_AND:p="U";break;case T_OR:p="U";break;case T_NOT:p="U";break;
        case T_XOR:p="U";break;case T_NAND:p="U";break;case T_NOR:p="U";break;
        case T_PROBE:p="PR";break;default:p="X";
    }
    char b[16];std::snprintf(b,sizeof(b),"%s%d",p,n);return b;
}
static std::string defVal(CType t){
    switch(t){case T_R:return "10k";case T_C:return "100n";case T_L:return "10u";
        case T_LED:return "Red";case T_BATT:return "5V";case T_CLK:return "1kHz";
        default:return "";}
}
static int compAt(App&a,int W,int Y){
    for(int i=(int)a.comps.size()-1;i>=0;i--){
        auto&c=a.comps[i];
        if(std::abs(c.x-W)<=2*GRID&&std::abs(c.y-Y)<=2*GRID)return i;
    }return -1;
}
static bool pinAt(App&a,int W,int Y,int&oc,int&op){
    for(int i=0;i<(int)a.comps.size();i++){
        auto P=pins(a.comps[i].type);
        for(int p=0;p<(int)P.size();p++){
            int px,py;pinPos(a.comps[i],p,px,py);
            if(std::abs(px-W)<=GRID&&std::abs(py-Y)<=GRID){oc=i;op=p;return true;}
        }
    }return false;
}
static void addLog(App&a,const std::string&s){
    a.log.push_back(s);if(a.log.size()>200)a.log.erase(a.log.begin());
}

// ---------------- serialize snapshot ----------------
static std::string snapshot(App&a){
    std::string s="C";
    char b[64];
    for(auto&c:a.comps){
        std::snprintf(b,sizeof(b),"%d,%d,%d,%d,%s,%s|",
            (int)c.type,c.x,c.y,c.rot,c.ref.c_str(),c.value.c_str());
        s+=b;
    }
    s+="W";
    for(auto&w:a.wires){
        std::snprintf(b,sizeof(b),"%d,%d,%d,%d|",w.ca,w.pa,w.cb,w.pb);
        s+=b;
    }
    return s;
}
static void restore(App&a,const std::string&s){
    a.comps.clear();a.wires.clear();
    size_t ci=s.find('C'),wi=s.find('W');
    std::string cs=s.substr(ci+1,wi-ci-1);
    std::string ws=s.substr(wi+1);
    // parse comps
    size_t pos=0;
    while(pos<cs.size()){
        size_t e=cs.find('|',pos); if(e==std::string::npos)break;
        std::string tok=cs.substr(pos,e-pos); pos=e+1;
        if(tok.empty())continue;
        Comp c;c.state=-1;c.sel=false;c.swClosed=false;
        // type,x,y,rot,ref,value
        int idx=0; std::string fields[6]; int fi=0; std::string cur;
        for(char ch:tok){ if(ch==','){if(fi<6)fields[fi++]=cur;cur.clear();}else cur+=ch;}
        if(!cur.empty()&&fi<6)fields[fi++]=cur;
        if(fi>=4){
            c.type=(CType)atoi(fields[0].c_str());
            c.x=atoi(fields[1].c_str());c.y=atoi(fields[2].c_str());
            c.rot=atoi(fields[3].c_str());
            if(fi>4)c.ref=fields[4]; if(fi>5)c.value=fields[5];
            else c.value="";
            if(c.ref.empty())c.ref=newRef(a,c.type);
            a.comps.push_back(c);
        }
    }
    pos=0;
    while(pos<ws.size()){
        size_t e=ws.find('|',pos); if(e==std::string::npos)break;
        std::string tok=ws.substr(pos,e-pos); pos=e+1;
        if(tok.empty())continue;
        Wire w;w.state=-1;
        int fi=0;std::string cur,fields[4];
        for(char ch:tok){if(ch==','){if(fi<4)fields[fi++]=cur;cur.clear();}else cur+=ch;}
        if(!cur.empty()&&fi<4)fields[fi++]=cur;
        if(fi>=4){w.ca=atoi(fields[0].c_str());w.pa=atoi(fields[1].c_str());
            w.cb=atoi(fields[2].c_str());w.pb=atoi(fields[3].c_str());a.wires.push_back(w);}
    }
}
static void pushUndo(App&a){
    a.undoStack.push_back(snapshot(a));
    if(a.undoStack.size()>50)a.undoStack.erase(a.undoStack.begin());
    a.redoStack.clear();
}
static void undo(App&a){
    if(a.undoStack.empty())return;
    a.redoStack.push_back(snapshot(a));
    std::string s=a.undoStack.back();a.undoStack.pop_back();
    restore(a,s); addLog(a,"Undo");
}
static void redo(App&a){
    if(a.redoStack.empty())return;
    a.undoStack.push_back(snapshot(a));
    std::string s=a.redoStack.back();a.redoStack.pop_back();
    restore(a,s); addLog(a,"Redo");
}

// ---------------- simulation ----------------
static int wireState(App&a,int ci,int pi){
    for(auto&w:a.wires)if((w.ca==ci&&w.pa==pi)||(w.cb==ci&&w.pb==pi))return w.state;
    return -1;
}
static void setWireState(App&a,int ci,int pi,int s){
    for(auto&w:a.wires)if((w.ca==ci&&w.pa==pi)||(w.cb==ci&&w.pb==pi))w.state=s;
}
static void simulate(App&a){
    for(auto&w:a.wires)w.state=-1;
    for(auto&c:a.comps){
        if(c.type==T_VCC||c.type==T_CLK)c.state=1;
        else if(c.type==T_GND)c.state=0; else c.state=-1;
    }
    for(size_t i=0;i<a.comps.size();i++){
        auto&c=a.comps[i];
        if(c.type==T_VCC||c.type==T_CLK)setWireState(a,(int)i,0,1);
        else if(c.type==T_GND)setWireState(a,(int)i,0,0);
    }
    for(int it=0;it<50;it++){
        bool ch=false;
        for(size_t i=0;i<a.comps.size();i++){
            auto&c=a.comps[i]; int n=(int)pins(c.type).size();
            int in0=wireState(a,(int)i,0);
            int in1=(n>1)?wireState(a,(int)i,1):-1;
            int out=-1;
            switch(c.type){
                case T_AND:if(in0>=0&&in1>=0)out=in0&in1;break;
                case T_OR:if(in0>=0&&in1>=0)out=in0|in1;break;
                case T_XOR:if(in0>=0&&in1>=0)out=in0^in1;break;
                case T_NAND:if(in0>=0&&in1>=0)out=!(in0&in1);break;
                case T_NOR:if(in0>=0&&in1>=0)out=!(in0|in1);break;
                case T_NOT:if(in0>=0)out=!in0;break;
                default:break;
            }
            if(out>=0){
                int oc=n-1;int cur=wireState(a,(int)i,oc);
                if(cur!=out){setWireState(a,(int)i,oc,out);ch=true;c.state=out;}
            }
        }
        if(!ch)break;
    }
}

// ---------------- save/load file ----------------
static void saveFile(App&a,const char*path){
    FILE*f=fopen(path,"w");if(!f)return;
    fprintf(f,"%s\n",snapshot(a).c_str());
    fclose(f); addLog(a,std::string("Saved ")+path);
}
static void loadFile(App&a,const char*path){
    FILE*f=fopen(path,"r");if(!f)return;
    std::string s;char buf[4096];size_t n;
    while((n=fread(buf,1,sizeof(buf),f))>0)s.append(buf,n);fclose(f);
    restore(a,s); addLog(a,std::string("Loaded ")+path);
}

// ---------------- canvas rect ----------------
static SDL_Rect cvRect(){return {LIB_W,MENU_H+TOOL_H,WIN_W-LIB_W-PROP_W,
    WIN_H-MENU_H-TOOL_H-STATUS_H-LOG_H};}

// ---------------- drawing UI ----------------
static void drawMenu(SDL_Renderer*r,TTF_Font*f,App&a){
    SDL_Rect bar{0,0,WIN_W,MENU_H};fr(r,bar,C_MENU);ln(r,0,MENU_H,WIN_W,MENU_H,C_EDGE);
    static const char*items[]={"File","Edit","View","Tools","Design","Library","Help"};
    int x=6;a.mb.clear();
    for(auto lbl:items){
        int w=8+(int)strlen(lbl)*7;SDL_Rect rc{x,0,w,MENU_H};a.mb.push_back({lbl,rc});
        bool hv=a.mx>=rc.x&&a.mx<rc.x+rc.w&&a.my<MENU_H;
        if(hv){fr(r,rc,C_MENUHV);txt(r,f,lbl,rc,{255,255,255,255});}
        else txt(r,f,lbl,rc,C_MENUTX);
        x+=w;
    }
}
static void drawToolbar(SDL_Renderer*r,TTF_Font*f,App&a){
    SDL_Rect bar{0,MENU_H,WIN_W,TOOL_H};fr(r,bar,C_TBBG);
    ln(r,0,MENU_H+TOOL_H,WIN_W,MENU_H+TOOL_H,C_EDGE);
    const char*btns[]={"New","Open","Save","|","Undo","Redo","|",
        "Run","Stop","|","Rotate","Grid","|","Export","DRC"};
    int x=6;a.tb.clear();
    for(auto bn:btns){
        if(bn[0]=='|'){ln(r,x+6,MENU_H+6,x+6,MENU_H+TOOL_H-6,C_EDGE);x+=12;continue;}
        int w=52;SDL_Rect rc{x,MENU_H+3,w,TOOL_H-6};a.tb.push_back({bn,rc});
        bool hv=a.mx>=rc.x&&a.mx<rc.x+rc.w&&a.my>=rc.y&&a.my<rc.y+rc.h;
        bool act=(std::string(bn)=="Run"&&a.running);
        if(act)fr(r,rc,C_TBACT);else if(hv)fr(r,rc,C_TBHV);
        rct(r,rc,C_EDGE);txt(r,f,bn,rc,C_TEXT);x+=w+2;
    }
}
static void drawLib(SDL_Renderer*r,TTF_Font*f,TTF_Font*fs,App&a){
    SDL_Rect p{0,MENU_H+TOOL_H,LIB_W,WIN_H-MENU_H-TOOL_H-STATUS_H};fr(r,p,C_PANEL);
    SDL_Rect hd{0,p.y,LIB_W,HDR_H};fr(r,hd,C_HDR);txt(r,f,"LIBRARY",hd,C_TEXT);
    SDL_Rect sb{4,p.y+HDR_H+3,LIB_W-8,20};fr(r,sb,{255,255,255,255});rct(r,sb,C_EDGE);
    txt(r,fs,("Search: "+a.search).c_str(),sb,C_DIM,false);
    int y=p.y+HDR_H+28;
    for(int t=0;t<T_COUNT;t++){
        CType tt=(CType)t;
        std::string nm=typeName(tt),ct=typeCat(tt);
        if(!containsIC(nm,a.search)&&!containsIC(ct,a.search))continue;
        SDL_Rect row{2,y,LIB_W-4,18};
        bool hv=a.mx>=row.x&&a.mx<row.x+row.w&&a.my>=row.y&&a.my<row.y+row.h;
        bool sel=(a.pending==tt);
        if(sel)fr(r,row,C_ACCENT);else if(hv)fr(r,row,{210,225,245,255});
        txt(r,fs,("\t"+nm).c_str(),row,sel?SDL_Color{255,255,255,255}:C_TEXT,false);
        y+=18;
    }
    ln(r,LIB_W,p.y,LIB_W,p.y+p.h,C_EDGE);
}
static void drawProp(SDL_Renderer*r,TTF_Font*f,TTF_Font*fs,App&a){
    int x=WIN_W-PROP_W;
    SDL_Rect p{x,MENU_H+TOOL_H,PROP_W,WIN_H-MENU_H-TOOL_H-STATUS_H};fr(r,p,C_PANEL);
    SDL_Rect hd{x,p.y,PROP_W,HDR_H};fr(r,hd,C_HDR);txt(r,f,"PROPERTIES",hd,C_TEXT);
    int sel=-1,cnt=0;
    for(size_t i=0;i<a.comps.size();i++)if(a.comps[i].sel){sel=(int)i;cnt++;}
    int y=p.y+HDR_H+6;std::string ls[8];int nl=0;
    if(cnt==0)ls[nl++]="No selection";
    else if(cnt>1)ls[nl++]=std::to_string(cnt)+" selected";
    else{
        auto&c=a.comps[sel];
        ls[nl++]=std::string("Type: ")+typeName(c.type);
        ls[nl++]=std::string("Ref:  ")+c.ref;
        ls[nl++]=std::string("Val:  ")+c.value;
        char b[64];std::snprintf(b,sizeof(b),"Pos:  (%d, %d)",c.x,c.y);ls[nl++]=b;
        const char*rn[]={"0","90","180","270"};ls[nl++]=std::string("Rot:  ")+rn[c.rot&3];
        ls[nl++]=std::string("State:")+(c.state<0?" -":(c.state?" 1":" 0"));
    }
    for(int i=0;i<nl;i++){SDL_Rect rr{x+6,y,PROP_W-12,18};txt(r,fs,ls[i].c_str(),rr,C_TEXT,false);y+=18;}
    y+=8;SDL_Rect hint{x+6,y,PROP_W-12,70};fr(r,hint,{250,250,235,255});rct(r,hint,C_EDGE);
    txt(r,fs,"R: rotate  Del: remove",hint,C_DIM,false);
    ln(r,x,p.y,x,p.y+p.h,C_EDGE);
}
static void drawCanvas(SDL_Renderer*r,TTF_Font*fs,App&a){
    SDL_Rect cv=cvRect();SDL_RenderSetClipRect(r,&cv);
    fr(r,cv,C_CANVAS);rct(r,cv,C_EDGE);
    if(a.gridOn){
        sc(r,C_GRID);int step=(int)(GRID*a.cam.zoom);if(step<5)step=5;
        for(int X=cv.x;X<cv.x+cv.w;X+=step)for(int Y=cv.y;Y<cv.y+cv.h;Y+=step)
            SDL_RenderDrawPoint(r,X,Y);
    }
    int ox=sx(0,a.cam),oy=sy(0,a.cam);
    ln(r,ox-6,oy,ox+6,oy,C_HI);ln(r,ox,oy-6,ox,oy+6,C_HI);
    // wires
    for(auto&w:a.wires){
        int x1,y1,x2,y2;
        if(w.ca>=0&&w.ca<(int)a.comps.size())pinPos(a.comps[w.ca],w.pa,x1,y1);else{x1=0;y1=0;}
        if(w.cb>=0&&w.cb<(int)a.comps.size())pinPos(a.comps[w.cb],w.pb,x2,y2);else{x2=0;y2=0;}
        int s1=sx(x1,a.cam),t1=sy(y1,a.cam),s2=sx(x2,a.cam),t2=sy(y2,a.cam);
        int my=(t1+t2)/2;
        SDL_Color wc=a.running?(w.state==1?C_HI:(w.state==0?C_LO:C_UNK)):C_WIRE;
        ln(r,s1,t1,s1,my,wc);ln(r,s1,my,s2,my,wc);ln(r,s2,my,s2,t2,wc);
    }
    if(a.wiring&&a.wc>=0&&a.wc<(int)a.comps.size()){
        int fx,fy;pinPos(a.comps[a.wc],a.wp,fx,fy);
        ln(r,sx(fx,a.cam),sy(fy,a.cam),a.mx,a.my,C_ACCENT);
    }
    // comps
    for(auto&c:a.comps){
        drawComp(r,c,a.cam);
        int cx=sx(c.x,a.cam);int cy=sy(c.y,a.cam);
        SDL_Rect lr{cx-30,cy-26,60,12};txt(r,fs,c.ref.c_str(),lr,C_DIM);
        if(!c.value.empty()){SDL_Rect lv{cx-30,cy+14,60,12};txt(r,fs,c.value.c_str(),lv,C_DIM);}
        if(c.sel){SDL_Rect sb{cx-3*GRID,cy-3*GRID,6*GRID,6*GRID};rct(r,sb,C_SEL);}
        if(c.type==T_PROBE){const char*t=c.state<0?"?":(c.state?"1":"0");
            SDL_Rect pr{cx-6,cy-6,16,16};txt(r,fs,t,pr,c.state==1?C_HI:C_LO);}
    }
    if(a.pending!=T_COUNT){
        int gwx=snap((int)wx(a.mx,a.cam)),gwy=snap((int)wy(a.my,a.cam));
        Comp g{a.pending,gwx,gwy,0,"","",false,-1,false};
        drawComp(r,g,a.cam);
    }
    SDL_RenderSetClipRect(r,nullptr);
}
// placeholder workaround removed (defined empty to satisfy old call)
static Cam cam_unused_workaround(const Comp&){Cam c;return c;}
static void drawStatus(SDL_Renderer*r,TTF_Font*fs,App&a){
    SDL_Rect bar{0,WIN_H-STATUS_H,WIN_W,STATUS_H};fr(r,bar,C_STATUS);
    ln(r,0,bar.y,WIN_W,bar.y,C_EDGE);
    SDL_Rect cv=cvRect();
    double W=wx(a.mx,a.cam),Y=wy(a.my,a.cam);
    bool ic=a.mx>=cv.x&&a.mx<cv.x+cv.w&&a.my>=cv.y&&a.my<cv.y+cv.h;
    char b[128];std::snprintf(b,sizeof(b),ic?"X:%d Y:%d":"X:- Y:-",(int)W,(int)Y);
    txt(r,fs,a.running?"Running":"Ready",SDL_Rect{4,bar.y,80,STATUS_H},a.running?C_HI:C_DIM,false);
    txt(r,fs,b,SDL_Rect{90,bar.y,140,STATUS_H},C_DIM,false);
    char z[32];std::snprintf(z,sizeof(z),"Zoom:%d%%",(int)(a.cam.zoom*100));
    txt(r,fs,z,SDL_Rect{240,bar.y,100,STATUS_H},C_DIM,false);
    txt(r,fs,(std::string("Snap:")+std::to_string(GRID)+"th").c_str(),
        SDL_Rect{350,bar.y,90,STATUS_H},C_DIM,false);
    txt(r,fs,a.gridOn?"Grid:ON":"Grid:OFF",SDL_Rect{450,bar.y,80,STATUS_H},
        a.gridOn?C_ACCENT:C_DIM,false);
    txt(r,fs,"Proteus-like",SDL_Rect{WIN_W-110,bar.y,106,STATUS_H},C_DIM,false);
}
static void drawLog(SDL_Renderer*r,TTF_Font*fs,App&a){
    int x=LIB_W,y=WIN_H-STATUS_H-LOG_H,w=WIN_W-LIB_W-PROP_W,h=LOG_H;
    SDL_Rect box{x,y,w,h};fr(r,box,{250,250,250,255});rct(r,box,C_EDGE);
    SDL_Rect hd{x,y,w,HDR_H};fr(r,hd,C_HDR);txt(r,fs,"LOG",hd,C_TEXT,false);
    int ly=y+HDR_H+2;
    for(int i=(int)a.log.size()-1;i>=0&&ly<y+h-14;--i,ly+=14)
        txt(r,fs,a.log[i].c_str(),SDL_Rect{x+4,ly,w-8,14},C_DIM,false);
}

// ---------------- toolbar action ----------------
static void doAction(SDL_Renderer*r,SDL_Window*win,App&a,const std::string&n){
    if(n=="Run"){a.running=true;simulate(a);addLog(a,"RUN");}
    else if(n=="Stop"){a.running=false;addLog(a,"STOP");}
    else if(n=="Undo")undo(a);
    else if(n=="Redo")redo(a);
    else if(n=="Rotate"){for(auto&c:a.comps)if(c.sel)c.rot=(c.rot+1)&3;addLog(a,"Rotated");}
    else if(n=="Grid"){a.gridOn=!a.gridOn;addLog(a,std::string("Grid ")+(a.gridOn?"on":"off"));}
    else if(n=="Save"){saveFile(a,"circuit.txt");}
    else if(n=="Open"){loadFile(a,"circuit.txt");}
    else if(n=="New"){pushUndo(a);a.comps.clear();a.wires.clear();addLog(a,"New project");}
    else if(n=="Export"){
        int ww,wh;SDL_GetWindowSize(win,&ww,&wh);
        SDL_Surface*sf=SDL_CreateRGBSurface(0,ww,wh,32,0,0,0,0);
        SDL_RenderReadPixels(r,nullptr,sf->format->format,sf->pixels,sf->pitch);
        SDL_SaveBMP(sf,"export.bmp");SDL_FreeSurface(sf);addLog(a,"Exported export.bmp");
    }
    else if(n=="DRC"){
        addLog(a,"--- DRC ---");int issues=0;
        for(size_t i=0;i<a.comps.size();i++){
            auto P=pins(a.comps[i].type);
            for(int p=0;p<(int)P.size();p++){
                bool found=false;
                for(auto&w:a.wires)if((w.ca==(int)i&&w.pa==p)||(w.cb==(int)i&&w.pb==p)){found=true;break;}
                if(!found){addLog(a,"[W] Floating: "+a.comps[i].ref+"."+std::to_string(p));issues++;}
            }
        }
        addLog(a,std::string("DRC: ")+std::to_string(issues)+" issues");
    }
}

// ---------------- main ----------------
int main(int,char*[]){
    SDL_SetMainReady();
    if(SDL_Init(SDL_INIT_VIDEO)){SDL_Log("SDL:%s",SDL_GetError());return 1;}
    if(TTF_Init()){SDL_Log("TTF:%s",TTF_GetError());return 1;}
    SDL_Window*win=SDL_CreateWindow("Proteus-like EDA",
        SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,WIN_W,WIN_H,SDL_WINDOW_SHOWN);
    SDL_Renderer*r=SDL_CreateRenderer(win,-1,SDL_RENDERER_ACCELERATED|SDL_RENDERER_PRESENTVSYNC);
    if(!r)r=SDL_CreateRenderer(win,-1,SDL_RENDERER_SOFTWARE);
    TTF_Font*f=TTF_OpenFont("C:/Windows/Fonts/arial.ttf",13);
    TTF_Font*fs=TTF_OpenFont("C:/Windows/Fonts/arial.ttf",11);
    App a;a.cam.px=LIB_W;a.cam.py=MENU_H+TOOL_H;
    addLog(a,"Welcome! Pick part from LIBRARY, click canvas to place.");
    addLog(a,"Keys: R rotate, Del remove, G grid, W wire, F5 run, Ctrl+S save, Ctrl+Z undo");
    SDL_StartTextInput();
    bool run=true;bool prevDown=false;
    while(run){
        SDL_Event e;int wheel=0;
        while(SDL_PollEvent(&e)){
            switch(e.type){
                case SDL_QUIT:run=false;break;
                case SDL_MOUSEWHEEL:wheel=e.wheel.y;break;
                case SDL_MOUSEMOTION:
                    a.mx=e.motion.x;a.my=e.motion.y;
                    if(a.panning){a.cam.px-=(e.motion.x-a.plx)/a.cam.zoom;
                        a.cam.py-=(e.motion.y-a.ply)/a.cam.zoom;}
                    a.plx=e.motion.x;a.ply=e.motion.y;
                    if(a.dragging){
                        int W=snap((int)wx(a.mx,a.cam)),Y=snap((int)wy(a.my,a.cam));
                        for(auto&c:a.comps)if(c.sel){c.x=W-a.dox;c.y=Y-a.doy;}
                    }
                    break;
                case SDL_MOUSEBUTTONDOWN:{
                    int mx=e.button.x,my=e.button.y;SDL_Rect cv=cvRect();
                    if(e.button.button==SDL_BUTTON_MIDDLE){a.panning=true;a.plx=mx;a.ply=my;break;}
                    if(mx<cv.x||mx>=cv.x+cv.w||my<cv.y||my>=cv.y+cv.h)break;
                    int W=snap((int)wx(mx,a.cam)),Y=snap((int)wy(my,a.cam));
                    if(e.button.button==SDL_BUTTON_LEFT){
                        // wiring click on pins takes priority
                        int oc,op;
                        if(a.wiring||pinAt(a,W,Y,oc,op)){
                            if(!a.wiring){a.wiring=true;a.wc=oc;a.wp=op;}
                            else{
                                pushUndo(a);
                                Wire ww;ww.ca=a.wc;ww.pa=a.wp;ww.cb=oc;ww.pb=op;ww.state=-1;
                                a.wires.push_back(ww);a.wiring=false;addLog(a,"Wire added");
                            }
                        }
                        else if(a.pending!=T_COUNT){
                            pushUndo(a);
                            Comp c{a.pending,W,Y,0,newRef(a,a.pending),defVal(a.pending),false,-1,false};
                            a.comps.push_back(c);addLog(a,"Placed "+std::string(typeName(a.pending)));
                        }else{
                            int ci=compAt(a,W,Y);
                            if(!(SDL_GetModState()&KMOD_SHIFT))for(auto&c:a.comps)c.sel=false;
                            if(ci>=0){a.comps[ci].sel=true;a.dragging=true;
                                a.dox=W-a.comps[ci].x;a.doy=Y-a.comps[ci].y;}
                        }
                    }else if(e.button.button==SDL_BUTTON_RIGHT){
                        a.pending=T_COUNT;a.wiring=false;
                        if(!(SDL_GetModState()&KMOD_SHIFT))for(auto&c:a.comps)c.sel=false;
                    }
                    break;}
                case SDL_MOUSEBUTTONUP:
                    if(e.button.button==SDL_BUTTON_MIDDLE)a.panning=false;
                    if(e.button.button==SDL_BUTTON_LEFT)a.dragging=false;
                    break;
                case SDL_KEYDOWN:{
                    SDL_Keycode k=e.key.keysym.sym;
                    auto delSel=[&](){
                        pushUndo(a);
                        for(int i=(int)a.comps.size()-1;i>=0;i--)if(a.comps[i].sel){
                            a.wires.erase(std::remove_if(a.wires.begin(),a.wires.end(),
                                [i](const Wire&w){return w.ca==i||w.cb==i;}),a.wires.end());
                            a.comps.erase(a.comps.begin()+i);}
                        addLog(a,"Deleted selected");};
                    if(k==SDLK_ESCAPE){a.pending=T_COUNT;a.wiring=false;for(auto&c:a.comps)c.sel=false;}
                    else if(k==SDLK_r){for(auto&c:a.comps)if(c.sel)c.rot=(c.rot+1)&3;addLog(a,"Rotated");}
                    else if(k==SDLK_DELETE||k==SDLK_BACKSPACE){
                        if(!a.search.empty())a.search.pop_back();
                        else delSel();
                    }
                    else if(k==SDLK_g){a.gridOn=!a.gridOn;addLog(a,std::string("Grid ")+(a.gridOn?"on":"off"));}
                    else if(k==SDLK_w)addLog(a,"Wire: click pin, then another pin");
                    else if(k==SDLK_F5){a.running=true;simulate(a);addLog(a,"RUN");}
                    else if(k==SDLK_F6){a.running=false;addLog(a,"STOP");}
                    else if(k==SDLK_s&&(e.key.keysym.mod&KMOD_CTRL)){saveFile(a,"circuit.txt");}
                    else if(k==SDLK_o&&(e.key.keysym.mod&KMOD_CTRL)){loadFile(a,"circuit.txt");}
                    else if(k==SDLK_z&&(e.key.keysym.mod&KMOD_CTRL)){if(e.key.keysym.mod&KMOD_SHIFT)redo(a);else undo(a);}
                    else if(k==SDLK_y&&(e.key.keysym.mod&KMOD_CTRL)){redo(a);}
                    break;}
                case SDL_TEXTINPUT:
                    if(a.search.size()<16)a.search+=e.text.text;
                    break;
            }
        }
        // toolbar/menu/library clicks (edge-triggered)
        {
            int mx,my;Uint32 bs=SDL_GetMouseState(&mx,&my);
            bool down=(bs&SDL_BUTTON_LMASK)!=0;
            if(down&&!prevDown){
                if(my>=MENU_H&&my<MENU_H+TOOL_H){
                    for(auto&tb:a.tb)if(mx>=tb.second.x&&mx<tb.second.x+tb.second.w
                        &&my>=tb.second.y&&my<tb.second.y+tb.second.h){
                        doAction(r,win,a,tb.first);break;}
                }else if(mx<LIB_W&&my>=MENU_H+TOOL_H&&my<WIN_H-STATUS_H){
                    int y=MENU_H+TOOL_H+HDR_H+28;
                    for(int t=0;t<T_COUNT;t++){
                        CType tt=(CType)t;
                        std::string nm=typeName(tt),ct=typeCat(tt);
                        if(!containsIC(nm,a.search)&&!containsIC(ct,a.search))continue;
                        if(my>=y&&my<y+18){a.pending=tt;a.search.clear();
                            addLog(a,"Selected: "+nm);break;}
                        y+=18;
                    }
                }else if(my<MENU_H){
                    for(auto&mb:a.mb)if(mx>=mb.second.x&&mx<mb.second.x+mb.second.w){
                        if(mb.first=="File")saveFile(a,"circuit.txt");
                        else if(mb.first=="Library")a.search.clear();
                        else if(mb.first=="Help")addLog(a,"Keys in status bar");
                        break;}
                }
            }
            prevDown=down;
        }
        if(wheel!=0){
            double f=wheel>0?1.15:0.87;double W=wx(a.mx,a.cam),Y=wy(a.my,a.cam);
            a.cam.zoom*=f;if(a.cam.zoom<0.2)a.cam.zoom=0.2;if(a.cam.zoom>8)a.cam.zoom=8;
            a.cam.px=W-a.mx/a.cam.zoom;a.cam.py=Y-a.my/a.cam.zoom;
        }
        sc(r,C_BG);SDL_RenderClear(r);
        drawMenu(r,f,a);drawToolbar(r,f,a);drawLib(r,f,fs,a);drawProp(r,f,fs,a);
        drawCanvas(r,fs,a);drawLog(r,fs,a);drawStatus(r,fs,a);
        SDL_RenderPresent(r);SDL_Delay(16);
    }
    TTF_CloseFont(f);TTF_CloseFont(fs);
    SDL_DestroyRenderer(r);SDL_DestroyWindow(win);TTF_Quit();SDL_Quit();
    return 0;
}
